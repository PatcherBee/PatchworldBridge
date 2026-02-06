/*
  ==============================================================================
    Source/Core/SystemController.cpp
    Role: Implementation of the wiring harness.
    Refactored: Centralized logic (Transport, Config, Inputs)
  ==============================================================================
*/

#include "../Core/SystemController.h"
#include "../Core/BridgeContext.h"
#include "../Core/Constants.h"
#include "../Core/CrashRecovery.h"
#include "../Core/DebugLog.h"
#include "../Core/LogService.h"
#include "../Core/MenuBuilder.h"
#include "../Core/MidiHardwareController.h"
#include "../Core/MixerViewModel.h"
#include "../Core/ProjectInfo.h"
#include "../Core/SequencerViewModel.h"
#include "../Core/ShortcutManager.h"
#include "../Core/ThreadingConfig.h"
#include "../Core/TimerHub.h"
#include "../Core/UIWatchdog.h"
#include "../UI/MainComponent.h"
#include "../UI/OscAddressDialog.h"
#include "../UI/Panels/ArpeggiatorPanel.h"
#include "../UI/Panels/ChordGeneratorPanel.h"
#include "../UI/Panels/ConfigPanel.h"
#include "../UI/Panels/HeaderPanel.h"
#include "../UI/Panels/LfoGeneratorPanel.h"
#include "../UI/Panels/MidiDevicePickerPanel.h"
#include "../UI/Panels/MidiPlaylist.h"
#include "../UI/Panels/MixerPanel.h"
#include "../UI/Panels/ModulesTogglePanel.h"
#include "../UI/Panels/PerformancePanel.h"
#include "../UI/Panels/TrafficMonitor.h"
#include "../UI/Panels/TransportPanel.h"
#include "../UI/PopupMenuOptions.h"
#include "../UI/RenderBackend.h"
#include "../UI/Widgets/ShortcutsPanel.h"
#include "../UI/Widgets/SignalPathLegend.h"
#include <atomic>

namespace {
static RenderBackend::Type backendNameToType(const juce::String &name) {
  if (name == "Software")
    return RenderBackend::Type::Software;
  if (name == "OpenGL")
    return RenderBackend::Type::OpenGL;
  if (name == "Metal")
    return RenderBackend::Type::Metal;
  if (name == "Vulkan")
    return RenderBackend::Type::Vulkan;
  if (name == "Auto")
    return RenderBackend::Type::Auto;
  return RenderBackend::Type::OpenGL;
}
} // namespace

// JUCE Modules
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Managed Components
#include "../Audio/AudioEngine.h"
#include "../Audio/CountInManager.h"
#include "../Audio/MidiRouter.h" // Needed for MidiInputCallback
#include "../Audio/PlaybackController.h"
#include "../Network/NetworkWorker.h"
#include "../Network/OscManager.h"
#include "../Network/RtpManager.h"
#include "../Services/GamepadService.h"
#include "../Services/LatencyCalibrator.h"
#include "../Services/MidiDeviceService.h"
#include "../Services/MidiMappingService.h"
#include "../Services/ProfileService.h"
#include "../UI/Widgets/ModuleWindow.h"

namespace {
std::atomic<SystemController *> s_livingController{nullptr};

juce::String getHelpText() {
  return juce::String::fromUTF8(
      "PATCHWORLD BRIDGE — HELP\n"
      "========================\n\n"
      "This app bridges OSC, MIDI, and Ableton Link between Patchworld and "
      "your DAW or hardware.\n\n"

      "QUICK SETUP\n"
      "----------\n"
      "1. Network: Open Connections > Network... (or Config > OSC Network). "
      "Enter the target IP (e.g., 127.0.0.1 or your headset's IP). Ports are "
      "auto-assigned (9000/9001) but can be changed. Click Connect.\n"
      "2. MIDI: Connections > MIDI Inputs/Outputs. Select checked devices to "
      "enable them. Use Config > MIDI Routing for global channel/Thru "
      "settings.\n"
      "3. Transport: Press Play in the dashboard to start the global transport "
      "Ensure 'Ableton Link' is enabled in Config if syncing with other "
      "apps.\n\n"

      "MAIN FEATURES\n"
      "-------------\n"
      "• Bridge: Automatically forwards incoming MIDI to OSC (and vice versa) "
      "based on the schema. See the Log window for traffic.\n"
      "• Sequencer: A simple 16-step sequencer. Click steps to toggle notes. "
      "Right-click to set velocity/gate time.\n"
      "• Arpeggiator: Hold keys on the virtual keyboard (or incoming MIDI) to "
      "generate patterns. Syncs to global transport.\n"
      "• Chord Gen: Play valid chords based on the selected scale/key. 'Auto "
      "Chord' triggers full chords from single notes.\n"
      "• Mixer: Visualizes activity on 16 MIDI channels. Mute/Solo active "
      "channels. 'Split' mode divides Ch1 into Lower (0-63) and Upper (64-127) "
      "zones.\n"
      "• Playlist: Drag & drop .mid files to queue them. They play in sync "
      "with the transport.\n\n"

      "TROUBLESHOOTING\n"
      "---------------\n"
      "• Devices show 'On' but don't work: The list shows your *saved* "
      "configuration. If a device fails to open (e.g. used by Chrome or "
      "another "
      "app), it may still look checked. Try unchecking and re-checking it, or "
      "disconnect/reconnect the device.\n"
      "• No OSC Connection: Check your firewall (allow PatchworldBridge). "
      "Verify the IP address matches the headset. 'Local IPs' shows your "
      "computer's "
      "addresses.\n"
      "• Audio Glitches: This app handles control data (MIDI/OSC) only. If you "
      "hear audio issues, check your DAW or Patchworld settings.\n"
      "• MIDI Thru Loops: If you get double notes, turn off 'MIDI Thru' in "
      "Config or in your DAW's monitoring settings.\n"
      "• Crash/Freeze: Use Connections > Reset to defaults to clear corrupt "
      "settings. 'Reset Window Layout' fixes UI glitches.\n\n"

      "SHORTCUTS\n"
      "---------\n"
      "Space: Play/Stop\n"
      "F1: Help\n"
      "Ctrl/Cmd+R: Reset Transport\n"
      "Double-click Faders/Knobs: Reset to default value\n");
}

struct HelpDialogContent : juce::Component {
  juce::TextEditor editor;
  HelpDialogContent() {
    addAndMakeVisible(editor);
    editor.setMultiLine(true);
    editor.setReadOnly(true);
    editor.setScrollbarsShown(true);
    editor.setFont(juce::Font(juce::FontOptions(14.0f)));
    editor.setText(getHelpText());
    editor.setColour(juce::TextEditor::backgroundColourId,
                     juce::Colour(0xff1a1a1a));
    editor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    editor.setColour(juce::TextEditor::highlightColourId,
                     juce::Colour(0xff404040));
  }
  void resized() override { editor.setBounds(getLocalBounds()); }
};

void launchHelpWindow(juce::Component *parent) {
  auto content = std::make_unique<HelpDialogContent>();
  content->setSize(580, 520);
  juce::DialogWindow::LaunchOptions opts;
  opts.content.setOwned(content.release());
  opts.dialogTitle = "Help — Patchworld Bridge";
  opts.escapeKeyTriggersCloseButton = true;
  opts.useNativeTitleBar = false;
  opts.resizable = true;
  opts.useBottomRightCornerResizer = true;
  opts.componentToCentreAround = parent;
  auto *dw = opts.launchAsync();
  if (dw)
    dw->setResizeLimits(400, 300, 900, 800);
}
} // namespace

SystemController *SystemController::getLivingInstance() {
  return s_livingController.load(std::memory_order_acquire);
}

void UndoButtonRefresher::changeListenerCallback(juce::ChangeBroadcaster *) {
  if (!controller)
    return;
  juce::MessageManager::callAsync([]() {
    auto *ctrl = SystemController::getLivingInstance();
    if (ctrl)
      ctrl->refreshUndoRedoButtons();
  });
}

void SystemController::refreshUndoRedoButtons() {
  if (!ui)
    return;
  bool canU = context.undoManager.canUndo();
  bool canR = context.undoManager.canRedo();
  ui->btnUndo.setEnabled(canU);
  ui->btnRedo.setEnabled(canR);
  juce::String undoTip = "Undo last edit (Ctrl+Z).";
  if (canU) {
    juce::String desc = context.undoManager.getUndoDescription();
    if (desc.isNotEmpty())
      undoTip = "Undo: " + desc;
  }
  ui->btnUndo.setTooltip(undoTip);
  juce::String redoTip = "Redo (Ctrl+Y).";
  if (canR) {
    juce::String desc = context.undoManager.getRedoDescription();
    if (desc.isNotEmpty())
      redoTip = "Redo: " + desc;
  }
  ui->btnRedo.setTooltip(redoTip);
  ui->btnUndo.setColour(juce::TextButton::buttonColourId,
                        canU ? Theme::accent.darker(0.3f)
                             : Theme::bgPanel.darker(0.2f));
  ui->btnRedo.setColour(juce::TextButton::buttonColourId,
                        canR ? Theme::accent.darker(0.3f)
                             : Theme::bgPanel.darker(0.2f));
  ui->btnUndo.repaint();
  ui->btnRedo.repaint();
}

SystemController::~SystemController() {
  // 0. Revoke living instance so pending callAsync (e.g. UndoButtonRefresher)
  // no-op
  s_livingController.store(nullptr, std::memory_order_release);
  // 1. Stop timers first so no callback runs with dangling this
  TimerHub::instance().unsubscribe("statusBar");
  TimerHub::instance().unsubscribe("uiWatchdog");
  TimerHub::instance().unsubscribe("crashRecovery");
  // 2. Remove UI listeners while mainUI is still valid
  if (ui && lfoPatchClickListener)
    ui->removeMouseListener(lfoPatchClickListener.get());
  ui = nullptr;
  // 3. Unhook undo manager
  context.undoManager.removeChangeListener(&undoButtonRefresher);
}

void SystemController::bindInterface(MainComponent &mainUI) {
  DebugLog::debugLog("bindInterface start");
  s_livingController.store(this, std::memory_order_release);
  ui = &mainUI;
  transportViewModel = std::make_unique<TransportViewModel>(context);

  // Register StatusBar with TimerHub (10Hz for scale debounce, stats throttled
  // internally)
  TimerHub::instance().subscribe(
      "statusBar",
      [this] {
        if (context.windowMinimised_.load(std::memory_order_relaxed))
          return;
        if (ui)
          ui->getStatusBar().tickFromMaster();
      },
      TimerHub::Rate10Hz);

  // NOTE: repaintCoordinator flush is now handled exclusively by handleVBlank()
  // in MainComponent.cpp to avoid duplicate 60Hz work. UIWatchdog is marked
  // alive there as well.

  // UI Watchdog: log if no UI update for 5s (freeze detection)
  TimerHub::instance().subscribe(
      "uiWatchdog", [] { UIWatchdog::check(); }, TimerHub::Low1Hz);

  // Crash recovery: save sentinel periodically; cleared on clean shutdown
  TimerHub::instance().subscribe(
      "crashRecovery", [] { CrashRecovery::saveRecoveryPoint(); },
      TimerHub::Rate0_017Hz);

  DebugLog::debugLog("bindInterface: TimerHub subscribed");
  bindGlobalNavigation(mainUI);
  DebugLog::debugLog("bindGlobalNavigation OK");
  bindHeader(mainUI); // Bind Header first
  DebugLog::debugLog("bindHeader OK");
  bindTransport(mainUI);
  DebugLog::debugLog("bindTransport OK");
  bindSidebar(mainUI);
  DebugLog::debugLog("bindSidebar OK");
  bindConfig(mainUI);

  // Sync MainComponent render mode changes back to ConfigPanel
  mainUI.onRenderModeChangedInternal = [this, &mainUI](int mode) {
    if (ui && ui->configPanel) {
      ui->configPanel->syncRenderModeTo(mode);
      // Update independent state if needed, though AppState is usually source
      // of truth Here we just ensure UI reflects reality if a fallback occurred
    }
  };

  DebugLog::debugLog("bindConfig OK");
  bindMixer(mainUI);
  DebugLog::debugLog("bindMixer OK");
  bindMappingManager(mainUI);
  DebugLog::debugLog("bindMappingManager OK");
  bindPerformance(mainUI);
  DebugLog::debugLog("bindPerformance OK");
  bindControlPage(mainUI);
  DebugLog::debugLog("bindControlPage OK");
  bindOscConfig(mainUI);
  DebugLog::debugLog("bindOscConfig OK");
  bindMacros(mainUI);
  DebugLog::debugLog("bindMacros OK");
  bindChordGenerator(mainUI);
  DebugLog::debugLog("bindChordGenerator OK");
  bindLfoPatching(mainUI);
  DebugLog::debugLog("bindLfoPatching OK");
  bindOscLog(mainUI);
  DebugLog::debugLog("bindOscLog OK");
  bindPlaybackController(mainUI);
  DebugLog::debugLog("bindPlaybackController OK");
  bindShortcuts(mainUI);
  DebugLog::debugLog("bindInterface done");
}

void SystemController::bindGlobalNavigation(MainComponent &mainUI) {
  // Single nav button: toggle between Dashboard and Config (button label set in
  // setView)
  mainUI.btnDash.onClick = [this] {
    if (!ui)
      return;
    auto v = ui->getCurrentView();
    if (v == MainComponent::AppView::Dashboard)
      ui->setView(MainComponent::AppView::OSC_Config);
    else
      ui->setView(MainComponent::AppView::Dashboard);
  };

  // Panic
  mainUI.btnPanic.onClick = [this] {
    if (context.midiRouter)
      context.midiRouter->sendPanic();
  };

  // MIDI Learn
  mainUI.btnMidiLearn.onClick = [this] {
    if (!ui)
      return;
    ui->isMidiLearnMode = ui->btnMidiLearn.getToggleState();
    if (context.mappingManager)
      context.mappingManager->setLearnModeActive(ui->isMidiLearnMode);
    ui->toggleMidiLearnOverlay(ui->isMidiLearnMode);
    ui->btnMidiLearn.setButtonText(ui->isMidiLearnMode ? "LEARNING..."
                                                       : "MIDI Learn");
  };

  // THRU and EXT at top bar (left of PANIC): sync with AppState and transport
  mainUI.btnThru.setToggleState(context.appState.getMidiThru(),
                                juce::dontSendNotification);
  // Startup Sync: ensure router has atomic set
  if (context.midiRouter)
    context.midiRouter->setMidiThru(context.appState.getMidiThru());

  mainUI.btnThru.onClick = [this] {
    if (!ui)
      return;
    bool on = ui->btnThru.getToggleState();
    context.appState.setMidiThru(on);
    if (context.deviceService)
      context.deviceService->setThruEnabled(on);
    if (context.midiRouter)
      context.midiRouter->setMidiThru(on);
    if (ui->transportPanel) // ...
      if (ui->transportPanel)
        ui->transportPanel->repaint();
    if (context.engine && ui->configPanel) {
      bool clockOn = ui->configPanel->btnClock.getToggleState() || on;
      context.engine->sendMidiClock = clockOn;
    }
  };

  bool extSync = context.engine && context.engine->isExtSyncActive();
  mainUI.btnExtSyncMenu.setToggleState(extSync, juce::dontSendNotification);
  mainUI.btnExtSyncMenu.onClick = [this] {
    if (!ui)
      return;
    bool on = ui->btnExtSyncMenu.getToggleState();
    if (context.engine)
      context.engine->setExtSyncActive(on);
    if (context.sequencer)
      context.sequencer->setExtSyncActive(on);
    if (ui->transportPanel)
      ui->transportPanel->repaint();
  };
}

