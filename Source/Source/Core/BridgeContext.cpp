/*
  ==============================================================================
    Source/Core/BridgeContext.cpp
    Role: Service Container Implementation.
  ==============================================================================
*/

#include "../Core/BridgeContext.h"
#include "../Core/BridgeEventBus.h"
#include "../Core/CrashRecovery.h"
#include "../Core/MidiHardwareController.h"

// Engines
#include "../Audio/AudioEngine.h"
#include "../Audio/CountInManager.h"
#include "../Audio/Metronome.h"
#include "../Audio/MidiRouter.h"
#include "../Audio/MidiScheduler.h" // MidiScheduler
#include "../Audio/PlaybackController.h"
#include "../Network/OscManager.h"
#include "../UI/Panels/MidiPlaylist.h" // Added for unique_ptr definition
#include "../UI/Panels/MixerPanel.h"
#include "../UI/Panels/SequencerPanel.h"
#include "MixerViewModel.h"
#include "SequencerViewModel.h"

// Services
#include "../Audio/NoteTracker.h"
#include "../Core/AudioWatchdog.h"
#include "../Services/AutoSaver.h"
#include "../Services/GamepadService.h"
#include "../Services/LatencyCalibrator.h"
#include "../Services/MidiDeviceService.h"
#include "../Services/MidiMappingService.h"
#include "../Services/ProfileService.h"

// Threads
#include "../Audio/ClockWorker.h"
#include "../Network/NetworkWorker.h"
#include "../Network/RtpManager.h"

namespace {
  std::atomic<BridgeContext *> s_livingContext{nullptr};
}

BridgeContext *BridgeContext::getLivingContext() {
  return s_livingContext.load(std::memory_order_acquire);
}

