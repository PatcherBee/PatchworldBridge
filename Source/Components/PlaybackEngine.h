/*
  ==============================================================================
    Source/Components/PlaybackEngine.h
    Role: Handles strict timing, sequencing, and Link sync.
  ==============================================================================
*/
#pragma once
#include "MidiScheduler.h"
#include "Sequencer.h"
#include <JuceHeader.h>
#include <ableton/Link.hpp>

class PlaybackEngine : public juce::HighResolutionTimer {
public:
  PlaybackEngine() : midiScheduler() {
    // Initialize Link with default 120 BPM
    link = new ableton::Link(120.0);
    link->enable(true);
    link->enableStartStopSync(true);
  }

  ~PlaybackEngine() override {
    stopTimer();
    if (link) {
      link->enable(false);
      delete link;
    }
  }

  // --- Public Control Interface ---
  void start() {
    if (isPlaying)
      return;
    pendingSyncStart = true;
    isPlaying = true;

    // Reset internal clock reference
    internalPlaybackStartTimeMs = juce::Time::getMillisecondCounterHiRes();

    // Send Start signal immediately (handled by callback)
    if (onMidiTransport)
      onMidiTransport(true);

    startTimer(1); // 1ms Interval
  }

  void stop() {
    isPlaying = false;
    pendingSyncStart = false;
    beatsPlayedOnPause = 0.0;
    playbackCursor = 0;
    lastProcessedBeat = -1.0;

    if (link && link->isEnabled()) {
      auto session = link->captureAppSessionState();
      session.setIsPlaying(false, link->clock().micros());
      link->commitAppSessionState(session);
    }

    if (onMidiTransport)
      onMidiTransport(false);
    stopTimer();
  }

  void setBpm(double bpm) {
    currentBpm = bpm;
    if (link) {
      auto state = link->captureAppSessionState();
      state.setTempo(bpm, link->clock().micros());
      link->commitAppSessionState(state);
    }
  }

  void setMidiSequence(const juce::MidiMessageSequence &seq, double ppq) {
    const juce::ScopedLock sl(engineLock);
    playbackSeq = seq; // Copy sequence
    ticksPerQuarterNote = ppq;
    sequenceLength = playbackSeq.getEndTime();
    playbackCursor = 0;
  }

  void scheduleNoteOff(int ch, int note, double timeMs) {
    midiScheduler.scheduleNoteOff(ch, note, timeMs);
  }

  void resetTransportForLoop() {
    const juce::ScopedLock sl(engineLock);
    playbackCursor = 0;
    lastProcessedBeat = -1.0;

    double ppq =
        (ticksPerQuarterNote == 0.0) ? 960.0 : std::abs(ticksPerQuarterNote);
    double fileLengthBeats = sequenceLength / ppq;
    if (fileLengthBeats <= 0.001)
      fileLengthBeats = 4.0;

    transportStartBeat += fileLengthBeats;
  }

  // --- Callbacks to MainComponent ---
  std::function<void(const juce::MidiMessage &, int targetCh)> onMidiEvent;
  std::function<void(bool isStart)> onMidiTransport;
  std::function<bool(int ch)> isChannelActive; // Check if mixer channel is mute
  std::function<void()> onSequenceEnd; // Callback for end of MIDI sequence

  // Pointers to data owned by MainComponent (Assumed valid for app lifetime)
  StepSequencer *sequencer = nullptr;

  // Public State Accessors (Thread Safe-ish for UI polling)
  double getCurrentBeat() const { return lastReportedBeat; }
  double getTicksPerQuarter() const { return ticksPerQuarterNote; }
  bool getIsPlaying() const { return isPlaying; }
  ableton::Link *getLink() { return link; } // For visualizer

private:
  // Core Engine Data
  ableton::Link *link = nullptr;
  MidiScheduler midiScheduler;
  juce::CriticalSection engineLock;

  juce::MidiMessageSequence playbackSeq;
  double ticksPerQuarterNote = 960.0;
  double sequenceLength = 0.0;

  // Transport State
  std::atomic<bool> isPlaying{false};
  bool pendingSyncStart = false;
  double transportStartBeat = 0.0;
  double beatsPlayedOnPause = 0.0;
  double internalPlaybackStartTimeMs = 0.0;
  double currentBpm = 120.0;
  double quantum = 4.0;
  double lastReportedBeat = 0.0;

  // Playback State
  int playbackCursor = 0;
  double lastProcessedBeat = -1.0;

