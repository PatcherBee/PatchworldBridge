/*
  ==============================================================================
    Source/Audio/AudioEngine.cpp
    Role: AudioEngine Implementation
  ==============================================================================
*/

#include "../Audio/AudioEngine.h"
#include "../Audio/ClockSmoother.h"
#include "../Audio/EditableNote.h"
#include "../Core/PlatformGuard.h"
#include "../Core/TimerHub.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

// ==============================================================================
// AudioEngine Implementation
// ==============================================================================

// CRITICAL FIX: Removed static atomic hack (ABA problem risk)
// Now using synchronous timer stop with RAII in destructor

void AudioEngine::ensureTimerStoppedSync() {
  if (juce::MessageManager::getInstance()->isThisTheMessageThread()) {
    juce::HighResolutionTimer::stopTimer();
  } else {
    juce::WaitableEvent event;
    juce::MessageManager::callAsync([this, &event] {
      juce::HighResolutionTimer::stopTimer();
      event.signal();
    });
    event.wait();
  }
}

AudioEngine::AudioEngine() {
  // CRITICAL: Connect recycler to dead pool so overflow goes to background
  // deleter
  stateRecycler.setDeleter(&deadPool);

  for (int i = 0; i < kMaxSequencerSlots; ++i)
    sequencerChannels[i].store(i + 1, std::memory_order_relaxed);

  // Initialize with empty state
  activeState = std::make_shared<EngineState>();
  link.enable(true);

  // Link watchdog: one-shot reconnection after 5s if enabled but 0 peers
  linkWatchdogHubId =
      "AudioEngine_linkWatchdog_" + juce::Uuid().toDashedString().toStdString();
  TimerHub::instance().subscribe(
      linkWatchdogHubId, [this] { linkWatchdogTick(); }, TimerHub::Rate0_2Hz);
}

void AudioEngine::linkWatchdogTick() {
  if (hasRunLinkCheck)
    return;
  hasRunLinkCheck = true;

  if (!linkWatchdogHubId.empty()) {
    TimerHub::instance().unsubscribe(linkWatchdogHubId);
    linkWatchdogHubId.clear();
  }

  if (link.isEnabled() && link.numPeers() == 0) {
    link.enable(false);
    link.enable(true);
    DBG("Link Watchdog: No peers at startup. Forced reconnection.");
  }
}

void AudioEngine::play() {
  if (getIsPlaying())
    return;

  // When Link enabled with start/stop sync: wait for next beat/bar (quantized
  // start)
  bool shouldWait = link.isEnabled() && link.isStartStopSyncEnabled();

  // Determine where we are starting from (0.0 or paused state)
  double startTick = 0.0;
  if (transport.isPaused.load(std::memory_order_acquire)) {
    startTick = transport.pausedTick.load(std::memory_order_acquire);
  } else {
    // FRESH START: Reset current tick immediately
    audioHot.currentTick.store(0.0, std::memory_order_release);
    nextEventIndex.store(0, std::memory_order_release);
  }

  if (shouldWait) {
    auto session = link.captureAppSessionState();
    auto now = link.clock().micros();
    double bpm = session.tempo();

    // Account for output latency (ms -> beats)
    double latencyBeats = (outputLatency.load() / 1000.0) * (bpm / 60.0);
    double currentBeat = session.beatAtTime(now, quantum) + latencyBeats;

    // Find next bar boundary
    double nextBar = std::ceil(currentBeat / quantum) * quantum;
    if (nextBar - currentBeat < 0.25)
      nextBar += quantum;

    pendingStartBeat.store(nextBar, std::memory_order_release);
    pendingResumeTick.store(startTick, std::memory_order_release);

    session.setIsPlaying(true, session.timeAtBeat(nextBar, quantum));
    link.commitAppSessionState(session);
  } else {
    // IMMEDIATE FIRE
    pendingStartBeat.store(-1.0, std::memory_order_release);
    pendingResumeTick.store(-1.0, std::memory_order_release);

    if (link.isEnabled()) {
      auto session = link.captureAppSessionState();
      auto now = link.clock().micros();
      linkBeatOffset =
          session.beatAtTime(now, quantum) - (startTick / getTicksPerQuarter());
      if (link.isStartStopSyncEnabled()) {
        session.setIsPlaying(true, now);
        link.commitAppSessionState(session);
      }
    } else {
      linkBeatOffset = 0.0;
    }

    transport.isPaused.store(false, std::memory_order_release);
    transport.isPlaying.store(true, std::memory_order_release);

    if (!juce::HighResolutionTimer::isTimerRunning())
      juce::HighResolutionTimer::startTimer(1);
  }
}

void AudioEngine::stop() {
  if (!getIsPlaying()) {
    pendingStartBeat.store(-1.0, std::memory_order_release);
    pendingStopBeat.store(-1.0, std::memory_order_release);
    pendingPauseBeat.store(-1.0, std::memory_order_release);
    pendingResumeTick.store(-1.0, std::memory_order_release);
    return;
  }
  if (link.isEnabled() && link.isStartStopSyncEnabled()) {
    auto session = link.captureAppSessionState();
    auto now = link.clock().micros();
    double currentBeat = session.beatAtTime(now, quantum);
    double nextBar = std::ceil(currentBeat / quantum) * quantum;
    if (nextBar - currentBeat < 0.1)
      nextBar += quantum;
    pendingStopBeat.store(nextBar, std::memory_order_release);
    session.setIsPlaying(false, session.timeAtBeat(nextBar, quantum));
    link.commitAppSessionState(session);
    return;
  }
  doStopNow();
}