void SystemController::bindHeader(MainComponent &mainUI) {
  if (context.oscManager) {
    context.oscManager->onLog = [this](const juce::String &msg, bool err) {
      if (ui)
        ui->onLogMessage(msg, err);
    };
  }

  // OSC/KBD/SEQ indicator pulse callbacks (now in log window)
  if (context.midiRouter && mainUI.logPanel) {
    context.midiRouter->onNetworkActivity = [this] {
      if (context.networkWorker)
        context.networkWorker->workSignal.signal();
      if (ui && ui->logPanel)
        ui->logPanel->signalLegend.pulse(SignalPathLegend::NET);
    };
    context.midiRouter->onMidiInputActivity = [this] {
      if (ui && ui->logPanel)
        ui->logPanel->signalLegend.pulse(SignalPathLegend::UI);
    };
  }

  // Network config panel (shown from Menu > Network)
  if (auto *netPanel = mainUI.networkConfigPanel.get()) {
    netPanel->edIp.setText(context.appState.getIp(),
                           juce::dontSendNotification);
    netPanel->edPortOut.setText(juce::String(context.appState.getPortOut()),
                                juce::dontSendNotification);
    netPanel->edPortIn.setText(juce::String(context.appState.getPortIn()),
                               juce::dontSendNotification);
    netPanel->btnConnect.onClick = [this, netPanel] {
      bool connect = netPanel->btnConnect.getToggleState();
      if (connect) {
        juce::String ip = netPanel->edIp.getText();
        int pOut = netPanel->edPortOut.getText().getIntValue();
        int pIn = netPanel->edPortIn.getText().getIntValue();
        if (context.oscManager) {
          bool ok = context.oscManager->connect(ip, pOut, pIn);
          context.appState.setIp(ip);
          context.appState.setPortOut(pOut);
          context.appState.setPortIn(pIn);
          if (ui) {
            if (ok)
              ui->onLogMessage(
                  "OSC connected: " + ip + ":" + juce::String(pOut), false);
            else {
              ui->onLogMessage("Could not connect to OSC. Check IP and ports. "
                               "Click Connect to retry.",
                               true);
              netPanel->btnConnect.setToggleState(false,
                                                  juce::dontSendNotification);
            }
          }
        }
      } else {
        if (context.oscManager)
          context.oscManager->disconnect();
        if (ui)
          ui->onLogMessage("OSC disconnected", false);
      }
    };
  }

  // Menu dropdown: Network, MIDI Inputs, MIDI Outputs, Modules, Help
  mainUI.onMenuClicked = [this](juce::Component *target) {
    if (!ui)
      return;
    juce::PopupMenu m;
    m.addSectionHeader("Connections");
    m.addItem("Network...", [this, target] {
      if (!ui)
        return;
      auto panel = std::make_unique<NetworkConfigPanel>();
      panel->setSize(560, 110);
      panel->edIp.setText(context.appState.getIp(), juce::dontSendNotification);
      panel->edPortOut.setText(juce::String(context.appState.getPortOut()),
                               juce::dontSendNotification);
      panel->edPortIn.setText(juce::String(context.appState.getPortIn()),
                              juce::dontSendNotification);
      panel->btnConnect.setToggleState(context.oscManager &&
                                           context.oscManager->isConnected(),
                                       juce::dontSendNotification);
      auto *panelPtr = panel.get();
      panel->btnConnect.onClick = [this, panelPtr] {
        bool connect = panelPtr->btnConnect.getToggleState();
        if (connect) {
          juce::String ip = panelPtr->edIp.getText();
          int pOut = panelPtr->edPortOut.getText().getIntValue();
          int pIn = panelPtr->edPortIn.getText().getIntValue();
          if (context.oscManager) {
            bool ok = context.oscManager->connect(ip, pOut, pIn);
            context.appState.setIp(ip);
            context.appState.setPortOut(pOut);
            context.appState.setPortIn(pIn);
            if (ui) {
              if (ok)
                ui->onLogMessage(
                    "OSC connected: " + ip + ":" + juce::String(pOut), false);
              else {
                ui->onLogMessage("Could not connect to OSC. Check IP and "
                                 "ports. Click Connect to retry.",
                                 true);
                panelPtr->btnConnect.setToggleState(false,
                                                    juce::dontSendNotification);
              }
            }
          }
        } else {
          if (context.oscManager)
            context.oscManager->disconnect();
          if (ui)
            ui->onLogMessage("OSC disconnected", false);
        }
      };
      juce::DialogWindow::LaunchOptions opts;
      opts.content.setOwned(panel.release());
      opts.dialogTitle = "Network";
      opts.escapeKeyTriggersCloseButton = true;
      opts.useNativeTitleBar = false;
      opts.resizable = false;
      opts.componentToCentreAround = ui;
      opts.content->setSize(560, 110);
      opts.launchAsync();
    });
    m.addItem("MIDI Inputs...", [this, target] {
      if (!ui)
        return;
      auto panel = MenuBuilder::createMidiInputPanel(context);
      panel->setSize(320, 380);
      juce::DialogWindow::LaunchOptions opts;
      opts.content.setOwned(panel.release());
      opts.dialogTitle = "MIDI Inputs";
      opts.escapeKeyTriggersCloseButton = true;
      opts.useNativeTitleBar = false;
      opts.resizable = true;
      opts.componentToCentreAround = ui;
      auto *dialog = opts.launchAsync();
      if (dialog)
        dialog->setResizeLimits(280, 320, 500, 600);
    });
    m.addItem("MIDI Outputs...", [this, target] {
      if (!ui)
        return;
      auto panel = MenuBuilder::createMidiOutputPanel(context);
      panel->setSize(320, 380);
      juce::DialogWindow::LaunchOptions opts;
      opts.content.setOwned(panel.release());
      opts.dialogTitle = "MIDI Outputs";
      opts.escapeKeyTriggersCloseButton = true;
      opts.useNativeTitleBar = false;
      opts.resizable = true;
      opts.componentToCentreAround = ui;
      auto *dialog = opts.launchAsync();
      if (dialog)
        dialog->setResizeLimits(280, 320, 500, 600);
    });
    m.addItem("OSC Addresses...", [this] {
      if (!ui)
        return;
      auto content = std::make_unique<OscAddressDialogContent>();
      content->onLoadSchema = [this] {
        return context.appState.loadOscSchema();
      };
      content->onApplySchema = [this](const OscNamingSchema &schema) {
        if (context.oscManager)
          context.oscManager->updateSchema(schema);
        if (context.networkWorker)
          context.networkWorker->setSchema(schema);
        if (context.oscSchema)
          *context.oscSchema = schema;
        context.appState.saveOscSchema(schema);
        if (ui)
          ui->onLogMessage("OSC Schema Updated.", false);
      };
      content->refresh();
      juce::DialogWindow::LaunchOptions opts;
      opts.content.setOwned(content.release());
      opts.dialogTitle = "OSC Addresses";
      opts.escapeKeyTriggersCloseButton = true;
      opts.useNativeTitleBar = false;
      opts.resizable = true;
      opts.useBottomRightCornerResizer = true;
      opts.componentToCentreAround = ui;
      int width = 500;
      int height = 680;
      opts.content->setSize(width, height);
      auto *dialog = opts.launchAsync();
      if (dialog)
        dialog->setResizeLimits(400, 400, 1200, 1000);
    });
    m.addItem("Bluetooth MIDI / Gamepad...", [this] {
      if (!ui)
        return;
      if (juce::BluetoothMidiDevicePairingDialogue::isAvailable()) {
        juce::BluetoothMidiDevicePairingDialogue::open();
        if (ui->configPanel)
          ui->configPanel->setBluetoothMidiStatus(
              "Select a device in the pairing dialogue. After pairing, click "
              "Scan or MIDI In to see it.");
        if (ui)
          ui->onLogMessage("Bluetooth MIDI pairing opened. After pairing, "
                           "click Scan or MIDI In to refresh.",
                           false);
      } else {
#if JUCE_WINDOWS
        juce::Process::openDocument("ms-settings:bluetooth", "");
        if (ui->configPanel)
          ui->configPanel->setBluetoothMidiStatus(
              "Pair your BT MIDI device in the opened window. Then click Scan "
              "or MIDI In. Gamepads: Config > Enable Gamepad Input.");
        if (ui)
          ui->onLogMessage(
              "After pairing in Bluetooth settings, click Scan or MIDI In to "
              "refresh. For Xbox/PS use Config > Enable Gamepad Input.",
              false);
#else
        if (ui->configPanel)
          ui->configPanel->setBluetoothMidiStatus("Use OS Bluetooth settings to pair. Then click Scan or MIDI In. Gamepads: Config > Enable Gamepad Input.");
        if (ui) ui->onLogMessage("Bluetooth MIDI: use OS settings to pair; then Scan or MIDI In. Gamepads: Config > Extended Input Devices.", false);
#endif
      }
    });
    m.addSeparator();
    juce::PopupMenu layoutMenu;
    layoutMenu.addItem("Load Minimal (Editor, OSC Log, Playlist)", [this] {
      if (ui)
        applyLayoutPreset("Minimal");
    });
    layoutMenu.addItem("Load Full (3×3 grid, all modules)", [this] {
      if (ui)
        applyLayoutPreset("Full");
    });
    layoutMenu.addSeparator();
    layoutMenu.addItem("Reset to default layout",
                       [this] { resetWindowLayout(); });
    m.addSubMenu("Layout", layoutMenu, true);
    m.addItem("Modules...", [this, target] {
      if (!ui)
        return;
      auto panel = MenuBuilder::createModulesTogglePanel(
          ui->winEditor.get(), ui->winMixer.get(), ui->winSequencer.get(),
          ui->winPlaylist.get(), ui->winArp.get(), ui->winMacros.get(),
          ui->winLog.get(), ui->winChords.get(), ui->winControl.get(),
          ui->winLfoGen.get());
      panel->onModuleVisibilityChanged = [this](ModuleWindow *w) {
        if (ui && w) {
          context.repaintCoordinator.markDirty(RepaintCoordinator::Dashboard);
          w->repaint();
        }
      };
      panel->setSize(240, 420);
      juce::DialogWindow::LaunchOptions opts;
      opts.content.setOwned(panel.release());
      opts.dialogTitle = "Modules";
      opts.escapeKeyTriggersCloseButton = true;
      opts.useNativeTitleBar = false;
      opts.resizable = false;
      opts.componentToCentreAround = ui;
      opts.content->setSize(240, 420);
      opts.launchAsync();
    });
    m.addItem("Reset to defaults", [this, target] {
      juce::NativeMessageBox::showOkCancelBox(
          juce::MessageBoxIconType::WarningIcon, "Reset to defaults",
          "Restore all settings to factory defaults and reset layout. "
          "Continue?",
          target, juce::ModalCallbackFunction::create([this](int result) {
            if (result == 1) {
              context.appState.resetToDefaults();
              if (context.mappingManager)
                context.mappingManager->resetMappings();
              if (context.deviceService)
                context.deviceService->loadConfig(context.midiRouter.get());
              resetWindowLayout();
              if (ui) {
                // Apply reset defaults: Software render mode and GPU backend
                // (set in AppState.resetToDefaults)
                int mode = context.appState.getRenderMode();
                if (mode >= 1 && mode <= 4) {
                  if (ui->configPanel) {
                    ui->configPanel->syncRenderModeTo(mode);
                    ui->configPanel->syncGpuBackendTo(
                        context.appState.getGpuBackend());
                  }
                  ui->handleRenderModeChange(mode);
                }
                if (ui->getMidiLearnOverlay())
                  ui->getMidiLearnOverlay()->refreshMappingList();
                ui->onLogMessage(
                    "Settings reset to defaults (mappings cleared).", false);
              }
            }
          }));
    });
    m.addItem("Keyboard shortcuts (F1)", [this, target] {
      if (!ui)
        return;
      auto panel = std::make_unique<ShortcutsPanel>();
      juce::DialogWindow::LaunchOptions opts;
      opts.content.setOwned(panel.release());
      opts.dialogTitle = "Keyboard Shortcuts";
      opts.escapeKeyTriggersCloseButton = true;
      opts.useNativeTitleBar = false;
      opts.resizable = true;
      opts.componentToCentreAround = ui;
      opts.content->setSize(400, 420);
      opts.launchAsync();
    });
    m.addSeparator();
    m.addItem("About", [this, target] {
      juce::String msg = juce::String(ProjectInfo::projectName) + " v" +
                         juce::String(ProjectInfo::versionString) +
                         "\n\nBuilt with JUCE and Ableton Link.";
      auto caps = RenderBackend::detectCapabilities();
      RenderBackend::Type backendType =
          context.appState.getGpuBackend().isEmpty()
              ? RenderBackend::getCurrentBackend()
              : backendNameToType(context.appState.getGpuBackend());
      msg += "\n\nRendering (current): " +
             RenderBackend::getBackendName(backendType);
      msg += "\n\nGPU / graphics:";
      if (caps.supportsOpenGL)
        msg += "\n  OpenGL: supported";
      if (caps.supportsVulkan)
        msg += "\n  Vulkan: " + caps.vulkanVersion;
      if (caps.supportsMetal)
        msg += "\n  Metal: " + caps.metalVersion;
      juce::StringArray backends = RenderBackend::getAvailableBackends();
      msg += "\n  Available backends: " + backends.joinIntoString(", ");
      msg += "\n\nConfig > App/General: Render mode (Eco/Pro/Software), GPU "
             "backend.";
      juce::NativeMessageBox::showMessageBoxAsync(
          juce::MessageBoxIconType::InfoIcon, "About", msg, target);
    });
    m.addItem("Help", [this] {
      if (ui) {
        launchHelpWindow(ui);
        ui->setView(MainComponent::AppView::Control);
      }
    });
    juce::Rectangle<int> menuAnchor = target->getScreenBounds();
    menuAnchor.setLeft(menuAnchor.getRight());
    menuAnchor.setWidth(1);
    auto opts =
        juce::PopupMenu::Options()
            .withTargetComponent(target)
            .withParentComponent(ui)
            .withTargetScreenArea(menuAnchor)
            .withStandardItemHeight(PopupMenuOptions::kStandardItemHeight);
    m.showMenuAsync(opts);
  };

  // Modules button: stay-open panel so user can select multiple before closing
  if (auto *h = mainUI.headerPanel.get()) {
    h->btnModules.onClick = [this, h] {
      if (!ui)
        return;
      auto panel = MenuBuilder::createModulesTogglePanel(
          ui->winEditor.get(), ui->winMixer.get(), ui->winSequencer.get(),
          ui->winPlaylist.get(), ui->winArp.get(), ui->winMacros.get(),
          ui->winLog.get(), ui->winChords.get(), ui->winControl.get(),
          ui->winLfoGen.get());
      panel->onModuleVisibilityChanged = [this](ModuleWindow *w) {
        if (ui && w) {
          context.repaintCoordinator.markDirty(RepaintCoordinator::Dashboard);
          w->repaint();
        }
      };
      panel->setSize(240, 420);
      juce::DialogWindow::LaunchOptions opts;
      opts.content.setOwned(panel.release());
      opts.dialogTitle = "Modules";
      opts.escapeKeyTriggersCloseButton = true;
      opts.useNativeTitleBar = false;
      opts.resizable = false;
      opts.componentToCentreAround = ui;
      opts.content->setSize(240, 420);
      opts.launchAsync();
    };
  }

  // Module visibility shortcuts (Show all / Hide all)
  ShortcutManager::instance().setAction("view.showAllModules", [this] {
    if (ui)
      ui->showAllModules();
  });
  ShortcutManager::instance().setAction("view.hideAllModules", [this] {
    if (ui)
      ui->hideAllModules();
  });
}

void SystemController::bindTransport(MainComponent &mainUI) {
  if (auto *t = mainUI.transportPanel.get()) {
    t->btnPlay.onClick = [this, t] {
      if (!context.engine || !context.playbackController)
        return;
      bool willBePlaying = false;
      if (context.engine->getIsPlaying()) {
        context.playbackController->pausePlayback();
        willBePlaying = false;
      } else if (context.engine->getIsPaused()) {
        context.playbackController->resumePlayback();
        willBePlaying = true;
      } else {
        transportViewModel->play();
        willBePlaying = true;
      }
      context.repaintCoordinator.markDirty(RepaintCoordinator::Dashboard);
      juce::String pid = "Transport_Play";
      float v = willBePlaying ? 1.0f : 0.0f;
      if (context.mappingManager)
        context.mappingManager->setParameterValue(pid, v);
      if (context.midiRouter) {
        auto ov = context.appState.getControlMessageOverride(pid);
        if (ov.type != 0 && ov.channel >= 1) {
          int ch = ov.channel;
          if (ov.type == 1)
            context.midiRouter->handleCC(ch, ov.noteOrCC, v,
                                         BridgeEvent::Source::UserInterface);
          else if (ov.type == 2) {
            if (willBePlaying)
              context.midiRouter->handleNoteOn(
                  ch, ov.noteOrCC, 1.0f, false, false,
                  BridgeEvent::Source::UserInterface);
            else
              context.midiRouter->handleNoteOff(
                  ch, ov.noteOrCC, 0.0f, false, false,
                  BridgeEvent::Source::UserInterface);
          }
        }
      }
    };

    t->btnStop.onClick = [this] {
      transportViewModel->stop();
      context.repaintCoordinator.markDirty(RepaintCoordinator::Dashboard);
      juce::String pid = "Transport_Stop";
      if (context.mappingManager)
        context.mappingManager->setParameterValue(pid, 1.0f);
      if (context.midiRouter) {
        auto ov = context.appState.getControlMessageOverride(pid);
        if (ov.type != 0 && ov.channel >= 1) {
          int ch = ov.channel;
          if (ov.type == 1)
            context.midiRouter->handleCC(ch, ov.noteOrCC, 1.0f,
                                         BridgeEvent::Source::UserInterface);
          else if (ov.type == 2)
            context.midiRouter->handleNoteOn(
                ch, ov.noteOrCC, 1.0f, false, false,
                BridgeEvent::Source::UserInterface);
        }
      }
    };

    t->btnPrev.onClick = [this] {
      if (context.playbackController)
        context.playbackController->skipToPrevious();
    };

    t->btnSkip.onClick = [this] {
      if (context.playbackController)
        context.playbackController->skipToNext();
    };

    t->btnReset.onClick = [this] {
      if (context.playbackController)
        context.playbackController->clearTrackAndGrids();
      if (context.engine)
        context.engine->stop();
      if (context.midiScheduler)
        context.midiScheduler->allNotesOff();
    };

    mainUI.btnUndo.onClick = [this] {
      if (context.undoManager.canUndo()) {
        context.undoManager.undo();
        refreshUndoRedoButtons();
      }
    };
    mainUI.btnRedo.onClick = [this] {
      if (context.undoManager.canRedo()) {
        context.undoManager.redo();
        refreshUndoRedoButtons();
      }
    };

    // Immediate Undo/Redo button refresh when stack changes
    undoButtonRefresher.controller = this;
    context.undoManager.addChangeListener(&undoButtonRefresher);

    auto resetBpmAction = [this] {
      double targetBpm = Constants::kDefaultBpm;
      if (context.playbackController &&
          context.playbackController->hasLoadedFile())
        targetBpm = context.playbackController->getLoadedFileBpm();
      if (context.engine)
        context.engine->setBpm(targetBpm);
      if (ui)
        ui->tempoSlider.setValue(targetBpm, juce::dontSendNotification);
    };
    t->btnResetBpm.onClick = resetBpmAction;
    mainUI.btnResetBpm.onClick = resetBpmAction;

    t->btnOctaveMinus.onClick = [this] {
      if (ui && ui->performancePanel && ui->performancePanel->onOctaveShift)
        ui->performancePanel->onOctaveShift(-1);
    };
    t->btnOctavePlus.onClick = [this] {
      if (ui && ui->performancePanel && ui->performancePanel->onOctaveShift)
        ui->performancePanel->onOctaveShift(1);
    };

    mainUI.tempoSlider.onValueChange = [this] {
      double bpm = (ui ? ui->tempoSlider.getValue() : 120.0);
      if (context.engine)
        context.engine->setBpm(bpm);
      if (context.oscManager)
        context.oscManager->sendFloat("/clock/bpm", (float)bpm);
      juce::String pid = "Transport_BPM";
      float norm = (float)((bpm - 20.0) / 280.0);
      if (context.mappingManager)
        context.mappingManager->setParameterValue(pid, norm);
      if (context.midiRouter) {
        auto ov = context.appState.getControlMessageOverride(pid);
        if (ov.type != 0 && ov.channel >= 1) {
          int ch = ov.channel;
          if (ov.type == 1)
            context.midiRouter->handleCC(ch, ov.noteOrCC, norm,
                                         BridgeEvent::Source::UserInterface);
          else if (ov.type == 3)
            context.midiRouter->handleBridgeEvent(BridgeEvent(
                EventType::PitchBend, EventSource::UserInterface, ch, 0, norm));
        }
      }
      if (ui)
        ui->onLogMessage("BPM: " + juce::String((int)bpm), false);
    };

    mainUI.btnTap.onClick = [this] {
      if (context.engine)
        context.engine->tapTempo();
    };

    t->btnNudgeMinus.onClick = [this] {
      if (context.engine)
        context.engine->nudge(-0.05);
    };
    t->btnNudgePlus.onClick = [this] {
      if (context.engine)
        context.engine->nudge(0.05);
    };

    t->btnQuantize.onClick = [this, t] {
      bool enabled = t->btnQuantize.getToggleState();
      if (context.midiRouter)
        context.midiRouter->isQuantizationEnabled = enabled;
    };

    t->btnBlock.onClick = [this, t] {
      if (context.midiRouter) {
        context.midiRouter->setBlockMidiOut(t->btnBlock.getToggleState());
        if (ui && ui->configPanel)
          ui->configPanel->btnBlockMidiOut.setToggleState(
              t->btnBlock.getToggleState(), juce::dontSendNotification);
      }
    };
    t->btnSplit.onClick = [this, t] {
      if (context.midiRouter) {
        context.midiRouter->setSplitMode(t->btnSplit.getToggleState());
        if (ui && ui->configPanel)
          ui->configPanel->btnSplit.setToggleState(t->btnSplit.getToggleState(),
                                                   juce::dontSendNotification);
      }
    };
    t->btnSnapshot.onClick = [this] {
      if (context.mappingManager) {
        auto obj = new juce::DynamicObject();
        context.mappingManager->saveMappingsToJSON(obj);
        juce::String snapshot = juce::JSON::toString(juce::var(obj));
        juce::SystemClipboard::copyTextToClipboard(snapshot);
        if (ui)
          ui->onLogMessage("Mapping snapshot copied to clipboard.", false);
      }
    };
  }
}

