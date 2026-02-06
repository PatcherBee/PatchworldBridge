/*
  ==============================================================================
    Source/Audio/MidiRouter.h
    Role: Central hub for MIDI/OSC routing, filtering, and loop prevention.
  ==============================================================================
*/
#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <vector>

// --- JUCE Framework ---
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_osc/juce_osc.h>

// --- Project Headers ---
#include "../Audio/ClockSmoother.h"
#include "../Audio/LockFreeRingBuffers.h"
#include "../Audio/OscTypes.h"
#include "../Core/AppState.h"
#include "../Core/BridgeSettings.h"
#include "../Core/CommandQueue.h"
#include "../Network/OscAirlock.h"
#include "../Services/MidiDeviceService.h"
#include "../Services/MidiTransformer.h"

// Forward declarations
class MixerPanel;     // formerly MixerContainer
class SequencerPanel; // Forward Declarations
class MidiScheduler;
class NoteTracker;
class AudioEngine; // formerly AudioEngine
class OscManager;
class NoteTracker;
class LatencyCalibrator;

struct ActiveCC {
  int channel;
  int cc;
  juce::LinearSmoothedValue<float> smoother;
  int lastSentValue = -1;
  bool active = false;
};

struct TrafficBreaker {
  std::atomic<int> eventCount{0};
  std::atomic<bool> tripped{false};

  void recordEvent() { eventCount.fetch_add(1, std::memory_order_relaxed); }

  void check(double /*sampleRate*/) {
    if (eventCount.load(std::memory_order_relaxed) > 500 && !tripped.load()) {
      tripped.store(true);
      DBG("!!! FEEDBACK LOOP DETECTED - OUTPUT DISABLED !!!");
    }
    eventCount.store(0, std::memory_order_relaxed);
  }

  void reset() { tripped.store(false); }
  bool isTripped() const { return tripped.load(); }
};

class JitterFilter {
public:
  JitterFilter() {
    // Initialize all times to 0
    juce::zeromem(lastTimeMap, sizeof(lastTimeMap));
  }

  bool shouldProcess(int channel, int cc, int64_t nowUs) {
    // Safety checks
    if (channel < 0 || channel > 16 || cc < 0 || cc > 127)
      return true;

    // Direct Array Access (0 allocs, 0 hasing overhead)
    if ((nowUs - lastTimeMap[channel][cc]) < 2000) // 2ms window
      return false;

    lastTimeMap[channel][cc] = nowUs;
    return true;
  }

private:
  // Flattened lookup: [Channel][CC]
  // 17 * 128 * 8 bytes ≈ 17KB (Fits easily in CPU L1/L2 Cache)
  int64_t lastTimeMap[17][128];
};

class MidiRouter : public juce::MidiInputCallback {
public:
  MidiRouter(BridgeSettings &s, EngineShadowState &ess);
  ~MidiRouter();

  OscAirlock &getUiLane() { return *commandLanePtr; }

  // UI-to-Audio Command Processing
  void processCommands(CommandQueue &queue);

  // Lock-free mixer remote levels: [Channel 0-16], -1.0f = no change
  std::array<std::atomic<float>, 17> channelRemoteLevels;

  // Main entry point for raw MIDI
  void handleBridgeEvent(const BridgeEvent &e) noexcept;
  void handleIncomingMidiMessage(juce::MidiInput *source,
                                 const juce::MidiMessage &message) override;
  void handleMidiMessage(const juce::MidiMessage &message, bool isPlayback,
                         BridgeEvent::Source source = BridgeEvent::Internal);

  // Specific handlers with loop prevention
  void handleNoteOn(int channel, int note, float velocity,
                    bool isPlayback = false, bool bypassMapping = false,
                    BridgeEvent::Source source = BridgeEvent::Internal);
  void handleNoteOff(int channel, int note, float velocity,
                     bool isPlayback = false, bool bypassMapping = false,
                     BridgeEvent::Source source = BridgeEvent::Internal);
  void handleCC(int channel, int cc, float value,
                BridgeEvent::Source source = BridgeEvent::Internal);
  void handleControlChange(int cc, int value);
  void sendPanic();
  void processAudioThreadEvents();

  // Setters
  void setMidiService(MidiDeviceService *service) { midiService = service; }
  void setLatencyCalibrator(LatencyCalibrator *lc) { latencyCalibrator = lc; }
  void setMixer(MixerPanel *mix) { mixer = mix; }
  void setSequencer(SequencerPanel *seq) { sequencer = seq; }
  void setEngine(AudioEngine *eng) { engine = eng; }
  void setMappingManager(class MidiMappingService *m) { mappingManager = m; }
  void setOscManager(OscManager *osc) { oscManager = osc; }
  void setAirlock(OscAirlock *a) { sharedAirlock = a; }
  void setScheduler(MidiScheduler *s) { scheduler = s; }
  void setNoteTracker(NoteTracker *t) { tracker = t; }