void AudioEngine::doStopNow() {
  if (link.isEnabled() && link.isStartStopSyncEnabled()) {
    auto session = link.captureAppSessionState();
    session.setIsPlaying(false, link.clock().micros());
    link.commitAppSessionState(session);
  }
  transport.isPlaying.store(false, std::memory_order_release);
  transport.isPaused.store(false, std::memory_order_release);
  pendingStartBeat.store(-1.0, std::memory_order_release);
  pendingStopBeat.store(-1.0, std::memory_order_release);
  pendingPauseBeat.store(-1.0, std::memory_order_release);
  pendingResumeTick.store(-1.0, std::memory_order_release);
  ensureTimerStoppedSync();
  if (onMidiEvent) {
    for (int i = 1; i <= 16; ++i)
      onMidiEvent(juce::MidiMessage::allNotesOff(i));
  }
}

void AudioEngine::pause() {
  if (!getIsPlaying())
    return;
  if (link.isEnabled() && link.isStartStopSyncEnabled()) {
    auto session = link.captureAppSessionState();
    auto now = link.clock().micros();
    double currentBeat = session.beatAtTime(now, quantum);
    double nextBar = std::ceil(currentBeat / quantum) * quantum;
    if (nextBar - currentBeat < 0.1)
      nextBar += quantum;
    pendingPauseBeat.store(nextBar, std::memory_order_release);
    return;
  }
  double tick = audioHot.currentTick.load(std::memory_order_relaxed);
  transport.pausedTick.store(tick, std::memory_order_release);
  transport.isPaused.store(true, std::memory_order_release);
  transport.isPlaying.store(false, std::memory_order_release);
  pendingStartBeat.store(-1.0, std::memory_order_release);
  ensureTimerStoppedSync();
  if (onMidiEvent) {
    for (int i = 1; i <= 16; ++i)
      onMidiEvent(juce::MidiMessage::allNotesOff(i));
  }
}

void AudioEngine::resume() {
  if (getIsPlaying())
    return;
  if (!transport.isPaused.load(std::memory_order_acquire))
    return;
  double tick = transport.pausedTick.load(std::memory_order_acquire);
  if (link.isEnabled() && link.isStartStopSyncEnabled()) {
    auto session = link.captureAppSessionState();
    auto now = link.clock().micros();
    double bpm = session.tempo();
    double latencyBeats = (outputLatency.load() / 1000.0) * (bpm / 60.0);
    double currentBeat = session.beatAtTime(now, quantum) + latencyBeats;
    double nextBar = std::ceil(currentBeat / quantum) * quantum;
    if (nextBar - currentBeat < 0.25)
      nextBar += quantum;
    pendingStartBeat.store(nextBar, std::memory_order_release);
    pendingResumeTick.store(tick, std::memory_order_release);
    session.setIsPlaying(true, session.timeAtBeat(nextBar, quantum));
    link.commitAppSessionState(session);
    return;
  }
  transport.isPaused.store(false, std::memory_order_release);
  audioHot.currentTick.store(tick, std::memory_order_release);
  std::shared_ptr<EngineState> state = std::atomic_load(&activeState);
  if (state)
    nextEventIndex.store(findIndexForTick(tick), std::memory_order_release);
  pendingStartBeat.store(-1.0, std::memory_order_release);
  transport.isPlaying.store(true, std::memory_order_release);
  if (!juce::HighResolutionTimer::isTimerRunning())
    juce::HighResolutionTimer::startTimer(1);
}

void AudioEngine::tapTempo() {
  double now = juce::Time::getMillisecondCounterHiRes();

  // Reset if it's been too long (> 2 seconds) since last tap
  if (!tapTimes.empty() && (now - tapTimes.back() > 2000.0)) {
    tapTimes.clear();
  }

  tapTimes.push_back(now);

  // Keep only last 4 taps
  if (tapTimes.size() > 4) {
    tapTimes.erase(tapTimes.begin());
  }

  // Only calculate if we have 4 taps (3 intervals)
  if (tapTimes.size() == 4) {
    double totalInterval = 0.0;
    for (size_t i = 1; i < tapTimes.size(); ++i) {
      totalInterval += (tapTimes[i] - tapTimes[i - 1]);
    }

    double avgInterval = totalInterval / 3.0; // Average of 3 intervals
    double bpm = 60000.0 / avgInterval;

    // Apply reasonable limits
    setBpm(juce::jlimit(20.0, 300.0, bpm));

    // Rolling average: don't clear tapTimes here
  }
}

void AudioEngine::driveAudioCallback(double numSamples, double sampleRate) {
  processAudioBlock(numSamples, sampleRate);

  // Update LFO Rate context
  lfo.setSampleRate(sampleRate);
}

