#pragma once
#include "../Audio/EditableNote.h"
#include "../Audio/LfoGenerator.h"
#include "../Audio/MidiScheduler.h"
#include "../Audio/NoteTracker.h"
#include "../Audio/OscTypes.h"
#include "../Audio/SwingProcessor.h"
#include "../Core/TimerHub.h"
#include "../Network/OscAirlock.h"
#include "../Services/DeferredDeleter.h"
#include "../Services/StateRecycler.h"
#include "../UI/Panels/SequencerPanel.h"
#include <ableton/Link.hpp>
#include <array>
#include <atomic>
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <string>
#include <vector>


// Renamed from ProPlaybackEngine to AudioEngine
class AudioEngine : public juce::HighResolutionTimer {
public:
  bool hasRunLinkCheck = false;
  // --- 1. THE TRUTH: Lock-Free State Containers ---

  // A. Note State (Bitmasks for speed)
  struct ChannelState {
    // [0] = Notes 0-63, [1] = Notes 64-127
    std::atomic<uint64_t> lowNotes{0};
    std::atomic<uint64_t> highNotes{0};
  };

  // B. CC State (Full resolution polling)
  // [Channel 0-16][CC 0-127] -> Current Value (0.0 - 1.0)
  struct ControlState {
    std::array<std::atomic<float>, 128> values;
    ControlState() {
      for (auto &v : values)
        v.store(0.0f);
    }
  };

  // --- State Arrays ---
  std::array<ChannelState, 17> activeNoteMasks; // 1-16 used (0 scratch)
  std::array<ControlState, 17> activeCCValues;  // 1-16 used

  // --- 2. WRITER API (Called by Audio/Midi Handler) ---
  // Zero-allocation, wait-free setters

  void setNoteState(int ch, int note, bool isOn) {
    if (ch < 1 || ch > 16 || note < 0 || note > 127)
      return;

    uint64_t mask = 1ULL << (note % 64);
    if (note < 64) {
      if (isOn)
        activeNoteMasks[ch].lowNotes.fetch_or(mask, std::memory_order_relaxed);
      else
        activeNoteMasks[ch].lowNotes.fetch_and(~mask,
                                               std::memory_order_relaxed);
    } else {
      if (isOn)
        activeNoteMasks[ch].highNotes.fetch_or(mask, std::memory_order_relaxed);
      else
        activeNoteMasks[ch].highNotes.fetch_and(~mask,
                                                std::memory_order_relaxed);
    }
  }

  void setCCState(int ch, int cc, float value) {
    if (ch < 1 || ch > 16 || cc < 0 || cc > 127)
      return;
    activeCCValues[ch].values[cc].store(value, std::memory_order_relaxed);
  }

  // --- 3. READER API (Called by UI Timer) ---
  // Fast polling for visuals

  bool isNoteActive(int ch, int note) const {
    if (ch < 1 || ch > 16 || note < 0 || note > 127)
      return false;

    uint64_t val =
        (note < 64)
            ? activeNoteMasks[ch].lowNotes.load(std::memory_order_relaxed)
            : activeNoteMasks[ch].highNotes.load(std::memory_order_relaxed);

    return (val >> (note % 64)) & 1ULL;
  }

  float getCCValue(int ch, int cc) const {
    if (ch < 1 || ch > 16 || cc < 0 || cc > 127)
      return 0.0f;
    return activeCCValues[ch].values[cc].load(std::memory_order_relaxed);
  }

  // --- Loop Settings ---
  struct LoopSettings {
    std::atomic<bool> enabled{false};
    std::atomic<double> startBeat{0.0};
    std::atomic<double> endBeat{4.0};
    std::atomic<int> maxIterations{-1};
    std::atomic<int> currentIteration{0};
    void reset() { currentIteration.store(0); }
  } loopSettings;

  void setLoopEnabled(bool e) { loopSettings.enabled.store(e); }
  void setLoopRegion(double start, double end) {
    loopSettings.startBeat.store(start);
    loopSettings.endBeat.store(end);
  }
  bool isLooping() const { return loopSettings.enabled.load(); }

  // --- 4. ENGINE CORE ---

  static constexpr int kMaxSequencerSlots = 4;

