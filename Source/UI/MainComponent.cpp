/*
  ==============================================================================
    Source/MainComponent.cpp
    Refactored: Modular Architecture (Panels & Delegated Handlers)
  ==============================================================================
*/

#include "../UI/MainComponent.h"
#include "../Audio/CountInManager.h"
#include "../Audio/Metronome.h"
#include "../Audio/MidiRouter.h"
#include "../Audio/PlaybackController.h"
#include "../Core/BridgeContext.h"
#include "../Core/Constants.h"
#include "../Core/DebugLog.h"
#include "../Core/LogService.h"
#include "../Core/RepaintCoordinator.h"
#include "../Core/SystemController.h"
#include "../Network/NetworkWorker.h"
#include "../Network/OscManager.h"
#include "../Services/MidiDeviceService.h"
#include "../Services/MidiMappingService.h"
#include "../Services/ProfileService.h"
#include <array>
#include <cmath>
#include <functional>

// Panels
#include "../UI/Panels/ArpeggiatorPanel.h"
#include "../UI/Panels/ConfigControls.h" // For ControlPage and OscAddressConfig
#include "../UI/Panels/ConfigPanel.h"
#include "../UI/Panels/LfoGeneratorPanel.h"
#include "../UI/Panels/MidiPlaylist.h"
#include "../UI/Panels/MixerPanel.h"
#include "../UI/Panels/PerformancePanel.h"
#include "../UI/Panels/SequencerPanel.h"
#include "../UI/Panels/TooltipManager.h"
#include "../UI/Panels/TrafficMonitor.h"
#include "../UI/Panels/TransportPanel.h"
#include "../UI/RenderConfig.h"
#include "Fonts.h"

#include "BinaryData.h"

// Widgets
#include "../Audio/NoteTracker.h"
#include "../UI/Animation.h"
#include "../UI/Widgets/DiagnosticOverlay.h"
#include "../UI/Widgets/MidiLearnOverlay.h"

// JUCE Modules
#include <juce_audio_basics/juce_audio_basics.h>

//==============================================================================
// CONSTRUCTOR & DESTRUCTOR
//==============================================================================
MainComponent::MainComponent() : configViewport(), statusBar(), logoView() {
  DebugLog::debugLog("MainComponent ctor start");
  initContextAndNetworkPanel();
  DebugLog::debugLog("initContextAndNetworkPanel OK");
  initPanels();
  DebugLog::debugLog("initPanels OK");
  initLookAndFeels();
  DebugLog::debugLog("initLookAndFeels OK");
  initModuleWindows();
  DebugLog::debugLog("initModuleWindows OK");
  wireModuleWindowCallbacks();
  DebugLog::debugLog("wireModuleWindowCallbacks OK");
  wireHeaderAndViewSwitching();
  DebugLog::debugLog("wireHeaderAndViewSwitching OK");
  wireTransportAndStatusBar();
  applyLayoutAndRestore();
  DebugLog::debugLog("applyLayoutAndRestore OK");
  wireOscLogAndConfigSync();
  wirePlaybackController();
  wireMappingManager();
  wireLfoPatching();
  DebugLog::debugLog("wireOscLog/Playback/Mapping/Lfo OK");
  initEngineAndStartServices();
  DebugLog::debugLog("initEngineAndStartServices OK");
  startAudioAndVBlank();
  tooltipTimer = std::make_unique<TooltipTimer>(*this);
  DebugLog::debugLog("MainComponent ctor done");
}

void MainComponent::initContextAndNetworkPanel() {
  context = std::make_unique<BridgeContext>();
  networkConfigPanel = std::make_unique<NetworkConfigPanel>();
  addChildComponent(*networkConfigPanel);
  networkConfigPanel->setVisible(false);
  context->keyboardState.addListener(this);
}

void MainComponent::initPanels() {
  headerPanel = std::make_unique<HeaderPanel>();
  transportPanel = std::make_unique<TransportPanel>(
      *context->engine, *context->midiRouter, *context);
  playlist = std::make_unique<MidiPlaylist>();
  logPanel = std::make_unique<TrafficMonitor>();
  arpPanel = std::make_unique<ArpeggiatorPanel>();
  performancePanel = std::make_unique<PerformancePanel>(
      context->keyboardState, *context->sequencer, *context);
  if (context->playbackController)
    context->playbackController->setSpliceEditor(
        &performancePanel->spliceEditor);
  configPanel = std::make_unique<ConfigPanel>();
  controlPage = std::make_unique<ControlPage>();
  oscConfigPanel = std::make_unique<OscAddressConfig>();
  chordPanel = std::make_unique<ChordGeneratorPanel>();
}

void MainComponent::initLookAndFeels() {
  fancyDialLF = std::make_unique<FancyDialLF>();
  for (auto *f : macroControls.faders)
    f->knob.setLookAndFeel(fancyDialLF.get());
  if (arpPanel) {
    // Arp uses ProKnob (physical-style); no custom LookAndFeel
  }
  mixerLookAndFeel = std::make_unique<MixerLookAndFeel>();
  context->mixer->setLookAndFeel(mixerLookAndFeel.get());
  menuLookAndFeel = std::make_unique<CustomMenuLookAndFeel>();
  juce::LookAndFeel::setDefaultLookAndFeel(menuLookAndFeel.get());
}

void MainComponent::initModuleWindows() {
  winEditor = std::make_unique<ModuleWindow>("Editor", *performancePanel);
  winMixer = std::make_unique<ModuleWindow>("Mixer", *context->mixer);
  winSequencer =
      std::make_unique<ModuleWindow>("Sequencer", *context->sequencer);
  winPlaylist = std::make_unique<ModuleWindow>("Playlist", *playlist);
  winLog = std::make_unique<ModuleWindow>("OSC Log", *logPanel);
  winArp = std::make_unique<ModuleWindow>("Arpeggiator", *arpPanel);
  winMacros = std::make_unique<ModuleWindow>("Macros", macroControls);
  winChords = std::make_unique<ModuleWindow>("Chords", *chordPanel);
  winLfoGen =
      std::make_unique<ModuleWindow>("LFO Generator", lfoGeneratorPanel);
  winControl = std::make_unique<ModuleWindow>("Control", *controlPage);
  winControl->setVisible(false);
  winControl->setBounds(320, 100, 380, 420);
  addAndMakeVisible(headerPanel.get());
  addAndMakeVisible(transportPanel.get());
}