void AudioEngine::processAudioBlock(double numSamples, double sampleRate) {
  PlatformGuard guard;
  juce::ScopedNoDenormals noDenormals;

  // --- CRITICAL FIX: PREVENT INSTANT PLAYBACK ---
  if (sampleRate < 1.0 || numSamples <= 0)
    return;

  // 1. EXT SYNC LOGIC: MIDI clock drives BPM (Link + internal)
  // Prefer Link/internal BPM when EXT is jittery; only update when stable
  if (extSyncActive.load() && smoother != nullptr) {
    if (smoother->getIsLocked()) {
      double targetBpm = smoother->getBpm();
      double currentBpm = internalBpm.load(std::memory_order_relaxed);

      // When EXT jitter is high, prefer Link BPM (or current) - don't chase
      // jitter
      double jitterMs = smoother->getJitterMs();
      double alpha = (jitterMs > 6.0) ? 0.02 : 0.04; // Extra slow when jittery
      if (link.isEnabled()) {
        double linkBpm = link.captureAppSessionState().tempo();
        if (jitterMs > 8.0 && std::abs(targetBpm - linkBpm) > 3.0)
          targetBpm =
              linkBpm; // Use Link as safer fallback when EXT very jittery
      }

      if (std::abs(targetBpm - currentBpm) > 0.02) {
        double newBpm = currentBpm + (targetBpm - currentBpm) * alpha;
        internalBpm.store(newBpm, std::memory_order_relaxed);

        // Only push to Link when change is small so Link phase stays stable
        if (link.isEnabled() && std::abs(newBpm - currentBpm) < 0.5) {
          auto s = link.captureAppSessionState();
          s.setTempo(newBpm, link.clock().micros());
          link.commitAppSessionState(s);
        }
      }
    }
  }

  // 2. THREAD-SAFE STATE SNAPSHOT (single atomic load - source of truth for
  // this block)
  auto state =
      std::atomic_load_explicit(&activeState, std::memory_order_acquire);
  if (!state)
    return;

  const double ppq =
      (state->ticksPerQuarter >= 24.0) ? state->ticksPerQuarter : 960.0;
  const auto &seq = state->sequence;
  const int seqSize = static_cast<int>(seq.size());
  int currentIdx = nextEventIndex.load(std::memory_order_relaxed);

  if (seqSize == 0) {
    currentIdx = 0;
  } else {
    if (currentIdx < 0 || currentIdx > seqSize) {
      double currentTick = audioHot.currentTick.load(std::memory_order_relaxed);
      currentIdx = findIndexForTick(currentTick);
    }
    currentIdx = std::clamp(currentIdx, 0, seqSize);
  }
  nextEventIndex.store(currentIdx, std::memory_order_release);

  // 2b. HANDLE QUANTIZED SEEK (Playing + Link) — apply only after reaching next
  // beat/bar
  if (isQuantizedSeek.load(std::memory_order_acquire)) {
    bool isLinked = link.isEnabled();
    if (isLinked) {
      auto session = link.captureAppSessionState();
      auto now = link.clock().micros();
      double currentLinkBeat = session.beatAtTime(now, quantum);
      double nextBoundary = std::ceil(currentLinkBeat / quantum) * quantum;
      // Apply seek only once we have reached the next boundary (wait-to-start
      // for scrubbing)
      if (currentLinkBeat >= nextBoundary - 0.0001) {
        double target = pendingSeekTarget.load(std::memory_order_acquire);
        if (target >= 0.0) {
          double newTick = target * ppq;
          audioHot.currentTick.store(newTick, std::memory_order_release);
          nextEventIndex.store(findIndexForTick(newTick),
                               std::memory_order_release);
          linkBeatOffset = currentLinkBeat - target;
          pendingSeekTarget.store(-1.0, std::memory_order_release);
          isQuantizedSeek.store(false, std::memory_order_release);
          if (airlockRef &&
              airlockRef->getNumReady() < OscAirlock::Capacity - 10)
            airlockRef->push(BridgeEvent(EventType::VisualParam,
                                         EventSource::EngineSequencer, 0, 999,
                                         (float)target));
        }
      }
    } else {
      double target = pendingSeekTarget.load(std::memory_order_acquire);
      if (target >= 0.0) {
        audioHot.currentTick.store(target * ppq, std::memory_order_release);
        nextEventIndex.store(findIndexForTick(target * ppq),
                             std::memory_order_release);
        pendingSeekTarget.store(-1.0, std::memory_order_release);
        isQuantizedSeek.store(false, std::memory_order_release);
      }
    }
  }

  // 3. LINK QUANTIZED STOP/PAUSE (execute at next beat/bar)
  if (transport.isPlaying.load(std::memory_order_acquire) && link.isEnabled()) {
    auto session = link.captureAppSessionState();
    auto now = link.clock().micros();
    double currentLinkBeat = session.beatAtTime(now, quantum);
    double stopAt = pendingStopBeat.load(std::memory_order_acquire);
    if (stopAt >= 0.0 && currentLinkBeat >= stopAt - 0.0001) {
      doStopNow();
      return;
    }
    double pauseAt = pendingPauseBeat.load(std::memory_order_acquire);
    if (pauseAt >= 0.0 && currentLinkBeat >= pauseAt - 0.0001) {
      double tick = audioHot.currentTick.load(std::memory_order_acquire);
      transport.pausedTick.store(tick, std::memory_order_release);
      transport.isPaused.store(true, std::memory_order_release);
      transport.isPlaying.store(false, std::memory_order_release);
      pendingPauseBeat.store(-1.0, std::memory_order_release);
      pendingStartBeat.store(-1.0, std::memory_order_release);
      pendingStartBeat.store(-1.0, std::memory_order_release);
      juce::HighResolutionTimer::stopTimer();
      if (onMidiEvent) {
        for (int i = 1; i <= 16; ++i)
          onMidiEvent(juce::MidiMessage::allNotesOff(i));
      }
      return;
    }
  }

  // 4. LINK / PLAYBACK LOGIC (Beat-synced start)
  double pStart = pendingStartBeat.load(std::memory_order_acquire);
  if (pStart >= 0.0) {
    auto session = link.captureAppSessionState();
    auto now = link.clock().micros();
    double blockStartBeat = session.beatAtTime(now, quantum);
    double blockDurationSecs = numSamples / sampleRate;
    double bpm = session.tempo();
    double blockEndBeat = blockStartBeat + (blockDurationSecs * (bpm / 60.0));

    if (blockEndBeat >= pStart) {
      double beatDiff = pStart - blockStartBeat;
      int startSampleOffset = 0;
      if (beatDiff > 0) {
        double beatsPerSample = (bpm / 60.0) / sampleRate;
        startSampleOffset = (int)(beatDiff / beatsPerSample);
      }
      startSampleOffset = std::clamp(startSampleOffset, 0, (int)numSamples - 1);

      transport.isPlaying.store(true, std::memory_order_release);
      transport.isPaused.store(false, std::memory_order_release);
      pendingStartBeat.store(-1.0, std::memory_order_release);
      sequenceEndFiredThisPlay.store(false, std::memory_order_release);

      double resumeTick =
          pendingResumeTick.exchange(-1.0, std::memory_order_release);
      if (resumeTick >= 0.0) {
        audioHot.currentTick.store(resumeTick, std::memory_order_release);
        nextEventIndex.store(findIndexForTick(resumeTick),
                             std::memory_order_release);
        linkBeatOffset = pStart - (resumeTick / ppq);
      } else {
        audioHot.currentTick.store(0.0, std::memory_order_release);
        nextEventIndex.store(0, std::memory_order_release);
        linkBeatOffset = pStart;
      }
      transportResetRequested.store(false, std::memory_order_release);

      if (!juce::HighResolutionTimer::isTimerRunning())
        juce::HighResolutionTimer::startTimer(1);
    } else {
      return;
    }
  }

  if (!transport.isPlaying.load(std::memory_order_acquire))
    return;

  if (transportResetRequested.exchange(false)) {
    audioHot.currentTick.store(0.0, std::memory_order_release);
    nextEventIndex.store(0, std::memory_order_release);
  }

  double startTick = audioHot.currentTick.load(std::memory_order_acquire);
  double endTick = 0.0;
  double currentBpm = getBpm();
  double targetBpm = currentBpm;
  double secondsPerBlock = numSamples / sampleRate;

  // 1. BPM HIERARCHY: EXT (MIDI clock) > Link > Int
  // EXT controls master BPM when enabled; Link stays on for transport sync.
  bool isLinked = link.isEnabled();
  bool isExtLocked =
      extSyncActive.load() && smoother && smoother->getIsLocked();

  if (isExtLocked) {
    targetBpm = smoother->getBpm();
  } else if (isLinked) {
    auto session = link.captureAppSessionState();
    targetBpm = session.tempo();
  }

  // 2. APPLY BPM TO TICKS CALCULATION
  double ticksPerSecond = (targetBpm / 60.0) * ppq;
  double ticksThisBlock = ticksPerSecond * secondsPerBlock;

  // 3. TRANSPORT MOVEMENT & PHASE LOCK (PI controller for anti-jitter)
  // EXT controls BPM; Link controls session sync. Gentler PI reduces phase
  // jitter.
  if (isLinked) {
    auto session = link.captureAppSessionState();
    auto now = link.clock().micros();
    double targetBeat = session.beatAtTime(now, quantum);

    double localBeat = startTick / ppq;
    double actualBeat = localBeat + linkBeatOffset;

    double latencyBeats = (outputLatency.load() / 1000.0) * (targetBpm / 60.0);
    actualBeat += latencyBeats;

    double error = targetBeat - actualBeat;
    while (error > quantum * 0.5)
      error -= quantum;
    while (error < -quantum * 0.5)
      error += quantum;

    double absError = std::abs(error);

    // PI controller (Kp=0.015, Ki=0.0008) for smoother lock, less jitter
    constexpr double Kp = 0.015;
    constexpr double Ki = 0.0008;
    constexpr double maxIntegral = 0.05;
    linkPhaseIntegral =
        std::clamp(linkPhaseIntegral + error * Ki, -maxIntegral, maxIntegral);
    double correction = error * Kp + linkPhaseIntegral;

    if (absError > 1.0) {
      // Large error: jump immediately and reset integral
      localBeat += error;
      startTick = localBeat * ppq;
      linkPhaseIntegral = 0.0;
    } else if (absError > 0.0001) {
      ticksThisBlock += (correction * ppq);
    }

    // Update sync quality for UI (1 = locked, 0 = large error)
    float quality = 1.0f - (float)juce::jmin(1.0, absError / 0.1);
    syncQuality.store(quality, std::memory_order_relaxed);
  } else {
    linkPhaseIntegral = 0.0;
    syncQuality.store(1.0f, std::memory_order_relaxed);
  }

  endTick = startTick + ticksThisBlock;
  // Do NOT store currentTick here — only at end of block. Otherwise we
  // overwrite a reset (position 0) and playhead shows wrong position; also
  // causes feedback perception from playhead jumping ahead.

  // Latency & Modulation (outputLatency is in ms)
  double latencyOffsetTicks =
      (outputLatency.load() / 1000.0) * (currentBpm / 60.0) * ppq;
  lfo.advance((int)numSamples);
  float lfoVal = lfo.getCurrentValue();
  lfoPhaseForUI.store(lfo.getPhaseNormalized(), std::memory_order_relaxed);

  // 4. OUTPUT LFO TO NETWORK (Throttled) - Lock-free OscAirlock only
  if (++lfoThrottle > lfoThrottleInterval) {
    lfoThrottle = 0;
    if (airlockRef && airlockRef->getNumReady() < OscAirlock::Capacity - 10)
      airlockRef->push(BridgeEvent(EventType::VisualParam,
                                   EventSource::EngineSequencer, 0, 900,
                                   lfoVal));
  }

  // MIDI Clock Generation (Sample Accurate) - skip if THRU is forwarding
  // external clock
  if (sendMidiClock &&
      (!isExternalClockForwarding || !isExternalClockForwarding())) {
    double pulsesPerBeat = 24.0;
    double pulsesPerSecond = (currentBpm / 60.0) * pulsesPerBeat;
    samplesPerMidiClock = sampleRate / pulsesPerSecond;
    for (int i = 0; i < (int)numSamples; ++i) {
      midiClockAccumulator += 1.0;
      if (midiClockAccumulator >= samplesPerMidiClock) {
        midiClockAccumulator -= samplesPerMidiClock;
        if (onMidiEvent)
          onMidiEvent(juce::MidiMessage(0xF8));
      }
    }
  }

  // --- ROLL / STUTTER LOGIC (Atomic-driven) ---
  {
    int rollDiv = audioHot.rollInterval.load(std::memory_order_relaxed);
    if (rollDiv > 0) {
      double rollTicks = ppq / (rollDiv / 4.0);
      double rollPos = std::fmod(startTick, rollTicks);
      if (rollPos < lastRollPos) {
        int note = lastRollNote.load(std::memory_order_relaxed);
        float vel = lastRollVel.load(std::memory_order_relaxed);
        int ch = lastRollCh.load(std::memory_order_relaxed);
        if (note >= 0 && vel > 0.001f && onMidiEvent)
          onMidiEvent(juce::MidiMessage::noteOn(ch, note, vel));
      }
      lastRollPos = rollPos;
    } else {
      lastRollPos = -1.0;
    }
  }

  // 4. PROCESS MIDI SEQUENCE (Sample Accurate with real-time Swing)
  // Recalculate index if wildly off (e.g. after loop or seek)
  if (currentIdx < seqSize && currentIdx > 0) {
    double eventTick = seq[currentIdx].getTimeStamp();
    if (std::abs(eventTick - startTick) > ppq) {
      currentIdx = findIndexForTick(startTick);
      nextEventIndex.store(currentIdx, std::memory_order_relaxed);
    }
  }
  if (currentIdx >= seqSize && seqSize > 0) {
    currentIdx = seqSize;
    nextEventIndex.store(seqSize, std::memory_order_relaxed);
  }
  double beatsPerSample = (getBpm() / 60.0) / sampleRate;
  double ticksPerSample = beatsPerSample * ppq;

  for (int i = 0; i < (int)numSamples; ++i) {
    double nextTick = startTick + (ticksPerSample * i);

    // SAFE LOOP: use local currentIdx, store back to atomic
    while (currentIdx < seqSize) {
      if (currentIdx < 0 || currentIdx >= seqSize)
        break;

      const auto &msg = seq[currentIdx];
      double originalTick = msg.getTimeStamp();
      // .mid playback: use straight time (no swing); swing applies to step
      // sequencer only
      double effectiveTick = originalTick - latencyOffsetTicks;

      if (effectiveTick > nextTick)
        break;

      // Only send channel messages (notes, CC, etc.) — skip meta/sysex to avoid
      // message flood and feedback; still advance index so playback stays in
      // sync
      bool isChannelMessage = msg.isNoteOnOrOff() || msg.isController() ||
                              msg.isPitchWheel() || msg.isProgramChange() ||
                              msg.isAftertouch() || msg.isChannelPressure();
      if (isChannelMessage && onMidiEvent) {
        int transpose = transport.globalTranspose.load();
        if (transpose != 0 && (msg.isNoteOn() || msg.isNoteOff())) {
          auto transMsg = msg;
          transMsg.setNoteNumber(
              juce::jlimit(0, 127, msg.getNoteNumber() + transpose));
          onMidiEvent(transMsg);
        } else {
          onMidiEvent(msg);
        }
      }

      currentIdx++;
      nextEventIndex.store(currentIdx, std::memory_order_relaxed);
    }
  }

  // 4b. If sequencer data was updated (steps cleared/edited), send
  // all-notes-off for sequencer channels so notes stop immediately
  if (pendingSequencerAllNotesOff.exchange(false, std::memory_order_acquire) &&
      onMidiEvent) {
    for (int slot = 0; slot < kMaxSequencerSlots; ++slot) {
      int ch = sequencerChannels[slot].load(std::memory_order_relaxed);
      if (ch >= 1 && ch <= 16)
        onMidiEvent(juce::MidiMessage::allNotesOff(ch));
    }
  }

  // 5. SEQUENCER PLAYBACK (once per block — was wrongly inside sample loop,
  //    causing each step to fire numSamples times and flood OSC/MIDI)
  {
    double ticksPer16th = ppq / 4.0;
    int stepIndexStart = (int)(startTick / ticksPer16th);
    int stepIndexEnd = (int)(endTick / ticksPer16th);
    int stepFirst = (stepIndexStart == 0 && startTick <= ticksPer16th * 0.5)
                        ? 0
                        : stepIndexStart + 1;

    if (stepFirst <= stepIndexEnd) {
      int loopSteps = momentaryLoopSteps.load(std::memory_order_relaxed);
      int baseLimit = 16;
      for (int i = 0; i < kMaxSequencerSlots; ++i) {
        if (sequencerRefs[i] != nullptr) {
          baseLimit = (int)sequencerRefs[i]->numSteps;
          break;
        }
      }
      int limit = (loopSteps > 0) ? loopSteps : baseLimit;
      limit = juce::jlimit(1, 128, limit);

      for (int slot = 0; slot < kMaxSequencerSlots; ++slot) {
        const auto &st = state->sequencerTracks[slot];
        int seqCh = sequencerChannels[slot].load(std::memory_order_relaxed);

        for (int s = stepFirst; s <= stepIndexEnd; ++s) {
          int step = ((s % limit) + limit) % limit;
          if (slot == 0)
            currentVisualStep.store(step);

          int maskIndex = step / 64;
          int bitIndex = step % 64;
          bool isStepActive = (st.activeStepMask[maskIndex] >> bitIndex) & 1ULL;

          if (isStepActive) {
            float globalProb =
                globalProbability.load(std::memory_order_relaxed);
            bool isMixerActive = true;
            if (isChannelActive)
              isMixerActive = isChannelActive(seqCh);

            if (isMixerActive) {
              const auto &voiceVels = st.velocities[step];
              const auto &voiceNotes = st.notes[step];
              const auto &voiceProbs = st.probabilities[step];

              for (int v = 0; v < 8; ++v) {
                float vel = voiceVels[v];
                float prob = voiceProbs[v] * globalProb;

                if (vel > 0.001f && humanizeParams.rng.nextFloat() <= prob) {
                  int note = voiceNotes[v];
                  float baseVel = vel;

                  if (humanizeParams.velocityAmt > 0.0f) {
                    float jitter =
                        (humanizeParams.rng.nextFloat() - 0.5f) * 2.0f;
                    baseVel += jitter * humanizeParams.velocityAmt;
                    baseVel = juce::jlimit(0.01f, 1.0f, baseVel);
                  }

                  double straightBeat = (double)s * 0.25;
                  double swungBeat =
                      swingProcessor.applySwing(s, straightBeat, 0.25);
                  double stepTick = swungBeat * ppq;
                  double offsetTicks = stepTick - startTick;

                  if (humanizeParams.timingAmt > 0.0f) {
                    int maxJitter = (int)((humanizeParams.timingAmt / 1000.0f) *
                                          sampleRate);
                    if (maxJitter > 0) {
                      double jitterSamples =
                          (double)humanizeParams.rng.nextInt(maxJitter);
                      offsetTicks +=
                          (jitterSamples / numSamples) * (endTick - startTick);
                    }
                  }

                  double fraction = juce::jlimit(
                      0.0, 1.0, offsetTicks / (endTick - startTick));
                  double offsetSamples = fraction * numSamples;

                  float sendVel = juce::jlimit(0.0f, 1.0f, baseVel);
                  juce::MidiMessage m =
                      juce::MidiMessage::noteOn(seqCh, note, sendVel);
                  m.setTimeStamp(std::max(0.0, offsetSamples));

                  if (onMidiEvent)
                    onMidiEvent(m);
                  if (onSequencerNoteSent)
                    onSequencerNoteSent();

                  juce::MidiMessage mo =
                      juce::MidiMessage::noteOff(seqCh, note);
                  mo.setTimeStamp(numSamples > 1.0 ? numSamples - 1.0 : 0.0);
                  if (onMidiEvent)
                    onMidiEvent(mo);
                }
              }
            }
          }
        }
      }
    }
  }

  // --- Loop Logic ---
  if (seqSize > 0 && loopSettings.enabled.load(std::memory_order_relaxed)) {
    double loopEnd = loopSettings.endBeat.load() * ppq;
    double loopStart = loopSettings.startBeat.load() * ppq;
    if (endTick >= loopEnd) {
      int maxIter = loopSettings.maxIterations.load();
      int curIter = loopSettings.currentIteration.fetch_add(1);
      if (maxIter >= 0 && curIter >= maxIter) {
        transport.isPlaying.store(false, std::memory_order_release);
        loopSettings.currentIteration.store(0);
        if (!sequenceEndFiredThisPlay.exchange(true,
                                               std::memory_order_acq_rel) &&
            onSequenceEnd)
          onSequenceEnd();
      } else {
        double overshoot = endTick - loopEnd;
        endTick = loopStart + overshoot;
        audioHot.currentTick.store(endTick, std::memory_order_release);
        nextEventIndex.store(findIndexForTick(loopStart),
                             std::memory_order_release);
      }
    }
  }

  // Sequence End & Gapless
  if (seqSize > 0 && currentIdx >= seqSize) {
    if (autoPlayNext) {
      std::shared_ptr<EngineState> next = std::atomic_load(&nextState);
      if (next) {
        // CRITICAL: Use exchange so we get the old state and defer its
        // deletion. Never destroy EngineState on the audio thread.
        auto oldState = std::atomic_exchange(&activeState, next);
        std::atomic_store(&nextState, std::shared_ptr<EngineState>(nullptr));
        if (oldState)
          deadPool.deleteAsync(std::move(oldState));
        autoPlayNext = false;
        audioHot.currentTick.store(0.0, std::memory_order_release);
        nextEventIndex.store(0, std::memory_order_release);
        return;
      }
    }
    if (!sequenceEndFiredThisPlay.exchange(true, std::memory_order_acq_rel) &&
        onSequenceEnd)
      onSequenceEnd();
  }

  audioHot.currentTick.store(endTick, std::memory_order_release);

  // Scheduler
  if (scheduler) {
    juce::MidiBuffer scheduledBuffer;
    scheduler->processBlock(scheduledBuffer, (int)numSamples, currentBpm,
                            sampleRate);
    if (onMidiEvent && !scheduledBuffer.isEmpty()) {
      for (const auto metadata : scheduledBuffer)
        onMidiEvent(metadata.getMessage());
    }
  }
}