BridgeContext::BridgeContext()
    : configManager(appState.getState()),
      workerPool(juce::jmax(1, threadingConfig.getEffectiveWorkerCount())) {
  s_livingContext.store(this, std::memory_order_release);
  eventBusGuard_ = std::make_shared<std::atomic<BridgeContext*>>(this);
  oscSchema = std::make_shared<OscNamingSchema>();

  // 1. Initialize Engines & Handlers (Order matters!)
  // AudioEngine -> AudioEngine
  engine = std::make_unique<AudioEngine>();

  playbackController = std::make_shared<PlaybackController>(engine.get(), this);
  midiScheduler = std::make_unique<MidiScheduler>();

  oscManager =
      std::make_unique<OscManager>(appState.settings, appState.engineState);

  // MixerContainer -> MixerPanel
  mixer = std::make_unique<MixerPanel>();
  mixer->onRequestRepaint = [this] {
    repaintCoordinator.markDirty(RepaintCoordinator::Mixer);
  };
  // StepSequencer -> SequencerPanel
  sequencer = std::make_unique<SequencerPanel>();

  midiRouter =
      std::make_unique<MidiRouter>(appState.settings, appState.engineState);

  profileManager = std::make_unique<ProfileService>();
  gamepadService = std::make_unique<GamepadService>();

  // Device Service (New DDA)
  deviceService = std::make_unique<MidiDeviceService>();
  deviceService->setAppState(&appState);
  midiHardwareController = std::make_unique<MidiHardwareController>();
  midiHardwareController->setDeviceService(deviceService.get());
  midiHardwareController->setAppState(&appState);
  mappingManager = std::make_unique<MidiMappingService>();
  noteTracker = std::make_unique<NoteTracker>();

  // --- PRO: Pre-calculate Strings (Zero-Alloc Path) ---
  visualAddressCache.reserve(32);
  visualIDCache.reserve(32);

  for (int i = 0; i < 32; ++i) {
    visualAddressCache.push_back("/v" + juce::String(i));
    visualIDCache.push_back("Vis_" + juce::String(i));
  }

  // --- PRO: Pre-hash OSC addresses as uint64_t (Zero-Alloc Hot Path) ---
  for (int ch = 1; ch <= 16; ++ch) {
    juce::String chStr = juce::String(ch);
    preHashedAddresses.noteOnHashes[ch] =
        FastOsc::hashString("/ch" + chStr + "note");
    preHashedAddresses.noteOffHashes[ch] =
        FastOsc::hashString("/ch" + chStr + "noteoff");
    preHashedAddresses.ccHashes[ch] = FastOsc::hashString("/ch" + chStr + "cc");
    preHashedAddresses.pitchHashes[ch] =
        FastOsc::hashString("/ch" + chStr + "pitch");
  }

  // 2. Establish Internal Wiring
  engine->setAirlock(&networkAirlock);
  engine->setScheduler(midiScheduler.get());
  engine->setSequencer(0, sequencer.get());

  mixerViewModel = std::make_unique<MixerViewModel>(*this);
  sequencerViewModel = std::make_unique<SequencerViewModel>(*this);

  commandDispatcher.engine = engine.get();
  commandDispatcher.router = midiRouter.get();
  commandDispatcher.mixer = mixer.get();
  commandDispatcher.mixerViewModel = mixerViewModel.get();
  commandDispatcher.oscManager = oscManager.get();
  commandDispatcher.playback = playbackController.get();
  commandDispatcher.sequencer = sequencer.get();
  commandDispatcher.sequencerViewModel = sequencerViewModel.get();

  // Wire mixer channel active query to engine (for mute/solo in sequencer)
  engine->isChannelActive = [this](int ch) -> bool {
    if (mixer)
      return mixer->isChannelActive(ch);
    return true;
  };

  midiRouter->setAirlock(&networkAirlock);
  midiRouter->setInboundLane(&inboundLane);
  midiRouter->setCommandLane(&commandLane);
  midiRouter->setEngine(engine.get());

  midiRouter->setScheduler(midiScheduler.get());

  midiRouter->setMixer(mixer.get());
  midiRouter->setSequencer(sequencer.get());
  midiRouter->setOscManager(oscManager.get());
  midiRouter->setNoteTracker(noteTracker.get());
  midiRouter->setMidiService(deviceService.get());
  midiRouter->setMappingManager(mappingManager.get());
  midiRouter->setAppState(&appState);

  latencyCalibrator = std::make_shared<LatencyCalibrator>();
  midiRouter->setLatencyCalibrator(latencyCalibrator.get());

  // 3. Setup Managers
  profileManager->setMappingService(mappingManager.get());
  profileManager->setMixer(mixer.get());
  profileManager->setAppState(&appState);

  autoSaver = std::make_unique<AutoSaver>(*profileManager);

  audioWatchdog = std::make_unique<AudioWatchdog>([this] {
    log("CRITICAL: Audio thread stalled!", true);
    if (deviceService)
      deviceService->forceAllNotesOff();
  });

  metronome = std::make_unique<Metronome>();
  countInManager = std::make_unique<CountInManager>();

  // 5. Inbound Lane Wiring
  oscManager->setInputAirlock(&inboundLane);
  oscManager->setScalingMode(appState.getMidiScaling());
  oscManager->setDeleter(&deferredDeleter);

  // 6. Playback Wiring (CRITICAL FIX) - shared clock smoother for EXT MIDI
  engine->setClockSmoother(&midiClockSmoother);
  midiRouter->setClockSmoother(&midiClockSmoother);
  // THRU: when forwarding external clock, engine skips generating (avoids
  // double clock)
  engine->isExternalClockForwarding = [this] {
    return appState.getMidiThru() && midiClockSmoother.getIsLocked();
  };
  // Virtual keyboard: highlight keys when MIDI input arrives (async to message
  // thread)
  midiRouter->onIncomingNoteForDisplay = [this](int ch, int note, float vel,
                                                bool isOn) {
    juce::MessageManager::callAsync([ch, note, vel, isOn]() {
      auto *ctx = BridgeContext::getLivingContext();
      if (!ctx) return;
      if (isOn)
        ctx->keyboardState.noteOn(ch, note, vel);
      else
        ctx->keyboardState.noteOff(ch, note, 0.5f);
    });
  };
  // Count-in: when complete, trigger play if pending
  countInManager->onCountInComplete = [this]() {
    juce::MessageManager::callAsync([]() {
      auto *ctx = BridgeContext::getLivingContext();
      if (!ctx || !ctx->pendingPlayAfterCountIn.exchange(false) || !ctx->engine || !ctx->playbackController)
        return;
      ctx->playbackController->startPlayback();
    });
  };
  // Count-in: play metronome click on each beat during count-in
  countInManager->onCountBeat = [this](int remaining, bool isDownbeat) {
    juce::ignoreUnused(remaining, isDownbeat);
    // Metronome will naturally play during count-in since it runs on
    // getCurrentBeat()
  };
  engine->onSequenceEnd = [this] {
    if (playbackController)
      playbackController->handleSequenceEnd();
  };

  engine->onMidiEvent = [this](const juce::MidiMessage &m) {
    if (midiRouter) {
      midiRouter->handleMidiMessage(m, true,
                                    BridgeEvent::Source::EngineSequencer);
    }
  };
  engine->onSequencerNoteSent = [this] {
    sequencerActivityPending.store(true, std::memory_order_relaxed);
  };

  // Wire Transport commands from the Handler back to the Controller
  midiRouter->onTransportCommand = [this](bool isPlay) {
    juce::MessageManager::callAsync([isPlay]() {
      auto *ctx = BridgeContext::getLivingContext();
      if (!ctx || !ctx->playbackController) return;
      if (isPlay)
        ctx->playbackController->startPlayback();
      else
        ctx->playbackController->stopPlayback();
    });
  };

  // Sequencer REC: record MIDI input to current step; skip during count-in (Cnt
  // enabled)
  midiRouter->onSequencerInput = [this](int ch, int note, float velocity) {
    juce::ignoreUnused(ch);
    if (countInManager && countInManager->isCounting())
      return; // Don't record during count-in
    int step = engine ? engine->getCurrentStepIndex() : 0;
    if (sequencer) {
      juce::MessageManager::callAsync([step, note, velocity]() {
        auto *ctx = BridgeContext::getLivingContext();
        if (!ctx || !ctx->sequencer) return;
        ctx->sequencer->recordNoteOnStep(step, note, velocity);
      });
    }
  };

  oscManager->scheduleOffCallback = midiRouter->scheduleOffCallback;

  gamepadService->setMappingManager(mappingManager.get());
  gamepadService->startPolling(60);

  oscManager->updateSchema(*oscSchema);

  // Use recovery file as the reliable signal (AppState "crashed" is only saved on clean exit).
  if (CrashRecovery::hasRecoveryData()) {
    log("WARNING: The bridge did not shut down cleanly last session.", true);
    appState.setUseOpenGL(false);
    appState.setCrashed(true);
  }
  appState.setCrashed(true);  // Sentinel for this run; cleared and saved on clean shutdown
}