void MainComponent::wireModuleWindowCallbacks() {
  // 1. Setup Close Callbacks and playback-safe drag for all windows
  // On move/resize: mark dashboard dirty and force full redraw to clear
  // ghosting (Eco and Pro).
  auto markDashboardDirty = [this]() {
    backgroundFillPending_ =
        true; // so paint() fills partial repaint region when GL attached
    if (context)
      context->repaintCoordinator.markDirty(RepaintCoordinator::Dashboard);
    repaint(); // Always mark main component dirty so full redraw is scheduled
    if (openGLContext.isAttached())
      openGLContext.triggerRepaint(); // Force GL layer to redraw (Eco + Pro)
  };
  auto setupWindow = [&](std::unique_ptr<ModuleWindow> &win) {
    win->onClose = [w = win.get()]() { w->setVisible(false); };
    win->isPlaying = [this]() { return isPlaying(); };
    win->onMoveOrResize = markDashboardDirty;
    win->onDetach = [this, w = win.get()] { detachModuleWindow(w); };
  };

  setupWindow(winEditor);
  setupWindow(winMixer);
  setupWindow(winSequencer);
  setupWindow(winPlaylist);
  setupWindow(winLog);
  setupWindow(winArp);
  setupWindow(winMacros);
  setupWindow(winChords);
  setupWindow(winLfoGen);
  setupWindow(winControl);

  // 2. Wire Header "Modules" Menu
  // Future: multi-instance modules (e.g. Sequencer +, Sequencer -) for multiple
  // sequencers on screen
  headerPanel->btnModules.onClick = [this, markDashboardDirty] {
    juce::PopupMenu m;
    m.addSectionHeader("Toggle Modules");

    auto addItem = [this, markDashboardDirty, &m](const juce::String &name,
                                                  ModuleWindow *win) {
      m.addItem(name, true, win->isVisible(), [this, win, markDashboardDirty] {
        bool willShow = !win->isVisible();
        if (willShow) {
          win->setVisible(true);
          Animation::fade(*win, 1.0f);
          win->toFront(true);
        } else {
          Animation::fade(*win, 0.0f);
          juce::Component::SafePointer<ModuleWindow> safe(win);
          juce::Timer::callAfterDelay(
              Animation::defaultDurationMs + 20,
              [this, safe, markDashboardDirty] {
                if (safe)
                  safe->setVisible(false);
                markDashboardDirty(); // Full repaint so vacated area clears
                                      // (avoids OpenGL ghosting)
              });
        }
      });
    };

    addItem("Editor", winEditor.get());
    addItem("Mixer", winMixer.get());
    // Sequencer: single instance now; future: + / - to add/remove extra
    // sequencer windows (channel switching)
    juce::PopupMenu seqSub;
    seqSub.addItem(
        "Sequencer (main)", true, winSequencer->isVisible(),
        [this, markDashboardDirty] {
          bool willShow = !winSequencer->isVisible();
          if (willShow) {
            winSequencer->setVisible(true);
            Animation::fade(*winSequencer, 1.0f);
            winSequencer->toFront(true);
          } else {
            Animation::fade(*winSequencer, 0.0f);
            juce::Component::SafePointer<ModuleWindow> safe(winSequencer.get());
            juce::Timer::callAfterDelay(Animation::defaultDurationMs + 20,
                                        [this, safe, markDashboardDirty] {
                                          if (safe)
                                            safe->setVisible(false);
                                          markDashboardDirty();
                                        });
          }
        });
    int maxExtra = context ? (BridgeContext::kMaxExtraSequencers -
                              (int)context->extraSequencers.size())
                           : 0;
    seqSub.addItem(
        "+ Add another Sequencer", maxExtra > 0, false,
        [this, markDashboardDirty] {
          if (!context || (int)context->extraSequencers.size() >=
                              BridgeContext::kMaxExtraSequencers)
            return;
          SequencerPanel *panel = context->addExtraSequencer();
          if (!panel || !sysController)
            return;
          int slot = context->getNumSequencerSlots() - 1;
          sysController->wireExtraSequencer(panel, slot);
          int n = (int)context->extraSequencers.size();
          juce::String name = "Sequencer " + juce::String(n + 1);
          extraModulePanels.push_back(std::make_unique<juce::Component>());
          auto *win = new ModuleWindow(name, *panel);
          extraModuleWindows.push_back(std::unique_ptr<ModuleWindow>(win));
          win->onClose = [this, win, panel] {
            removeExtraModuleWindow(win);
            if (context)
              context->removeExtraSequencer(panel);
          };
          win->isPlaying = [this]() { return isPlaying(); };
          win->onMoveOrResize = markDashboardDirty;
          win->onDetach = [this, win] { detachModuleWindow(win); };
          addAndMakeVisible(win);
          win->setBounds(8 + n * 20, 420 + n * 25, 520, 88);
          win->setVisible(true);
          win->toFront(true);
        });
    m.addSubMenu("Sequencer", seqSub);

    juce::PopupMenu lfoSub;
    lfoSub.addItem(
        "LFO Generator", true, winLfoGen->isVisible(),
        [this, markDashboardDirty] {
          bool willShow = !winLfoGen->isVisible();
          if (willShow) {
            winLfoGen->setVisible(true);
            Animation::fade(*winLfoGen, 1.0f);
            winLfoGen->toFront(true);
          } else {
            Animation::fade(*winLfoGen, 0.0f);
            juce::Component::SafePointer<ModuleWindow> safe(winLfoGen.get());
            juce::Timer::callAfterDelay(Animation::defaultDurationMs + 20,
                                        [this, safe, markDashboardDirty] {
                                          if (safe)
                                            safe->setVisible(false);
                                          markDashboardDirty();
                                        });
          }
        });
    lfoSub.addItem(
        "+ Add another LFO Generator", true, false, [this, markDashboardDirty] {
          auto *panel = new LfoGeneratorPanel();
          extraModulePanels.push_back(std::unique_ptr<juce::Component>(panel));
          auto *win = new ModuleWindow(
              "LFO " + juce::String(extraModuleWindows.size() + 2), *panel);
          extraModuleWindows.push_back(std::unique_ptr<ModuleWindow>(win));
          win->onClose = [this, win] { removeExtraModuleWindow(win); };
          win->isPlaying = [this]() { return isPlaying(); };
          win->onMoveOrResize = markDashboardDirty;
          win->onDetach = [this, win] { detachModuleWindow(win); };
          addAndMakeVisible(win);
          win->setBounds(536 + (int)extraModuleWindows.size() * 12,
                         132 + (int)extraModuleWindows.size() * 100, 260, 220);
          win->setVisible(true);
          win->toFront(true);
        });
    m.addSubMenu("LFO Generator", lfoSub);

    addItem("Playlist", winPlaylist.get());
    addItem("Arpeggiator", winArp.get());
    addItem("Macros", winMacros.get());
    addItem("Chords", winChords.get());
    addItem("Control", winControl.get());
    addItem("Log", winLog.get());

    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(
        &headerPanel->btnModules));
  };

  // 4b. UI HIERARCHY — Floating ModuleWindows
  addAndMakeVisible(winEditor.get());
  addAndMakeVisible(winMixer.get());
  addAndMakeVisible(winSequencer.get());
  addAndMakeVisible(winPlaylist.get());
  addAndMakeVisible(winLog.get());
  addAndMakeVisible(winArp.get());
  addAndMakeVisible(winMacros.get());
  addAndMakeVisible(winChords.get());
  addAndMakeVisible(winLfoGen.get());
  addAndMakeVisible(winControl.get());

  // 4c. Default layout = Full (3×3 grid): Log|Editor|Arp,
  // Playlist|Sequencer|Chords, Mixer|LFO|Macros Arp default size (compact);
  // slightly narrower Log/Playlist; slightly shorter Sequencer row
  const int topY = 68, leftX = 10, leftW = 268, centerX = 288, centerW = 404;
  const int rightX = 702, rightW = 268;
  const int row1H = 180, row2H = 188,
            row3H = 203; // row1H = default Arp height; row2H slightly reduced
  winLog->setBounds(leftX, topY, leftW, row1H);
  winEditor->setBounds(centerX, topY, centerW, row1H);
  winArp->setBounds(rightX, topY, rightW, row1H);
  winPlaylist->setBounds(leftX, topY + row1H, leftW, row2H);
  winSequencer->setBounds(centerX, topY + row1H, centerW, row2H);
  winChords->setBounds(rightX, topY + row1H, rightW, row2H);
  winMixer->setBounds(leftX, topY + row1H + row2H, leftW, row3H);
  winLfoGen->setBounds(centerX, topY + row1H + row2H, centerW, row3H);
  winMacros->setBounds(rightX, topY + row1H + row2H, rightW, row3H);

  // 4d. Wire Arp callback (previously in SidebarPanel)
  arpPanel->onArpUpdate = [this](int s, int v, int p, int o, float g) {
    if (context->midiRouter)
      context->midiRouter->updateArpSettings(s, v, p, o, g);
  };

  // Single nav button: toggles Dashboard <-> Config (label shows the other
  // view)
  addAndMakeVisible(btnDash);
  btnDash.setButtonText("Config");
  btnDash.setColour(juce::TextButton::buttonOnColourId,
                    Theme::accent.darker(0.3f));
  btnDash.onClick = [this] {
    setView(currentView == AppView::Dashboard ? AppView::OSC_Config
                                              : AppView::Dashboard);
  };

  addAndMakeVisible(btnMenu);
  btnMenu.setButtonText("Connections");
  btnMenu.setColour(juce::TextButton::buttonColourId, Theme::bgPanel);
  btnMenu.setColour(juce::TextButton::textColourOffId, Theme::text);
  btnMenu.onClick = [this] {
    if (onMenuClicked)
      onMenuClicked(&btnMenu);
  };

  addAndMakeVisible(btnUndo);
  btnUndo.setButtonText("Undo");
  btnUndo.setTooltip("Undo last edit (Ctrl+Z).");
  addAndMakeVisible(btnRedo);
  btnRedo.setButtonText("Redo");
  btnRedo.setTooltip("Redo (Ctrl+Y).");

  // BPM slider + Tap (top bar, right of Redo)
  addAndMakeVisible(tempoSlider);
  tempoSlider.getProperties().set("paramID", "Transport_BPM");
  tempoSlider.setSliderStyle(juce::Slider::LinearBar);
  tempoSlider.setRange(20.0, 300.0, 1.0);
  tempoSlider.setValue(120.0);
  tempoSlider.setDefaultValue(120.0);
  tempoSlider.setTextValueSuffix(" BPM");
  tempoSlider.setTooltip("Master tempo (BPM). Double-click value to type.");
  addChildComponent(lblBpm);
  lblBpm.setText("BPM", juce::dontSendNotification);
  lblBpm.setJustificationType(juce::Justification::centredRight);
  lblBpm.setVisible(false);
  addAndMakeVisible(btnTap);
  btnTap.setButtonText("TAP");
  btnTap.setTooltip("Tap to set BPM from your taps.");
  addAndMakeVisible(btnResetBpm);
  btnResetBpm.setButtonText("BPM");
  btnResetBpm.setTooltip("Reset BPM to default (from Config).");

  // Link button + beat indicator (top bar, left of THRU)
  addAndMakeVisible(btnLink);
  btnLink.setButtonText("Link");
  btnLink.setClickingTogglesState(true);
  btnLink.setTooltip("Enable/Disable Ableton Link");
  btnLink.setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
  addAndMakeVisible(linkIndicator);

  addAndMakeVisible(btnPanic);
  btnPanic.setButtonText("PANIC");
  btnPanic.setColour(juce::TextButton::buttonColourId,
                     juce::Colours::red.darker(0.5f));

  addAndMakeVisible(btnMidiLearn);
  btnMidiLearn.setButtonText("MIDI Learn");
  btnMidiLearn.setClickingTogglesState(true);
  btnMidiLearn.setColour(juce::TextButton::buttonOnColourId,
                         juce::Colours::orange);
  btnMidiLearn.onClick = [this] {
    toggleMidiLearnOverlay(btnMidiLearn.getToggleState());
  };

  addAndMakeVisible(btnExtSyncMenu);
  btnExtSyncMenu.setButtonText("EXT");
  btnExtSyncMenu.setClickingTogglesState(true);

  addAndMakeVisible(btnThru);
  btnThru.setButtonText("THRU");
  btnThru.setClickingTogglesState(true);

  addAndMakeVisible(statusBar);
  statusBar.setDeviceManager(&deviceManager);
  statusBar.onScaleChanged = [this](float scale) {
    juce::Desktop::getInstance().setGlobalScaleFactor(scale);
    if (context)
      context->configManager.set("uiScale", static_cast<double>(scale));
  };
  statusBar.setStatus("Ready");

  float savedScale =
      static_cast<float>(context->configManager.get<double>("uiScale", 0.9));
  statusBar.setScale(savedScale);
  juce::Desktop::getInstance().setGlobalScaleFactor(savedScale);

  // Viewports (config overlay only — mixer is now in its own ModuleWindow)
  configViewport.setScrollBarsShown(true, false);
  configViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  addChildComponent(configViewport);

  // Backgrounds
  headerPanel->setOpaque(false);
  performancePanel->setOpaque(false);

  addAndMakeVisible(dynamicBg);
  dynamicBg.toBack();

  // Logo
  addAndMakeVisible(logoView);
  auto logoImage = juce::ImageCache::getFromMemory(BinaryData::logo_png,
                                                   BinaryData::logo_pngSize);
  if (logoImage.isValid())
    logoView.setImage(logoImage, juce::RectanglePlacement::centred);

  // Overlays
  midiLearnOverlay =
      std::make_unique<MidiLearnOverlay>(*context->mappingManager, *this);
  midiLearnOverlay->onDone = [this] {
    toggleMidiLearnOverlay(false);
    btnMidiLearn.setToggleState(false, juce::dontSendNotification);
    btnMidiLearn.setButtonText("MIDI Learn");
    if (context && context->mappingManager)
      context->mappingManager->setLearnModeActive(false);
  };
  addChildComponent(midiLearnOverlay.get());

  diagOverlay = std::make_unique<DiagnosticOverlay>(context->diagData);
  addChildComponent(diagOverlay.get());

  // Setup Wizard
  if (!context->appState.hasSeenTour()) {
    addAndMakeVisible(setupWizard);
    setupWizard.onFinished = [this] {
      context->appState.setSeenTour(true);
      setupWizard.setVisible(false);
    };
  }
}