double AudioEngine::getBpm() {
  // EXT MIDI clock drives master BPM when active
  if (extSyncActive.load() && smoother != nullptr && smoother->getIsLocked())
    return smoother->getBpm();
  return link.isEnabled() ? link.captureAppSessionState().tempo()
                          : internalBpm.load();
}

void AudioEngine::setBpm(double bpm) {
  if (link.isEnabled()) {
    auto s = link.captureAppSessionState();
    s.setTempo(bpm, link.clock().micros());
    link.commitAppSessionState(s);
  }
  internalBpm.store(bpm);
}

double AudioEngine::getTicksPerQuarter() const {
  auto s = std::atomic_load(&activeState);
  return s ? s->ticksPerQuarter : 960.0;
}

double AudioEngine::getLoopLengthTicks() const {
  auto s = std::atomic_load(&activeState);
  if (!s || s->sequence.empty())
    return 0.0;
  return s->sequence.back().getTimeStamp();
}

double AudioEngine::getCurrentBeat() {
  return audioHot.currentTick.load(std::memory_order_acquire) /
         getTicksPerQuarter();
}

void AudioEngine::setQuantum(double q) {
  if (q >= 1.0)
    quantum = q;
}

void AudioEngine::setSequence(const juce::MidiMessageSequence &seq, double ppq,
                              double fileBpm) {
  transport.isPlaying.store(false);
  transport.isPaused.store(false);

  pendingStartBeat.store(-1.0);
  pendingStopBeat.store(-1.0);
  pendingPauseBeat.store(-1.0);
  pendingResumeTick.store(-1.0);

  auto newState = stateRecycler.checkout();
  newState->clear();
  newState->ticksPerQuarter = (ppq > 0) ? ppq : 960.0;

  auto current = std::atomic_load(&activeState);
  if (current) {
    newState->sequencerTracks = current->sequencerTracks;
  }

  newState->sequence.reserve(seq.getNumEvents());
  for (int i = 0; i < seq.getNumEvents(); ++i) {
    newState->sequence.push_back(seq.getEventPointer(i)->message);
  }
  // Sort by timestamp so multi-track .mid plays in correct order (merge order ≠
  // time order)
  std::sort(newState->sequence.begin(), newState->sequence.end(),
            [](const juce::MidiMessage &a, const juce::MidiMessage &b) {
              return a.getTimeStamp() < b.getTimeStamp();
            });

  auto oldState = std::atomic_exchange(&activeState, newState);
  if (oldState)
    stateRecycler.recycle(oldState);

  // 5. Update Transport
  transport.ticksPerQuarter.store(newState->ticksPerQuarter);
  if (fileBpm > 0)
    internalBpm.store(fileBpm);

  // 6. Reset Position
  audioHot.currentTick.store(0.0);
  nextEventIndex.store(0, std::memory_order_release);

  syncQuality.store(1.0f);
}