void SystemController::bindSidebar(MainComponent &mainUI) {
  // Playlist (now standalone, no longer inside SidebarPanel)
  if (auto *pl = mainUI.playlist.get()) {
    pl->loadPlaylist(); // Restore saved playlist on startup
    pl->onFileSelected = [this](const juce::String &path) {
      juce::File f(path);
      if (f.existsAsFile() && context.playbackController) {
        context.playbackController->loadMidiFile(f);
      }
    };
    pl->onRecentRequest = [this](juce::Component *target) {
      juce::StringArray recent = context.appState.getRecentMidiFiles();
      if (recent.isEmpty()) {
        if (ui)
          ui->onLogMessage("No recent .mid files.", false);
        return;
      }
      juce::PopupMenu m;
      for (int i = 0; i < recent.size(); ++i) {
        juce::File f(recent[i]);
        m.addItem(f.getFileName(), [this, path = recent[i]] {
          juce::File file(path);
          if (file.existsAsFile() && context.playbackController)
            context.playbackController->loadMidiFile(file);
        });
      }
      m.showMenuAsync(juce::PopupMenu::Options()
                          .withTargetComponent(target)
                          .withParentComponent(nullptr));
    };
  }

  // Arp Controls (now standalone)
  if (auto *arp = mainUI.arpPanel.get()) {
    arp->onArpOnChanged = [this, arp](bool on) {
      if (context.midiRouter)
        context.midiRouter->setArpEnabled(on);
    };

    arp->onArpUpdate = [this](int spd, int vel, int pat, int oct, float gate) {
      if (context.midiRouter)
        context.midiRouter->updateArpSettings(spd, vel, pat, oct, gate);
    };

    arp->btnArpLatch.onClick = [this, arp] {
      if (context.midiRouter)
        context.midiRouter->setArpLatch(arp->btnArpLatch.getToggleState());
    };

    arp->onBpmBlockChanged = [this](bool blocked) {
      if (context.engine)
        context.engine->blockBpmChanges = blocked;
    };

    arp->btnArpSync.onClick = [this, arp] {
      if (context.midiRouter)
        context.midiRouter->setArpSyncEnabled(arp->btnArpSync.getToggleState());
    };

    if (context.midiRouter) {
      context.midiRouter->setArpEnabled(arp->btnArpOn.getToggleState());
      context.midiRouter->setArpLatch(arp->btnArpLatch.getToggleState());
      context.midiRouter->updateArpSettings(
          (int)arp->knobArpSpeed.getValue(), (int)arp->knobArpVel.getValue(),
          arp->cmbArpPattern.getSelectedId(),
          (int)arp->sliderArpOctave.getValue(),
          (float)arp->knobArpGate.getValue());
    }
  }
}

void SystemController::bindConfig(MainComponent &mainUI) {
  if (auto *c = mainUI.configPanel.get()) {
    // Theme Switching (applies to main + mixer and any other custom LaFs)
    c->onThemeChanged = [this](int id) {
      context.configManager.set("themeId", id);
      if (ui) {
        ui->applyThemeToAllLookAndFeels(id);
        ui->repaint();
      }
      if (ui)
        ui->onLogMessage("Theme changed to ID " + juce::String(id), false);
    };

    // Config Panel Connect (use IP/ports from config panel and connect OSC)
    c->btnConnect.setClickingTogglesState(true);
    c->btnConnect.setToggleState(context.oscManager &&
                                     context.oscManager->isConnected(),
                                 juce::dontSendNotification);
    c->btnConnect.onClick = [this, c] {
      bool connect = c->btnConnect.getToggleState();
      if (connect) {
        juce::String ip = c->edIp.getText().trim();
        int pOut = c->edPOut.getText().getIntValue();
        int pIn = c->edPIn.getText().getIntValue();
        if (pOut <= 0)
          pOut = 7000;
        if (pIn <= 0)
          pIn = 9000;
        if (context.oscManager) {
          bool ok = context.oscManager->connect(ip, pOut, pIn);
          context.appState.setIp(ip);
          context.appState.setPortOut(pOut);
          context.appState.setPortIn(pIn);
          if (ui) {
            if (ok)
              ui->onLogMessage(
                  "OSC connected: " + ip + ":" + juce::String(pOut), false);
            else {
              ui->onLogMessage("Could not connect to OSC. Check IP and ports. "
                               "Click Connect to retry.",
                               true);
              c->btnConnect.setToggleState(false, juce::dontSendNotification);
            }
          }
        }
      } else {
        if (context.oscManager)
          context.oscManager->disconnect();
        if (ui)
          ui->onLogMessage("OSC disconnected", false);
      }
    };

    // MIDI hardware toggles (via MidiHardwareController)
    c->onInputToggle = [this](juce::String id) {
      if (context.midiHardwareController)
        context.midiHardwareController->setInputEnabled(
            id, !context.midiHardwareController->isInputEnabled(id),
            context.midiRouter.get());
    };

    c->onOutputToggle = [this](juce::String id) {
      if (context.midiHardwareController)
        context.midiHardwareController->setOutputEnabled(
            id, !context.midiHardwareController->isOutputEnabled(id));
    };

    c->btnTestMidi.onClick = [this] {
      if (context.midiRouter) {
        context.midiRouter->handleNoteOn(1, 60, 1.0f, false, false,
                                         BridgeEvent::Source::UserInterface);
        if (ui)
          ui->onLogMessage(
              "MIDI Test: note sent (Ch1, C4). Check your MIDI output.", false);
        juce::Timer::callAfterDelay(200, [this] {
          if (context.midiRouter)
            context.midiRouter->handleNoteOff(
                1, 60, 0.0f, false, false, BridgeEvent::Source::UserInterface);
        });
      }
    };

    // Multicast / Broadcast
    c->onMulticastToggle = [this](bool enable) {
      context.appState.props->setValue("multicast", enable);
      juce::String targetIP =
          enable ? "255.255.255.255" : context.appState.getIp();
      if (context.oscManager) {
        bool ok =
            context.oscManager->connect(targetIP, context.appState.getPortOut(),
                                        context.appState.getPortIn());
        if (ui) {
          if (ok)
            ui->onLogMessage(enable ? "Switched to Broadcast"
                                    : "Switched to Direct IP",
                             false);
          else
            ui->onLogMessage("Could not connect to OSC. Check port settings. "
                             "Click Connect to retry.",
                             true);
        }
      }
    };

    // ZeroConfig (Discovery beacon)
    c->onZeroConfigToggle = [this](bool enable) {
      if (context.oscManager)
        context.oscManager->setZeroConfig(enable);
      context.configManager.set("zeroconf", enable);
    };

    // IPv6 (reconnect required)
    c->btnIPv6.onClick = [this, c] {
      bool useV6 = c->btnIPv6.getToggleState();
      context.appState.setUseIPv6(useV6);
      if (context.oscManager) {
        bool ok = context.oscManager->connect(
            context.appState.getIp(), context.appState.getPortOut(),
            context.appState.getPortIn(), useV6);
        if (ui) {
          if (ok)
            ui->onLogMessage(useV6 ? "IPv6 enabled" : "IPv6 disabled", false);
          else
            ui->onLogMessage("Could not reconnect to OSC after IPv6 change. "
                             "Check IP and port. Click Connect to retry.",
                             true);
        }
      }
    };

    // GPU backend first so handleRenderModeChange sees correct backend (OpenGL
    // vs Vulkan)
    juce::StringArray backends = RenderBackend::getAvailableBackends();
    juce::String savedBackend = context.appState.getGpuBackend();
    int backendIdx = backends.indexOf(savedBackend);
    if (backendIdx >= 0)
      c->cmbGpuBackend.setSelectedId(backendIdx + 1,
                                     juce::dontSendNotification);
    RenderBackend::setCurrentBackend(backendNameToType(savedBackend));

    // Render Mode (Eco / Pro / Software / Auto)
    int savedRenderMode = context.appState.getRenderMode();
    if (savedRenderMode >= 1 && savedRenderMode <= 4) {
      c->syncRenderModeTo(savedRenderMode);
      mainUI.handleRenderModeChange(savedRenderMode);
    }
    c->onRenderModeChanged = [this](int mode) {
      context.appState.setRenderMode(mode);
      if (ui)
        ui->handleRenderModeChange(mode);
    };
    c->onGpuBackendChanged = [this](const juce::String &name) {
      auto type = backendNameToType(name);
      RenderBackend::setCurrentBackend(type);
      context.appState.setGpuBackend(name);
      if (ui && !RenderBackend::isBackendImplemented(type))
        ui->onLogMessage("GPU backend \"" + name +
                             "\" preferred; using OpenGL for now.",
                         false);
      int mode = context.appState.getRenderMode();
      if (ui && mode >= 1 && mode <= 4)
        ui->handleRenderModeChange(mode);
    };

    // Thru (sync with TransportPanel) - THRU sends MIDI clock (forwarded or app
    // BPM)
    c->btnThru.onClick = [this, c] {
      if (context.deviceService) {
        bool on = c->btnThru.getToggleState();
        context.deviceService->setThruEnabled(on);
        if (context.midiRouter)
          context.midiRouter->setMidiThru(on);
        // THRU enables MIDI clock: forward incoming clock, or send app BPM when
        // none
        if (context.engine) {
          bool clockOn =
              c->btnClock.getToggleState() || c->btnThru.getToggleState();
          context.engine->sendMidiClock = clockOn;
        }
      }
    };

    // Split Mode
    c->onSplitToggle = [this](bool enabled) {
      if (context.midiRouter) {
        context.midiRouter->setSplitMode(enabled);
        if (ui && ui->transportPanel)
          ui->transportPanel->btnSplit.setToggleState(
              enabled, juce::dontSendNotification);
      }
    };

    // MIDI Clock (also enabled by THRU - engine sends app BPM when no external
    // clock)
    c->btnClock.onClick = [this, c] {
      if (context.engine) {
        bool clockOn =
            c->btnClock.getToggleState() || context.appState.getMidiThru();
        context.engine->sendMidiClock = clockOn;
      }
    };

    // Block MIDI Out (sync with TransportPanel btnBlock)
    c->btnBlockMidiOut.onClick = [this, c] {
      if (context.midiRouter) {
        context.midiRouter->setBlockMidiOut(
            c->btnBlockMidiOut.getToggleState());
        if (ui && ui->transportPanel)
          ui->transportPanel->btnBlock.setToggleState(
              c->btnBlockMidiOut.getToggleState(), juce::dontSendNotification);
      }
    };

    // MIDI Channel Select (1-16, All=17 for routing)
    int savedCh = context.appState.getMidiOutChannel();
    c->cmbMidiCh.setSelectedId((savedCh >= 1 && savedCh <= 17) ? savedCh : 1,
                               juce::dontSendNotification);
    if (context.midiRouter && savedCh >= 1 && savedCh <= 16)
      context.midiRouter->selectedChannel = savedCh;
    if (context.engine)
      context.engine->setSequencerChannel(
          0, (savedCh >= 1 && savedCh <= 16) ? savedCh : 1);
    c->cmbMidiCh.onChange = [this, c] {
      int id = c->cmbMidiCh.getSelectedId();
      int ch = (id >= 1 && id <= 16) ? id : 1;
      context.appState.setMidiOutChannel(id);
      if (context.midiRouter)
        context.midiRouter->selectedChannel = ch;
      if (context.engine)
        context.engine->setSequencerChannel(0, ch);
    };

    // MIDI Scaling
    c->btnMidiScaling.onClick = [this, c] {
      if (context.midiRouter)
        context.midiRouter->setMidiScaling127(
            c->btnMidiScaling.getToggleState());
      if (context.oscManager)
        context.oscManager->setScalingMode(c->btnMidiScaling.getToggleState());
    };

    // Link Enable (Config panel + top bar button)
    bool linkPref = context.appState.getLinkPref();
    c->btnLinkEnable.setToggleState(linkPref, juce::dontSendNotification);
    mainUI.btnLink.setToggleState(linkPref, juce::dontSendNotification);
    if (context.engine)
      context.engine->setLinkEnabled(linkPref);
    auto syncLinkState = [this, c](bool enabled) {
      if (context.engine)
        context.engine->setLinkEnabled(enabled);
      context.appState.setLinkPref(enabled);
      if (c)
        c->btnLinkEnable.setToggleState(enabled, juce::dontSendNotification);
      if (ui)
        ui->btnLink.setToggleState(enabled, juce::dontSendNotification);
      if (c)
        c->updateGroups();
    };
    c->btnLinkEnable.onClick = [this, c, syncLinkState] {
      syncLinkState(c->btnLinkEnable.getToggleState());
    };
    mainUI.btnLink.onClick = [this, syncLinkState] {
      syncLinkState(ui ? ui->btnLink.getToggleState() : false);
    };

    // Quantum
    c->cmbQuantum.onChange = [this, c] {
      int id = c->cmbQuantum.getSelectedId();
      double q = (id == 1) ? 1.0 : (id == 2) ? 2.0 : (id == 4) ? 8.0 : 4.0;
      if (context.engine)
        context.engine->setQuantum(q);
    };

    // LFO
    c->onLfoChanged = [this](float freq, float depth, int waveform) {
      if (context.engine) {
        context.engine->setLfoFrequency(freq);
        context.engine->setLfoDepth(depth);
        context.engine->setLfoWaveform(waveform);
      }
    };

    // Input/Output enabled queries for config panel checkmarks
    c->isInputEnabled = [this](juce::String id) -> bool {
      return context.midiHardwareController
                 ? context.midiHardwareController->isInputEnabled(id)
                 : context.appState.getActiveMidiIds(true).contains(id);
    };
    c->isOutputEnabled = [this](juce::String id) -> bool {
      return context.midiHardwareController
                 ? context.midiHardwareController->isOutputEnabled(id)
                 : context.appState.getActiveMidiIds(false).contains(id);
    };

    c->onInputToggle = [this, c](juce::String id) {
      if (context.midiHardwareController &&
          context.midiHardwareController->setInputEnabled(
              id, !context.midiHardwareController->isInputEnabled(id),
              context.midiRouter.get()))
        c->repaint();
    };

    c->onOutputToggle = [this, c](juce::String id) {
      if (context.midiHardwareController &&
          context.midiHardwareController->setOutputEnabled(
              id, !context.midiHardwareController->isOutputEnabled(id)))
        c->repaint();
    };

    // Threading (worker pool mode; pool size set at launch from
    // ThreadingConfig)
    c->onThreadingModeChanged = [this](int mode) {
      auto m = static_cast<ThreadingConfig::Mode>(juce::jlimit(0, 2, mode));
      context.threadingConfig.mode.store(m, std::memory_order_relaxed);
    };
    c->cmbThreadingMode.setSelectedId(
        static_cast<int>(
            context.threadingConfig.mode.load(std::memory_order_relaxed)) +
            1,
        juce::dontSendNotification);

    // Lookahead bypass (Low Latency / Bypass Buffer) - keep both buttons in
    // sync
    c->onLookaheadBypassChanged = [this, c](bool bypassed) {
      context.appState.setLookaheadBypass(bypassed);
      c->btnBypassLookahead.setToggleState(bypassed,
                                           juce::dontSendNotification);
      c->btnLowLatency.setToggleState(bypassed, juce::dontSendNotification);
    };

    // --- Profile Management ---
    c->btnSaveProfile.onClick = [this, c] {
      auto name = c->cmbCtrlProfile.getText();
      if (name.isEmpty() || name == "- Select Profile -") {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Save Profile",
            juce::File::getSpecialLocation(
                juce::File::userApplicationDataDirectory)
                .getChildFile("PatchworldBridge")
                .getChildFile("Profiles"),
            "*.json");
        fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode,
            [this, c](const juce::FileChooser &fc) {
              auto result = fc.getResult();
              if (result != juce::File() && context.profileManager) {
                if (context.profileManager->saveProfile(result)) {
                  c->refreshProfileList(result.getFileNameWithoutExtension());
                  c->setProfileFeedback("Saved", false);
                  if (ui)
                    ui->onLogMessage("Profile saved: " +
                                         result.getFileNameWithoutExtension(),
                                     false);
                } else
                  c->setProfileFeedback(
                      "Could not save profile. Check path and try again.",
                      true);
              }
            });
      } else if (context.profileManager) {
        auto file = juce::File::getSpecialLocation(
                        juce::File::userApplicationDataDirectory)
                        .getChildFile("PatchworldBridge")
                        .getChildFile("Profiles")
                        .getChildFile(name + ".json");
        if (context.profileManager->saveProfile(file)) {
          c->setProfileFeedback("Saved", false);
          if (ui)
            ui->onLogMessage("Profile saved: " + name, false);
        } else
          c->setProfileFeedback(
              "Could not save profile. Check path and try again.", true);
      }
    };

    c->btnLoadProfile.onClick = [this, c] {
      fileChooser = std::make_unique<juce::FileChooser>(
          "Load Profile",
          juce::File::getSpecialLocation(
              juce::File::userApplicationDataDirectory)
              .getChildFile("PatchworldBridge")
              .getChildFile("Profiles"),
          "*.json");
      fileChooser->launchAsync(
          juce::FileBrowserComponent::openMode,
          [this, c](const juce::FileChooser &fc) {
            auto result = fc.getResult();
            if (result.existsAsFile() && context.profileManager) {
              if (context.profileManager->loadProfile(result)) {
                c->refreshProfileList(result.getFileNameWithoutExtension());
                c->setProfileFeedback("Loaded", false);
                if (ui)
                  ui->onLogMessage("Profile loaded: " +
                                       result.getFileNameWithoutExtension(),
                                   false);
              } else
                c->setProfileFeedback(
                    "Could not load profile. File may be missing or invalid.",
                    true);
            }
          });
    };

    c->btnDeleteProfile.onClick = [this, c] {
      auto name = c->cmbCtrlProfile.getText();
      if (name.isNotEmpty() && name != "- Select Profile -" &&
          context.profileManager) {
        context.profileManager->deleteProfile(name);
        c->refreshProfileList();
        if (ui)
          ui->onLogMessage("Profile deleted: " + name, false);
      }
    };

    // Refresh profile list on startup
    c->refreshProfileList();

    // --- OSC Schema ---
    c->onSchemaUpdated = [this](const OscNamingSchema &schema) {
      if (context.oscManager)
        context.oscManager->updateSchema(schema);
      if (context.networkWorker)
        context.networkWorker->setSchema(schema);
      if (context.oscSchema)
        *context.oscSchema = schema;
      context.appState.saveOscSchema(schema);
      if (ui)
        ui->onLogMessage("OSC Schema Updated.", false);
    };

    // Load saved schema into osc address editors
    {
      auto saved = context.appState.loadOscSchema();
      c->oscAddresses.applySchema(saved);
    }

    // --- Clock Source (MIDI real-time filter) ---
    auto refreshClockList = [this, c]() {
      auto devices = juce::MidiInput::getAvailableDevices();
      juce::String current =
          context.midiRouter ? context.midiRouter->getClockSourceID() : "";
      c->refreshClockSources(devices, current);
    };
    refreshClockList();

    c->onClockSourceChanged = [this, c](juce::String devId) {
      if (context.midiRouter)
        context.midiRouter->setClockSourceID(devId);
      context.appState.props->setValue(
          "clockSourceId",
          context.midiRouter ? context.midiRouter->getClockSourceID() : "");
    };

    // --- RTP Mode ---
    c->onRtpModeChanged = [this](int mode) {
      if (context.rtpManager) {
        auto m = (mode == 1)   ? RtpManager::Mode::OsDriver
                 : (mode == 2) ? RtpManager::Mode::EmbeddedServer
                               : RtpManager::Mode::Off;
        context.rtpManager->setMode(m);
      }
    };

    // --- Diagnostics HUD ---
    c->onDiagToggleChanged = [this](bool show) {
      juce::ignoreUnused(show);
      // DiagnosticOverlay toggle handled by MainComponent if needed
    };

    // --- Reset Tour ---
    c->onResetTourRequested = [this] { context.appState.setSeenTour(false); };

    c->onLayoutResetRequested = [this] { resetWindowLayout(); };

    c->onOpenHelpRequested = [this] {
      if (ui)
        launchHelpWindow(ui);
    };

    // --- Gamepad ---
    if (context.gamepadService) {
      c->sliderGamepadDeadzone.setValue(context.gamepadService->deadzone,
                                        juce::dontSendNotification);
      c->sliderGamepadSensitivity.setValue(context.gamepadService->sensitivity,
                                           juce::dontSendNotification);
      c->cmbGamepadController.setSelectedId(
          context.gamepadService->getControllerType() + 1,
          juce::dontSendNotification);
    }
    c->onGamepadEnable = [this](bool enabled) {
      if (context.gamepadService) {
        if (enabled)
          context.gamepadService->startPolling(60);
        else
          context.gamepadService->stopPolling();
      }
    };

    c->onGamepadDeadzone = [this](float deadzone) {
      if (context.gamepadService)
        context.gamepadService->deadzone = deadzone;
    };
    c->onGamepadSensitivity = [this](float sensitivity) {
      if (context.gamepadService)
        context.gamepadService->sensitivity = sensitivity;
    };
    c->onGamepadControllerType = [this](int type) {
      if (context.gamepadService)
        context.gamepadService->setControllerType(type);
    };

    // --- Bluetooth MIDI ---
    c->onBluetoothMidiPair = [this, c] {
#if JUCE_IOS || JUCE_MAC
      if (juce::BluetoothMidiDevicePairingDialogue::isAvailable()) {
        juce::BluetoothMidiDevicePairingDialogue::open();
        c->setBluetoothMidiStatus(
            "Select a device in the pairing dialogue. After pairing, click "
            "Scan or MIDI In to see it.");
        if (ui)
          ui->onLogMessage("Bluetooth MIDI pairing opened. After pairing, "
                           "click Scan or MIDI In to refresh.",
                           false);
      } else {
        c->setBluetoothMidiStatus("Bluetooth MIDI: use System Preferences > "
                                  "MIDI. Then click Scan or MIDI In.");
      }
#endif
#if JUCE_WINDOWS
      juce::Process::openDocument("ms-settings:bluetooth", "");
      c->setBluetoothMidiStatus(
          "Pair your Bluetooth MIDI device in the opened window. Then click "
          "Scan or MIDI In to see it.");
      if (ui)
        ui->onLogMessage("After pairing in Bluetooth settings, click Scan or "
                         "MIDI In to refresh the device list.",
                         false);
#endif
#if JUCE_ANDROID
      if (juce::BluetoothMidiDevicePairingDialogue::isAvailable()) {
        juce::BluetoothMidiDevicePairingDialogue::open();
        c->setBluetoothMidiStatus(
            "Select a device in the pairing dialogue. After pairing, click "
            "Scan or MIDI In to see it.");
        if (ui)
          ui->onLogMessage("Bluetooth MIDI pairing opened. After pairing, "
                           "click Scan or MIDI In to refresh.",
                           false);
      } else {
        c->setBluetoothMidiStatus(
            "Enable Bluetooth and pair a MIDI device in system settings. Then "
            "click Scan or MIDI In.");
      }
#endif
#if JUCE_LINUX
      c->setBluetoothMidiStatus(
          "Use system Bluetooth settings to pair your MIDI device. Then click "
          "Scan or MIDI In to refresh.");
      if (ui)
        ui->onLogMessage(
            "Bluetooth MIDI: pair in system settings, then Scan or MIDI In.",
            false);
#endif
    };

    // --- Performance Mode ---
    c->btnPerformanceMode.onClick = [this, c] {
      context.setPerformanceMode(c->btnPerformanceMode.getToggleState());
    };

    // --- Force Grid (sequencer record snap) ---
    c->btnForceGrid.onClick = [this, c] {
      if (context.sequencer)
        context.sequencer->setForceGridRecord(c->btnForceGrid.getToggleState());
    };

    // --- Note Quantize (scale quantizer) ---
    c->btnNoteQuantize.onClick = [this, c] {
      if (context.midiRouter)
        context.midiRouter->setQuantizationEnabled(
            c->btnNoteQuantize.getToggleState());
    };

    // --- Direct Input ---
    c->btnDirectInput.onClick = [this, c] {
      if (context.midiRouter)
        context.midiRouter->setNetworkLookahead(
            c->btnDirectInput.getToggleState() ? 0.0f : 20.0f);
    };

    // --- Link BPM Slider ---
    c->sliderLinkBpm.onValueChange = [this, c] {
      if (context.engine)
        context.engine->setBpm(c->sliderLinkBpm.getValue());
    };

    c->onLatencyChange = [this](double ms) {
      if (context.engine)
        context.engine->setOutputLatency(ms);
      context.appState.setNetworkLookahead(ms);
    };
    c->onClockOffsetChange = [this](double ms) {
      context.appState.setClockOffset(ms);
    };

    c->sliderLookahead.onValueChange = [this, c] {
      if (context.midiRouter)
        context.midiRouter->setNetworkLookahead(
            (float)c->sliderLookahead.getValue());
      context.appState.setNetworkLookahead(c->sliderLookahead.getValue());
    };

    // --- Sync Buffer ---
    c->sliderSyncBuffer.onValueChange = [this, c] {
      float ms = (float)c->sliderSyncBuffer.getValue();
      if (context.midiRouter)
        context.midiRouter->setNetworkLookahead(ms);
      context.appState.setNetworkLookahead(ms);
    };

    // --- MIDI device options (Track/Sync/Remote/MPE) for ConfigPanel menus ---
    c->getMidiDeviceOptions = [this](bool isInput, juce::String deviceId) {
      return context.appState.getMidiDeviceOptions(isInput, deviceId);
    };
    c->setMidiDeviceOptions = [this](bool isInput, juce::String deviceId,
                                     const AppState::MidiDeviceOptions &opts) {
      context.appState.setMidiDeviceOptions(isInput, deviceId, opts);
    };

    // When MIDI devices are disconnected (or reconnected), refresh Config
    // labels and log
    if (context.deviceService) {
      context.deviceService->setOnDeviceListChanged([this]() {
        if (MainComponent *mc = getUi())
          if (mc->configPanel) {
            mc->configPanel->updateMidiButtonLabels();
            mc->configPanel->repaint();
            if (ui)
              ui->onLogMessage("MIDI devices changed. Re-open MIDI In/Out menu "
                               "to see updated list.",
                               false);
          }
      });
    }
    c->updateMidiButtonLabels();

    // --- Calibrate (MIDI loopback latency measurement) ---
    c->btnCalibrate.onClick = [this, c] {
      auto *cal = context.latencyCalibrator.get();
      if (!cal)
        return;
      cal->onSendPing = [this](const juce::MidiMessage &m) {
        if (context.deviceService)
          context.deviceService->sendMessage(m);
      };
      juce::Component::SafePointer<ConfigPanel> safeConfig(c);
      cal->onResult = [this, safeConfig](double avgMs) {
        juce::MessageManager::callAsync([this, safeConfig, avgMs] {
          if (!safeConfig)
            return;
          safeConfig->sliderLatency.setValue(avgMs, juce::sendNotification);
          if (safeConfig->onLatencyChange)
            safeConfig->onLatencyChange(avgMs);
          if (ui)
            ui->onLogMessage(
                "Latency calibrated: " + juce::String(avgMs, 1) + " ms", false);
        });
      };
      cal->startMeasurement();
      if (ui)
        ui->onLogMessage(
            "Calibrating... Connect MIDI Out to MIDI In (loopback).", false);
    };

    // --- Reset Mixer ---
    c->btnResetMixer.onClick = [this] {
      if (context.mixerViewModel)
        context.mixerViewModel->reset();
    };

    // --- MIDI Map Import/Export ---
    c->btnImportMap.onClick = [this] {
      fileChooser = std::make_unique<juce::FileChooser>("Import MIDI Mappings",
                                                        juce::File(), "*.json");
      fileChooser->launchAsync(
          juce::FileBrowserComponent::openMode,
          [this](const juce::FileChooser &fc) {
            auto result = fc.getResult();
            if (result.existsAsFile() && context.mappingManager) {
              if (context.mappingManager->loadMappingsFromFile(result)) {
                if (ui)
                  ui->onLogMessage("Mappings imported.", false);
              } else if (ui) {
                ui->onLogMessage("Could not import mappings. File may be "
                                 "missing or invalid.",
                                 true);
              }
            }
          });
    };

    c->btnExportMap.onClick = [this] {
      fileChooser = std::make_unique<juce::FileChooser>("Export MIDI Mappings",
                                                        juce::File(), "*.json");
      fileChooser->launchAsync(
          juce::FileBrowserComponent::saveMode,
          [this](const juce::FileChooser &fc) {
            auto result = fc.getResult();
            if (result != juce::File() && context.mappingManager) {
              if (context.mappingManager->saveMappingsToFile(result)) {
                if (ui)
                  ui->onLogMessage("Mappings exported.", false);
              } else if (ui) {
                ui->onLogMessage(
                    "Could not export mappings. Check path and permissions.",
                    true);
              }
            }
          });
    };

    c->btnResetMaps.onClick = [this] {
      if (context.mappingManager) {
        const juce::ScopedWriteLock sl(context.mappingManager->mappingLock);
        context.mappingManager->mappings.clear();
        context.mappingManager->rebuildFastLookup();
        if (ui)
          ui->onLogMessage("All MIDI mappings cleared.", false);
      }
    };

    // --- Config Panel Logging ---
    c->onLog = [this](juce::String msg, bool err) {
      if (ui)
        ui->onLogMessage(msg, err);
    };
  }

  // Wire mapping manager log callback
  if (context.mappingManager) {
    context.mappingManager->onMidiLogCallback = [this](juce::String msg) {
      if (ui)
        ui->onLogMessage(msg, false);
    };
  }
}

