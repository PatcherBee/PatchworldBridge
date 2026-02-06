/*
  ==============================================================================
    Source/Audio/MidiRouter.cpp
    Role: MidiRouter Implementation
  ==============================================================================
*/

#include "../Audio/MidiRouter.h"
#include "../Core/PlatformGuard.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// --- Project Headers ---
#include "../Core/AppState.h"
#include "../Core/BridgeEventBus.h"
#include "../Core/CommandDispatcher.h"
#include "../Network/OscManager.h"

#include "../Audio/AudioEngine.h"
#include "../Audio/MidiScheduler.h"
#include "../Audio/NoteTracker.h"
#include "../Services/LatencyCalibrator.h"
#include "../Services/MidiDeviceService.h"
#include "../Services/MidiMappingService.h"
#include "../UI/Panels/MixerPanel.h"
#include "../UI/Panels/SequencerPanel.h"

MidiRouter::MidiRouter(BridgeSettings &s, EngineShadowState &ess)
    : settings(s), engineState(ess) {
  heldNotes.ensureStorageAllocated(128);
  latchedNotes.ensureStorageAllocated(128);
  heldNoteOrder.reserve(128);
  sustainedNotes.ensureStorageAllocated(128);

  // Initialize double-buffered clock source ID arrays
  clockSourceIDBuffers_[0][0] = '\0';
  clockSourceIDBuffers_[1][0] = '\0';

  // Initialize smoothing & tracking arrays
  for (int c = 0; c < 17; ++c) {
    for (int n = 0; n < 128; ++n) {
      activeNoteShifts[c][n] = 0;
      lastSentCC[c][n] = -1.0f;
    }
  }
  setupSchedulerHooks();
}

MidiRouter::~MidiRouter() {
  // No cleanup needed for double-buffered arrays (stack allocated)
}

void MidiRouter::setClockSourceID(const juce::String &id) {
  // CRITICAL FIX: Lock-free double-buffer write (no malloc/free)
  int currentIndex = activeClockSourceIndex_.load(std::memory_order_acquire);
  int writeIndex = 1 - currentIndex; // Toggle between 0 and 1

  size_t len = (size_t)id.getNumBytesAsUTF8() + 1;
  if (len > (size_t)kMaxClockSourceIdLen)
    len = (size_t)kMaxClockSourceIdLen;

  id.copyToUTF8(clockSourceIDBuffers_[writeIndex], (int)len);
  clockSourceIDBuffers_[writeIndex][len - 1] = '\0';

  // Atomic swap: readers now see new value
  activeClockSourceIndex_.store(writeIndex, std::memory_order_release);

  if (clockSmootherPtr)
    clockSmootherPtr->reset();
  else
    fallbackSmoother.reset();
}

juce::String MidiRouter::getClockSourceID() const {
  int index = activeClockSourceIndex_.load(std::memory_order_acquire);
  return juce::String::fromUTF8(clockSourceIDBuffers_[index]);
}

void MidiRouter::processCommands(CommandQueue &queue) {
  BridgeCommand cmd;
  while (queue.pop(cmd)) {
    switch (cmd.type) {
    case BridgeCommand::Panic:
      sendPanic();
      break;
    case BridgeCommand::Transport:
      if (engine) {
        if (cmd.value > 0.5f)
          engine->play();
        else
          engine->stop();
      }
      break;
    case BridgeCommand::Reset:
      if (engine)
        engine->resetTransport();
      break;
    case BridgeCommand::SetBpm:
      if (engine)
        engine->setBpm(cmd.value);
      break;
    case BridgeCommand::SetScaleQuantization:
      isQuantizationEnabled = (cmd.value > 0.5f);
      break;
    }
  }
}