  void triggerVirtualPanic();
  void setScheduleOffCallback(std::function<void(int, int, double)> cb) {
    scheduleOffCallback = cb;
  }
  void setSplitMode(bool enabled) { splitMode = enabled; }
  void setQuantizationEnabled(bool enabled) { isQuantizationEnabled = enabled; }

  /** Number of split zones (2, 3, or 4). Note range 0-127 is divided equally.
   */
  int getSplitNumZones() const { return splitNumZones; }
  void setSplitNumZones(int n) { splitNumZones = juce::jlimit(2, 4, n); }

  /** Output channel for zone index 0..3 (used when splitNumZones is 2, 3, or
   * 4). */
  int getSplitZoneChannel(int zoneIndex) const {
    return (zoneIndex >= 0 && zoneIndex < 4) ? splitZoneChannels[zoneIndex] : 1;
  }
  void setSplitZoneChannel(int zoneIndex, int ch) {
    if (zoneIndex >= 0 && zoneIndex < 4)
      splitZoneChannels[zoneIndex] = juce::jlimit(1, 16, ch);
  }

  void setClockSmoother(ClockSmoother *s) { clockSmootherPtr = s; }
  void setAppState(AppState *a) { appState = a; }

  // Clock Source: only accept real-time (Clock/Start/Stop/Continue) from this
  // device
  void setClockSourceID(const juce::String &id);
  juce::String getClockSourceID() const;
  /** Lock-free, no allocation. Returns pointer to active buffer for comparison
   * in hot path. */
  const char *getClockSourceIDPointer() const {
    int index = activeClockSourceIndex_.load(std::memory_order_acquire);
    return clockSourceIDBuffers_[index];
  }
  void setBlockMidiOut(bool blocked) { blockMidiOut = blocked; }
  void setMidiScaling127(bool use127) { midiScaling127 = use127; }
  int getSplitPoint() const { return splitNote; }
  void setInboundLane(OscAirlock *lane) { inboundLanePtr = lane; }
  void setCommandLane(OscAirlock *lane) { commandLanePtr = lane; }
  void setNetworkLookahead(float ms) { networkLookaheadMs.store(ms); }

  // Octave Shift Logic
  void setGlobalOctaveShift(int shift) { globalOctaveShift.store(shift); }
  void handleSustainPedal(int channel, int value);
  bool isSustainDown() const;
  void addSustainedNote(int note);

  float getAirlockPressure() const {
    return sharedAirlock ? sharedAirlock->getPressure() : 0.0f;
  }
  void allNotesOff();

  // Public Callbacks
  std::function<void()> onNetworkActivity;
  std::function<void()> onMidiInputActivity;
  std::function<void()> onMidiOutActivity;
  std::function<void(int)> onNotesOff;
  std::function<void(juce::String, bool)> onLog;
  std::function<void(int, int, float)> onSequencerInput;
  std::function<void(bool)> onTransportCommand; // true=play, false=stop
  std::function<void(int)> onPlaylistCommand;   // 1=Next, -1=Prev, 0=Select
  std::function<void(const juce::MidiMessage &)> onMidiOutputGenerated;
  /** Called from MIDI thread when hardware note on/off arrives; implementor
   * should MessageManager::callAsync to update UI (e.g. virtual keyboard
   * highlight). */
  std::function<void(int ch, int note, float vel, bool isOn)>
      onIncomingNoteForDisplay;
  // Outbound bridge events: use BridgeEventBus::subscribe(); no per-router
  // callback.

  // Buffers for Lock-Free Communication
  LogBuffer logBuffer;
  VisualBuffer visualBuffer;

  void pushEngineMidi(const juce::MidiMessage &m);
  void pushEngineEvent(const BridgeEvent &e) { engineLane.push(e); }

  std::atomic<bool> needsUiUpdate{false};
  std::atomic<bool> midiActivityFlag{false};

  void sendSplitOscMessage(const juce::MidiMessage &msg,
                           int overrideChannel = -1);
  std::array<std::atomic<int>, 17> lastReceivedCC;

  // Public state flags
  bool splitMode = false;
  bool blockMidiOut = false;
  bool midiScaling127 = false;
  bool isHandlingOsc = false;
  bool arpEnabled = false;
  bool arpLatch = false;
  bool retriggerEnabled = false;
  bool isQuantizationEnabled = false;
  int selectedChannel = 1;
  int splitNote = 64;    // legacy single split point
  int splitNumZones = 2; // 2, 3, or 4 zones (0-127 divided equally)
  int splitZoneChannels[4] = {1, 2, 3, 4}; // output channel per zone

  // Safe Threading for high-frequency checks
  std::atomic<bool> midiThru{false};
  void setMidiThru(bool enabled) {
    midiThru.store(enabled, std::memory_order_release);
  }