void SystemController::bindMixer(MainComponent &mainUI) {
  juce::ignoreUnused(mainUI);
  if (context.mixer) {
    context.mixer->onMixerActivity = [this](int visualIdx, float val,
                                            int outputCh) {
      juce::String paramID = "MixerStrip_" + juce::String(visualIdx) + "_Vol";
      juce::String mappingID = "Mixer_" + juce::String(visualIdx + 1) + "_Vol";
      if (context.mappingManager)
        context.mappingManager->setParameterValue(mappingID, val / 127.0f);

      if (context.midiRouter) {
        auto ov = context.appState.getControlMessageOverride(paramID);
        int ch = (ov.type != 0 && ov.channel >= 1)   ? ov.channel
                 : (outputCh >= 1 && outputCh <= 16) ? outputCh
                 : (context.midiRouter->selectedChannel >= 1 &&
                    context.midiRouter->selectedChannel <= 16)
                     ? context.midiRouter->selectedChannel
                     : 1;
        float norm = val / 127.0f;
        if (ov.type == 1)
          context.midiRouter->handleCC(ch, ov.noteOrCC, norm,
                                       BridgeEvent::Source::UserInterface);
        else if (ov.type == 3)
          context.midiRouter->handleBridgeEvent(BridgeEvent(
              EventType::PitchBend, EventSource::UserInterface, ch, 0, norm));
        else
          context.midiRouter->handleBridgeEvent(
              BridgeEvent(EventType::ControlChange,
                          BridgeEvent::Source::UserInterface, ch, 7, norm));
      }
    };

    // Send knob -> MIDI/OSC (use override if set)
    context.mixer->onSendChanged = [this](int ch, int defaultCC, float val) {
      juce::String paramID = "MixerStrip_" + juce::String(ch - 1) + "_Send";
      float norm = val / 127.0f;
      if (context.oscManager && context.oscManager->isConnected())
        context.oscManager->sendCC(ch, defaultCC, norm);
      if (context.midiRouter) {
        auto ov = context.appState.getControlMessageOverride(paramID);
        int midiCh = (ov.type != 0 && ov.channel >= 1) ? ov.channel
                     : (context.midiRouter->selectedChannel >= 1 &&
                        context.midiRouter->selectedChannel <= 16)
                         ? context.midiRouter->selectedChannel
                         : 1;
        if (ov.type == 1)
          context.midiRouter->handleCC(midiCh, ov.noteOrCC, norm,
                                       BridgeEvent::Source::UserInterface);
        else if (ov.type == 3)
          context.midiRouter->handleBridgeEvent(
              BridgeEvent(EventType::PitchBend, EventSource::UserInterface,
                          midiCh, 0, norm));
        else
          context.midiRouter->handleCC(midiCh, defaultCC, norm,
                                       BridgeEvent::Source::UserInterface);
      }
    };

    // Status bar updates from mixer interactions
    context.mixer->onStatusUpdate = [this](juce::String txt) {
      if (ui)
        ui->getStatusBar().setText(txt, juce::dontSendNotification);
    };

    // File dropped onto mixer strip
    context.mixer->onFileDropped = [this](juce::String path, int ch) {
      juce::ignoreUnused(ch);
      juce::File f(path);
      if (f.existsAsFile() && context.playbackController)
        context.playbackController->loadMidiFile(f);
    };

    // MIDI Learn from mixer context menu: show overlay and queue param so user
    // can move a control
    context.mixer->onLearnRequested = [this](juce::String paramID) {
      if (context.mappingManager) {
        context.mappingManager->setSelectedParameterForLearning(paramID);
        context.mappingManager->setLearnModeActive(true);
      }
      if (ui) {
        ui->isMidiLearnMode = true;
        ui->btnMidiLearn.setToggleState(true, juce::dontSendNotification);
        ui->btnMidiLearn.setButtonText("LEARNING...");
        ui->toggleMidiLearnOverlay(true);
        if (ui->getMidiLearnOverlay())
          ui->getMidiLearnOverlay()->refreshMappingList();
      }
    };

    // Routing changed (strip reorder, custom OSC address)
    context.mixer->onRoutingChanged = [this] {
      // Rebuild custom routing if needed
    };

    // Channel toggle (mute) -> send MIDI if override set
    context.mixer->onChannelToggle = [this](int ch, bool active) {
      juce::String paramID = "MixerStrip_" + juce::String(ch - 1) + "_On";
      if (context.midiRouter) {
        auto ov = context.appState.getControlMessageOverride(paramID);
        if (ov.type != 0 && ov.channel >= 1) {
          int midiCh = ov.channel;
          float v = active ? 1.0f : 0.0f;
          if (ov.type == 1)
            context.midiRouter->handleCC(midiCh, ov.noteOrCC, v,
                                         BridgeEvent::Source::UserInterface);
          else if (ov.type == 2) {
            if (active)
              context.midiRouter->handleNoteOn(
                  midiCh, ov.noteOrCC, 1.0f, false, false,
                  BridgeEvent::Source::UserInterface);
            else
              context.midiRouter->handleNoteOff(
                  midiCh, ov.noteOrCC, 0.0f, false, false,
                  BridgeEvent::Source::UserInterface);
          }
        }
      }
    };

    // Solo state changed -> send MIDI if override set
    context.mixer->onSoloStateChanged = [this](int ch, bool solo) {
      juce::String paramID = "MixerStrip_" + juce::String(ch - 1) + "_Solo";
      if (context.midiRouter) {
        auto ov = context.appState.getControlMessageOverride(paramID);
        if (ov.type != 0 && ov.channel >= 1) {
          int midiCh = ov.channel;
          if (ov.type == 1)
            context.midiRouter->handleCC(midiCh, ov.noteOrCC,
                                         solo ? 1.0f : 0.0f,
                                         BridgeEvent::Source::UserInterface);
          else if (ov.type == 2) {
            if (solo)
              context.midiRouter->handleNoteOn(
                  midiCh, ov.noteOrCC, 1.0f, false, false,
                  BridgeEvent::Source::UserInterface);
            else
              context.midiRouter->handleNoteOff(
                  midiCh, ov.noteOrCC, 0.0f, false, false,
                  BridgeEvent::Source::UserInterface);
          }
        }
      }
    };

    // Reset mixer button
    context.mixer->onResetRequested = [this] {
      if (context.mixerViewModel)
        context.mixerViewModel->reset();
    };
  }
}