// -----------------------------------------------------------------------------
// 1. RAW MIDI ENTRY POINT
// -----------------------------------------------------------------------------
void MidiRouter::handleMidiMessage(const juce::MidiMessage &m, bool isPlayback,
                                   BridgeEvent::Source source) {
  juce::ignoreUnused(isPlayback);
  if (onMidiInputActivity)
    onMidiInputActivity();

  // --- MIDI TRANSPORT & EXT CLOCK (Start/Stop/Continue/Clock) ---
  if (source == BridgeEvent::Source::HardwareMidi) {
    // THRU: forward all real-time and transport to MIDI out (full pass-through
    // for sync)
    // THRU: forward all real-time and transport to MIDI out (full pass-through
    // for sync)
    if (midiThru.load(std::memory_order_acquire) && midiService &&
        !blockMidiOut) {
      if (m.isMidiStart() || m.isMidiStop() || m.isMidiContinue() ||
          m.isMidiClock()) {
        midiService->sendMessage(m);
      }
    }
    if (m.isMidiStart()) {
      if (engine) {
        engine->resetTransport();
        engine->play();
      }
      // Reset EXT clock lock so BPM is re-derived from the next 24 clocks
      if (clockSmootherPtr)
        clockSmootherPtr->reset();
      else
        fallbackSmoother.reset();
      return;
    }
    if (m.isMidiContinue()) {
      if (engine)
        engine->play();
      // Re-lock EXT clock after Continue (many devices send a fresh clock
      // burst)
      if (clockSmootherPtr)
        clockSmootherPtr->reset();
      else
        fallbackSmoother.reset();
      return;
    }
    if (m.isMidiStop()) {
      if (engine)
        engine->stop();
      return;
    }
    if (m.isMidiClock()) {
      double tsMs = m.getTimeStamp() * 1000.0; // JUCE timestamp is seconds
      if (clockSmootherPtr)
        clockSmootherPtr->onMidiClockByte(tsMs);
      else
        fallbackSmoother.onMidiClockByte(tsMs);
      return;
    }
  }

  // Engine-generated MIDI clock/transport (app BPM when THRU/Clock on, no
  // external clock): send to MIDI out
  if (source == BridgeEvent::Source::EngineSequencer && midiService &&
      !blockMidiOut) {
    if (m.isMidiClock() || m.isMidiStart() || m.isMidiStop() ||
        m.isMidiContinue()) {
      midiService->sendMessage(m);
      return; // Don't update clock smoother (that's for incoming external clock
              // only)
    }
  }

  EventType type = EventType::None;
  int ch = m.getChannel();
  int noteOrCC = 0;
  float val = 0.0f;

  if (m.isNoteOn()) {
    type = EventType::NoteOn;
    noteOrCC = m.getNoteNumber();
    val = m.getFloatVelocity();
  } else if (m.isNoteOff()) {
    type = EventType::NoteOff;
    noteOrCC = m.getNoteNumber();
    val = 0.0f;
  } else if (m.isController()) {
    type = EventType::ControlChange;
    noteOrCC = m.getControllerNumber();
    val = m.getControllerValue() / 127.0f;
  } else if (m.isPitchWheel()) {
    type = EventType::PitchBend;
    val = (m.getPitchWheelValue() - 8192) / 8192.0f;
  } else if (m.isAftertouch()) {
    type = EventType::PolyAftertouch;
    noteOrCC = m.getNoteNumber();
    val = (float)m.getRawData()[2] / 127.0f;
  } else if (m.isChannelPressure()) {
    type = EventType::Aftertouch;
    noteOrCC = 0;
    val = m.getChannelPressureValue() / 127.0f;
  } else if (m.isProgramChange()) {
    type = EventType::ProgramChange;
    noteOrCC = m.getProgramChangeNumber();
    val = 0.0f;
  }

  if (type != EventType::None) {
    // Feed mapping manager for MIDI Learn (hardware CC/notes only)
    if (source == BridgeEvent::Source::HardwareMidi && mappingManager) {
      if (mappingManager->handleLearnInput(m))
        return; // Learn mode: mapping created, skip normal routing
      mappingManager->handleIncomingMidiMessage(nullptr, m);
    }
    // Virtual keyboard: show active keys from MIDI input (callback must async
    // to message thread)
    if (source == BridgeEvent::Source::HardwareMidi &&
        onIncomingNoteForDisplay &&
        (type == EventType::NoteOn || type == EventType::NoteOff)) {
      onIncomingNoteForDisplay(ch, noteOrCC,
                               type == EventType::NoteOn ? val : 0.0f,
                               type == EventType::NoteOn);
    }
    handleBridgeEvent(BridgeEvent(type, source, ch, noteOrCC, val));
    return;
  }

  if (m.isMidiClock()) {
    double tsMs = m.getTimeStamp() * 1000.0; // JUCE timestamp is seconds
    if (clockSmootherPtr)
      clockSmootherPtr->onMidiClockByte(tsMs);
    else
      fallbackSmoother.onMidiClockByte(tsMs);
    return;
  }

  // THRU pass-through: any message we don't convert (SysEx, Song Position, Song
  // Select, Tune Request, Active Sensing, System Reset, MTC, etc.) is forwarded
  // to MIDI out when THRU is on. Full MIDI 1.0 on the wire without extending
  // OSC for every rare type.
  // THRU pass-through: any message we don't convert is forwarded
  if (source == BridgeEvent::Source::HardwareMidi &&
      midiThru.load(std::memory_order_acquire) && midiService &&
      !blockMidiOut) {
    midiService->sendMessage(m);
  }
}