BridgeContext::~BridgeContext() {
  // 0. Revoke "living" pointer first so any pending callAsync will no-op
  s_livingContext.store(nullptr, std::memory_order_release);

  // 1. Clear async-triggering callbacks so no new callAsync are queued
  if (midiRouter) {
    midiRouter->onTransportCommand = nullptr;
    midiRouter->onSequencerInput = nullptr;
    midiRouter->onIncomingNoteForDisplay = nullptr;
  }
  if (countInManager)
    countInManager->onCountInComplete = nullptr;

  // 2. SILENCE AUDIO
  if (engine) {
    engine->stop();
    juce::Thread::sleep(20);
  }

  // 3. Clear cross-thread callbacks so no late callbacks run after destroy
  if (deviceService) {
    deviceService->setOnDeviceOpenError({});
    deviceService->setOnDeviceListChanged({});
  }
  if (clockWorker)
    clockWorker->onClockPulse = nullptr;

  // 4. Nullify deviceService before destruction
  if (midiRouter)
    midiRouter->setMidiService(nullptr);
  if (gamepadService)
    gamepadService->stopPolling();

  // 5. All notes off
  if (midiRouter)
    midiRouter->allNotesOff();
  if (deviceService)
    deviceService->forceAllNotesOff();

  // 6. STOP WORKERS (Signal -> Wait)
  if (networkWorker) {
    networkWorker->signalThreadShouldExit();
    networkWorker->workSignal.signal();
    if (!networkWorker->waitForThreadToExit(1000))
      juce::Logger::writeToLog("Warning: Network thread unresponsive.");
  }
  if (clockWorker) {
    clockWorker->signalThreadShouldExit();
    if (!clockWorker->waitForThreadToExit(500))
      juce::Logger::writeToLog("Warning: Clock thread unresponsive.");
  }

  // 7. Invalidate event-bus guard then unsubscribe (avoids callback after destroy)
  if (eventBusGuard_)
    eventBusGuard_->store(nullptr);
  if (bridgeEventBusSubscriptionId_ >= 0) {
    BridgeEventBus::instance().unsubscribe(bridgeEventBusSubscriptionId_);
    bridgeEventBusSubscriptionId_ = -1;
  }

  // 8. Flush logs & persist
  flightRecorder.log("System Shutdown Complete.");
  flightRecorder.flushToFile();
  appState.setCrashed(false);
  appState.save();
}