void AudioEngine::queueNextSequence(const juce::MidiMessageSequence &seq,
                                    double ppq) {
  auto newState = std::make_shared<EngineState>();
  newState->ticksPerQuarter = ppq;
  newState->sequence.reserve(seq.getNumEvents());
  for (int i = 0; i < seq.getNumEvents(); ++i) {
    newState->sequence.push_back(seq.getEventPointer(i)->message);
  }
  std::stable_sort(newState->sequence.begin(), newState->sequence.end(),
                   [](const juce::MidiMessage &a, const juce::MidiMessage &b) {
                     return a.getTimeStamp() < b.getTimeStamp();
                   });
  auto current = std::atomic_load(&activeState);
  if (current) {
    newState->sequencerTracks = current->sequencerTracks;
  }
  std::atomic_store(&nextState, newState);
  autoPlayNext = true;
}

void AudioEngine::resetTransport() {
  transportResetRequested.store(true);
  pendingSeekTarget.store(-1.0, std::memory_order_release);
  isQuantizedSeek.store(false, std::memory_order_release);
  // Always reset position so .mid starts at beginning (with or without Link)
  audioHot.currentTick.store(0.0, std::memory_order_release);
  nextEventIndex.store(0, std::memory_order_release);
}
void AudioEngine::resetTransportForLoop() {
  resetTransport();
  // So the next time the sequence ends we fire onSequenceEnd again (Loop One /
  // Loop All)
  sequenceEndFiredThisPlay.store(false, std::memory_order_release);
}