// --- Init phases (called from constructor after wireModuleWindowCallbacks) ---
void MainComponent::wireHeaderAndViewSwitching() {
  juce::String savedClockId =
      context->configManager.get<juce::String>("clockSourceId", "");
  if (context->midiRouter && savedClockId.isNotEmpty())
    context->midiRouter->setClockSourceID(savedClockId);
  sysController = std::make_unique<SystemController>(*context);
  sysController->bindInterface(*this);
}

void MainComponent::wireTransportAndStatusBar() {
  // Transport and status bar are bound in SystemController::bindInterface
  // (bindTransport, bindHeader).
}

void MainComponent::applyLayoutAndRestore() {
  if (!context->appState.hasSeenLayoutWizard()) {
    sysController->resetWindowLayout();
    context->appState.setCurrentLayoutName("Full");
    addAndMakeVisible(layoutChoiceWizard);
    layoutChoiceWizard.onLayoutChosen = [this](const juce::String &name) {
      sysController->applyLayoutPreset(name);
      context->appState.setSeenLayoutWizard(true);
      layoutChoiceWizard.setVisible(false);
      if (auto *w = findParentComponentOfClass<juce::ResizableWindow>()) {
        if (name == "Minimal")
          w->setSize(920, 620);
        else
          w->setSize(1024, 768);
      }
    };
    layoutChoiceWizard.setVisible(true);
  } else {
    sysController->restoreWindowLayout();
  }
}