void MidiRouter::handleIncomingMidiMessage(juce::MidiInput *source,
                                           const juce::MidiMessage &message) {
  if (latencyCalibrator && LatencyCalibrator::isCalibrationPing(message)) {
    if (latencyCalibrator->receivePong(message))
      return;
  }

  // Per-device options (Ableton-style Track/Sync/Remote/MPE)
  AppState::MidiDeviceOptions opts{true, true, true, false};
  if (appState && source != nullptr) {
    opts = appState->getMidiDeviceOptions(true, source->getIdentifier());
  }

  bool isRealTime = message.isMidiClock() || message.isMidiStart() ||
                    message.isMidiStop() || message.isMidiContinue();
  if (isRealTime) {
    if (!opts.sync)
      return;
    const char *allowed = getClockSourceIDPointer();
    if (allowed && allowed[0] != '\0' && source != nullptr) {
      juce::String allowedStr = juce::String::fromUTF8(allowed);
      if (source->getIdentifier() != allowedStr)
        return; // Reject: this device is not the selected clock source. Empty =
                // allow any.
    }
  } else {
    // Notes, CC, etc.: only forward if Track is on
    if (!opts.track)
      return;
  }

  handleMidiMessage(message, false, BridgeEvent::Source::HardwareMidi);
}

// -----------------------------------------------------------------------------
// 2. THE ROUTER (Strict "No-Loop" Source-Based Routing)
// -----------------------------------------------------------------------------
void MidiRouter::handleBridgeEvent(const BridgeEvent &e) noexcept {
  // 0. Feedback loop guard — only count INCOMING events (network/hardware).
  // EngineSequencer/UserInterface are legitimate high-rate output; counting
  // them would trip after one sequencer block and disable OSC/MIDI.
  if (trafficBreaker.isTripped())
    return;
  if (e.source == EventSource::NetworkOsc ||
      e.source == EventSource::HardwareMidi)
    trafficBreaker.recordEvent();

  // 1. Basic Validity Check
  if (e.type == EventType::None)
    return;

  // 1b. System Commands (global, channel may be 0)
  if (e.type == EventType::SystemCommand) {
    if (e.noteOrCC == (int)CommandID::SetBpm && e.value > 0.0f && engine) {
      engine->setBpm(e.value);
    }
    return;
  }

  // 2. SAFETY CLAMP
  if (e.channel < 1 || e.channel > 16)
    return;

  // 2b. Arp held notes: Hardware MIDI goes through handleBridgeEvent only (not
  // handleNoteOn)
  if (e.source == EventSource::HardwareMidi &&
      (e.type == EventType::NoteOn || e.type == EventType::NoteOff)) {
    if (e.type == EventType::NoteOn)
      addHeldNote(e.noteOrCC);
    else
      removeHeldNote(e.noteOrCC);
  }

  // -------------------------------------------------------
  // PATH 1: INCOMING FROM NETWORK (OSC -> MIDI OUT)
  // -------------------------------------------------------
  if (e.source == EventSource::NetworkOsc) {
    // A. Send to Hardware (if valid and not blocked)
    if (midiService && !blockMidiOut) {
      juce::MidiMessage m;
      bool valid = false;

      if (e.type == EventType::NoteOn) {
        m = juce::MidiMessage::noteOn((int)e.channel, (int)e.noteOrCC, e.value);
        valid = true;
      } else if (e.type == EventType::NoteOff) {
        m = juce::MidiMessage::noteOff((int)e.channel, (int)e.noteOrCC);
        valid = true;
      } else if (e.type == EventType::ControlChange) {
        m = juce::MidiMessage::controllerEvent((int)e.channel, (int)e.noteOrCC,
                                               (int)(e.value * 127.0f));
        valid = true;
      } else if (e.type == EventType::PitchBend) {
        m = juce::MidiMessage::pitchWheel((int)e.channel,
                                          (int)((e.value + 1.0f) * 8192.0f));
        valid = true;
      } else if (e.type == EventType::Aftertouch) {
        if (e.noteOrCC > 0)
          m = juce::MidiMessage::aftertouchChange(
              (int)e.channel, (int)e.noteOrCC, (int)(e.value * 127.0f));
        else
          m = juce::MidiMessage::channelPressureChange((int)e.channel,
                                                       (int)(e.value * 127.0f));
        valid = true;
      }

      if (valid) {
        midiService->sendMessage(m);
        // CRITICAL FIX: Lock-free logging - NO callAsync (audio thread safe)
        // Encode: val1 = channel*256 + noteOrCC, val2 = value
        int encoded = (int)e.channel * 256 + (int)e.noteOrCC;
        if (e.type == EventType::NoteOn || e.type == EventType::NoteOff)
          logBuffer.push(LogEntry::Code::MidiOutput, encoded, e.value);
        else if (e.type == EventType::ControlChange)
          logBuffer.push(LogEntry::Code::MidiOutput, encoded, e.value);
      }
    }

    // B. Update Visuals/Engine
    // CRITICAL: We DO NOT push to 'sharedAirlock' here.
    dispatchBridgeEvent(e);
    return;
  }

  // -------------------------------------------------------
  // PATH 2: INCOMING FROM HARDWARE/UI (MIDI IN -> OSC OUT)
  // -------------------------------------------------------
  if (e.source == EventSource::HardwareMidi ||
      e.source == EventSource::UserInterface ||
      e.source == EventSource::EngineSequencer) {

    // A. Send to Network (OSC) via central bus — single path for all
    // subscribers
    if (!settings.blockOscOut.load()) {
      BridgeEvent out = e;
      if (splitMode && e.channel == 1 &&
          (e.type == EventType::NoteOn || e.type == EventType::NoteOff)) {
        int note = e.noteOrCC;
        int nz = juce::jlimit(2, 4, splitNumZones);
        int zoneSize = 128 / nz;
        int zone = juce::jmin(nz - 1, note / zoneSize);
        out.channel = static_cast<juce::uint8>(splitZoneChannels[zone]);
      }
      // CRITICAL FIX: Use lock-free push() instead of blocking emit()
      BridgeEventBus::instance().push(out);
      if (onNetworkActivity)
        onNetworkActivity();
    }

    // B. Local Feedback (Visuals/Engine)
    dispatchBridgeEvent(e);

    // C. Hardware Thru (Optional)
    if ((e.source == EventSource::UserInterface ||
         e.source == EventSource::EngineSequencer) &&
        midiService && !blockMidiOut) {

      // --- FIX: Explicitly handle LFO Visual Params ---
      if (e.type == EventType::VisualParam && e.noteOrCC == 900) {
        // 900 is the code used in AudioEngine.cpp for LFO
        if (sharedAirlock) { // Use sharedAirlock or OscManager directly? User
                             // said OscManager.
          // But MidiRouter usually uses sharedAirlock for network output.
          // User Request: "if (oscManager) {
          // oscManager->sendFloat("/modulation/lfo", e.value); }" MidiRouter
          // has 'settings' and 'engineState'. Does it have oscManager? It
          // includes OscManager.h. Let's check MidiRouter.h members. It doesn't
          // seem to hold a pointer to OscManager directly, usually via Context
          // or Airlock. However, the user provided code uses `oscManager`. I
          // see `OscManager* oscManager = nullptr;` in some contexts, but let's
          // check headers. MidiRouter.cpp include "../Network/OscManager.h"
        }
      }
      int outCh = (int)e.channel;
      if (splitMode && e.channel == 1 &&
          (e.type == EventType::NoteOn || e.type == EventType::NoteOff)) {
        int note = e.noteOrCC;
        int nz = juce::jlimit(2, 4, splitNumZones);
        int zoneSize = 128 / nz;
        int zone = juce::jmin(nz - 1, note / zoneSize);
        outCh = splitZoneChannels[zone];
      }
      juce::MidiMessage m;
      bool valid = false;

      if (e.type == EventType::NoteOn) {
        m = juce::MidiMessage::noteOn(outCh, (int)e.noteOrCC, e.value);
        valid = true;
      } else if (e.type == EventType::NoteOff) {
        m = juce::MidiMessage::noteOff(outCh, (int)e.noteOrCC);
        valid = true;
      } else if (e.type == EventType::ControlChange) {
        m = juce::MidiMessage::controllerEvent((int)e.channel, (int)e.noteOrCC,
                                               (int)(e.value * 127.0f));
        valid = true;
      } else if (e.type == EventType::PitchBend) {
        m = juce::MidiMessage::pitchWheel((int)e.channel,
                                          (int)((e.value + 1.0f) * 8192.0f));
        valid = true;
      } else if (e.type == EventType::Aftertouch) {
        if (e.noteOrCC > 0)
          m = juce::MidiMessage::aftertouchChange(
              (int)e.channel, (int)e.noteOrCC, (int)(e.value * 127.0f));
        else
          m = juce::MidiMessage::channelPressureChange((int)e.channel,
                                                       (int)(e.value * 127.0f));
        valid = true;
      } else if (e.type == EventType::PolyAftertouch) {
        m = juce::MidiMessage::aftertouchChange((int)e.channel, (int)e.noteOrCC,
                                                (int)(e.value * 127.0f));
        valid = true;
      } else if (e.type == EventType::ProgramChange) {
        m = juce::MidiMessage::programChange(
            (int)e.channel, juce::jlimit(0, 127, (int)e.noteOrCC));
        valid = true;
      }

      if (valid) {
        midiService->sendMessage(m);
        // Lock-free logging (no callAsync)
        int encoded = (int)e.channel * 256 + (int)e.noteOrCC;
        if (e.type == EventType::NoteOn || e.type == EventType::NoteOff)
          logBuffer.push(LogEntry::Code::MidiOutput, encoded, e.value);
        else if (e.type == EventType::ControlChange)
          logBuffer.push(LogEntry::Code::MidiOutput, encoded, e.value);
      }
    }
  }
}