SequencerPanel *BridgeContext::getSequencer(int slot) const {
  if (slot == 0)
    return sequencer.get();
  int idx = slot - 1;
  if (idx >= 0 && idx < (int)extraSequencers.size())
    return extraSequencers[(size_t)idx].get();
  return nullptr;
}

SequencerPanel *BridgeContext::addExtraSequencer() {
  if ((int)extraSequencers.size() >= kMaxExtraSequencers)
    return nullptr;
  auto panel = std::make_unique<SequencerPanel>();
  int slot = 1 + (int)extraSequencers.size();
  if (engine)
    engine->setSequencer(slot, panel.get());
  if (engine)
    engine->setSequencerChannel(slot, slot + 1);
  SequencerPanel *ptr = panel.get();
  extraSequencers.push_back(std::move(panel));
  return ptr;
}

void BridgeContext::removeExtraSequencer(SequencerPanel *panel) {
  for (size_t i = 0; i < extraSequencers.size(); ++i) {
    if (extraSequencers[i].get() == panel) {
      int slot = 1 + (int)i;
      if (engine)
        engine->setSequencer(slot, nullptr);
      extraSequencers.erase(extraSequencers.begin() + (ptrdiff_t)i);
      return;
    }
  }
}

void BridgeContext::startServices() {
  // --- 4. CREATE WORKERS HERE (Moved from Destructor) ---
  // In user snippet they were created in Constructor but "Moved from
  // Destructor" comment suggests better placement. The user snippet ACTUALLY
  // put them in Constructor at the end. I will honor that or put them in
  // startServices to be safe (so UI can load first). Let's put in
  // startServices() as that's safer for threading.

  if (!networkWorker) {
    networkWorker = std::make_unique<NetworkWorker>(networkAirlock, inboundLane,
                                                    *oscManager);
    if (oscSchema)
      networkWorker->setSchema(*oscSchema);

    std::shared_ptr<std::atomic<BridgeContext*>> guard = eventBusGuard_;
    networkAirlock.setOnPush([guard] {
      BridgeContext* ctx = guard ? guard->load() : nullptr;
      if (!ctx || !ctx->networkWorker)
        return;
      ctx->networkWorker->workSignal.signal();
    });
    networkWorker->startThread();

    // Single path: BridgeEventBus subscriber pushes to network. Guard avoids use-after-free if emit runs during destroy.
    bridgeEventBusSubscriptionId_ = BridgeEventBus::instance().subscribe(
        [guard](const BridgeEvent &e) {
          BridgeContext* ctx = guard ? guard->load() : nullptr;
          if (!ctx || !ctx->networkWorker)
            return;
          ctx->networkWorker->pushEvent(e);
        });
  }

  if (!rtpManager) {
    // RtpManager now takes MidiDeviceService
    rtpManager = std::make_unique<RtpManager>(*deviceService, *midiRouter);
  }

  if (!clockWorker) {
    clockWorker = std::make_unique<ClockWorker>(midiClockSmoother);
    clockWorker->onClockPulse = [this] {
      if (deviceService)
        deviceService->sendMessage(juce::MidiMessage(0xF8));
    };
    clockWorker->startThread();
  }

  // Restore MIDI Connections (with user-visible errors if a device fails to
  // open)
  if (deviceService) {
    deviceService->setOnDeviceOpenError(
        [this](const juce::String &m) { log(m, true); });
    if (midiHardwareController && midiRouter)
      midiHardwareController->loadConfig(midiRouter.get());
  }

  // Restore last session's MIDI mappings (saved on close)
  if (profileManager && mappingManager) {
    juce::File mappingsFile =
        profileManager->getRootFolder().getChildFile("_mappings.json");
    if (mappingsFile.existsAsFile() &&
        !mappingManager->loadMappingsFromFile(mappingsFile))
      log("Could not load saved MIDI mappings. File may be invalid.", true);
  }
}