void SystemController::bindMappingManager(MainComponent &) {
  if (!context.mappingManager)
    return;
  if (context.mixer) {
    context.mixer->getCCForParamCallback = [this](juce::String paramID) {
      return context.mappingManager
                 ? context.mappingManager->getCCForParam(paramID)
                 : -1;
    };
    context.mixer->refreshVolumeCCLabels();
  }
  context.mappingManager->onMappingChanged = [this] {
    if (ui && ui->getMidiLearnOverlay())
      ui->getMidiLearnOverlay()->refreshMappingList();
    if (context.mixer)
      context.mixer->refreshVolumeCCLabels();
  };
  context.mappingManager->onHardwarePositionChanged =
      [this](juce::String paramID, float rawVal) {
        if (context.mappingManager &&
            context.mappingManager->setParameterValueCallback)
          context.mappingManager->setParameterValueCallback(paramID, rawVal);
      };
  context.mappingManager->setParameterValueCallback = [this](
                                                          juce::String paramID,
                                                          float value) {
    if (!ui)
      return;
    auto sendMappedParamOut = [this](const juce::String &pid, float val) {
      if (!context.midiRouter)
        return;
      auto ov = context.appState.getControlMessageOverride(pid);
      int ch = (ov.type != 0 && ov.channel >= 1) ? ov.channel
               : (context.midiRouter->selectedChannel >= 1 &&
                  context.midiRouter->selectedChannel <= 16)
                   ? context.midiRouter->selectedChannel
                   : 1;
      if (pid.startsWith("Macro_Fader_")) {
        int idx = pid.fromLastOccurrenceOf("_", false, false).getIntValue() - 1;
        int defaultCC = 30 + idx;
        if (ov.type == 1)
          context.midiRouter->handleCC(ch, ov.noteOrCC, val,
                                       BridgeEvent::Source::UserInterface);
        else if (ov.type == 3)
          context.midiRouter->handleBridgeEvent(BridgeEvent(
              EventType::PitchBend, EventSource::UserInterface, ch, 0, val));
        else
          context.midiRouter->handleCC(ch, defaultCC, val,
                                       BridgeEvent::Source::UserInterface);
      } else if (pid.startsWith("Macro_Btn_")) {
        int idx = pid.fromLastOccurrenceOf("_", false, false).getIntValue() - 1;
        int defaultNote = 60 + idx;
        int note = (ov.type == 2) ? ov.noteOrCC : defaultNote;
        if (val > 0.5f)
          context.midiRouter->handleNoteOn(ch, note, 1.0f, false, false,
                                           BridgeEvent::Source::UserInterface);
        else
          context.midiRouter->handleNoteOff(ch, note, 0.0f, false, false,
                                            BridgeEvent::Source::UserInterface);
      } else if (pid.startsWith("MixerStrip_")) {
        int stripIdx = pid.fromFirstOccurrenceOf("_", false, false)
                           .upToFirstOccurrenceOf("_", false, false)
                           .getIntValue();
        int defaultCh = (context.mixer)
                            ? context.mixer->getOutputChannelForStrip(stripIdx)
                            : (stripIdx + 1);
        int midiCh = (ov.type != 0 && ov.channel >= 1) ? ov.channel : defaultCh;
        if (pid.endsWith("_Vol")) {
          if (ov.type == 1)
            context.midiRouter->handleCC(midiCh, ov.noteOrCC, val,
                                         BridgeEvent::Source::UserInterface);
          else if (ov.type == 3)
            context.midiRouter->handleBridgeEvent(
                BridgeEvent(EventType::PitchBend, EventSource::UserInterface,
                            midiCh, 0, val));
          else
            context.midiRouter->handleBridgeEvent(BridgeEvent(
                EventType::ControlChange, BridgeEvent::Source::UserInterface,
                midiCh, 7, val));
        } else if (pid.endsWith("_Send")) {
          if (ov.type == 1)
            context.midiRouter->handleCC(midiCh, ov.noteOrCC, val,
                                         BridgeEvent::Source::UserInterface);
          else if (ov.type == 3)
            context.midiRouter->handleBridgeEvent(
                BridgeEvent(EventType::PitchBend, EventSource::UserInterface,
                            midiCh, 0, val));
          else
            context.midiRouter->handleCC(midiCh, 12, val,
                                         BridgeEvent::Source::UserInterface);
        } else if (pid.endsWith("_On")) {
          float v = val > 0.5f ? 1.0f : 0.0f;
          if (ov.type == 1)
            context.midiRouter->handleCC(midiCh, ov.noteOrCC, v,
                                         BridgeEvent::Source::UserInterface);
          else if (ov.type == 2) {
            if (v > 0.5f)
              context.midiRouter->handleNoteOn(
                  midiCh, ov.noteOrCC, 1.0f, false, false,
                  BridgeEvent::Source::UserInterface);
            else
              context.midiRouter->handleNoteOff(
                  midiCh, ov.noteOrCC, 0.0f, false, false,
                  BridgeEvent::Source::UserInterface);
          }
        } else if (pid.endsWith("_Solo")) {
          float v = val > 0.5f ? 1.0f : 0.0f;
          if (ov.type == 1)
            context.midiRouter->handleCC(midiCh, ov.noteOrCC, v,
                                         BridgeEvent::Source::UserInterface);
          else if (ov.type == 2) {
            if (v > 0.5f)
              context.midiRouter->handleNoteOn(
                  midiCh, ov.noteOrCC, 1.0f, false, false,
                  BridgeEvent::Source::UserInterface);
            else
              context.midiRouter->handleNoteOff(
                  midiCh, ov.noteOrCC, 0.0f, false, false,
                  BridgeEvent::Source::UserInterface);
          }
        }
      }
    };

    if (paramID.startsWith("MixerStrip_")) {
      if (context.mixer)
        context.mixer->updateHardwarePosition(paramID, value);
      sendMappedParamOut(paramID, value);
    } else if (paramID.startsWith("Macro_Fader_")) {
      int index =
          paramID.fromLastOccurrenceOf("_", false, false).getIntValue() - 1;
      if (index >= 0 && index < ui->macroControls.faders.size())
        ui->macroControls.faders[index]->knob.setValue(
            value, juce::dontSendNotification);
      sendMappedParamOut(paramID, value);
    } else if (paramID.startsWith("Macro_Btn_")) {
      int index =
          paramID.fromLastOccurrenceOf("_", false, false).getIntValue() - 1;
      if (index >= 0 && index < ui->macroControls.buttons.size())
        ui->macroControls.buttons[index]->btn.setToggleState(
            value > 0.5f, juce::dontSendNotification);
      sendMappedParamOut(paramID, value);
    } else if (paramID == "Transport_BPM" && ui->transportPanel) {
      double bpm = 20.0 + value * 280.0;
      ui->tempoSlider.setValue(bpm, juce::dontSendNotification);
      if (context.oscManager && context.oscManager->isConnected())
        context.oscManager->sendFloat("/clock/bpm", (float)bpm);
    } else if (paramID == "Transport_Play") {
      if (context.playbackController) {
        if (value > 0.5f)
          context.playbackController->resumePlayback();
        else if (context.engine && context.engine->getIsPlaying())
          context.playbackController->pausePlayback();
      }
    } else if (paramID == "Transport_Stop" && value > 0.5f &&
               ui->transportPanel) {
      ui->transportPanel->btnStop.triggerClick();
    } else if (paramID == "Arp_Rate" && ui->arpPanel) {
      double v = 1.0 + value * 31.0;
      ui->arpPanel->knobArpSpeed.setValue(v, juce::dontSendNotification);
      if (ui->arpPanel->onArpUpdate)
        ui->arpPanel->onArpUpdate((int)ui->arpPanel->knobArpSpeed.getValue(),
                                  (int)ui->arpPanel->knobArpVel.getValue(),
                                  ui->arpPanel->cmbArpPattern.getSelectedId(),
                                  (int)ui->arpPanel->sliderArpOctave.getValue(),
                                  (float)ui->arpPanel->knobArpGate.getValue());
    } else if (paramID == "Arp_Vel" && ui->arpPanel) {
      double v = value * 127.0;
      ui->arpPanel->knobArpVel.setValue(v, juce::dontSendNotification);
      if (ui->arpPanel->onArpUpdate)
        ui->arpPanel->onArpUpdate((int)ui->arpPanel->knobArpSpeed.getValue(),
                                  (int)ui->arpPanel->knobArpVel.getValue(),
                                  ui->arpPanel->cmbArpPattern.getSelectedId(),
                                  (int)ui->arpPanel->sliderArpOctave.getValue(),
                                  (float)ui->arpPanel->knobArpGate.getValue());
    } else if (paramID == "Arp_Gate" && ui->arpPanel) {
      double v = 0.1 + value * 0.9;
      ui->arpPanel->knobArpGate.setValue(v, juce::dontSendNotification);
      if (ui->arpPanel->onArpUpdate)
        ui->arpPanel->onArpUpdate((int)ui->arpPanel->knobArpSpeed.getValue(),
                                  (int)ui->arpPanel->knobArpVel.getValue(),
                                  ui->arpPanel->cmbArpPattern.getSelectedId(),
                                  (int)ui->arpPanel->sliderArpOctave.getValue(),
                                  (float)ui->arpPanel->knobArpGate.getValue());
    } else if (paramID == "Arp_Octave" && ui->arpPanel) {
      double v = 1.0 + value * 3.0;
      ui->arpPanel->sliderArpOctave.setValue(v, juce::dontSendNotification);
      if (ui->arpPanel->onArpUpdate)
        ui->arpPanel->onArpUpdate((int)ui->arpPanel->knobArpSpeed.getValue(),
                                  (int)ui->arpPanel->knobArpVel.getValue(),
                                  ui->arpPanel->cmbArpPattern.getSelectedId(),
                                  (int)ui->arpPanel->sliderArpOctave.getValue(),
                                  (float)ui->arpPanel->knobArpGate.getValue());
    } else if (paramID == "LFO_Rate" || paramID == "LFO_Depth" ||
               paramID == "LFO_Attack" || paramID == "LFO_Decay" ||
               paramID == "LFO_Sustain" || paramID == "LFO_Release") {
      auto *panel = &ui->lfoGeneratorPanel;
      if (paramID == "LFO_Rate")
        panel->rateKnob.setValue(value * 19.99 + 0.01,
                                 juce::dontSendNotification);
      else if (paramID == "LFO_Depth")
        panel->depthKnob.setValue(value, juce::dontSendNotification);
      else if (paramID == "LFO_Attack")
        panel->attackKnob.setValue(value, juce::dontSendNotification);
      else if (paramID == "LFO_Decay")
        panel->decayKnob.setValue(value, juce::dontSendNotification);
      else if (paramID == "LFO_Sustain")
        panel->sustainKnob.setValue(value, juce::dontSendNotification);
      else if (paramID == "LFO_Release")
        panel->releaseKnob.setValue(value, juce::dontSendNotification);
      panel->flushControlsToSelectedSlot();
      if (panel->onLfoParamsChanged)
        panel->onLfoParamsChanged((float)panel->rateKnob.getValue(),
                                  (float)panel->depthKnob.getValue(),
                                  panel->getShape(panel->getSelectedSlot()) - 1,
                                  (float)panel->attackKnob.getValue(),
                                  (float)panel->decayKnob.getValue(),
                                  (float)panel->sustainKnob.getValue(),
                                  (float)panel->releaseKnob.getValue());
    } else if (paramID == "LFO_Shape") {
      auto *panel = &ui->lfoGeneratorPanel;
      // Shape fixed to sine; no combo. Still flush and notify so other params
      // stay in sync.
      panel->flushControlsToSelectedSlot();
      if (panel->onLfoParamsChanged)
        panel->onLfoParamsChanged((float)panel->rateKnob.getValue(),
                                  (float)panel->depthKnob.getValue(),
                                  panel->getShape(panel->getSelectedSlot()) - 1,
                                  (float)panel->attackKnob.getValue(),
                                  (float)panel->decayKnob.getValue(),
                                  (float)panel->sustainKnob.getValue(),
                                  (float)panel->releaseKnob.getValue());
    }
  };
  context.mappingManager->getParameterValue =
      [this](juce::String paramID) -> float {
    if (!ui)
      return 0.0f;
    if (paramID == "Transport_BPM" && ui->transportPanel)
      return (float)((ui->tempoSlider.getValue() - 20.0) / 280.0);
    if (paramID == "Transport_Play")
      return (context.engine &&
              (context.engine->getIsPlaying() || context.engine->getIsPaused()))
                 ? 1.0f
                 : 0.0f;
    if (paramID.startsWith("Macro_Fader_")) {
      int index =
          paramID.fromLastOccurrenceOf("_", false, false).getIntValue() - 1;
      if (index >= 0 && index < ui->macroControls.faders.size())
        return (float)ui->macroControls.faders[index]->knob.getValue();
    }
    if (paramID.startsWith("Macro_Btn_")) {
      int index =
          paramID.fromLastOccurrenceOf("_", false, false).getIntValue() - 1;
      if (index >= 0 && index < ui->macroControls.buttons.size())
        return ui->macroControls.buttons[index]->btn.getToggleState() ? 1.0f
                                                                      : 0.0f;
    }
    if (paramID.startsWith("MixerStrip_") && context.mixer) {
      int stripIdx = paramID.fromFirstOccurrenceOf("_", false, false)
                         .upToFirstOccurrenceOf("_", false, false)
                         .getIntValue();
      juce::String suffix = paramID.fromLastOccurrenceOf("_", false, false);
      if (stripIdx >= 0 && stripIdx < context.mixer->strips.size()) {
        auto *s = context.mixer->strips[stripIdx];
        if (s && suffix == "Vol")
          return (float)s->volSlider.getValue();
        if (s && suffix == "Pan")
          return (float)s->panSlider.getValue();
        if (s && suffix == "Send")
          return (float)(s->sendKnob.getValue() / 127.0);
        if (s && suffix == "On")
          return s->btnActive.getToggleState() ? 1.0f : 0.0f;
        if (s && suffix == "Solo")
          return s->btnSolo.getToggleState() ? 1.0f : 0.0f;
      }
    }
    if (paramID == "Arp_Rate" && ui->arpPanel)
      return (float)((ui->arpPanel->knobArpSpeed.getValue() - 1.0) / 31.0);
    if (paramID == "Arp_Vel" && ui->arpPanel)
      return (float)(ui->arpPanel->knobArpVel.getValue() / 127.0);
    if (paramID == "Arp_Gate" && ui->arpPanel)
      return (float)((ui->arpPanel->knobArpGate.getValue() - 0.1) / 0.9);
    if (paramID == "Arp_Octave" && ui->arpPanel)
      return (float)((ui->arpPanel->sliderArpOctave.getValue() - 1.0) / 3.0);
    if (paramID == "LFO_Rate")
      return (float)((ui->lfoGeneratorPanel.rateKnob.getValue() - 0.01) /
                     19.99);
    if (paramID == "LFO_Depth")
      return (float)ui->lfoGeneratorPanel.depthKnob.getValue();
    if (paramID == "LFO_Attack")
      return (float)ui->lfoGeneratorPanel.attackKnob.getValue();
    if (paramID == "LFO_Decay")
      return (float)ui->lfoGeneratorPanel.decayKnob.getValue();
    if (paramID == "LFO_Sustain")
      return (float)ui->lfoGeneratorPanel.sustainKnob.getValue();
    if (paramID == "LFO_Release")
      return (float)ui->lfoGeneratorPanel.releaseKnob.getValue();
    if (paramID == "LFO_Shape")
      return 0.0f; // Shape fixed to sine (index 0)
    return 0.0f;
  };
}