void MainComponent::wireOscLogAndConfigSync() {
  if (networkConfigPanel) {
    networkConfigPanel->edIp.setText(context->appState.getIp(),
                                     juce::dontSendNotification);
    networkConfigPanel->edPortOut.setText(
        juce::String(context->appState.getPortOut()),
        juce::dontSendNotification);
    networkConfigPanel->edPortIn.setText(
        juce::String(context->appState.getPortIn()),
        juce::dontSendNotification);
  }
  if (context->oscManager) {
    bool ok = context->oscManager->connect(context->appState.getIp(),
                                           context->appState.getPortOut(),
                                           context->appState.getPortIn());
    if (!ok)
      onLogMessage("Could not connect to OSC. Check IP and ports. Click "
                   "Connect to retry.",
                   true);
  }
  if (configPanel) {
    configPanel->sliderLatency.setValue(context->appState.getNetworkLookahead(),
                                        juce::dontSendNotification);
    bool bypass = context->appState.getLookaheadBypass();
    configPanel->btnLowLatency.setToggleState(bypass,
                                              juce::dontSendNotification);
    configPanel->btnBypassLookahead.setToggleState(bypass,
                                                   juce::dontSendNotification);
    configPanel->sliderClockOffset.setValue(context->appState.getClockOffset(),
                                            juce::dontSendNotification);
    if (context->engine)
      context->engine->setOutputLatency(
          context->appState.getNetworkLookahead());
    bool mc = context->appState.props->getBoolValue("multicast", false);
    configPanel->btnMulticast.setToggleState(mc, juce::dontSendNotification);
    bool zc = context->appState.props->getBoolValue("zeroconf", true);
    configPanel->btnZeroConfig.setToggleState(zc, juce::dontSendNotification);
    configPanel->edIp.setEnabled(!zc);
    configPanel->edIp.setText(zc ? "Searching..." : context->appState.getIp(),
                              juce::dontSendNotification);
    bool ipv6 = context->appState.getUseIPv6();
    configPanel->btnIPv6.setToggleState(ipv6, juce::dontSendNotification);
    if (context->oscManager) {
      context->oscManager->setZeroConfig(zc);
      if (mc) {
        bool ok = context->oscManager->connect("255.255.255.255",
                                               context->appState.getPortOut(),
                                               context->appState.getPortIn());
        if (!ok)
          onLogMessage("Could not connect to OSC. Check IP and ports. Click "
                       "Connect to retry.",
                       true);
      }
    }
  }
  int lastTheme = juce::jlimit(1, ThemeManager::getNumThemes(),
                               context->configManager.get<int>("themeId", 1));
  applyThemeToAllLookAndFeels(lastTheme);
}