void AudioEngine::setSwing(float amount) {
  swingProcessor.setSwingAmount(amount);
}

double AudioEngine::getSwungTick(double originalTick, double ppq,
                                 float swingAmt) const {
  if (swingAmt <= 0.01f)
    return originalTick;

  double ticksPer16th = ppq / 4.0;
  double gridPos = originalTick / ticksPer16th;
  int stepIndex = (int)gridPos;

  // Only swing odd 16th notes (the "and" of the beat)
  if (stepIndex % 2 != 0) {
    double swingOffset = ticksPer16th * (swingAmt * 0.33);
    return originalTick + swingOffset;
  }
  return originalTick;
}

void AudioEngine::updateSequencerData(int slot,
                                      const SequencerPanel::EngineData &data) {
  if (slot < 0 || slot >= kMaxSequencerSlots)
    return;
  // 1. RECYCLE: Get a pre-allocated state object from the pool
  auto newState = stateRecycler.checkout();

  // 2. COPY: Copy existing state from active
  auto current = std::atomic_load(&activeState);
  if (current) {
    newState->ticksPerQuarter = current->ticksPerQuarter;
    newState->sequence = current->sequence;
    newState->sequencerTracks = current->sequencerTracks;
  }

  // 3. UPDATE: Apply this slot's sequencer data
  newState->sequencerTracks[slot].velocities = data.sequencerData.velocities;
  newState->sequencerTracks[slot].notes = data.sequencerData.notes;
  newState->sequencerTracks[slot].probabilities =
      data.sequencerData.probabilities;
  newState->sequencerTracks[slot].activeStepMask =
      data.sequencerData.activeStepMask;

  // 4. SWAP: Atomic exchange
  auto oldState = std::atomic_exchange(&activeState, newState);

  if (oldState)
    stateRecycler.recycle(oldState);

  // 5. Request all-notes-off for sequencer channels on next block so
  // steps-cleared/edited stops sounding immediately
  pendingSequencerAllNotesOff.store(true, std::memory_order_release);
}