void BridgeContext::sendVisualParam(int paramIndex, float value) {
  if (paramIndex < 0 || paramIndex >= (int)visualAddressCache.size())
    return;

  if (oscManager && oscManager->isConnected()) {
    oscManager->sendFloat(visualAddressCache[paramIndex], value);
  }

  if (mappingManager)
    mappingManager->setParameterValue(visualIDCache[paramIndex], value);
}

void BridgeContext::dispatchParallelOsc(const BridgeEvent &e) {
  if (!isHighPerformanceMode)
    return;

  workerPool.addJob([this, e] {
    if (oscManager && oscManager->isConnected()) {
      if (e.type == EventType::NoteOn)
        oscManager->sendNoteOn(e.channel, e.noteOrCC, e.value);
      else if (e.type == EventType::NoteOff)
        oscManager->sendNoteOff(e.channel, e.noteOrCC);
      else if (e.type == EventType::ControlChange)
        oscManager->sendCC(e.channel, e.noteOrCC, e.value);
    }
  });
}

void BridgeContext::checkHardwareChanges() {
  if (deviceService)
    deviceService->reconcileHardware();
}

void BridgeContext::setPerformanceMode(bool isPro) {
  const juce::ScopedLock sl(modeLock);
  isHighPerformanceMode = isPro;
  applyAffinityForMode(isPro);
}

void BridgeContext::transitionMode(bool toPro) {
  if (midiRouter)
    midiRouter->allNotesOff();
  if (midiScheduler)
    midiScheduler->forceResetTime();
  setPerformanceMode(toPro);
  if (mappingManager && engine)
    mappingManager->publishChanges(engine->getBpm());
}

void BridgeContext::transitionSyncMode(bool useExt) {
  if (useExt) {
    engine->setLinkEnabled(false);  // Link OFF
    engine->setExtSyncActive(true); // Ext ON
    log("Sync: Slaved to External MIDI Clock", false);
  } else {
    engine->setExtSyncActive(false);
    // Only enable link if the app state wants it
    engine->setLinkEnabled(appState.getLinkPref());
    log("Sync: Internal/Link Mode", false);
  }
}

void BridgeContext::applyAffinityForMode(bool isPro) {
  juce::ignoreUnused(isPro);
}

void BridgeContext::dispatchCommand(const BridgeEvent &cmd) {
  if (systemState.load() != 1 && // 1=Ready
      cmd.type != EventType::SystemCommand) {
    return;
  }
  commandLane.push(cmd);
}

void BridgeContext::initializationComplete() {
  systemState.store(1); // Ready
  this->log("System Ready.", false);
}

void BridgeContext::updateInputs() {
  if (gamepadService)
    gamepadService->update();

  static int hwCheckCounter = 0;
  if (++hwCheckCounter > 100) {
    hwCheckCounter = 0;
    checkHardwareChanges();
  }
}