void MainComponent::wirePlaybackController() {
  // Wired in SystemController::bindPlaybackController (called from
  // bindInterface).
}

void MainComponent::wireMappingManager() {
  // Wired in SystemController::bindMappingManager (called from bindInterface).
}

void MainComponent::wireLfoPatching() {
  // Wired in SystemController::bindLfoPatching (called from bindInterface).
}

void MainComponent::initEngineAndStartServices() {
  if (context->engine)
    context->engine->setBpm(Constants::kDefaultBpm);
  context->startServices();
  context->initializationComplete();
  if (context->engine) {
    context->engine->setLfoFrequency(lfoGeneratorPanel.getRate(0));
    context->engine->setLfoDepth(lfoGeneratorPanel.getDepth(0));
    context->engine->setLfoWaveform(lfoGeneratorPanel.getShape(0) - 1);
  }
  deviceManager.addChangeListener(this);
}

void MainComponent::startAudioAndVBlank() {
  setSize(1024, 768);
  DebugLog::debugLog("startAudioAndVBlank: before setAudioChannels");
  setAudioChannels(0, 2);
  DebugLog::debugLog("startAudioAndVBlank: setAudioChannels done");
  // Refactored: Delegated to handleVBlank for cleanliness (Issue #5)
  vBlankAttachment = std::make_unique<juce::VBlankAttachment>(
      this, [this]() { handleVBlank(); });
}