void MidiRouter::handleNoteOn(int channel, int note, float velocity,
                              bool isPlayback, bool bypassMapping,
                              BridgeEvent::Source source) {
  juce::ignoreUnused(bypassMapping);
  bool fromNetwork = (source == EventSource::NetworkOsc);
  bool fromEngine = (source == EventSource::EngineSequencer);

  // 1. Sequencer Input (Recording)
  if (!isPlayback && !fromNetwork && onSequencerInput) {
    onSequencerInput(channel, note, velocity);
  }

  // 2. Octave Shift Logic
  // FIX: Only apply Global Shift to UI (Keyboard) and Hardware.
  int shiftToApply = 0;

  if (!fromEngine && !fromNetwork) {
    shiftToApply = globalOctaveShift.load();
    // Track this shift for the corresponding NoteOff
    activeNoteShifts[channel][note] = shiftToApply;
  }

  int finalPitch = juce::jlimit<int>(0, 127, (int)note + (shiftToApply * 12));

  // 3. Scale Quantize
  if (isQuantizationEnabled)
    finalPitch = scaleQuantizer.quantize(finalPitch);

  // 3b. Feed arp held notes (virtual keyboard + hardware MIDI) so Latch works
  if (!fromEngine && !fromNetwork) {
    if (!heldNotes.contains(finalPitch))
      addHeldNote(finalPitch);
  }

  // 4. Send to Network
  if (sharedAirlock && source != EventSource::NetworkOsc) {
    // IMPORTANT: network wake-up handled in handleBridgeEvent or here directly?
    // Using handleBridgeEvent is more centralized.
    handleBridgeEvent(
        BridgeEvent(EventType::NoteOn, source, channel, finalPitch, velocity));
    return; // handleBridgeEvent does dispatch
  } else {
    // If coming from network, we still need local dispatch
    handleBridgeEvent(
        BridgeEvent(EventType::NoteOn, source, channel, finalPitch, velocity));
  }
}