  struct SequencerTrackData {
    std::array<std::array<float, 8>, 128> velocities;
    std::array<std::array<int, 8>, 128> notes;
    std::array<std::array<float, 8>, 128> probabilities;
    std::array<uint64_t, 2> activeStepMask;
  };

  struct EngineState {
    double ticksPerQuarter = 960.0;
    std::array<SequencerTrackData, kMaxSequencerSlots> sequencerTracks;

    // Legacy (File Playback)
    std::vector<juce::MidiMessage> sequence;

    void clear() {
      sequence.clear();
      if (sequence.capacity() < 4096)
        sequence.reserve(4096);
      ticksPerQuarter = 960.0;
      for (auto &st : sequencerTracks)
        st.activeStepMask.fill(0);
    }

    size_t getApproximateMemoryUsage() const {
      return sizeof(*this) + (sequence.capacity() * sizeof(juce::MidiMessage));
    }
  };

  AudioEngine();
  ~AudioEngine() override {
    stateRecycler.setDeleter(nullptr);
    ensureTimerStoppedSync();
    if (!linkWatchdogHubId.empty())
      TimerHub::instance().unsubscribe(linkWatchdogHubId);
  }

  /** Safely stops the timer synchronously, handling thread context. */
  void ensureTimerStoppedSync();

  void prepareToPlay(double sampleRate, int samplesPerBlock);

  // --- Transport & Clock ---
  // --- CACHE LINE 1: Transport State (Read mostly by UI/Link) ---
  struct TransportState {
    std::atomic<bool> isPlaying{false};
    std::atomic<bool> isPaused{false};
    std::atomic<double> pausedTick{0.0};
    std::atomic<double> ticksPerQuarter{960.0};
    std::atomic<int> globalTranspose{0};
    std::atomic<int> timeSigNumerator{4};
    std::atomic<int> timeSigDenominator{4};
  } transport;

  double getCurrentTick() const {
    return audioHot.currentTick.load(std::memory_order_relaxed);
  }
  void setGlobalTranspose(int t) {
    transport.globalTranspose.store(t, std::memory_order_relaxed);
  }
  int getGlobalTranspose() const {
    return transport.globalTranspose.load(std::memory_order_relaxed);
  }
  void setTimeSignature(int num, int den) {
    transport.timeSigNumerator.store(num, std::memory_order_relaxed);
    transport.timeSigDenominator.store(den, std::memory_order_relaxed);
  }

  // --- CACHE LINE 2: Audio Thread Hot Counters (Written 1000x/sec) ---
#if _MSC_VER
#pragma warning(push)
#pragma warning(disable                                                        \
                : 4324) // structure was padded due to alignment specifier
#endif
  struct alignas(64) AudioHotState {
    std::atomic<double> currentTick{0.0};
    std::atomic<int> rollInterval{0};
    std::atomic<double> stutterTickStart{0.0};

    // Pad to fill 64 bytes
    char padding[64 - sizeof(std::atomic<double>) * 2 -
                 sizeof(std::atomic<int>)];
  } audioHot;