void MainComponent::handleVBlank() {
  try {
    flushPendingResize();

    // 1. Gather State
    bool isPlaying =
        context && context->engine && context->engine->getIsPlaying();
    bool hasVisuals = dynamicBg.hasActiveParticles();
    bool mouseActive = false;
    if (auto *mouseSrc = juce::Desktop::getInstance().getMouseSource(0))
      mouseActive = mouseSrc->isDragging();

    // 2. Idle Check: If nothing happened, check occasional scale update and
    // sleep
    if (!isPlaying && !hasVisuals && !mouseActive) {
      static int idleFrames = 0;
      bool windowFocused = true;
      if (auto *rw = findParentComponentOfClass<juce::ResizableWindow>())
        windowFocused = rw->hasKeyboardFocus(true) || rw->isActiveWindow();

      int idleThreshold = windowFocused ? 30 : 10;
      if (++idleFrames < idleThreshold) {
        if (vBlankWasAnimating) {
          repaint(); // One final paint to settle
          vBlankWasAnimating = false;
        }
        return;
      }

      // Occasional heavy check (scale factor)
      idleFrames = 0;
      float newScale = 1.0f;
      if (auto *disp =
              juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        newScale = static_cast<float>(disp->scale);
      if (std::abs(newScale - cachedDisplayScale) > 0.01f) {
        cachedDisplayScale = newScale;
        repaint();
      }
      return;
    }

    // 3. Active Frame
    vBlankWasAnimating = true;
    double now = juce::Time::getMillisecondCounterHiRes();
    float dt =
        (lastFrameTime > 0.0) ? (float)(now - lastFrameTime) / 1000.0f : 0.016f;
    lastFrameTime = now;

    // 4. Updates (LFO, Animation)
    if (lfoGeneratorPanel.isLfoRunning())
      updateLfoPatches(dt);
    if (hasVisuals)
      dynamicBg.updateAnimation(dt);

    // 5. Controller Updates (Reduced rate for CPU saving)
    // Run full UI updates at ~15Hz (every 4th frame at 60Hz)
    static int updateFrame = 0;
    updateFrame = (updateFrame + 1) % 4;

    // Always flush repaint coordinator to catch dirty regions
    if (context) {
      context->repaintCoordinator.flush(
          [this](uint32_t dirtyBits) { repaintDirtyRegions(dirtyBits); });
    }

    if (updateFrame == 0 && sysController) {
      sysController->processUpdates(true);
    }

    // 6. Repaint Trigger
    // If OpenGL attached, we must trigger; otherwise repaint() handles dirty
    // regions
    if (openGLContext.isAttached()) {
      openGLContext.triggerRepaint();
    } else if (context && (context->repaintCoordinator.hadDirtyLastFlush() ||
                           hasVisuals)) {
      repaint();
    }

  } catch (const std::exception &e) {
    DebugLog::debugLog(juce::String("VBlank exception: ") + e.what());
  } catch (...) {
    DebugLog::debugLog("VBlank exception: unknown");
  }
}

MainComponent::~MainComponent() {
  DebugLog::debugLog("~MainComponent: start");
  // 1. Stop vblank and audio first (no more callbacks touching UI/GL)
  vBlankAttachment.reset();
  DebugLog::debugLog("~MainComponent: vblank stopped");
  deviceManager.removeChangeListener(this);
  shutdownAudio();
  DebugLog::debugLog("~MainComponent: audio shutdown done");

  // 2. Detach GPU contexts before destroying components
#if PATCHWORLD_VULKAN_SUPPORT
  if (vulkanContext && vulkanContext->isAttached())
    vulkanContext->detach();
  vulkanContext.reset();
  DebugLog::debugLog("~MainComponent: Vulkan detached");
#endif
  openGLContext.detach();
  DebugLog::debugLog("~MainComponent: OpenGL detached");

  // 3. Unhook listeners and clear module windows
  if (context)
    context->keyboardState.removeListener(this);
  extraModuleWindows.clear();
  extraModulePanels.clear();

  // 4. Clear LookAndFeel references before destroying
  for (auto *f : macroControls.faders)
    f->knob.setLookAndFeel(nullptr);
  if (arpPanel) {
  }
  if (context && context->mixer)
    context->mixer->setLookAndFeel(nullptr);
  juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
  fancyDialLF.reset();
  mixerLookAndFeel.reset();
  menuLookAndFeel.reset();

  // 5. Clear LogService callback to avoid use-after-free
  LogService::instance().onLogEntry = nullptr;

  // 6. Destroy controller and context (SystemController dtor unsubscribes
  // TimerHub etc.)
  sysController.reset();
  DebugLog::debugLog("~MainComponent: sysController reset");
  context.reset();
  DebugLog::debugLog("~MainComponent: done");
}

void MainComponent::saveStateBeforeShutdown() {
  if (!context)
    return;
  context->appState.setCleanExit(true);
  context->appState.forceSave();
  if (context->mappingManager && context->profileManager) {
    juce::File mappingsFile =
        context->profileManager->getRootFolder().getChildFile("_mappings.json");
    context->mappingManager->saveMappingsToFile(mappingsFile);
  }
  if (sysController)
    sysController->saveWindowLayout();
}

// Audio lifecycle (prepareToPlay, getNextAudioBlock, releaseResources,
// isPlaying) in MainComponentCore.cpp

void MainComponent::removeExtraModuleWindow(ModuleWindow *win) {
  if (!win)
    return;
  for (size_t i = 0; i < extraModuleWindows.size(); ++i) {
    if (extraModuleWindows[i].get() == win) {
      removeChildComponent(win);
      extraModuleWindows.erase(extraModuleWindows.begin() + (ptrdiff_t)i);
      extraModulePanels.erase(extraModulePanels.begin() + (ptrdiff_t)i);
      break;
    }
  }
}

void MainComponent::showAllModules() {
  for (ModuleWindow *w :
       {winEditor.get(), winMixer.get(), winSequencer.get(), winPlaylist.get(),
        winLog.get(), winArp.get(), winMacros.get(), winChords.get(),
        winLfoGen.get(), winControl.get()}) {
    if (w) {
      w->setVisible(true);
      w->toFront(true);
    }
  }
  repaint();
}

void MainComponent::hideAllModules() {
  for (ModuleWindow *w :
       {winEditor.get(), winMixer.get(), winSequencer.get(), winPlaylist.get(),
        winLog.get(), winArp.get(), winMacros.get(), winChords.get(),
        winLfoGen.get(), winControl.get()}) {
    if (w)
      w->setVisible(false);
  }
  repaint();
}

void MainComponent::updateLfoPatches(float dtSec) {
  if (lfoPatches_.empty() || !context || !context->mappingManager)
    return;
  static const double twoPi = 6.283185307179586;
  const float phaseDelta =
      0.01f; // for envelope derivative (steep = slow, slope = fast)
  for (int i = 0; i < 4; ++i) {
    double baseRate = (double)lfoGeneratorPanel.getRate(i);
    float depth = lfoGeneratorPanel.getDepth(i);
    int shapeId = lfoGeneratorPanel.getShape(i);

    // Patched control value (0–1): scale LFO rate so moving the assigned
    // control sets speed
    float patchedControl = 0.5f;
    for (const auto &p : lfoPatches_) {
      if (p.first == i && context->mappingManager->getParameterValue) {
        patchedControl = context->mappingManager->getParameterValue(p.second);
        patchedControl = juce::jlimit(0.0f, 1.0f, patchedControl);
        break;
      }
    }
    double rateScale = 0.2 + 0.8 * (double)patchedControl;

    float phase = (float)lfoPhase_[i];
    float env = lfoGeneratorPanel.getEnvelopeAtPhase(i, phase);
    // Curve respect: steep envelope (high |dEnv/dPhase|) slows LFO; gentle
    // slope speeds it up
    float envNext = lfoGeneratorPanel.getEnvelopeAtPhase(
        i, juce::jmin(1.0f, phase + phaseDelta));
    float envPrev = lfoGeneratorPanel.getEnvelopeAtPhase(
        i, juce::jmax(0.0f, phase - phaseDelta));
    float envDeriv = (envNext - envPrev) / (2.0f * phaseDelta);
    float steepFactor =
        1.0f / (1.0f + 2.0f * std::abs(envDeriv)); // steep -> slow
    double effectiveRate = baseRate * rateScale * (double)steepFactor;
    lfoPhase_[i] += effectiveRate * (double)dtSec;
    if (lfoPhase_[i] >= 1.0)
      lfoPhase_[i] -= 1.0;
    if (lfoPhase_[i] < 0.0)
      lfoPhase_[i] += 1.0;
    phase = (float)lfoPhase_[i];
    env = lfoGeneratorPanel.getEnvelopeAtPhase(i, phase);

    float wave = 0.5f;
    if (shapeId == 5) {
      wave = env;
    } else {
      if (shapeId == 1)
        wave = 0.5f + 0.5f * std::sin(phase * (float)twoPi);
      else if (shapeId == 2)
        wave = 2.0f * std::abs(phase - 0.5f);
      else if (shapeId == 3)
        wave = phase;
      else if (shapeId == 4)
        wave = phase < 0.5f ? 0.0f : 1.0f;
      wave *= env;
    }
    // Output 0–1 so patched fader/slider/knob behaves like MIDI (full range)
    float value = depth * wave + (1.0f - depth) * 0.5f;
    value = juce::jlimit(0.0f, 1.0f, value);
    for (const auto &p : lfoPatches_) {
      if (p.first == i)
        context->mappingManager->setParameterValue(p.second, value);
    }
  }
}

// --- GUI LIFECYCLE ---
void MainComponent::paint(juce::Graphics &g) {
  static bool firstPaint = true;
  if (firstPaint) {
    firstPaint = false;
    DebugLog::debugLog("paint() first call");
  }
  try {
    if (!isGpuAvailable.load(std::memory_order_relaxed)) {
      g.fillAll(Theme::bgDark);
      if (showGpuUnavailableMessage.load(std::memory_order_relaxed)) {
        g.setColour(Theme::text.withAlpha(0.5f));
        g.setFont(Fonts::header());
        g.drawText("Software rendering (GPU unavailable)", getLocalBounds(),
                   juce::Justification::centred);
      }
      if (isBoxSelecting) {
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.fillRect(selectionBox);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.drawRect(selectionBox, 2);
      }
      return;
    }
#if PATCHWORLD_VULKAN_SUPPORT
    if (vulkanContext && vulkanContext->isAttached())
      return;
#endif
    if (openGLContext.isAttached()) {
      // After a module move, fill partial repaint region (vacated area) with
      // background to clear ghosting
      if (backgroundFillPending_) {
        auto clip = g.getClipBounds();
        int w = getWidth(), h = getHeight();
        if (w > 0 && h > 0 && (clip.getWidth() < w || clip.getHeight() < h))
          g.fillAll(Theme::bgDark);
        backgroundFillPending_ = false;
      }
      return;
    }
    g.fillAll(Theme::bgDark);

    // Draw multi-select box
    if (isBoxSelecting) {
      g.setColour(juce::Colours::white.withAlpha(0.1f));
      g.fillRect(selectionBox);
      g.setColour(juce::Colours::white.withAlpha(0.5f));
      g.drawRect(selectionBox, 2);
    }
  } catch (const std::exception &e) {
    DebugLog::debugLog(juce::String("paint() exception: ") + e.what());
    g.fillAll(Theme::bgDark);
  } catch (...) {
    DebugLog::debugLog("paint() exception: unknown");
    g.fillAll(Theme::bgDark);
  }
}

// --- Detached Window Implementation ---
class MainComponent::DetachedWindow : public juce::DocumentWindow {
public:
  DetachedWindow(const juce::String &name, juce::Component &content,
                 std::function<void()> onClose)
      : DocumentWindow(name, Theme::bgPanel, DocumentWindow::allButtons),
        closeCallback(std::move(onClose)) {
    setUsingNativeTitleBar(true);
    setContentNonOwned(&content, true);
    setResizable(true, false);
    setResizeLimits(300, 200, 2000, 1500);
    centreWithSize(600, 400); // Default size if not saved
    setVisible(true);
  }

  void closeButtonPressed() override {
    if (closeCallback)
      closeCallback();
  }
  std::function<void()> closeCallback;
};

void MainComponent::detachModuleWindow(ModuleWindow *win) {
  if (!win)
    return;

  juce::String name = win->getName();
  if (detachedWindows.count(name)) {
    // Already detached, just bring to front
    detachedWindows[name]->toFront(true);
    return;
  }

  // Move content to new window.
  // Using getContent() which returns reference to the panel owned by
  // MainComponent
  auto &content = win->getContent();

  // Create wrapper window
  auto dw = std::make_unique<DetachedWindow>(
      name, content, [this, name] { reattachModuleWindow(name); });

  // Save state before hiding? optional.

  detachedWindows[name] = std::move(dw);
  win->setVisible(false);
}

void MainComponent::reattachModuleWindow(const juce::String &moduleName) {
  auto it = detachedWindows.find(moduleName);
  if (it == detachedWindows.end())
    return;

  // Find the original module window wrapper
  ModuleWindow *targetWin = nullptr;
  std::vector<ModuleWindow *> windows = {
      winEditor.get(), winMixer.get(),  winSequencer.get(), winPlaylist.get(),
      winLog.get(),    winArp.get(),    winMacros.get(),    winChords.get(),
      winLfoGen.get(), winControl.get()};

  for (auto *w : windows) {
    if (w && w->getName() == moduleName) {
      targetWin = w;
      break;
    }
  }

  // Also check extra windows if supported later, but for now fixed list covers
  // main ones.

  if (targetWin) {
    // Move content back to ModuleWindow
    // content ref is still valid (owned by MainComponent members)
    targetWin->addAndMakeVisible(targetWin->getContent());
    targetWin->setVisible(true);
    targetWin->toFront(true);
    if (targetWin->onMoveOrResize)
      targetWin->onMoveOrResize();
  } else {
    // Check extra windows
    for (auto &w : extraModuleWindows) {
      if (w && w->getName() == moduleName) {
        w->addAndMakeVisible(w->getContent());
        w->setVisible(true);
        w->toFront(true);
        if (w->onMoveOrResize)
          w->onMoveOrResize();
        targetWin = w.get();
        break;
      }
    }
  }

  detachedWindows.erase(it); // Destroys DetachedWindow
}

void MainComponent::setupComponentCaching() {
  if (!context)
    return;
  // Static panels — cache aggressively
  if (headerPanel)
    headerPanel->setBufferedToImage(true);
  if (transportPanel)
    transportPanel->setBufferedToImage(true);

  // Dynamic panels — never cache (constantly updating)
  if (performancePanel) {
    performancePanel->setBufferedToImage(false);
    performancePanel->playView.setBufferedToImage(false);
    performancePanel->spliceEditor.setBufferedToImage(false);
  }

  // Mixer — cache strips, not meters
  if (context && context->mixer) {
    for (auto *strip : context->mixer->strips) {
      strip->setBufferedToImage(true);
      strip->meter.setBufferedToImage(false);
    }
  }

  // Module windows — don't cache (can be resized/moved)
  for (auto *win :
       {winEditor.get(), winMixer.get(), winSequencer.get(), winPlaylist.get(),
        winLog.get(), winArp.get(), winMacros.get(), winChords.get(),
        winLfoGen.get(), winControl.get()}) {
    if (win)
      win->setBufferedToImage(false);
  }
}

void MainComponent::flushPendingResize() {
  if (resizePending.exchange(false, std::memory_order_acquire)) {
    juce::Rectangle<int> bounds;
    {
      const juce::ScopedLock sl(resizeLock);
      bounds = pendingResizeBounds;
    }
    bounds.setWidth(juce::jmax(1, bounds.getWidth()));
    bounds.setHeight(juce::jmax(1, bounds.getHeight()));
    applyLayout(bounds);
  }
}

void MainComponent::repaintDirtyRegions(uint32_t dirtyBits) {
  using RC = RepaintCoordinator;
  // Repaint only the affected components; full repaint only when Dashboard is
  // dirty.
  if ((dirtyBits & RC::PianoRoll) || (dirtyBits & RC::Playhead) ||
      (dirtyBits & RC::VelocityLane))
    if (performancePanel)
      performancePanel->repaint();
  if (dirtyBits & RC::Mixer)
    if (winMixer)
      winMixer->repaint();
  if (dirtyBits & RC::Sequencer)
    if (winSequencer)
      winSequencer->repaint();
  if (dirtyBits & RC::Transport)
    if (transportPanel)
      transportPanel->repaint();
  if (dirtyBits & RC::Log)
    if (logPanel)
      logPanel->repaint();
  if (dirtyBits & RC::Dashboard)
    repaint(); // Full repaint only when module layout/windows changed
}

// resized() and applyLayout() implemented in MainComponentCore.cpp

// --- OPENGL --- (see MainComponentGL.cpp)

// --- AUDIO DEVICE HOT-SWAP ---
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster *source) {
  if (source != &deviceManager)
    return;

  auto *device = deviceManager.getCurrentAudioDevice();
  if (device) {
    double rate = device->getCurrentSampleRate();
    int blockSize = device->getCurrentBufferSizeSamples();

    if (context && context->engine) {
      bool wasPlaying = context->engine->getIsPlaying();
      context->engine->stop();
      context->engine->prepareToPlay(rate, blockSize);
      if (wasPlaying)
        context->engine->play();
    }
    onLogMessage("Audio device changed: " + juce::String(rate, 1) + " Hz",
                 false);
  } else {
    onLogMessage("The audio device was disconnected. Select a new device in "
                 "Control or reconnect.",
                 true);
  }
}