void SystemController::bindPerformance(MainComponent &mainUI) {
  if (auto *p = mainUI.performancePanel.get()) {
    // 1. Sequencer & Playhead
    p->timeline.onSeek = [this, p](double beat) {
      if (context.engine) {
        context.engine->seek(beat);
        p->updatePlayhead(beat, context.engine->getTicksPerQuarter());
      }
    };
    p->timeline.onLoopSelect = [this](double start, double end) {
      if (context.engine && end > start) {
        context.engine->setLoopRegion(start, end);
        context.engine->setLoopEnabled(true);
      }
    };

    // 2. Probability
    p->onProbabilityChange = [this](float val) {
      if (context.engine)
        context.engine->setGlobalProbability(val);
    };

    // 3. Sequencer Channel
    p->onSequencerChannelChange = [this](int ch) {
      context.sequencerViewModel->setSequencerChannel(ch);
    };

    p->horizontalKeyboard.setKeyPressBaseOctave(4);
    p->verticalKeyboard.setKeyPressBaseOctave(4);
    p->onOctaveShift = [this, p](int dir) {
      if (context.engine) {
        // Update Transport Transpose (Global)
        int current = context.engine->transport.globalTranspose.load();
        int next = juce::jlimit(-36, 36, current + (dir * 12));
        context.engine->transport.globalTranspose.store(next);

        // Sync MidiRouter so OSC/MIDI note pitch follows octave (virtual
        // keyboard + hardware input)
        if (context.midiRouter)
          context.midiRouter->setGlobalOctaveShift(next / 12);

        // Update Virtual Keyboard Base Octave (Visual)
        int baseOct = 4 + (next / 12);
        p->horizontalKeyboard.setKeyPressBaseOctave(baseOct);
        p->verticalKeyboard.setKeyPressBaseOctave(baseOct);

        // Visual Grid Shift
        p->trackGrid.setVisualOctaveShift(next / 12);

        // Edit mode: scroll piano roll by one octave so drawn notes stay in
        // view
        if (p->spliceEditor.isVisible()) {
          float nh = p->spliceEditor.getNoteHeight();
          if (nh > 0.1f) {
            float scrollDelta = 12.0f * nh * (float)dir;
            float newScrollY = p->spliceEditor.getScrollY() + scrollDelta;
            p->spliceEditor.setScrollY(newScrollY);
          }
        }

        // Refresh layout so PlayView key range matches keyboard (drawn notes
        // align with keys)
        p->resized();

        // Panic to prevent stuck notes
        if (context.deviceService)
          context.deviceService->forceAllNotesOff();
      }
    };

    // Keep MidiRouter octave in sync with engine (e.g. after load state)
    if (context.engine && context.midiRouter)
      context.midiRouter->setGlobalOctaveShift(
          context.engine->getGlobalTranspose() / 12);

    // 5. Pitch Wheel -> MIDI Pitch Bend
    p->pitchWheel.setRange(-1.0, 1.0, 0.01);
    p->pitchWheel.setValue(0.0);
    p->pitchWheel.onValueChange = [this, p] {
      float val = (float)p->pitchWheel.getValue();
      if (context.midiRouter) {
        int pitchVal = (int)((val + 1.0f) * 8192.0f);
        pitchVal = juce::jlimit(0, 16383, pitchVal);
        context.midiRouter->handleBridgeEvent(BridgeEvent(
            EventType::PitchBend, BridgeEvent::UserInterface, 1, 0, val));
      }
    };

    // 5. Mod Wheel -> MIDI CC1
    p->modWheel.setRange(0.0, 1.0, 0.01);
    p->modWheel.setValue(0.0);
    p->modWheel.onValueChange = [this, p] {
      float val = (float)p->modWheel.getValue();
      if (context.midiRouter)
        context.midiRouter->handleCC(1, 1, val, BridgeEvent::UserInterface);
    };
  }

  // --- Sequencer Wiring ---
  if (context.sequencer && context.sequencerViewModel) {
    context.sequencer->onStepChanged = [this] {
      context.sequencerViewModel->updateData();
    };
    context.sequencer->onClearRequested = [this] {
      if (context.midiRouter)
        context.midiRouter->allNotesOff();
    };
    context.sequencerViewModel->updateData();

    // Roll buttons -> engine (Roll mode)
    context.sequencer->onRollChange = [this](int div) {
      context.sequencerViewModel->setRoll(div);
    };

    // Time sig restore (when releasing Time-mode div button)
    context.sequencer->onTimeSigRestore = [this] {
      if (!context.sequencer)
        return;
      int id = context.sequencer->cmbTimeSig.getSelectedId();
      int num = (id == 2) ? 3 : (id == 3 ? 5 : 4);
      context.sequencerViewModel->setTimeSignature(num, 4);
    };

    // Loop length override (Loop mode)
    context.sequencer->onLoopChange = [this](int steps) {
      context.sequencerViewModel->setMomentaryLoopSteps(steps);
    };

    // Swing slider -> engine
    context.sequencer->swingSlider.onValueChange = [this] {
      if (context.sequencer)
        context.sequencerViewModel->setSwing(
            (float)(context.sequencer->swingSlider.getValue() / 100.0));
    };

    // Time signature -> engine
    context.sequencer->onTimeSigChange = [this](int num, int den) {
      context.sequencerViewModel->setTimeSignature(num, den);
    };

    context.sequencer->onSequencerChannelChange = [this](int ch) {
      context.sequencerViewModel->setSequencerChannel(ch);
    };

    // Sequencer export
    context.sequencer->onExportRequest = [this] {
      context.sequencerViewModel->requestExport();
    };
    context.sequencerViewModel->updateData();
  }
}

void SystemController::wireExtraSequencer(SequencerPanel *panel, int slot) {
  if (!panel || !context.sequencerViewModel || slot < 1 ||
      slot >= context.getNumSequencerSlots())
    return;
  panel->onStepChanged = [this] { context.sequencerViewModel->updateData(); };
  panel->onClearRequested = [this] {
    if (context.midiRouter)
      context.midiRouter->allNotesOff();
    context.sequencerViewModel->updateData();
  };
  panel->onRollChange = [this](int div) {
    context.sequencerViewModel->setRoll(div);
  };
  panel->onTimeSigRestore = [this, panel] {
    if (!panel)
      return;
    int id = panel->cmbTimeSig.getSelectedId();
    int num = (id == 2) ? 3 : (id == 3 ? 5 : 4);
    context.sequencerViewModel->setTimeSignature(num, 4);
  };
  panel->onLoopChange = [this](int steps) {
    context.sequencerViewModel->setMomentaryLoopSteps(steps);
  };
  panel->swingSlider.onValueChange = [this, panel] {
    if (panel)
      context.sequencerViewModel->setSwing(
          (float)(panel->swingSlider.getValue() / 100.0));
  };
  panel->onTimeSigChange = [this](int num, int den) {
    context.sequencerViewModel->setTimeSignature(num, den);
  };
  panel->onSequencerChannelChange = [this, slot](int ch) {
    context.sequencerViewModel->setSequencerChannel(slot, ch);
  };
  panel->onExportRequest = [this, panel] {
    if (!context.engine || !panel)
      return;
    panel->setExportBpm(context.engine->getBpm());
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Sequence as MIDI", juce::File(), "*.mid");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode,
                         [panel, chooser](const juce::FileChooser &fc) {
                           auto result = fc.getResult();
                           if (result != juce::File() && panel)
                             panel->exportToMidi(result);
                         });
  };
  context.sequencerViewModel->updateData();
}

void SystemController::processUpdates(bool fullUpdate) {
  static bool firstProcessUpdates = true;
  if (firstProcessUpdates) {
    firstProcessUpdates = false;
    DebugLog::debugLog("processUpdates() first call");
  }
  if (!ui)
    return;

  // Thread-safety: Only access UI from message thread; guard against stale refs
  if (!juce::MessageManager::existsAndIsCurrentThread())
    return;

  // Detect minimised state before tick so TimerHub callbacks
  // (repaintCoordinator, statusBar) can cull
  bool wasMinimised = context.windowMinimised_.load(std::memory_order_relaxed);
  bool nowMinimised = false;
  if (auto *rw = ui->findParentComponentOfClass<juce::ResizableWindow>())
    nowMinimised = rw->isMinimised();
  context.windowMinimised_.store(nowMinimised, std::memory_order_relaxed);

  // Master tick: drives TimerHub subscribers (repaintCoordinator flush, etc.)
  try {
    TimerHub::instance().tick();
  } catch (const std::exception &e) {
    DebugLog::debugLog(juce::String("TimerHub::tick exception: ") + e.what());
  } catch (...) {
    DebugLog::debugLog("TimerHub::tick exception: unknown");
  }
  static bool afterFirstTickLogged = false;
  if (!afterFirstTickLogged) {
    afterFirstTickLogged = true;
    DebugLog::debugLog("processUpdates: after first tick");
  }

  try {
    if (!ui)
      return;
    // On restore from minimised: flush all dirty regions and repaint so UI is
    // correct
    if (wasMinimised && !nowMinimised && ui) {
      context.repaintCoordinator.flushAll([this](uint32_t flags) {
        if (ui)
          ui->repaintDirtyRegions(flags);
      });
      ui->repaint();
    }

    // When minimised: only keep core sync (sequencer isPlaying); skip all extra
    // UI animations
    if (nowMinimised) {
      if (context.engine) {
        bool playing = context.engine->getIsPlaying();
        for (int slot = 0; slot < context.getNumSequencerSlots(); ++slot) {
          SequencerPanel *seq = context.getSequencer(slot);
          if (seq)
            seq->isPlaying.store(playing);
        }
      }
      return;
    }

    // Heavy UI: only at ~15 Hz when runtime (saves 10–15% CPU)
    if (fullUpdate) {
      refreshUndoRedoButtons();
    }

    auto *perfPanel = ui->performancePanel.get();

    // 0. SEQ indicator (full only)
    if (fullUpdate &&
        context.sequencerActivityPending.exchange(false,
                                                  std::memory_order_relaxed) &&
        ui->logPanel)
      ui->logPanel->signalLegend.pulse(SignalPathLegend::ENG);

    // 1. Drain Logs (full only)
    if (fullUpdate) {
      int logsProcessed = 0;
      if (context.midiRouter && ui->logPanel && ui->winLog &&
          ui->winLog->isVisible()) {
        context.midiRouter->logBuffer.process(
            [this, &logsProcessed](const LogEntry &e) {
              if (logsProcessed < 50 && ui && ui->logPanel) {
                ui->logPanel->logEntry(e);
                logsProcessed++;
              }
            });
      }
    }

    // 2. Visual Buffer (Track Grid) - playback critical, every frame
    if (context.midiRouter && perfPanel && ui->winEditor &&
        ui->winEditor->isVisible()) {
      context.midiRouter->visualBuffer.process(
          [this, perfPanel](const VisualEvent &e) {
            if (ui && ui->performancePanel.get() == perfPanel) {
              if (e.type == VisualEvent::Type::NoteOn)
                perfPanel->trackGrid.visualNoteOn(e.noteOrCC, e.channel);
              else if (e.type == VisualEvent::Type::NoteOff)
                perfPanel->trackGrid.visualNoteOff(e.noteOrCC, e.channel);
            }
          });
      if (perfPanel->trackGrid.isVisible())
        perfPanel->trackGrid.repaint();
    }

    // 3. Playhead - only when playing and editor visible; show pending seek
    // target when scrubbing with beat/bar link
    if (context.engine) {
      if (context.engine->getIsPlaying()) {
        double ppq = context.engine->getTicksPerQuarter();
        double beatToShow = context.engine->getCurrentBeat();
        if (context.engine->getIsQuantizedSeek()) {
          double pending = context.engine->getPendingSeekTarget();
          if (pending >= 0.0)
            beatToShow = pending; // Show scrub target until seek is applied at
                                  // next beat/bar
        } else {
          beatToShow = context.engine->getCurrentTick() / (ppq > 0 ? ppq : 1.0);
        }
        if (ppq > 0 && perfPanel && ui->winEditor && ui->winEditor->isVisible())
          perfPanel->updatePlayhead(beatToShow, ppq);

        ui->linkIndicator.setCurrentBeat(context.engine->getCurrentBeat(),
                                         context.engine->getQuantum());
      } else if (perfPanel) {
        perfPanel->trackGrid.showPlayhead = false;
      }
      if (fullUpdate && ui && context.engine && ui->winLfoGen &&
          ui->winLfoGen->isVisible())
        ui->lfoGeneratorPanel.setLfoPhase(context.engine->getLfoPhaseForUI());
      if (ui->winArp && ui->winArp->isVisible() && context.engine &&
          context.engine->getIsPlaying()) {
        double b = context.engine->getCurrentBeat();
        ui->arpPanel->setLivePhase((float)(b - std::floor(b)));
      }
    }

    // 3b. Undo/Redo button state (full only)
    if (fullUpdate) {
      {
        bool canU = context.undoManager.canUndo();
        bool canR = context.undoManager.canRedo();
        ui->btnUndo.setEnabled(canU);
        ui->btnRedo.setEnabled(canR);
        if (canU) {
          juce::String desc = context.undoManager.getUndoDescription();
          ui->btnUndo.setTooltip(
              desc.isNotEmpty() ? "Undo: " + desc : "Undo last edit (Ctrl+Z).");
        } else {
          ui->btnUndo.setTooltip("Undo last edit (Ctrl+Z).");
        }
        if (canR) {
          juce::String desc = context.undoManager.getRedoDescription();
          ui->btnRedo.setTooltip(desc.isNotEmpty() ? "Redo: " + desc
                                                   : "Redo (Ctrl+Y).");
        } else {
          ui->btnRedo.setTooltip("Redo (Ctrl+Y).");
        }
        ui->btnUndo.setColour(juce::TextButton::buttonColourId,
                              canU ? Theme::accent.darker(0.3f)
                                   : Theme::bgPanel.darker(0.2f));
        ui->btnRedo.setColour(juce::TextButton::buttonColourId,
                              canR ? Theme::accent.darker(0.3f)
                                   : Theme::bgPanel.darker(0.2f));
        ui->btnUndo.repaint();
        ui->btnRedo.repaint();
      }
    }

    // 4. Sequencer - playback critical, every frame; skip when window hidden
    // (performance)
    if (context.engine && ui->winSequencer && ui->winSequencer->isVisible()) {
      bool playing = context.engine->getIsPlaying();
      int step = context.engine->getCurrentStepIndex();
      for (int slot = 0; slot < context.getNumSequencerSlots(); ++slot) {
        SequencerPanel *seq = context.getSequencer(slot);
        if (seq) {
          seq->isPlaying.store(playing);
          seq->visualizeStep(step);
        }
      }
    }

    // 5. Panel visuals (full only)
    if (fullUpdate) {
      if (context.mixer && ui->winMixer && ui->winMixer->isVisible())
        context.mixer->updateVisuals();
      if (perfPanel && ui->winEditor && ui->winEditor->isVisible() &&
          perfPanel->spliceEditor.isVisible())
        perfPanel->spliceEditor.updateVisuals();
    }

    // 6. Transport/BPM/Link sync (full only)
    if (fullUpdate && context.engine) {
      bool isLink = context.engine->isLinkEnabled();
      bool isExt = context.engine->isExtSyncActive();
      int numPeers = context.engine->getNumPeers();

      if (ui->logPanel)
        ui->logPanel->setLinkPeers(numPeers);

      if (ui->transportPanel)
        ui->transportPanel->setNumLinkPeers(numPeers);
      {
        float syncQ = context.engine->getSyncQuality();
        juce::String tip = "Ableton Link: " + juce::String(numPeers) + " peer" +
                           (numPeers != 1 ? "s" : "");
        if (isLink && numPeers > 0)
          tip += " | Sync: " + juce::String((int)(syncQ * 100)) + "%";
        ui->linkIndicator.setTooltip(tip);
        ui->tempoSlider.setEnabled(!isExt);

        if (isLink)
          ui->lblBpm.setText("LINK", juce::dontSendNotification);
        else if (isExt)
          ui->lblBpm.setText("EXT", juce::dontSendNotification);
        else
          ui->lblBpm.setText("BPM", juce::dontSendNotification);

        juce::String playText;
        if (context.engine->getIsPlaying())
          playText = "PAUSE";
        else if (context.engine->getIsPaused())
          playText = "RESUME";
        else
          playText = "PLAY";
        if (ui->transportPanel->btnPlay.getButtonText() != playText) {
          ui->transportPanel->btnPlay.setButtonText(playText);
          ui->transportPanel->btnPlay.repaint();
          context.repaintCoordinator.markDirty(RepaintCoordinator::Dashboard);
        }

        if (!ui->tempoSlider.isMouseButtonDown()) {
          double currentBpm = context.engine->getBpm();
          if (std::abs(ui->tempoSlider.getValue() - currentBpm) > 0.1)
            ui->tempoSlider.setValue(currentBpm, juce::dontSendNotification);
        }

        ui->getStatusBar().setBpmAndTransport(context.engine->getBpm(),
                                              context.engine->getIsPlaying());
      }
    }
  } catch (const std::exception &e) {
    DebugLog::debugLog(juce::String("processUpdates exception: ") + e.what());
  } catch (...) {
    DebugLog::debugLog("processUpdates exception: unknown");
  }
}

void SystemController::bindControlPage(MainComponent &mainUI) {
  if (auto *cp = mainUI.controlPage.get()) {
    for (auto *c : cp->controls) {
      c->onAction = [this](juce::String addr, float val) {
        juce::String t = addr.trim();
        if (t.isEmpty())
          return;
        // OSC: send to user-defined address (e.g. /ctrls/1, /my/control)
        if (context.oscManager && context.oscManager->isConnected())
          context.oscManager->sendFloat(t, val);
        // MIDI: if address is "midi:cc:ch:num" or "cc:ch:num", also send MIDI
        // CC (val 0–1 → 0–127)
        if (context.midiRouter && (t.startsWithIgnoreCase("midi:cc:") ||
                                   t.startsWithIgnoreCase("cc:"))) {
          juce::String rest =
              t.startsWithIgnoreCase("midi:cc:")
                  ? t.fromFirstOccurrenceOf(":", false, false)
                        .fromFirstOccurrenceOf(":", false, false) // "1:74"
                  : t.fromFirstOccurrenceOf(":", false, false);   // "1:74"
          int ch = rest.upToFirstOccurrenceOf(":", false, false).getIntValue();
          int cc = rest.fromFirstOccurrenceOf(":", false, false).getIntValue();
          if (ch >= 1 && ch <= 16 && cc >= 0 && cc <= 127) {
            float norm = juce::jlimit(0.0f, 1.0f, val);
            context.midiRouter->handleCC(ch, cc, norm,
                                         BridgeEvent::Source::UserInterface);
          }
        }
      };
    }
    cp->onXYPadChanged = [this](float x, float y) {
      if (context.midiRouter) {
        context.midiRouter->handleCC(1, 74, x,
                                     BridgeEvent::Source::UserInterface);
        context.midiRouter->handleCC(1, 1, y,
                                     BridgeEvent::Source::UserInterface);
      }
      if (context.mappingManager) {
        context.mappingManager->setParameterValue("Main_X", x);
        context.mappingManager->setParameterValue("Main_Y", y);
      }
    };
    cp->onMorphChanged = [this](float val) {
      if (context.oscManager)
        context.oscManager->sendFloat("/morph", val);
      if (context.mappingManager)
        context.mappingManager->setParameterValue("Main_Morph", val);
    };
  }
}