#if _MSC_VER
#pragma warning(pop)
#endif

  ableton::Link link{120.0};
  double quantum = 1.0; // Beat-level sync when Link enabled (was 4.0 = bar)
  std::atomic<int> momentaryLoopSteps{
      0}; // Time/Loop mode: override loop length

  void play();
  void start() { play(); }
  void stop();
  void pause();
  void resume();
  bool getIsPaused() const {
    return transport.isPaused.load(std::memory_order_acquire);
  }
  void tapTempo();

  // Main Processing Block
  void driveAudioCallback(double numSamples, double sampleRate);
  void processAudioBlock(double numSamples, double sampleRate);

  // Callbacks (no onEngineEvent - use OscAirlock only to avoid priority
  // inversion)
  std::function<void(const juce::MidiMessage &)> onMidiEvent;
  /** Called only when a note is sent from sequencer steps (not .mid playback).
   * For SEQ indicator. */
  std::function<void()> onSequencerNoteSent;
  std::function<void()> onSequenceEnd;
  std::function<bool(int)> isChannelActive; // UI can query if channel has data

  // --- Public State (Needed by MainComponent / UI) ---
  std::atomic<int> currentVisualStep{-1};
  std::atomic<bool> hasSequencerEvent{false};
  bool sendMidiClock = false;
  /** When true, engine skips generating clock (external clock is being
   * forwarded via THRU). */
  std::function<bool()> isExternalClockForwarding;
  bool blockBpmChanges =
      false; // Prevents loaded MIDI files from changing tempo

  // --- Humanizer (Velocity & Timing Jitter) ---
  struct HumanizeParams {
    float velocityAmt = 0.0f; // 0.0 to 0.2 (20% variance)
    float timingAmt = 0.0f;   // 0.0 to 20.0 ms
    juce::Random rng;
  } humanizeParams;
  void setHumanizeVelocity(float amt) { humanizeParams.velocityAmt = amt; }
  void setHumanizeTiming(float ms) { humanizeParams.timingAmt = ms; }

  // --- Roll Engine (Audio-thread tight timing) ---
  // Moved to AudioHotState struct
  std::atomic<int> lastRollNote{-1};
  std::atomic<float> lastRollVel{0.0f};
  std::atomic<int> lastRollCh{1};
  // Moved to AudioHotState struct
  std::atomic<float> globalProbability{1.0f};

  void setRoll(int division) {
    int oldDiv = audioHot.rollInterval.exchange(division);
    if (oldDiv == 0 && division > 0) {
      audioHot.stutterTickStart.store(audioHot.currentTick.load(),
                                      std::memory_order_release);
    }
  }

  void setMomentaryLoopSteps(int steps) {
    momentaryLoopSteps.store(steps > 0 ? steps : 0, std::memory_order_release);
  }

  void setGlobalProbability(float p) {
    globalProbability.store(p, std::memory_order_relaxed);
  }

  // Getters/Setters
  double getBpm();
  void setBpm(double bpm);
  double getCurrentBeat();
  int getNumPeers() const { return (int)link.numPeers(); }
  /** Sync quality 0-1 for Link phase lock (1=locked). */
  float getSyncQuality() const {
    return syncQuality.load(std::memory_order_relaxed);
  }
  bool getIsPlaying() const {
    return transport.isPlaying.load(std::memory_order_acquire);
  }
  double getTicksPerQuarter() const;
  double getLoopLengthTicks() const;
  int getCurrentStepIndex() const { return currentVisualStep.load(); }

  // Sequence Management (extended)
  void queueNextSequence(const juce::MidiMessageSequence &seq, double ppq);
  void setMidiCallback(std::function<void(const juce::MidiMessage &)> cb) {
    onMidiEvent = std::move(cb);
  }
  static double getDurationFromVelocity(float velocity0to1) {
    return 50.0 + (velocity0to1 * 450.0);
  }

  // Seek (timeline drag)
  void seek(double beat);
  void setCurrentBeat(double beat) { seek(beat); }
  void setQuantum(double q);
  double getQuantum() const { return quantum; }
  // Lookahead bypass is persisted in AppState; engine use reserved for future.
  // Link = BPM bridge between devices (keep running). EXT = slave to external
  // MIDI clock.
  void setLinkEnabled(bool enabled) {
    link.enable(enabled);
    if (enabled)
      link.enableStartStopSync(true);
    // EXT and Link can both be on: EXT controls master BPM, Link handles
    // transport sync
  }

  bool isLinkEnabled() const { return link.isEnabled(); }

  // EXT = External MIDI clock. Does NOT disable Link - Link stays on as BPM
  // bridge.
  void setExtSyncActive(bool active) { extSyncActive = active; }

  bool isExtSyncActive() const { return extSyncActive; }

  // Sequence Management
  void setSequence(const juce::MidiMessageSequence &seq, double ppq,
                   double fileBpm = -1.0);
  void setNewSequence(std::vector<EditableNote> seq);
  // Multi-slot sequencer (0..kMaxSequencerSlots-1)
  void updateSequencerData(int slot, const SequencerPanel::EngineData &data);
  std::shared_ptr<EngineState> getActiveState() const {
    return std::atomic_load(&activeState);
  }

  // Utilities
  void setClockSmoother(ClockSmoother *s) { smoother = s; }
  void setScheduler(MidiScheduler *s) { scheduler = s; }
  void setSequencer(int slot, SequencerPanel *s);
  void setSequencerChannel(int slot, int ch);
  int getSequencerChannel(int slot) const;
  void resetTransport();
  void resetTransportForLoop();
  void nudge(double amt);
  void setSwing(float amount);
  double getSwungTick(double originalTick, double ppq, float swingAmt) const;

  void setOutputLatency(double ms) { outputLatency.store(ms); }

  void linkWatchdogTick(); // Link watchdog (one-shot after 5s)

  // LFO control
  void setLfoFrequency(float freq) { lfo.setFrequency(freq); }
  void setLfoDepth(float depth) { lfo.setDepth(depth); }
  void setLfoWaveform(int waveform) {
    lfo.setWaveform(static_cast<LfoGenerator::Waveform>(waveform));
  }
  /** ADSR envelope (0..1) applied per LFO cycle to shape the curve. */
  void setLfoEnvelope(float attack, float decay, float sustain, float release) {
    lfo.setEnvelope(attack, decay, sustain, release);
  }
  /** LFO phase 0..1 for UI position bar. Updated from audio thread. */
  float getLfoPhaseForUI() const {
    return lfoPhaseForUI.load(std::memory_order_relaxed);
  }

  /** When true, timeline should show pending seek target until applied at next
   * beat/bar. */
  bool getIsQuantizedSeek() const {
    return isQuantizedSeek.load(std::memory_order_acquire);
  }
  double getPendingSeekTarget() const {
    return pendingSeekTarget.load(std::memory_order_acquire);
  }