// --- LOGIC DELEGATION ---
void MainComponent::setView(AppView v) {
  currentView = v;

  // Single nav button: show the view you'll switch to (Config when on
  // Dashboard, Dashboard when on Config)
  btnDash.setButtonText(v == AppView::Dashboard ? "Config" : "Dashboard");

  if (v == AppView::Control) {
    if (winControl) {
      winControl->setVisible(true);
      winControl->toFront(true);
    }
    v = AppView::Dashboard;
    currentView = AppView::Dashboard;
    btnDash.setButtonText("Config");
  }
  if (v == AppView::OSC_Config) {
    configViewport.setViewedComponent(configPanel.get(), false);
    if (sysController)
      sysController->refreshConfigPanelFromBackend();
    // Ensure Config shows in the main window: bring main window to front so it
    // isn't hidden behind Editor/modules
    if (auto *rw = findParentComponentOfClass<juce::ResizableWindow>())
      rw->toFront(true);
  } else if (v == AppView::Dashboard) {
    if (sysController)
      sysController->refreshTransportFromBackend();
    // Do not change module visibility here — layout (Full/Minimal/saved)
    // decides which are visible
  }

  resized();
}

void MainComponent::scrollConfigToOscAddresses() {
  if (!configPanel || configViewport.getViewedComponent() != configPanel.get())
    return;
  int y = configPanel->oscAddresses.getY();
  configViewport.setViewPosition(0, juce::jmax(0, y - 40));
}