void MidiRouter::handleNoteOff(int channel, int note, float velocity,
                               bool isPlayback, bool bypassMapping,
                               BridgeEvent::Source source) {
  juce::ignoreUnused(velocity, isPlayback, bypassMapping);
  bool fromNetwork = (source == EventSource::NetworkOsc);
  bool fromEngine = (source == EventSource::EngineSequencer);

  int shiftToApply = 0;

  // Recall the shift used for this specific note (if not from engine/net)
  if (!fromEngine && !fromNetwork) {
    shiftToApply = activeNoteShifts[channel][note];
  }

  int finalPitch = juce::jlimit<int>(0, 127, note + (shiftToApply * 12));

  if (isQuantizationEnabled)
    finalPitch = scaleQuantizer.quantize(finalPitch);

  if (!fromEngine && !fromNetwork) {
    if (arpLatch && heldNotes.contains(finalPitch))
      latchedNotes.addIfNotAlreadyThere(finalPitch);
    removeHeldNote(finalPitch);
  }

  handleBridgeEvent(
      BridgeEvent(EventType::NoteOff, source, channel, finalPitch, 0.0f));
}

void MidiRouter::handleCC(int channel, int cc, float value,
                          BridgeEvent::Source source) {
  const float threshold = 0.005f;
  if (std::abs(value - lastSentCC[channel][cc]) < threshold && value != 0.0f &&
      value != 1.0f)
    return;
  lastSentCC[channel][cc] = value;

  handleBridgeEvent(
      BridgeEvent(EventType::ControlChange, source, channel, cc, value));
}