private:
  void hiResTimerCallback() override;
  int findIndexForTick(double tick);
  void
  doStopNow(); // Immediate stop (used when Link disabled or at quantized beat)

  DeferredDeleter deadPool;
  StateRecycler<EngineState> stateRecycler;
  std::shared_ptr<EngineState> activeState;
  std::shared_ptr<EngineState> nextState;

  std::atomic<double> internalBpm{120.0};
  std::atomic<bool> transportResetRequested{false};
  std::atomic<double> outputLatency{0.0};
  std::atomic<float> syncQuality{1.0f};

  // Internal Modules
  LfoGenerator lfo;
  SwingProcessor swingProcessor;
  ClockSmoother *smoother = nullptr;
  MidiScheduler *scheduler = nullptr;
  std::array<SequencerPanel *, kMaxSequencerSlots> sequencerRefs{};
  std::array<std::atomic<int>, kMaxSequencerSlots> sequencerChannels;

  std::atomic<float> lfoPhaseForUI{0.0f};

  // Helper Variables (no static locals on audio thread)
  int lfoThrottle = 0;
  int lfoThrottleInterval = 1470; // sampleRate/30, set in prepareToPlay
  double lastRollPos = -1.0;
  bool autoPlayNext = false;
  std::atomic<double> pendingStartBeat{-1.0};
  double linkBeatOffset = 0.0;
  double linkPhaseIntegral = 0.0; // PI controller integral for phase lock
  std::vector<double> tapTimes;
  double midiClockAccumulator = 0.0;
  double lastOscBeat = -1.0;
  std::atomic<int> nextEventIndex{0};
  double samplesPerMidiClock = 0.0;
  std::atomic<bool> extSyncActive{false};

  std::atomic<double> pendingSeekTarget{-1.0};
  std::atomic<bool> isQuantizedSeek{false};

  // Link-quantized stop/pause: execute at next beat/bar
  std::atomic<double> pendingStopBeat{-1.0};
  std::atomic<double> pendingPauseBeat{-1.0};
  std::atomic<double> pendingResumeTick{
      -1.0}; // When resuming at next bar, restore this tick

  // One-shot: fire onSequenceEnd only once per play (avoids spam when at end
  // every sample)
  std::atomic<bool> sequenceEndFiredThisPlay{false};

  /** When set by updateSequencerData, next processBlock sends all-notes-off for
   * sequencer channels so steps-cleared/edited stops sounding. */
  std::atomic<bool> pendingSequencerAllNotesOff{false};

  // Lock-free OSC output (no onEngineEvent - avoids priority inversion)
  OscAirlock *airlockRef = nullptr;

  std::string linkWatchdogHubId;

public:
  void setAirlock(OscAirlock *al) { airlockRef = al; }
};