void SystemController::bindOscConfig(MainComponent &mainUI) {
  if (auto *oc = mainUI.oscConfigPanel.get()) {
    oc->onSchemaChanged = [this, oc] {
      // Update schema from UI editors
      // This requires mapping the text editors back to the schema object
      // For now, we just trigger an update
      if (context.oscManager) {
        // Logic to scrape values from oc->eOUTn etc. into oscSchema
        // This might need a helper in OscAddressConfig to export a struct
      }
    };
  }
}

bool SystemController::handleGlobalKeyPress(const juce::KeyPress &key) {
  if (ShortcutManager::instance().handleKeyPress(key))
    return true;

  if (key == juce::KeyPress::escapeKey) {
    if (ui && ui->isMidiLearnMode) {
      ui->isMidiLearnMode = false;
      ui->btnMidiLearn.setToggleState(false, juce::dontSendNotification);
      ui->btnMidiLearn.setButtonText("MIDI Learn");
      if (context.mappingManager)
        context.mappingManager->setLearnModeActive(false);
      ui->toggleMidiLearnOverlay(false);
      return true;
    }
    if (context.midiRouter) {
      context.midiRouter->sendPanic();
      if (ui)
        ui->onLogMessage("PANIC: All Notes Off sent.", false);
    }
    if (ui)
      ui->setView(MainComponent::AppView::Dashboard);
    return true;
  }
  return false;
}

void SystemController::handleFileDrop(const juce::StringArray &files) {
  if (!context.playbackController)
    return;
  // Use UI playlist (MainComponent owns it; context.playlist is not used)
  auto *pl = ui ? ui->playlist.get() : nullptr;
  if (!pl)
    return;

  // 1. Add ALL .mid/.midi to Playlist
  for (const auto &f : files) {
    juce::File file(f);
    if (file.hasFileExtension(".mid") || file.hasFileExtension(".midi")) {
      pl->addFile(file.getFullPathName());
    } else if (file.hasFileExtension(".json") && context.mappingManager) {
      if (!context.mappingManager->loadMappingsFromFile(file) && ui)
        ui->onLogMessage(
            "Could not load MIDI mappings from " + file.getFileName(), true);
    }
  }

  // 2. Load LAST .mid file and select in list
  if (!files.isEmpty()) {
    juce::File last(files[files.size() - 1]);
    if (last.hasFileExtension(".mid") || last.hasFileExtension(".midi")) {
      int idx = pl->files.indexOf(last.getFullPathName());
      if (idx >= 0) {
        pl->currentIndex = idx;
        pl->selectFileAtIndex(idx);
      }
      context.playbackController->loadMidiFile(last);
    }
  }
  pl->savePlaylist();
}

void SystemController::handleSliderTouch(const juce::String &paramID) {
  if (context.mappingManager && context.isMidiLearnMode) {
    context.mappingManager->setSelectedParameterForLearning(paramID);
  }
}

void SystemController::handleSliderRelease(const juce::String &paramID) {
  juce::ignoreUnused(paramID);
}

void SystemController::bindMacros(MainComponent &mainUI) {
  // Clear any previous right-click listeners
  for (auto &listener : controlMenuListeners) {
    if (listener->attachedTo)
      listener->attachedTo->removeMouseListener(listener.get());
  }
  controlMenuListeners.clear();

  // Bind Faders - send to OSC/MIDI and mapping manager (use override if set)
  int idx = 0;
  for (auto *f : mainUI.macroControls.faders) {
    int defaultCC = 30 + idx;
    juce::String pid = f->knob.getProperties()["paramID"];
    f->knob.getProperties().set("suppressContextMenu", true);
    f->onSlide = [this, pid, defaultCC](float val) {
      if (context.mappingManager)
        context.mappingManager->setParameterValue(pid, val);
      if (context.midiRouter) {
        auto ov = context.appState.getControlMessageOverride(pid);
        int ch = (ov.type != 0 && ov.channel >= 1) ? ov.channel
                 : (context.midiRouter->selectedChannel >= 1 &&
                    context.midiRouter->selectedChannel <= 16)
                     ? context.midiRouter->selectedChannel
                     : 1;
        if (ov.type == 1)
          context.midiRouter->handleCC(ch, ov.noteOrCC, val,
                                       BridgeEvent::Source::UserInterface);
        else if (ov.type == 3)
          context.midiRouter->handleBridgeEvent(BridgeEvent(
              EventType::PitchBend, EventSource::UserInterface, ch, 0, val));
        else
          context.midiRouter->handleCC(ch, defaultCC, val,
                                       BridgeEvent::Source::UserInterface);
      }
    };
    auto listener = std::make_unique<ControlMessageMenuListener>();
    listener->paramID = pid;
    listener->isButton = false;
    listener->onRightClick = [this, f](juce::String id, bool btn,
                                       juce::Component *c) {
      juce::PopupMenu m;
      m.addSectionHeader(
          f->knob.getLabelText().isEmpty() ? "Value" : f->knob.getLabelText());
      m.addItem("Set value...", [this, f] {
        juce::Slider *knob = &f->knob;
        auto *aw = new juce::AlertWindow(
            "Set value",
            "Enter value (" + juce::String(knob->getMinimum(), 2) + " to " +
                juce::String(knob->getMaximum(), 2) + "):",
            juce::MessageBoxIconType::QuestionIcon);
        aw->addTextEditor("val", knob->getTextFromValue(knob->getValue()),
                          "Value:");
        aw->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        aw->enterModalState(
            true, juce::ModalCallbackFunction::create([knob, aw](int result) {
              if (result == 1) {
                double v = knob->getValueFromText(
                    aw->getTextEditorContents("val").trim());
                knob->setValue(
                    juce::jlimit(knob->getMinimum(), knob->getMaximum(), v),
                    juce::sendNotification);
              }
              delete aw;
            }),
            false);
      });
      m.addSubMenu("Change message...", buildChangeMessageSubmenu(id, btn),
                   true);
      m.showMenuAsync(
          juce::PopupMenu::Options().withTargetComponent(c).withParentComponent(
              nullptr));
    };
    listener->attachedTo = &f->knob;
    f->knob.addMouseListener(listener.get(), false);
    controlMenuListeners.push_back(std::move(listener));
    ++idx;
  }

  // Bind Buttons - send to OSC/MIDI and mapping manager (use override if set)
  idx = 0;
  for (auto *b : mainUI.macroControls.buttons) {
    int defaultNote = 60 + idx;
    juce::String pid = b->btn.getProperties()["paramID"];
    b->onTrigger = [this, pid, defaultNote](bool state) {
      if (context.mappingManager)
        context.mappingManager->setParameterValue(pid, state ? 1.0f : 0.0f);
      if (context.midiRouter) {
        auto ov = context.appState.getControlMessageOverride(pid);
        int ch = (ov.type != 0 && ov.channel >= 1) ? ov.channel
                 : (context.midiRouter->selectedChannel >= 1 &&
                    context.midiRouter->selectedChannel <= 16)
                     ? context.midiRouter->selectedChannel
                     : 1;
        int note = (ov.type == 2) ? ov.noteOrCC : defaultNote;
        if (state)
          context.midiRouter->handleNoteOn(ch, note, 1.0f, false, false,
                                           BridgeEvent::Source::UserInterface);
        else
          context.midiRouter->handleNoteOff(ch, note, 0.0f, false, false,
                                            BridgeEvent::Source::UserInterface);
      }
    };
    auto listener = std::make_unique<ControlMessageMenuListener>();
    listener->paramID = pid;
    listener->isButton = true;
    listener->onRightClick = [this](juce::String id, bool btn,
                                    juce::Component *c) {
      showControlMessageMenu(id, btn, c);
    };
    listener->attachedTo = &b->btn;
    b->btn.addMouseListener(listener.get(), false);
    controlMenuListeners.push_back(std::move(listener));
    ++idx;
  }

  // Mixer strips: right-click "Change message" on Vol, Pan, Send, On, Solo
  if (context.mixer) {
    for (auto *s : context.mixer->strips) {
      if (!s)
        continue;
      auto addControlMenu = [this](juce::Component &comp, juce::String pid,
                                   bool isBtn) {
        auto listener = std::make_unique<ControlMessageMenuListener>();
        listener->paramID = pid;
        listener->isButton = isBtn;
        listener->onRightClick = [this](juce::String id, bool btn,
                                        juce::Component *c) {
          showControlMessageMenu(id, btn, c);
        };
        listener->attachedTo = &comp;
        comp.addMouseListener(listener.get(), false);
        controlMenuListeners.push_back(std::move(listener));
      };
      addControlMenu(
          s->volSlider,
          s->volSlider.getProperties().getWithDefault("paramID", "").toString(),
          false);
      addControlMenu(
          s->panSlider,
          s->panSlider.getProperties().getWithDefault("paramID", "").toString(),
          false);
      addControlMenu(
          s->sendKnob,
          s->sendKnob.getProperties().getWithDefault("paramID", "").toString(),
          false);
      addControlMenu(
          s->btnActive,
          s->btnActive.getProperties().getWithDefault("paramID", "").toString(),
          true);
      addControlMenu(
          s->btnSolo,
          s->btnSolo.getProperties().getWithDefault("paramID", "").toString(),
          true);
    }
  }

  // Transport: right-click "Change message" on BPM, Play, Stop
  if (mainUI.transportPanel) {
    auto *t = mainUI.transportPanel.get();
    auto addTransportMenu = [this](juce::Component &comp, juce::String pid,
                                   bool isBtn) {
      auto listener = std::make_unique<ControlMessageMenuListener>();
      listener->paramID = pid;
      listener->isButton = isBtn;
      listener->onRightClick = [this](juce::String id, bool btn,
                                      juce::Component *c) {
        showControlMessageMenu(id, btn, c);
      };
      listener->attachedTo = &comp;
      comp.addMouseListener(listener.get(), false);
      controlMenuListeners.push_back(std::move(listener));
    };
    addTransportMenu(mainUI.tempoSlider,
                     mainUI.tempoSlider.getProperties()
                         .getWithDefault("paramID", "")
                         .toString(),
                     false);
    addTransportMenu(
        t->btnPlay,
        t->btnPlay.getProperties().getWithDefault("paramID", "").toString(),
        true);
    addTransportMenu(
        t->btnStop,
        t->btnStop.getProperties().getWithDefault("paramID", "").toString(),
        true);
  }
}

juce::PopupMenu
SystemController::buildChangeMessageSubmenu(juce::String paramID,
                                            bool isButton) {
  juce::PopupMenu sub;
  sub.addItem("Send MIDI CC", true, false,
              [this, paramID] { showControlMessageDialog(paramID, 1); });
  sub.addItem("Send MIDI Note", true, false,
              [this, paramID] { showControlMessageDialog(paramID, 2); });
  if (!isButton)
    sub.addItem("Send Pitch Bend", true, false,
                [this, paramID] { showControlMessageDialog(paramID, 3); });
  sub.addSeparator();
  sub.addItem("Reset to default", true, false, [this, paramID] {
    context.appState.clearControlMessageOverride(paramID);
    if (ui)
      ui->onLogMessage("Control \"" + paramID + "\" reset to default message.",
                       false);
  });
  return sub;
}

void SystemController::showControlMessageMenu(juce::String paramID,
                                              bool isButton,
                                              juce::Component *target) {
  if (!target)
    return;
  juce::PopupMenu m;
  m.addSubMenu("Change message...",
               buildChangeMessageSubmenu(paramID, isButton), true);
  m.showMenuAsync(juce::PopupMenu::Options()
                      .withTargetComponent(target)
                      .withParentComponent(nullptr));
}

void SystemController::showControlMessageDialog(juce::String paramID,
                                                int type) {
  juce::AlertWindow *w =
      new juce::AlertWindow("Set MIDI message",
                            type == 1   ? "Channel (1-16) and CC (0-127):"
                            : type == 2 ? "Channel (1-16) and Note (0-127):"
                                        : "Channel (1-16) for Pitch Bend:",
                            juce::AlertWindow::NoIcon);
  auto ov = context.appState.getControlMessageOverride(paramID);
  w->addTextEditor("channel", juce::String(juce::jlimit(1, 16, ov.channel)),
                   "Channel", false);
  if (type == 1 || type == 2)
    w->addTextEditor("value", juce::String(juce::jlimit(0, 127, ov.noteOrCC)),
                     type == 1 ? "CC number" : "Note number", false);
  w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
  w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  SystemController *self = this;
  w->enterModalState(
      true,
      juce::ModalCallbackFunction::create([w, self, paramID, type](int result) {
        if (result == 1) {
          int ch = juce::jlimit(
              1, 16, w->getTextEditorContents("channel").getIntValue());
          int val =
              (type == 1 || type == 2)
                  ? juce::jlimit(
                        0, 127, w->getTextEditorContents("value").getIntValue())
                  : 0;
          AppState::ControlMessageOverride o;
          o.type = type;
          o.channel = ch;
          o.noteOrCC = val;
          self->getContext().appState.setControlMessageOverride(paramID, o);
          if (MainComponent *u = self->getUi()) {
            juce::String msg = paramID + " -> ";
            if (type == 1)
              msg += "CC Ch" + juce::String(ch) + " CC" + juce::String(val);
            else if (type == 2)
              msg += "Note Ch" + juce::String(ch) + " Note" + juce::String(val);
            else
              msg += "Pitch Bend Ch" + juce::String(ch);
            u->onLogMessage(msg, false);
          }
        }
        delete w;
      }),
      true);
}

void SystemController::refreshConfigPanelFromBackend() {
  if (!ui || !ui->configPanel)
    return;
  auto *c = ui->configPanel.get();
  c->btnConnect.setToggleState(context.oscManager &&
                                   context.oscManager->isConnected(),
                               juce::dontSendNotification);
  c->btnThru.setToggleState(context.appState.getMidiThru(),
                            juce::dontSendNotification);
  // THRU enables MIDI clock; sync engine sendMidiClock
  if (context.engine)
    context.engine->sendMidiClock =
        (c->btnClock.getToggleState() || context.appState.getMidiThru());
  if (context.midiRouter) {
    c->btnBlockMidiOut.setToggleState(context.midiRouter->blockMidiOut,
                                      juce::dontSendNotification);
    c->btnSplit.setToggleState(context.midiRouter->splitMode,
                               juce::dontSendNotification);
  }
  c->cmbThreadingMode.setSelectedId(
      static_cast<int>(
          context.threadingConfig.mode.load(std::memory_order_relaxed)) +
          1,
      juce::dontSendNotification);
  int renderMode = context.appState.getRenderMode();
  if (renderMode >= 1 && renderMode <= 4)
    c->syncRenderModeTo(renderMode);
  juce::String backendName = context.appState.getGpuBackend();
  juce::StringArray backends = RenderBackend::getAvailableBackends();
  int bidx = backends.indexOf(backendName);
  if (bidx >= 0)
    c->cmbGpuBackend.setSelectedId(bidx + 1, juce::dontSendNotification);
  // Sync lookahead / bypass / default BPM from AppState
  double lookahead = context.appState.getNetworkLookahead();
  c->sliderLookahead.setValue(lookahead, juce::dontSendNotification);
  c->sliderSyncBuffer.setValue(lookahead, juce::dontSendNotification);
  bool bypass = context.appState.getLookaheadBypass();
  c->btnBypassLookahead.setToggleState(bypass, juce::dontSendNotification);
  c->btnLowLatency.setToggleState(bypass, juce::dontSendNotification);
  if (context.sequencer)
    c->btnForceGrid.setToggleState(
        context.sequencer->btnForceGrid.getToggleState(),
        juce::dontSendNotification);
  if (context.midiRouter)
    c->btnNoteQuantize.setToggleState(context.midiRouter->isQuantizationEnabled,
                                      juce::dontSendNotification);
  c->btnPerformanceMode.setToggleState(context.appState.getPerformanceMode(),
                                       juce::dontSendNotification);
}

void SystemController::refreshTransportFromBackend() {
  if (!ui || !ui->transportPanel)
    return;
  auto *t = ui->transportPanel.get();
  // THRU/EXT live in header only; sync engine clock from config/app state
  if (context.engine && ui->configPanel)
    context.engine->sendMidiClock =
        (ui->configPanel->btnClock.getToggleState() ||
         context.appState.getMidiThru());
  if (context.midiRouter) {
    t->btnBlock.setToggleState(context.midiRouter->blockMidiOut,
                               juce::dontSendNotification);
    t->btnSplit.setToggleState(context.midiRouter->splitMode,
                               juce::dontSendNotification);
  }
}

static juce::ValueTree captureWindowLayout(MainComponent *ui) {
  juce::ValueTree layout("WindowLayout");
  if (!ui)
    return layout;
  auto capture = [&](const juce::String &id, ModuleWindow *win) {
    if (!win)
      return;
    juce::ValueTree node("Win");
    node.setProperty("id", id, nullptr);
    node.setProperty("x", win->getX(), nullptr);
    node.setProperty("y", win->getY(), nullptr);
    node.setProperty("w", win->getWidth(), nullptr);
    node.setProperty("h", win->getHeight(), nullptr);
    node.setProperty("visible", win->isVisible(), nullptr);
    node.setProperty("folded", win->isFolded, nullptr);
    node.setProperty("unfoldedH", win->unfoldedHeight, nullptr);
    layout.addChild(node, -1, nullptr);
  };
  capture("Editor", ui->winEditor.get());
  capture("Mixer", ui->winMixer.get());
  capture("Sequencer", ui->winSequencer.get());
  capture("Playlist", ui->winPlaylist.get());
  capture("Log", ui->winLog.get());
  capture("Arp", ui->winArp.get());
  capture("Macros", ui->winMacros.get());
  capture("Chords", ui->winChords.get());
  capture("LFO Generator", ui->winLfoGen.get());
  if (ui->winControl.get())
    capture("Control", ui->winControl.get());
  return layout;
}