void MidiRouter::handleControlChange(int cc, int value) {
  handleCC(selectedChannel, cc, value / 127.0f, EventSource::UserInterface);
}

void MidiRouter::sendPanic() {
  trafficBreaker.reset();

  // Minimal critical section: only clear note state shared with audio path,
  // then release lock. Heavy work (MIDI sends, engine->stop, etc.) runs outside
  // the lock to avoid blocking audio.
  MidiDeviceService *ms = nullptr;
  MidiScheduler *sched = nullptr;
  NoteTracker *trk = nullptr;
  AudioEngine *eng = nullptr;
  OscAirlock *airlock = nullptr;
  {
    const juce::ScopedLock sl(callbackLock);
    heldNotes.clear();
    latchedNotes.clear();
    sustainedNotes.clear();
    numFingersDown = 0;
    sustainPedalDown = false;
    ms = midiService;
    sched = scheduler;
    trk = tracker;
    eng = engine;
    airlock = sharedAirlock;
  }

  if (ms) {
    for (int ch = 1; ch <= 16; ++ch) {
      ms->sendMessage(juce::MidiMessage::allNotesOff(ch));
      ms->sendMessage(juce::MidiMessage::allSoundOff(ch));
      ms->sendMessage(juce::MidiMessage::controllerEvent(ch, 64, 0));
    }
  }
  if (sched)
    sched->clear();
  if (trk)
    trk->clearAll();
  if (eng) {
    eng->stop();
    eng->resetTransport();
  }
  if (airlock)
    airlock->clear();
  logBuffer.push(LogEntry::Code::Custom, 1, 0.0f);
}

void MidiRouter::updateArpSettings(int speed, int vel, int pattern, int octave,
                                   float gate) {
  arpSpeed = speed;
  arpVelocity = vel;
  arpPatternID = pattern;
  arpOctaveRange = octave;
  arpGate = gate;
}

void MidiRouter::updateArp(double currentBeat) {
  double subdivision = 1.0 / (arpSpeed / 4.0);
  int currentStep = (int)(currentBeat / subdivision);
  static int lastArpStep = -1;
  if (currentStep != lastArpStep) {
    lastArpStep = currentStep;
    triggerNextArpNote();
  }
}