void AudioEngine::setSequencer(int slot, SequencerPanel *s) {
  if (slot >= 0 && slot < kMaxSequencerSlots)
    sequencerRefs[slot] = s;
}

void AudioEngine::setSequencerChannel(int slot, int ch) {
  if (slot >= 0 && slot < kMaxSequencerSlots)
    sequencerChannels[slot].store(juce::jlimit(1, 16, ch),
                                  std::memory_order_relaxed);
}

int AudioEngine::getSequencerChannel(int slot) const {
  if (slot >= 0 && slot < kMaxSequencerSlots)
    return sequencerChannels[slot].load(std::memory_order_relaxed);
  return 1;
}

void AudioEngine::seek(double beat) {
  bool playing = transport.isPlaying.load(std::memory_order_acquire);
  bool linked = link.isEnabled();

  if (!playing || !linked) {
    double ppq = transport.ticksPerQuarter.load(std::memory_order_acquire);
    double newTick = beat * ppq;
    audioHot.currentTick.store(newTick, std::memory_order_release);
    nextEventIndex.store(findIndexForTick(newTick), std::memory_order_release);

    if (linked) {
      // Update local offset only; never commit Link session from seek (keeps
      // peers in sync)
      auto session = link.captureAppSessionState();
      auto time = link.clock().micros();
      double linkBeat = session.beatAtTime(time, quantum);
      linkBeatOffset = linkBeat - beat;
    } else {
      pendingStartBeat.store(-1.0, std::memory_order_release);
    }

    if (airlockRef && airlockRef->getNumReady() < OscAirlock::Capacity - 10)
      airlockRef->push(BridgeEvent(EventType::VisualParam,
                                   EventSource::EngineSequencer, 0, 999,
                                   (float)beat));
    return;
  }

  // When Link + playing: throttle scrub so we don't spam pending seek (avoids
  // phase drift)
  double pending = pendingSeekTarget.load(std::memory_order_acquire);
  const double scrubThreshold = 0.25;
  if (pending >= 0.0 && std::abs(beat - pending) < scrubThreshold)
    return;
  pendingSeekTarget.store(beat, std::memory_order_release);
  isQuantizedSeek.store(true, std::memory_order_release);
}

