/*
  ==============================================================================
    Source/Core/BridgeContext.h
    Role: The Global "Glue" that holds the Domain Engines and Services.
  ==============================================================================
*/
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_osc/juce_osc.h>

#include "../Audio/ClockSmoother.h"
#include "../Audio/OscTypes.h"
#include "../Core/AppState.h"
#include "../Core/AudioWatchdog.h"
#include "../Core/BridgeListener.h"
#include "../Core/CommandQueue.h"
#include "../Core/ConfigManager.h"
#include "../Core/CommandDispatcher.h"
#include "../Core/Diagnostics.h"
#include "../Core/FlightRecorder.h"
#include "../Core/RepaintCoordinator.h"
#include "../Core/ThreadingConfig.h"
#include "../Network/OscAirlock.h"
#include "../Services/DeferredDeleter.h"

// Forward Declarations
class AudioEngine;
class MidiRouter;
class MidiScheduler;
class OscManager;
class MixerPanel;
class MixerViewModel;
class SequencerPanel;
class SequencerViewModel;
class MidiPlaylist;
class MidiDeviceService;
class MidiHardwareController;
class MidiMappingService;
class GamepadService;
class ProfileService;
class AutoSaver;
class LatencyCalibrator;
class AudioWatchdog;
class NoteTracker;
class NetworkWorker;
class RtpManager;
class ClockWorker;
class Metronome;
class CountInManager;
class PlaybackController;

/**
 * BridgeContext: The Global State Container.
 * It owns the lifecycle of the fundamental Domain objects.
 */
class BridgeContext {
public:
  BridgeContext();
  ~BridgeContext();

  /** Returns the current living instance, or nullptr during/after destructor. Use in async callbacks to avoid use-after-free. */
  static BridgeContext *getLivingContext();

  // --- Core State ---
  AppState appState;
  ConfigManager configManager;
  std::shared_ptr<OscNamingSchema> oscSchema;
  CommandQueue commandQueue;
  juce::ListenerList<BridgeListener> listeners;

  // 0=Init, 1=Ready, 2=Suspended
  std::atomic<int> systemState{0};
  /** Set by engine when sequencer sends note; main thread pulses SEQ indicator and clears. */
  std::atomic<bool> sequencerActivityPending{false};

  void initializationComplete();
  void startServices();
  void dispatchCommand(const BridgeEvent &cmd);
  void updateInputs();
  void checkHardwareChanges();

  // --- Communication Lanes ---
  OscAirlock airlock;        // Outbound (Legacy/General)
  OscAirlock commandLane;    // UI -> Engine (Commands)
  OscAirlock networkAirlock; // Outbound (Audio -> Network)
  OscAirlock inboundLane;    // Network -> Audio/UI
  OscAirlock telemetryLane;

  // --- Logic Domains ---
  std::unique_ptr<AudioEngine> engine;
  std::unique_ptr<MidiRouter> midiRouter;
  std::unique_ptr<OscManager> oscManager;
  std::unique_ptr<NetworkWorker> networkWorker;
  std::unique_ptr<MidiScheduler> midiScheduler;
  std::shared_ptr<PlaybackController> playbackController;

  // --- Services ---
  std::unique_ptr<MidiDeviceService> deviceService;
  std::unique_ptr<MidiHardwareController> midiHardwareController;
  std::unique_ptr<MidiMappingService> mappingManager;
  std::unique_ptr<ProfileService> profileManager;
  std::unique_ptr<AutoSaver> autoSaver;
  std::unique_ptr<AudioWatchdog> audioWatchdog;
  std::shared_ptr<LatencyCalibrator> latencyCalibrator;
  std::unique_ptr<Metronome> metronome;
  std::unique_ptr<CountInManager> countInManager;
  std::unique_ptr<GamepadService> gamepadService;
  std::unique_ptr<NoteTracker> noteTracker;

  // --- UI Models/Panels ---
  std::unique_ptr<MixerPanel> mixer;
  std::unique_ptr<MixerViewModel> mixerViewModel;
  std::unique_ptr<SequencerPanel> sequencer;
  std::vector<std::unique_ptr<SequencerPanel>> extraSequencers;
  std::unique_ptr<SequencerViewModel> sequencerViewModel;
  std::unique_ptr<MidiPlaylist> playlist;

  static constexpr int kMaxExtraSequencers = 3;

  SequencerPanel *getSequencer(int slot) const;
  int getNumSequencerSlots() const {
    return 1 + (int)extraSequencers.size();
  }
  SequencerPanel *addExtraSequencer();
  void removeExtraSequencer(SequencerPanel *panel);

  // --- Workers ---
  std::unique_ptr<RtpManager> rtpManager;
  std::unique_ptr<ClockWorker> clockWorker;

  DiagnosticData diagData;
  DeferredDeleter deferredDeleter;
  FlightRecorder flightRecorder;
  CommandDispatcher commandDispatcher;
  RepaintCoordinator repaintCoordinator;
  ThreadingConfig threadingConfig;
  juce::UndoManager undoManager{5};

  juce::ThreadPool workerPool;
  ClockSmoother midiClockSmoother;

  // --- Global Settings / Flags ---
  std::atomic<int> virtualOctaveShift{0};
  std::atomic<bool> isMidiLearnMode{false};
  std::atomic<bool> isHighPerformanceMode{true};
  std::atomic<bool> pendingPlayAfterCountIn{false};
  /** True when main window is minimised; used to cull extra UI animations (repaints, status bar, etc.). Core (audio, MIDI, OSC, sequencer, LFO) never culled. */
  std::atomic<bool> windowMinimised_{false};

  void setPerformanceMode(bool isPro);
  void transitionMode(bool toPro);
  void transitionSyncMode(bool useExt);
  void dispatchParallelOsc(const BridgeEvent &e);
  void sendVisualParam(int paramIndex, float value);

  // --- Helpers ---
  void log(const juce::String &msg, bool err) {
    listeners.call([&](BridgeListener &l) { l.onLogMessage(msg, err); });
  }

  // --- Caches ---
  struct PreHashedAddresses {
    uint64_t noteOnHashes[17] = {};
    uint64_t noteOffHashes[17] = {};
    uint64_t ccHashes[17] = {};
    uint64_t pitchHashes[17] = {};
  } preHashedAddresses;

  struct StringCache {
    juce::String chPrefix = "/ch";
    juce::String noteSuffix = "note";
    juce::String noteOffSuffix = "noteoff";
    juce::String ccSuffix = "cc";
    juce::String pitchSuffix = "pitch";
    juce::String pressSuffix = "press";
    juce::String susSuffix = "sus";
  } cache;

  std::vector<juce::String> visualAddressCache;
  std::vector<juce::String> visualIDCache;

  // --- UI State ---
  juce::MidiKeyboardState keyboardState;

private:
  juce::CriticalSection modeLock;
  void applyAffinityForMode(bool isPro);
  int lastKnownInputCount = 0;
  int bridgeEventBusSubscriptionId_ = -1;
  /** Guard for BridgeEventBus subscriber: nulled in dtor so callback is no-op after destroy. */
  std::shared_ptr<std::atomic<BridgeContext*>> eventBusGuard_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BridgeContext)
};