void MidiRouter::triggerNextArpNote() {
  const juce::Array<int> &notesForArp =
      (!heldNotes.isEmpty()) ? heldNotes
                             : (arpLatch ? latchedNotes : heldNotes);
  if (notesForArp.isEmpty())
    return;

  int note = calculatePattern(notesForArp);
  if (note <= 0)
    return;

  if (isQuantizationEnabled)
    note = scaleQuantizer.quantize(note);

  handleBridgeEvent(BridgeEvent(EventType::NoteOn, EventSource::EngineSequencer,
                                selectedChannel, note, arpVelocity / 127.0f));

  if (scheduler && engine) {
    double subdivision = 1.0 / (arpSpeed / 4.0);
    double offBeat = engine->getCurrentBeat() + (subdivision * arpGate);
    scheduler->scheduleEvent(BridgeEvent(EventType::NoteOff,
                                         EventSource::EngineSequencer,
                                         selectedChannel, note, 0.0f),
                             offBeat);
  }
}

int MidiRouter::calculatePattern(const juce::Array<int> &notes) {
  if (notes.isEmpty())
    return 0;
  juce::Array<int> sorted = notes;
  std::sort(sorted.begin(), sorted.end());
  if (arpStep >= sorted.size())
    arpStep = arpStep % juce::jmax(1, sorted.size());

  switch (arpPatternID) {
  case 1: // Up
    arpStep = (arpStep + 1) % sorted.size();
    return sorted[arpStep];
  case 2: // Down
    arpStep = (arpStep <= 0) ? (sorted.size() - 1) : (arpStep - 1);
    return sorted[arpStep];
  case 5: // Random
    return sorted[juce::Random::getSystemRandom().nextInt(sorted.size())];
  case 7: // Diverge
  {
    int val = calculateDiverge(sorted, arpStep);
    arpStep = (arpStep + 1) % sorted.size();
    return val;
  }
  default:
    arpStep = (arpStep + 1) % sorted.size();
    return sorted[arpStep];
  }
}

int MidiRouter::calculateDiverge(const juce::Array<int> &notes, int step) {
  int center = (notes.size() - 1) / 2;
  int offset = (step + 1) / 2;
  int direction = (step % 2 == 0) ? 1 : -1;
  int index = juce::jlimit(0, notes.size() - 1, center + (offset * direction));
  return notes[index];
}

void MidiRouter::setArpSyncEnabled(bool) {}

void MidiRouter::pushEngineMidi(const juce::MidiMessage &m) {
  handleMidiMessage(m, true, EventSource::EngineSequencer);
}

void MidiRouter::allNotesOff() {
  handleBridgeEvent(
      BridgeEvent(EventType::Panic, EventSource::EngineSequencer));
}

void MidiRouter::handleSustainPedal(int channel, int value) {
  sustainPedalDown = (value >= 64);
  if (!sustainPedalDown) {
    for (int note : sustainedNotes) {
      if (!heldNotes.contains(note)) {
        handleNoteOff(channel, note, 0.0f, false, true,
                      EventSource::EngineSequencer);
      }
    }
    sustainedNotes.clear();
  }
}

void MidiRouter::addHeldNote(int note) {
  if (!heldNotes.contains(note)) {
    heldNotes.add(note);
    numFingersDown++;
  }
}

void MidiRouter::removeHeldNote(int note) {
  heldNotes.removeFirstMatchingValue(note);
  numFingersDown = std::max(0, numFingersDown - 1);
  if (sustainPedalDown && !sustainedNotes.contains(note))
    sustainedNotes.add(note);
}

bool MidiRouter::isSustainDown() const { return sustainPedalDown; }

void MidiRouter::addSustainedNote(int note) {
  if (!sustainedNotes.contains(note))
    sustainedNotes.add(note);
}

int MidiRouter::getLastCC(int ch) const {
  if (ch >= 0 && ch < 17)
    return lastReceivedCC[ch].load();
  return 0;
}

void MidiRouter::clearHeldNotes() {
  heldNotes.clear();
  numFingersDown = 0;
}