  // --- The Critical 1ms Loop ---
  void hiResTimerCallback() override {
    double nowMs = juce::Time::getMillisecondCounterHiRes();

    // 1. Process Scheduler (Note Offs)
    auto dueNotes = midiScheduler.processDueNotes(nowMs);
    for (auto &n : dueNotes) {
      if (onMidiEvent)
        onMidiEvent(juce::MidiMessage::noteOff(n.channel, n.note), n.channel);
    }

    // 2. Calculate Current Beat
    double currentBeat = 0.0;
    if (link && link->isEnabled()) {
      auto session = link->captureAppSessionState();
      auto micros = link->clock().micros();
      currentBeat = session.beatAtTime(micros, quantum);

      // Link Start Sync
      if (pendingSyncStart && isPlaying) {
        if (link->numPeers() == 0 ||
            session.phaseAtTime(micros, quantum) < 0.05) {
          transportStartBeat = currentBeat - beatsPlayedOnPause;
          pendingSyncStart = false;
        } else {
          return; // Wait for phase alignment
        }
      }
    } else if (isPlaying) {
      // Internal Clock
      double elapsedMs = nowMs - internalPlaybackStartTimeMs;
      currentBeat = (elapsedMs * (currentBpm / 60000.0)) + beatsPlayedOnPause;
    }

    lastReportedBeat = currentBeat;
    double playbackBeats = currentBeat - transportStartBeat;

    // Auto-Stop / Count-in Logic could go here... but strictly Engine just
    // runs.

    if (!isPlaying)
      return;

    // 3. MIDI File Playback
    if (!pendingSyncStart) { // playbackSeq handled generally
      const juce::ScopedLock sl(engineLock);

      // Handle Looping (Simplified LoopOne Logic for robustness)
      // Real implementation should probably communicate "Sequence Ended" to
      // main but for now, we assume simple playback or standard handling.

      double ppq =
          (ticksPerQuarterNote == 0) ? 960.0 : std::abs(ticksPerQuarterNote);

      // Playback Loop
      while (playbackCursor < playbackSeq.getNumEvents()) {
        auto *ev = playbackSeq.getEventPointer(playbackCursor);
        double eventBeat = ev->message.getTimeStamp() / ppq;

        if (eventBeat > playbackBeats)
          break;

        if (eventBeat >= lastProcessedBeat) {
          int ch = ev->message.getChannel();
          // Check mixer mute via callback
          if (!isChannelActive || isChannelActive(ch)) {
            auto msg = ev->message;
            // Process Note On/Off transparency or shifts could happen here
            // but we delegate "processing" to the callback ideally.
            // For now, raw pass:
            if (onMidiEvent)
              onMidiEvent(msg, ch);
          }
        }
        playbackCursor++;
      }
      lastProcessedBeat = playbackBeats;

      // Check End
      if (playbackSeq.getNumEvents() > 0 &&
          playbackCursor >= playbackSeq.getNumEvents()) {
        if (onSequenceEnd)
          onSequenceEnd();
        // If the main component wants to Loop, it will call
        // stop/start/setTransportStart in the callback But for tight API loop,
        // we might need LoopOne logic here.
      }
    }

    // 4. Step Sequencer Triggering
    if (sequencer && isPlaying) {
      // Roll/Sequencer Logic adapted
      int currentStepPos = -1;

      if (sequencer->activeRollDiv > 0) {
        if (!sequencer->isRollActive) {
          sequencer->rollCaptureBeat = currentBeat;
          sequencer->isRollActive = true;
        }
        double loopLengthBeats = 4.0 / (double)sequencer->activeRollDiv;

        if (sequencer->currentMode == StepSequencer::Loop) {
          double offset = std::fmod(currentBeat - sequencer->rollCaptureBeat,
                                    loopLengthBeats);
          currentStepPos = (int)((sequencer->rollCaptureBeat + offset) * 4.0) %
                           sequencer->numSteps;
        } else if (sequencer->currentMode == StepSequencer::Roll) {
          currentStepPos = (int)(currentBeat * 4.0) % sequencer->numSteps;
          // Ratchet
          double subStep = (currentBeat * 4.0) - std::floor(currentBeat * 4.0);
          int ratchetCount = std::max(1, sequencer->activeRollDiv / 4);
          double ratchetSize = 1.0 / (double)ratchetCount;
          int substepIdx = (int)(subStep / ratchetSize);

          static int lastSubStepIdx = -1;
          if (substepIdx != lastSubStepIdx &&
              sequencer->isStepActive(currentStepPos)) {
            lastSubStepIdx = substepIdx;
            int note = sequencer->getStepNote(currentStepPos);
            if (note == 0)
              note = (int)sequencer->noteSlider.getValue();
            int ch = sequencer->outputChannel;

            if (!isChannelActive || isChannelActive(ch)) {
              if (onMidiEvent)
                onMidiEvent(juce::MidiMessage::noteOn(ch, note, 1.0f), ch);
              double duration =
                  (60000.0 / currentBpm) / (double)sequencer->activeRollDiv;
              midiScheduler.scheduleNoteOff(ch, note, nowMs + (duration * 0.8));
            }
          }
        }
      } else {
        sequencer->isRollActive = false;
        currentStepPos = (int)(currentBeat * 4.0) % sequencer->numSteps;
      }

      // Standard Grid
      if (currentStepPos != sequencer->currentStep) {
        sequencer->setActiveStep(currentStepPos);
        if (sequencer->isStepActive(currentStepPos)) {
          if (sequencer->activeRollDiv == 0 ||
              sequencer->currentMode != StepSequencer::Roll) {
            int note = sequencer->getStepNote(currentStepPos);
            if (note == 0)
              note = (int)sequencer->noteSlider.getValue();
            int ch = sequencer->outputChannel;

            if (!isChannelActive || isChannelActive(ch)) {
              if (onMidiEvent)
                onMidiEvent(juce::MidiMessage::noteOn(ch, note, 1.0f), ch);
              double stepDur = (60000.0 / currentBpm) / 4.0;
              midiScheduler.scheduleNoteOff(ch, note, nowMs + (stepDur * 0.8));
            }
          }
        }
      }
    }
  }
};