  void updateArpSettings(int speed, int vel, int pattern, int octave,
                         float gate);
  void setArpSyncEnabled(bool enabled);
  void setArpEnabled(bool enabled) { arpEnabled = enabled; }
  void setArpLatch(bool latch) {
    if (arpLatch && !latch) {
      // Exiting latch: send note-offs for held + latched notes to prevent hang
      if (midiService && !blockMidiOut) {
        for (int note : heldNotes)
          midiService->sendMessage(
              juce::MidiMessage::noteOff(selectedChannel, note));
        for (int note : latchedNotes)
          midiService->sendMessage(
              juce::MidiMessage::noteOff(selectedChannel, note));
      }
      heldNotes.clear();
      latchedNotes.clear();
      numFingersDown = 0;
    }
    arpLatch = latch;
  }

  // Octave Shift Tracking
  std::atomic<int> globalOctaveShift{0};

  // MIDI Input Transformer (deadzone, velocity curve, channel remap)
  MidiTransformer &getTransformer() { return transformer; }

  // MIDI Input Transformer
  void prepareToPlay(double sampleRate, int samplesPerBlock);

  MidiDeviceService *midiService = nullptr;
  NoteTracker *tracker = nullptr;
  LatencyCalibrator *latencyCalibrator = nullptr;
  std::function<void(int, int, double)> scheduleOffCallback;

  // Public Quantizer
  ScaleQuantizer scaleQuantizer;
  void clearHeldNotes();
  void addHeldNote(int note);
  void removeHeldNote(int note);
  int getLastCC(int ch) const;

private:
  // --- Internal Logic Handlers (Declared exactly once) ---
  void setupSchedulerHooks();
  void dispatchBridgeEvent(const BridgeEvent &ev);
  void applyToEngine(const BridgeEvent &e);

  double getDurationFromVelocity(float velocity) const;
  static double velocityToDuration(float velocity0to1);
  uint64_t hashEvent(const BridgeEvent &e) const;

  mutable juce::CriticalSection callbackLock;
  OscAirlock *sharedAirlock = nullptr;
  MidiScheduler *scheduler = nullptr;
  std::array<BridgeEvent, 128> batchBuffer;

  MixerPanel *mixer = nullptr;
  SequencerPanel *sequencer = nullptr;
  AudioEngine *engine = nullptr;
  OscManager *oscManager = nullptr;
  class MidiMappingService *mappingManager = nullptr;
  BridgeSettings &settings;
  EngineShadowState &engineState;
  AppState *appState = nullptr;

  juce::Array<int> heldNotes;
  juce::Array<int> latchedNotes; // when arpLatch is on, notes kept here after
                                 // release so arp keeps playing
  std::vector<int> heldNoteOrder;
  int numFingersDown = 0;
  bool sustainPedalDown = false;
  juce::Array<int> sustainedNotes;
  float lastSentCC[17][128] = {0.0f};
  int ccMsbValues[17][32] = {0};
  ClockSmoother *clockSmootherPtr =
      nullptr;                    // Shared with engine (BridgeContext)
  ClockSmoother fallbackSmoother; // Used when no external smoother set

  // --- INPUT LANES ---
  OscAirlock *inboundLanePtr = nullptr;
  OscAirlock *commandLanePtr = nullptr;
  OscAirlock engineLane;

  // CC Smoothing Pool
  std::array<ActiveCC, 32> ccSmoothers;

  std::atomic<float> networkLookaheadMs{20.0f};

  juce::MidiBuffer midiBatchBuffer;

  // Pre-Flight Smoothing State
  juce::LinearSmoothedValue<float> oscCcSmoother;
  double currentSampleRate = 44100.0;
  int lastTargetCC = -1;
  int lastTargetChannel = -1;

  // MIDI Input Transformer
  MidiTransformer transformer;

  // Track the octave shift used when a note was triggered
  // [Channel][Note]
  int activeNoteShifts[17][128];

  JitterFilter jitterFilter;
  TrafficBreaker trafficBreaker;

  // Clock source: empty = any device; non-empty = only this device for
  // real-time. Lock-free: double-buffered array with atomic index swap; readers
  // never block.
  static constexpr int kMaxClockSourceIdLen = 256;
  std::atomic<int> activeClockSourceIndex_{0}; // 0 or 1
  char clockSourceIDBuffers_[2][kMaxClockSourceIdLen];

  // Network echo guard: true while processing an incoming NetworkOsc event.
  bool isProcessingNetworkEvent = false;

  // Arp state
  int arpSpeed = 2;
  int arpVelocity = 100;
  int arpPatternID = 1;
  int arpOctaveRange = 1;
  float arpGate = 0.5f;
  int arpStep = 0;
  void updateArp(double currentBeat);
  void triggerNextArpNote();
  int calculatePattern(const juce::Array<int> &notes);
  int calculateDiverge(const juce::Array<int> &notes, int step);
};