int AudioEngine::findIndexForTick(double tick) {
  auto s = std::atomic_load(&activeState);
  if (!s || s->sequence.empty())
    return 0;

  // Binary search for the event at or after 'tick'
  auto it = std::lower_bound(s->sequence.begin(), s->sequence.end(), tick,
                             [](const juce::MidiMessage &m, double t) {
                               return m.getTimeStamp() < t;
                             });

  return (int)std::distance(s->sequence.begin(), it);
}

void AudioEngine::nudge(double amt) {
  double tick = audioHot.currentTick.load(std::memory_order_acquire);
  audioHot.currentTick.store(tick + amt * transport.ticksPerQuarter.load(),
                             std::memory_order_release);
}

void AudioEngine::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
  lfo.setSampleRate(sampleRate);
  samplesPerMidiClock = sampleRate / (getBpm() / 60.0 * 24.0);
  lfoThrottleInterval = (int)(sampleRate / 30.0);
  if (lfoThrottleInterval < 1)
    lfoThrottleInterval = 1;
}

void AudioEngine::hiResTimerCallback() {}

void AudioEngine::setNewSequence(std::vector<EditableNote> seq) {
  // 1. Get a clean state object
  auto newState = stateRecycler.checkout();
  newState->clear();
  newState->ticksPerQuarter = 960.0;

  // 2. Convert GUI Notes (EditableNote) to Audio Engine Events
  // (MidiMessage) — apply per-note velocity curve
  for (const auto &n : seq) {
    double startTick = n.startBeat * 960.0;
    double endTick = (n.startBeat + n.durationBeats) * 960.0;
    float vel = EditableNote::applyVelocityCurve(n.velocity, n.velocityCurve);

    auto on = juce::MidiMessage::noteOn(n.channel, n.noteNumber, vel);
    on.setTimeStamp(startTick);
    newState->sequence.push_back(on);

    auto off = juce::MidiMessage::noteOff(n.channel, n.noteNumber);
    off.setTimeStamp(endTick);
    newState->sequence.push_back(off);
  }

  // 3. Sort by Time (CRITICAL for the sequencer to play correctly)
  std::sort(newState->sequence.begin(), newState->sequence.end(),
            [](const juce::MidiMessage &a, const juce::MidiMessage &b) {
              return a.getTimeStamp() < b.getTimeStamp();
            });

  // 4. Hot Swap the State
  auto oldState = std::atomic_exchange(&activeState, newState);
  if (oldState) {
    stateRecycler.recycle(oldState);
  }

  // 5. CRITICAL FIX: Don't hard reset if playing!
  // Allows live coding/drawing without stutter.
  if (!transport.isPlaying.load(std::memory_order_acquire)) {
    resetTransport();
  }
  // When playing, bounds check in processAudioBlock handles index validity.
}