void MainComponent::toggleMidiLearnOverlay(bool show) {
  if (context)
    context->isMidiLearnMode.store(show);
  if (midiLearnOverlay) {
    midiLearnOverlay->setOverlayActive(show);
    if (show) {
      midiLearnOverlay->toFront(
          true); // Ensure overlay is on top (avoids inconsistent visibility)
      midiLearnOverlay->setAlwaysOnTop(true);
    } else {
      midiLearnOverlay->setAlwaysOnTop(false);
    }
  }
}

// --- MIDI Keyboard State Listener (Virtual Keyboard -> MIDI) ---
void MainComponent::handleNoteOn(juce::MidiKeyboardState *, int ch, int note,
                                 float vel) {
  if (context && context->midiRouter) {
    int sel = context->midiRouter->selectedChannel;
    int outCh = (sel >= 1 && sel <= 16) ? sel : juce::jlimit(1, 16, ch);
    context->midiRouter->handleNoteOn(outCh, note, vel, false, false,
                                      BridgeEvent::Source::UserInterface);
  }
}

void MainComponent::handleNoteOff(juce::MidiKeyboardState *, int ch, int note,
                                  float vel) {
  juce::ignoreUnused(vel);
  if (context && context->midiRouter) {
    int sel = context->midiRouter->selectedChannel;
    int outCh = (sel >= 1 && sel <= 16) ? sel : juce::jlimit(1, 16, ch);
    context->midiRouter->handleNoteOff(outCh, note, 0.0f, false, false,
                                       BridgeEvent::Source::UserInterface);
  }
}

// File drag, mouse, key — see MainComponentEvents.cpp

void MainComponent::onLogMessage(const juce::String &msg, bool /*isError*/) {
  if (logPanel)
    logPanel->log(msg, true);
}

void MainComponent::applyThemeToAllLookAndFeels(int themeId) {
  ThemeManager::applyTheme(themeId, getLookAndFeel());
  if (mixerLookAndFeel)
    ThemeManager::applyTheme(themeId, *mixerLookAndFeel);
  if (fancyDialLF)
    ThemeManager::applyTheme(themeId, *fancyDialLF);
  if (menuLookAndFeel)
    ThemeManager::applyTheme(themeId, *menuLookAndFeel);
  sendLookAndFeelChange();
  repaint();
}

// handleRenderModeChange in MainComponentGL.cpp