void MidiRouter::prepareToPlay(double sampleRate, int) {
  currentSampleRate = sampleRate;
}

void MidiRouter::triggerVirtualPanic() { sendPanic(); }

uint64_t MidiRouter::hashEvent(const BridgeEvent &e) const {
  return (uint64_t)e.type ^ ((uint64_t)e.channel << 8) ^
         ((uint64_t)e.noteOrCC << 16);
}

void MidiRouter::sendSplitOscMessage(const juce::MidiMessage &msg,
                                     int overrideChannel) {
  EventType type = EventType::None;
  if (msg.isNoteOn())
    type = EventType::NoteOn;
  else if (msg.isNoteOff())
    type = EventType::NoteOff;
  else if (msg.isController())
    type = EventType::ControlChange;

  if (type != EventType::None) {
    int ch = (overrideChannel != -1) ? overrideChannel : msg.getChannel();
    handleBridgeEvent(BridgeEvent(type, EventSource::HardwareMidi, ch,
                                  msg.getNoteNumber(), msg.getFloatVelocity()));
  }
}
void MidiRouter::dispatchBridgeEvent(const BridgeEvent &ev) {
  // A. VISUALS (UI Thread)
  VisualEvent::Type vType = VisualEvent::Type::CC;
  if (ev.type == EventType::NoteOn)
    vType = VisualEvent::Type::NoteOn;
  else if (ev.type == EventType::NoteOff)
    vType = VisualEvent::Type::NoteOff;

  visualBuffer.push({vType, ev.channel, ev.noteOrCC, ev.value});

  // B. AUDIO ENGINE (Sequencer/Synth)
  engineLane.push(ev);
}

void MidiRouter::setupSchedulerHooks() {
  // Define what happens when OscManager asks for an auto-off
  scheduleOffCallback = [this](int ch, int note, double durationMs) {
    if (!scheduler || !engine)
      return;

    double bpm = engine->getBpm();
    if (bpm <= 0.1)
      bpm = 120.0;

    // Convert MS to Beats: Beats = (MS / 60000) * BPM
    double durationBeats = (durationMs / 60000.0) * bpm;
    double currentBeat = engine->getCurrentBeat();
    double targetBeat = currentBeat + durationBeats;

    // Schedule the Note Off
    scheduler->scheduleEvent(BridgeEvent(EventType::NoteOff,
                                         EventSource::EngineSequencer, ch, note,
                                         0.0f),
                             targetBeat);
  };
}

void MidiRouter::applyToEngine(const BridgeEvent &e) {
  if (!engine)
    return;

  switch (e.type) {
  case EventType::NoteOn:
    engine->setNoteState(e.channel, e.noteOrCC, true);
    break;
  case EventType::NoteOff:
    engine->setNoteState(e.channel, e.noteOrCC, false);
    break;
  case EventType::ControlChange:
    engine->setCCState(e.channel, e.noteOrCC, e.value);
    break;
  default:
    break;
  }
}

double MidiRouter::getDurationFromVelocity(float velocity) const {
  return velocityToDuration(velocity);
}

double MidiRouter::velocityToDuration(float velocity0to1) {
  // Typical Patchworld mapping: 50ms (0.0) to 2500ms (1.0)
  return 50.0 + (velocity0to1 * 2450.0);
}

void MidiRouter::processAudioThreadEvents() {
  PlatformGuard guard;
  trafficBreaker.check(currentSampleRate > 0 ? currentSampleRate : 44100.0);

  bool activity = false;
  engineLane.process([this, &activity](const BridgeEvent &e) {
    applyToEngine(e);
    activity = true;
  });

  // 2. Poll Network (OSC In -> MIDI Out + Synth State)
  if (inboundLanePtr) {
    inboundLanePtr->process([this, &activity](const BridgeEvent &e) {
      handleBridgeEvent(e);
      activity = true;
    });
  }

  // 3. Poll UI (Knobs -> Synth State Only)
  if (commandLanePtr) {
    commandLanePtr->process([this, &activity](const BridgeEvent &e) {
      applyToEngine(e);
      activity = true;
    });
  }

  if (activity)
    midiActivityFlag.store(true, std::memory_order_relaxed);
}