static void applyLayoutFromTree(MainComponent *ui,
                                const juce::ValueTree &layout) {
  if (!ui || !layout.isValid())
    return;
  auto apply = [](ModuleWindow *win, const juce::ValueTree &node) {
    if (!win)
      return;
    win->setBounds(node.getProperty("x"), node.getProperty("y"),
                   node.getProperty("w"), node.getProperty("h"));
    win->setVisible(node.getProperty("visible"));
    win->unfoldedHeight = node.getProperty("unfoldedH", 200);
    bool shouldFold = node.getProperty("folded", false);
    if (shouldFold != win->isFolded)
      win->toggleFold();
  };
  for (const auto &node : layout) {
    juce::String id = node.getProperty("id");
    if (id == "Editor")
      apply(ui->winEditor.get(), node);
    else if (id == "Mixer")
      apply(ui->winMixer.get(), node);
    else if (id == "Sequencer")
      apply(ui->winSequencer.get(), node);
    else if (id == "Playlist")
      apply(ui->winPlaylist.get(), node);
    else if (id == "Log")
      apply(ui->winLog.get(), node);
    else if (id == "Arp")
      apply(ui->winArp.get(), node);
    else if (id == "Macros")
      apply(ui->winMacros.get(), node);
    else if (id == "Chords")
      apply(ui->winChords.get(), node);
    else if (id == "LFO Generator")
      apply(ui->winLfoGen.get(), node);
    else if (id == "Control" && ui->winControl.get())
      apply(ui->winControl.get(), node);
  }
}

void SystemController::saveWindowLayout() {
  if (!ui)
    return;
  juce::ValueTree layout = captureWindowLayout(ui);
  auto xml = layout.createXml();
  if (xml) {
    context.appState.props->setValue("savedLayout", xml->toString());
    context.appState.props->saveIfNeeded();
  }
}

juce::String SystemController::getCurrentLayoutXml() const {
  juce::ValueTree layout = captureWindowLayout(ui);
  auto xml = layout.createXml();
  return xml ? xml->toString() : juce::String();
}

void SystemController::restoreWindowLayout() {
  if (!ui)
    return;
  juce::String name = context.appState.getCurrentLayoutName();
  if (name == "Minimal")
    applyLayoutPreset("Minimal");
  else
    resetWindowLayout(); // Full is the only other default
}

void SystemController::restoreLayoutFromXml(const juce::String &xmlStr) {
  if (!ui || xmlStr.isEmpty())
    return;
  auto xml = juce::parseXML(xmlStr);
  if (!xml)
    return;
  auto layout = juce::ValueTree::fromXml(*xml);
  if (!layout.isValid())
    return;
  applyLayoutFromTree(ui, layout);
}

void SystemController::applyLayoutPreset(const juce::String &name) {
  if (!ui)
    return;
  // Only two built-in layouts; never restore from saved XML.
  if (name == "Minimal") {
    // Minimal: Editor (left), OSC Log (top right), Playlist (bottom right)
    // only. All other modules hidden.
    const int topY = 68;
    const int contentH = 552; // 620 window - topY
    ui->winEditor->setVisible(true);
    ui->winEditor->setBounds(10, topY, 580, contentH);
    if (ui->winEditor->isFolded)
      ui->winEditor->toggleFold();
    ui->winLog->setVisible(true);
    ui->winLog->setBounds(600, topY, 268, 240);
    ui->winPlaylist->setVisible(true);
    ui->winPlaylist->setBounds(600, topY + 240, 268, contentH - 240);
    ui->winSequencer->setVisible(false);
    ui->winMixer->setVisible(false);
    ui->winArp->setVisible(false);
    ui->winMacros->setVisible(false);
    if (ui->winChords)
      ui->winChords->setVisible(false);
    if (ui->winLfoGen)
      ui->winLfoGen->setVisible(false);
    if (ui->winControl)
      ui->winControl->setVisible(false);
  } else if (name == "Full") {
    resetWindowLayout();
  }
  context.appState.setCurrentLayoutName(name);
  if (auto *w = ui->findParentComponentOfClass<juce::ResizableWindow>()) {
    if (name == "Minimal")
      w->setSize(920, 620);
    else
      w->setSize(1024, 768);
  }
  context.repaintCoordinator.markDirty(RepaintCoordinator::Dashboard);
}

void SystemController::resetWindowLayout() {
  if (!ui)
    return;
  // Default Full layout: 3×3 grid (same as MainComponent initial bounds).
  // Left: Log, Playlist, Mixer. Center: Editor, Sequencer, LFO. Right: Arp,
  // Chords, Macros.
  const int topY = 68;
  const int leftX = 10, leftW = 268;
  const int centerX = 288, centerW = 404;
  const int rightX = 702, rightW = 268;
  const int row1H = 180, row2H = 188, row3H = 203;

  // Row 1: OSC Log | Editor | Arpeggiator
  ui->winLog->setVisible(true);
  ui->winLog->setBounds(leftX, topY, leftW, row1H);
  ui->winEditor->setVisible(true);
  ui->winEditor->setBounds(centerX, topY, centerW, row1H);
  if (ui->winEditor->isFolded)
    ui->winEditor->toggleFold();
  ui->winArp->setVisible(true);
  ui->winArp->setBounds(rightX, topY, rightW, row1H);

  // Row 2: Playlist | Sequencer | Chords
  ui->winPlaylist->setVisible(true);
  ui->winPlaylist->setBounds(leftX, topY + row1H, leftW, row2H);
  ui->winSequencer->setVisible(true);
  ui->winSequencer->setBounds(centerX, topY + row1H, centerW, row2H);
  if (ui->winSequencer->isFolded)
    ui->winSequencer->toggleFold();
  if (ui->winChords) {
    ui->winChords->setVisible(true);
    ui->winChords->setBounds(rightX, topY + row1H, rightW, row2H);
  }

  // Row 3: Mixer | LFO Generator | Macros
  ui->winMixer->setVisible(true);
  ui->winMixer->setBounds(leftX, topY + row1H + row2H, leftW, row3H);
  if (ui->winMixer->isFolded)
    ui->winMixer->toggleFold();
  if (ui->winLfoGen) {
    ui->winLfoGen->setVisible(true);
    ui->winLfoGen->setBounds(centerX, topY + row1H + row2H, centerW, row3H);
  }
  ui->winMacros->setVisible(true);
  ui->winMacros->setBounds(rightX, topY + row1H + row2H, rightW, row3H);
  if (ui->winControl)
    ui->winControl->setVisible(
        false); // Control hidden by default; enable via Connections > Modules
  context.appState.setCurrentLayoutName("Full");
  if (auto *w = ui->findParentComponentOfClass<juce::ResizableWindow>())
    w->setSize(1024, 768);
  context.repaintCoordinator.markDirty(RepaintCoordinator::Dashboard);
}

// ==============================================================================
// LFO Patching Binding
// ==============================================================================
void SystemController::LfoPatchClickListener::mouseDown(
    const juce::MouseEvent &e) {
  if (!main || !e.mods.isLeftButtonDown() ||
      !main->lfoGeneratorPanel.isPatchingModeActive())
    return;
  // Ignore clicks on the LFO panel itself (user must click controls in other
  // modules)
  juce::Component *target = e.eventComponent;
  if (target && main->lfoGeneratorPanel.isParentOf(target))
    return;
  juce::Component *c = target;
  juce::String paramID;
  while (c) {
    paramID = c->getProperties().getWithDefault("paramID", "").toString();
    if (paramID.isNotEmpty())
      break;
    c = c->getParentComponent();
  }
  if (paramID.isEmpty())
    return;
  int slot = main->lfoGeneratorPanel.getSelectedSlot();
  if (slot < 0 || slot >= 4)
    return;
  auto &p = main->getLfoPatches();
  p.erase(std::remove_if(p.begin(), p.end(),
                         [slot](const auto &x) { return x.first == slot; }),
          p.end());
  p.push_back({slot, paramID});
  main->lfoGeneratorPanel.setPatchingHint("LFO " + juce::String(slot + 1) +
                                          " \u2192 " + paramID);
}

void SystemController::bindLfoPatching(MainComponent &mainUI) {
  auto &panel = mainUI.lfoGeneratorPanel;
  panel.onRequestPatchLfo = [this](int lfoIndex) {
    if (!ui)
      return;
    juce::PopupMenu m;
    m.addSectionHeader("Assign LFO " + juce::String(lfoIndex + 1) + " to");
    m.addItem("Macro Fader 1", true, true, [this, lfoIndex] {
      if (!ui)
        return;
      auto &p = ui->getLfoPatches();
      p.erase(std::remove_if(
                  p.begin(), p.end(),
                  [lfoIndex](const auto &x) { return x.first == lfoIndex; }),
              p.end());
      p.push_back({lfoIndex, "Macro_Fader_1"});
      ui->lfoGeneratorPanel.setPatchingHint(
          "LFO " + juce::String(lfoIndex + 1) + " → Macro Fader 1");
    });
    m.addItem("Macro Fader 2", true, true, [this, lfoIndex] {
      if (!ui)
        return;
      auto &p = ui->getLfoPatches();
      p.erase(std::remove_if(
                  p.begin(), p.end(),
                  [lfoIndex](const auto &x) { return x.first == lfoIndex; }),
              p.end());
      p.push_back({lfoIndex, "Macro_Fader_2"});
      ui->lfoGeneratorPanel.setPatchingHint(
          "LFO " + juce::String(lfoIndex + 1) + " → Macro Fader 2");
    });
    m.addItem("Macro Fader 3", true, true, [this, lfoIndex] {
      if (!ui)
        return;
      auto &p = ui->getLfoPatches();
      p.erase(std::remove_if(
                  p.begin(), p.end(),
                  [lfoIndex](const auto &x) { return x.first == lfoIndex; }),
              p.end());
      p.push_back({lfoIndex, "Macro_Fader_3"});
      ui->lfoGeneratorPanel.setPatchingHint(
          "LFO " + juce::String(lfoIndex + 1) + " → Macro Fader 3");
    });
    m.addItem("Macro Button 1", true, true, [this, lfoIndex] {
      if (!ui)
        return;
      auto &p = ui->getLfoPatches();
      p.erase(std::remove_if(
                  p.begin(), p.end(),
                  [lfoIndex](const auto &x) { return x.first == lfoIndex; }),
              p.end());
      p.push_back({lfoIndex, "Macro_Btn_1"});
      ui->lfoGeneratorPanel.setPatchingHint(
          "LFO " + juce::String(lfoIndex + 1) + " → Macro Btn 1");
    });
    m.addItem("Macro Button 2", true, true, [this, lfoIndex] {
      if (!ui)
        return;
      auto &p = ui->getLfoPatches();
      p.erase(std::remove_if(
                  p.begin(), p.end(),
                  [lfoIndex](const auto &x) { return x.first == lfoIndex; }),
              p.end());
      p.push_back({lfoIndex, "Macro_Btn_2"});
      ui->lfoGeneratorPanel.setPatchingHint(
          "LFO " + juce::String(lfoIndex + 1) + " → Macro Btn 2");
    });
    m.addItem("Macro Button 3", true, true, [this, lfoIndex] {
      if (!ui)
        return;
      auto &p = ui->getLfoPatches();
      p.erase(std::remove_if(
                  p.begin(), p.end(),
                  [lfoIndex](const auto &x) { return x.first == lfoIndex; }),
              p.end());
      p.push_back({lfoIndex, "Macro_Btn_3"});
      ui->lfoGeneratorPanel.setPatchingHint(
          "LFO " + juce::String(lfoIndex + 1) + " → Macro Btn 3");
    });
    m.addItem("Transport BPM", true, true, [this, lfoIndex] {
      if (!ui)
        return;
      auto &p = ui->getLfoPatches();
      p.erase(std::remove_if(
                  p.begin(), p.end(),
                  [lfoIndex](const auto &x) { return x.first == lfoIndex; }),
              p.end());
      p.push_back({lfoIndex, "Transport_BPM"});
      ui->lfoGeneratorPanel.setPatchingHint(
          "LFO " + juce::String(lfoIndex + 1) + " → Transport BPM");
    });
    m.addSeparator();
    m.addItem("Unpatch (remove assignment)", true, true, [this, lfoIndex] {
      if (!ui)
        return;
      auto &p = ui->getLfoPatches();
      p.erase(std::remove_if(
                  p.begin(), p.end(),
                  [lfoIndex](const auto &x) { return x.first == lfoIndex; }),
              p.end());
      ui->lfoGeneratorPanel.setPatchingHint(
          "Connect LFO to a control via + on each LFO.");
    });
    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetComponent(&ui->lfoGeneratorPanel)
                        .withParentComponent(nullptr)
                        .withStandardItemHeight(24));
  };
  panel.onLfoParamsChanged = [this](float freq, float depth, int waveform,
                                    float attack, float decay, float sustain,
                                    float release) {
    if (context.engine) {
      context.engine->setLfoFrequency(freq);
      context.engine->setLfoDepth(depth);
      context.engine->setLfoWaveform(waveform);
      context.engine->setLfoEnvelope(attack, decay, sustain, release);
    }
  };

  // Patching mode: left-click any control (mixer, macros, transport, etc.) to
  // assign LFO to it
  lfoPatchClickListener = std::make_unique<LfoPatchClickListener>();
  lfoPatchClickListener->main = ui;
  mainUI.addMouseListener(lfoPatchClickListener.get(), true);
}

// ==============================================================================
// OSC / Log Binding
// ==============================================================================
void SystemController::bindOscLog(MainComponent &) {
  if (context.oscManager)
    context.oscManager->onLog = [this](const juce::String &msg, bool err) {
      if (ui)
        ui->onLogMessage(msg, err);
    };
  if (context.midiRouter)
    context.midiRouter->onLog = [this](const juce::String &msg, bool err) {
      if (ui)
        ui->onLogMessage(msg, err);
    };
  LogService::instance().onLogEntry = [this](const juce::String &msg,
                                             bool isError) {
    juce::MessageManager::callAsync([this, msg, isError]() {
      if (ui && ui->logPanel)
        ui->logPanel->log(msg, isError);
    });
  };
}

// ==============================================================================
// Playback Controller Binding
// ==============================================================================
void SystemController::bindPlaybackController(MainComponent &mainUI) {
  if (!context.playbackController)
    return;
  context.playbackController->setTrackGrid(
      mainUI.performancePanel ? &mainUI.performancePanel->trackGrid : nullptr);
  context.playbackController->setPlaylist(mainUI.playlist.get());
  context.playbackController->setMixer(context.mixer.get());
  context.playbackController->setSequencer(context.sequencer.get());
  context.playbackController->setScheduler(context.midiScheduler.get());
  context.playbackController->onBpmUpdate = [this](double bpm) {
    if (ui && ui->transportPanel)
      ui->tempoSlider.setValue(bpm, juce::dontSendNotification);
  };
  context.playbackController->onLengthUpdate = [this](double beats) {
    if (ui && ui->performancePanel)
      ui->performancePanel->timeline.setTotalLength(beats);
  };
  context.playbackController->onLog = [this](const juce::String &msg,
                                             bool err) {
    if (ui)
      ui->onLogMessage(msg, err);
  };
  context.playbackController->onReset = [this]() {
    if (ui && ui->performancePanel)
      ui->performancePanel->syncNotesToPlayView();
  };
}

// ==============================================================================
// Chord Generator Binding
// ==============================================================================
void SystemController::bindChordGenerator(MainComponent &mainUI) {
  if (auto *chordGen = mainUI.chordPanel.get()) {
    chordGen->onChordTriggered = [this,
                                  chordGen](int root,
                                            const std::vector<int> &intervals,
                                            float vel) {
      if (!context.midiRouter)
        return;
      int ch = chordGen->getChordOutputChannel();
      ch = juce::jlimit(1, 16, ch);
      for (int offset : intervals) {
        int note = juce::jlimit(0, 127, root + offset);
        context.midiRouter->handleNoteOn(ch, note, vel, false, false,
                                         BridgeEvent::Source::UserInterface);
      }
    };

    chordGen->onChordReleased = [this,
                                 chordGen](int root,
                                           const std::vector<int> &intervals) {
      if (!context.midiRouter)
        return;
      int ch = chordGen->getChordOutputChannel();
      ch = juce::jlimit(1, 16, ch);
      for (int offset : intervals) {
        int note = juce::jlimit(0, 127, root + offset);
        context.midiRouter->handleNoteOff(ch, note, 0.0f, false, false,
                                          BridgeEvent::Source::UserInterface);
      }
    };
  }
}

void SystemController::bindShortcuts(MainComponent &) {
  auto &shortcuts = ShortcutManager::instance();

  shortcuts.setAction("view.shortcuts", [this] {
    if (ui)
      showShortcutsPanel(*ui);
  });

  shortcuts.setAction("transport.play", [this] {
    if (transportViewModel)
      transportViewModel->togglePlay();
  });

  shortcuts.setAction("transport.stop", [this] {
    if (transportViewModel)
      transportViewModel->stop();
  });

  shortcuts.setAction("edit.undo", [this] {
    if (context.undoManager.canUndo())
      context.undoManager.undo();
  });

  shortcuts.setAction("edit.redo", [this] {
    if (context.undoManager.canRedo())
      context.undoManager.redo();
  });

  shortcuts.setAction("note.octaveUp", [this] {
    if (ui && ui->performancePanel && ui->performancePanel->onOctaveShift)
      ui->performancePanel->onOctaveShift(1);
  });

  shortcuts.setAction("note.octaveDown", [this] {
    if (ui && ui->performancePanel && ui->performancePanel->onOctaveShift)
      ui->performancePanel->onOctaveShift(-1);
  });

  shortcuts.setAction("view.zoomIn", [this] {
    if (!ui)
      return;
    float s = juce::jlimit(0.5f, 2.0f, ui->getStatusBar().getScale() + 0.1f);
    ui->getStatusBar().setScale(s, true);
  });

  shortcuts.setAction("view.zoomOut", [this] {
    if (!ui)
      return;
    float s = juce::jlimit(0.5f, 2.0f, ui->getStatusBar().getScale() - 0.1f);
    ui->getStatusBar().setScale(s, true);
  });
}

void SystemController::showShortcutsPanel(MainComponent &mainUI) {
  auto panel = std::make_unique<ShortcutsPanel>();
  juce::Rectangle<int> anchor = mainUI.getScreenBounds();
  if (auto *h = mainUI.headerPanel.get())
    anchor = h->getScreenBounds().withX(h->getScreenX() - 20);
  else
    anchor = anchor.withWidth(1).withX(anchor.getX() + 20);
  juce::CallOutBox::launchAsynchronously(std::move(panel), anchor, &mainUI);
}
