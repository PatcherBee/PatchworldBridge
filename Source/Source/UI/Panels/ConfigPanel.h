
/*
  ==============================================================================
    Source/UI/Panels/ConfigPanel.h
    Role: Network and MIDI device configuration
  ==============================================================================
*/
#pragma once
#include "../../Network/LocalAddresses.h"
#include "../Audio/OscTypes.h"
#include "../Core/AppState.h"
#include "../Core/Constants.h"
#include "../ControlHelpers.h"
#include "../Panels/ConfigControls.h"
#include "../Fonts.h"
#include "../RenderBackend.h"
#include "../RenderConfig.h"
#include "../Theme.h"
#include "../Widgets/Indicators.h"
#include "MidiPortsTablePanel.h"
#include <algorithm>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

class ConfigPanel : public juce::Component, public juce::AsyncUpdater, public juce::Timer {
public:
  // Manual Scan: list Bluetooth MIDI and controller-like devices (Xbox/PS may appear as HID, not MIDI on Windows)
  void scanBluetoothMidi() {
    auto devices = juce::MidiInput::getAvailableDevices();
    juce::String btFound;
    juce::String controllerFound;
    for (auto &d : devices) {
      juce::String name = d.name;
      if (name.containsIgnoreCase("Bluetooth") || name.containsIgnoreCase("BT ")
          || name.containsIgnoreCase("BLE") || name.containsIgnoreCase("Bluetooth LE")) {
        if (btFound.isNotEmpty()) btFound += ", ";
        btFound += name;
      }
      if (name.containsIgnoreCase("Xbox") || name.containsIgnoreCase("PlayStation")
          || name.containsIgnoreCase("Controller") || name.containsIgnoreCase("Wireless Controller")) {
        if (controllerFound.isNotEmpty()) controllerFound += ", ";
        controllerFound += name;
      }
    }
    juce::String status;
    if (btFound.isNotEmpty())
      status = "BT MIDI: " + btFound + ". Enable in MIDI In above.";
    if (controllerFound.isNotEmpty())
      status += juce::String(status.isNotEmpty() ? " " : "") + "Controllers: " + controllerFound + ".";
    if (status.isEmpty())
      status = "No BT MIDI in list. Click \"Pair Bluetooth MIDI...\" then pair in the opened window; click Scan or MIDI In after. Gamepads: use Enable Gamepad Input below.";
    setBluetoothMidiStatus(status);
    updateMidiButtonLabels();
  }
  // ----------------------------------------
  // 1. Define the Callback (Public)
  std::function<void(int)> onThemeChanged;
  std::function<void(int)> onRenderModeChanged;
  std::function<void(const juce::String &)> onGpuBackendChanged;
  std::function<void(bool)> onPerfModeChanged;
  std::function<void(juce::String)> onSaveProfileRequested;
  std::function<void(int)> onRtpModeChanged; // 0=Off, 1=Driver, 2=Internal
  std::function<void(float, float, int)> onLfoChanged; // freq, depth, waveform
  std::function<void(bool)> onSplitToggle;
  std::function<void(juce::String)> onInputToggle;
  std::function<void(juce::String)> onOutputToggle;
  std::function<void()> onBluetoothMidiPair;     // Bluetooth MIDI pairing
  std::function<void(bool)> onGamepadEnable;     // Enable/disable gamepad
  std::function<void(float)> onGamepadDeadzone;  // Gamepad deadzone setting
  std::function<void(float)> onGamepadSensitivity; // Axis sensitivity
  std::function<void(int)> onGamepadControllerType; // 0=Xbox, 1=PS, 2=Wii
  std::function<void(bool)> onDiagToggleChanged; // Show/Hide HUD
  std::function<void()> onResetTourRequested;    // Reset setup wizard
  std::function<void()> onLayoutResetRequested;  // Reset window layout
  /** Open the Help text window (troubleshooting and usage). Called from Connections > Help or Config > Help button. */
  std::function<void()> onOpenHelpRequested;
  std::function<void(bool)> onLookaheadBypassChanged;
  std::function<void(bool)> onMulticastToggle;
  std::function<void(bool)> onZeroConfigToggle;
  std::function<void(double)> onLatencyChange;
  std::function<void(double)> onClockOffsetChange;
  std::function<void(juce::String)> onClockSourceChanged;
  /** 0=Single, 1=MultiCore, 2=Adaptive. Worker pool size takes effect on next launch. */
  std::function<void(int)> onThreadingModeChanged;

  /** Default BPM for new sessions / reset (20–300). */
  std::function<void(double)> onDefaultBpmChanged;

  std::function<bool(juce::String)> isInputEnabled;
  std::function<bool(juce::String)> isOutputEnabled;

  /** Return per-device options (Track, Sync, Remote, MPE) for MIDI In/Out menus. */
  std::function<AppState::MidiDeviceOptions(bool isInput, juce::String deviceId)>
      getMidiDeviceOptions;
  std::function<void(bool isInput, juce::String deviceId,
                     const AppState::MidiDeviceOptions &)> setMidiDeviceOptions;

  // NEW: Schema Callback
  std::function<void(const OscNamingSchema &)>
      onSchemaUpdated; // We pass the Full Schema

  /** Sync render mode display (e.g. after Reset to defaults). mode: 1=Eco, 2=Pro, 3=Software. */
  void syncRenderModeTo(int mode) {
    int id = juce::jlimit(1, 4, mode);
    currentRenderMode = (id == 2) ? (int)RenderConfig::OpenGL_Perf : 1;
    cmbRenderMode.setSelectedId(id, juce::dontSendNotification);
  }

  /** Sync GPU backend combo to actual backend (e.g. after Vulkan attach fails and we fall back to OpenGL). */
  void syncGpuBackendTo(const juce::String& backendName) {
    juce::StringArray backends = RenderBackend::getAvailableBackends();
    int idx = backends.indexOf(backendName);
    if (idx >= 0)
      cmbGpuBackend.setSelectedId(idx + 1, juce::dontSendNotification);
  }

  /** Show and expand the OSC Addresses section (for "OSC Addresses" button / menu). */
  void showOscAddressesSection() {
    btnOscAdvanced.setToggleState(true, juce::dontSendNotification);
    btnOscAdvanced.setButtonText("Hide OSC Addresses <");
    oscAddresses.setVisible(true);
    oscAddresses.addressesVisible = true;
    resized();
  }

  ConfigPanel() {
    // Input/Output menus moved to HeaderPanel

    // ... existing setup ...

    // 2. Trigger the callback when ComboBox changes
    cmbTheme.onChange = [this] {
      if (onThemeChanged) {
        // Pass the selected ID (1, 2, 3...) to the listener
        onThemeChanged(cmbTheme.getSelectedId());
      }
    };
    // Title
    addAndMakeVisible(lblTitle);
    lblTitle.setText("CONFIGURATION", juce::dontSendNotification);
    lblTitle.setFont(Fonts::headerLarge());
    lblTitle.setColour(juce::Label::textColourId, Theme::accent);

    // Profile & Theme Section
    addAndMakeVisible(grpTheme);
    grpTheme.setText("Themes & Profiles");

    addAndMakeVisible(lblTheme);
    lblTheme.setText("Theme:", juce::dontSendNotification);
    addAndMakeVisible(cmbTheme);
    for (int i = 1; i <= ThemeManager::getNumThemes(); ++i)
      cmbTheme.addItem(ThemeManager::getThemeName(i), i);
    cmbTheme.setSelectedId(Theme::currentThemeId, juce::dontSendNotification);

    addAndMakeVisible(lblMidiMap);
    lblMidiMap.setText("MIDI Map:", juce::dontSendNotification);
    addAndMakeVisible(cmbMidiMap);

    addAndMakeVisible(btnImportMap);
    btnImportMap.setButtonText("Import JSON");
    addAndMakeVisible(btnExportMap);
    btnExportMap.setButtonText("Export JSON");

    addAndMakeVisible(btnResetMaps);
    btnResetMaps.setButtonText("Reset All Mappings");
    btnResetMaps.setColour(juce::TextButton::buttonOnColourId,
                           juce::Colours::red);

    // --- App / General Section (Default BPM removed from Config; use Transport or file) ---
    addAndMakeVisible(grpApp);
    grpApp.setText("App / General");
    addAndMakeVisible(lblCtrlProfile);
    lblCtrlProfile.setText("Controller:", juce::dontSendNotification);
    addAndMakeVisible(cmbCtrlProfile);
    addAndMakeVisible(lblRenderMode);
    addAndMakeVisible(btnSaveProfile);
    btnSaveProfile.setButtonText("Save Profile");
    btnSaveProfile.setTooltip("Save Current Profile");
    addAndMakeVisible(btnLoadProfile);
    btnLoadProfile.setButtonText("Load Profile");
    btnDeleteProfile.setTooltip("Delete the selected profile file.");
    addAndMakeVisible(btnDeleteProfile);
    btnDeleteProfile.setButtonText("Delete Profile");
    addAndMakeVisible(lblProfileStatus);
    lblProfileStatus.setText("", juce::dontSendNotification);

    addAndMakeVisible(grpSession);
    grpSession.setText("Session & Playback");

    // --- Network Group (Restored for side-by-side layout) ---
    addAndMakeVisible(grpNet);
    grpNet.setText("OSC Network Configuration");

    addAndMakeVisible(lblIp);
    lblIp.setText("Target IP:", juce::dontSendNotification);
    addAndMakeVisible(edIp);
    edIp.setText("127.0.0.1", juce::dontSendNotification);
    addAndMakeVisible(btnLocalIps);
    btnLocalIps.setButtonText("Local IPs...");
    btnLocalIps.setTooltip("Pick a local IPv4 address (this PC, headset, or other device on your network).");
    btnLocalIps.onClick = [this] {
      juce::StringArray addrs = getLocalIPv4Addresses();
      juce::PopupMenu m;
      m.addSectionHeader("Local IPv4 addresses");
      for (int i = 0; i < addrs.size(); ++i)
        m.addItem(addrs[i], [this, addr = addrs[i]] { edIp.setText(addr, juce::dontSendNotification); });
      if (addrs.isEmpty())
        m.addItem("(none found)", false);
      m.showMenuAsync(juce::PopupMenu::Options()
                          .withTargetComponent(&btnLocalIps)
                          .withParentComponent(getParentComponent()));
    };

    addAndMakeVisible(lblPOut);
    lblPOut.setText("Port Out:", juce::dontSendNotification);
    addAndMakeVisible(edPOut);
    edPOut.setText("3330", juce::dontSendNotification);

    addAndMakeVisible(lblPIn);
    lblPIn.setText("Port In:", juce::dontSendNotification);
    addAndMakeVisible(edPIn);
    edPIn.setText("5550", juce::dontSendNotification);

    addAndMakeVisible(btnConnect);
    btnConnect.setButtonText("Connect");
    btnConnect.setTooltip("Connect OSC to the IP and ports above. Disconnect before changing IP/ports.");
    addAndMakeVisible(btnOscAddresses);
    btnOscAddresses.setButtonText("OSC Addresses...");
    btnOscAddresses.setTooltip("Open OSC address editor. Right-click any address field to copy.");
    btnOscAddresses.onClick = [this] { showOscAddressesSection(); };

    addAndMakeVisible(btnIPv6);
    btnIPv6.setButtonText("Use IPv6");
    btnIPv6.setClickingTogglesState(true);

    addAndMakeVisible(btnMulticast);
    btnMulticast.setButtonText("Multicast");
    btnMulticast.setClickingTogglesState(true);
    btnMulticast.onClick = [this] {
      if (onMulticastToggle)
        onMulticastToggle(btnMulticast.getToggleState());
    };

    addAndMakeVisible(btnZeroConfig);
    btnZeroConfig.setButtonText("ZeroConf");
    btnZeroConfig.setClickingTogglesState(true);
    btnZeroConfig.setToggleState(true, juce::dontSendNotification);
    btnZeroConfig.onClick = [this] {
      bool zero = btnZeroConfig.getToggleState();
      edIp.setEnabled(!zero);
      if (zero)
        edIp.setText("Searching...", juce::dontSendNotification);
      if (onZeroConfigToggle)
        onZeroConfigToggle(zero);
    };

    addAndMakeVisible(btnLowLatency);
    btnLowLatency.setButtonText("Low Latency");
    btnLowLatency.setClickingTogglesState(true);
    btnLowLatency.onClick = [this] {
      if (onLookaheadBypassChanged)
        onLookaheadBypassChanged(btnLowLatency.getToggleState());
    };

    // MIDI Group
    addAndMakeVisible(grpIo);
    grpIo.setText("MIDI Configuration");

    addAndMakeVisible(lblIn);
    addAndMakeVisible(btnMidiIn);
    // FIXED: Explicit type extraction for safe lambda capture
    btnMidiIn.onClick = [this] {
      auto devices = juce::MidiInput::getAvailableDevices();
      juce::PopupMenu m;
      m.addSectionHeader("MIDI Inputs (Ableton-style: Track / Sync / Remote / MPE)");

      bool virtualActive = isInputEnabled ? isInputEnabled("VirtualKeyboard") : false;
      m.addItem("Virtual Keyboard", true, virtualActive, [this] {
        if (onInputToggle) onInputToggle("VirtualKeyboard");
      });
      m.addSeparator();

      for (const auto &d : devices) {
        juce::String name = d.name;
        juce::String id = d.identifier;
        bool enabled = isInputEnabled ? isInputEnabled(id) : false;
        AppState::MidiDeviceOptions opts = getMidiDeviceOptions ? getMidiDeviceOptions(true, id) : AppState::MidiDeviceOptions{};

        juce::PopupMenu sub;
        sub.addItem("Enable", true, enabled, [this, id] {
          if (onInputToggle) onInputToggle(id);
        });
        sub.addSeparator();
        sub.addItem("Track (notes/CC)", true, opts.track, [this, id, opts] {
          AppState::MidiDeviceOptions o = opts;
          o.track = !o.track;
          if (setMidiDeviceOptions) setMidiDeviceOptions(true, id, o);
        });
        sub.addItem("Sync (clock)", true, opts.sync, [this, id, opts] {
          AppState::MidiDeviceOptions o = opts;
          o.sync = !o.sync;
          if (setMidiDeviceOptions) setMidiDeviceOptions(true, id, o);
        });
        sub.addItem("Remote (transport)", true, opts.remote, [this, id, opts] {
          AppState::MidiDeviceOptions o = opts;
          o.remote = !o.remote;
          if (setMidiDeviceOptions) setMidiDeviceOptions(true, id, o);
        });
        sub.addItem("MPE", true, opts.mpe, [this, id, opts] {
          AppState::MidiDeviceOptions o = opts;
          o.mpe = !o.mpe;
          if (setMidiDeviceOptions) setMidiDeviceOptions(true, id, o);
        });
        m.addSubMenu(name + (enabled ? " \u2713" : ""), sub, true);
      }
      m.showMenuAsync(juce::PopupMenu::Options()
                          .withParentComponent(nullptr)
                          .withTargetComponent(&btnMidiIn)
                          .withStandardItemHeight(24));
    };

    addAndMakeVisible(btnMidiPorts);
    btnMidiPorts.setButtonText("MIDI Ports...");
    btnMidiPorts.setTooltip("Open table: Track / Sync / Remote / MPE per device.");
    btnMidiPorts.onClick = [this] {
      auto *panel = new MidiPortsTablePanel();
      panel->setCallbacks({
        isInputEnabled,
        isOutputEnabled,
        getMidiDeviceOptions,
        setMidiDeviceOptions,
        onInputToggle,
        onOutputToggle
      });
      panel->refresh();
      panel->setSize(460, 320);
      juce::CallOutBox::launchAsynchronously(
          std::unique_ptr<juce::Component>(panel),
          btnMidiPorts.getScreenBounds(),
          this);
    };

    addAndMakeVisible(lblOut);
    addAndMakeVisible(btnMidiOut);
    btnMidiOut.onClick = [this] {
      auto devices = juce::MidiOutput::getAvailableDevices();
      juce::PopupMenu m;
      m.addSectionHeader("MIDI Outputs (Track / Sync / Remote / MPE)");
      for (const auto &d : devices) {
        juce::String name = d.name;
        juce::String id = d.identifier;
        bool enabled = isOutputEnabled ? isOutputEnabled(id) : false;
        AppState::MidiDeviceOptions opts = getMidiDeviceOptions ? getMidiDeviceOptions(false, id) : AppState::MidiDeviceOptions{};

        juce::PopupMenu sub;
        sub.addItem("Enable", true, enabled, [this, id] {
          if (onOutputToggle) onOutputToggle(id);
        });
        sub.addSeparator();
        sub.addItem("Track (notes/CC)", true, opts.track, [this, id, opts] {
          AppState::MidiDeviceOptions o = opts;
          o.track = !o.track;
          if (setMidiDeviceOptions) setMidiDeviceOptions(false, id, o);
        });
        sub.addItem("Sync (clock)", true, opts.sync, [this, id, opts] {
          AppState::MidiDeviceOptions o = opts;
          o.sync = !o.sync;
          if (setMidiDeviceOptions) setMidiDeviceOptions(false, id, o);
        });
        sub.addItem("Remote (transport)", true, opts.remote, [this, id, opts] {
          AppState::MidiDeviceOptions o = opts;
          o.remote = !o.remote;
          if (setMidiDeviceOptions) setMidiDeviceOptions(false, id, o);
        });
        sub.addItem("MPE", true, opts.mpe, [this, id, opts] {
          AppState::MidiDeviceOptions o = opts;
          o.mpe = !o.mpe;
          if (setMidiDeviceOptions) setMidiDeviceOptions(false, id, o);
        });
        m.addSubMenu(name + (enabled ? " \u2713" : ""), sub, true);
      }
      m.showMenuAsync(juce::PopupMenu::Options()
                          .withParentComponent(nullptr)
                          .withTargetComponent(&btnMidiOut)
                          .withStandardItemHeight(24));
    };
    addAndMakeVisible(lblCh);
    addAndMakeVisible(cmbMidiCh);

    lblIn.setText("In:", juce::dontSendNotification);
    lblIn.setJustificationType(juce::Justification::centredRight);
    lblOut.setText("Out:", juce::dontSendNotification);
    lblOut.setJustificationType(juce::Justification::centredRight);
    lblCh.setText("CH:", juce::dontSendNotification);
    lblCh.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(lblClockSource);
    lblClockSource.setText("Clock Source:", juce::dontSendNotification);
    cmbClockSource.setTooltip("Choose which MIDI input drives tempo sync. \"Any\" merges all; pick one for stable sync.");
    addAndMakeVisible(cmbClockSource);
    cmbClockSource.addItem("Any / Merge (Not Recommended)", 1);
    cmbClockSource.onChange = [this] {
      if (onClockSourceChanged) {
        int id = cmbClockSource.getSelectedId();
        juce::String devId = (id <= 1) ? juce::String() : clockSourceIds[id - 2];
        onClockSourceChanged(devId);
      }
    };

    // Device lists populated externally
    // cmbMidiIn.addItem("None", 1);
    // cmbMidiOut.addItem("None", 1);

    cmbMidiCh.addItem("All", 17);
    for (int i = 1; i <= 16; ++i)
      cmbMidiCh.addItem(juce::String(i), i);
    cmbMidiCh.setSelectedId(17);

    // Toggle buttons
    addAndMakeVisible(btnThru);
    addAndMakeVisible(btnClock);
    addAndMakeVisible(btnSplit);
    addAndMakeVisible(btnMidiScaling);
    addAndMakeVisible(btnBlockMidiOut);

    btnThru.setTooltip("Forward MIDI input to output (soft thru).");
    btnThru.setButtonText("Thru");
    btnThru.setClickingTogglesState(true);
    btnThru.setColour(juce::TextButton::buttonOnColourId,
                      juce::Colours::green.darker(0.4f));

    btnClock.setButtonText("Clock");
    btnClock.setTooltip("Send MIDI clock from transport to selected outputs.");
    btnClock.setClickingTogglesState(true);
    btnClock.setColour(juce::TextButton::buttonOnColourId,
                       juce::Colours::orange.darker(0.1f));

    btnSplit.setTooltip("Split keyboard by note range to multiple channels (see Transport SPLIT).");
    btnSplit.setButtonText("Split");
    btnSplit.setClickingTogglesState(true);
    btnSplit.setColour(juce::TextButton::buttonOnColourId, juce::Colours::blue);
    btnSplit.onClick = [this] {
      if (onSplitToggle)
        onSplitToggle(btnSplit.getToggleState());
    };

    btnMidiScaling.setButtonText("MIDI Scale: 0-1");
    btnMidiScaling.setTooltip("Send CC/values as 0–1 float (off: 0–127 integer).");
    btnMidiScaling.setClickingTogglesState(true);

    btnBlockMidiOut.setTooltip("Block all MIDI output (same as Transport BLOCK).");
    btnBlockMidiOut.setButtonText("Block MIDI Out");
    btnBlockMidiOut.setClickingTogglesState(true);
    btnBlockMidiOut.setColour(juce::TextButton::buttonOnColourId,
                              juce::Colours::red.darker(0.5f));

    addAndMakeVisible(btnDirectInput);
    btnDirectInput.setButtonText("Direct Input (Fast)");
    btnDirectInput.setClickingTogglesState(true);
    btnDirectInput.setToggleState(
        true, juce::dontSendNotification); // Default to Fast
    btnDirectInput.setColour(juce::TextButton::buttonOnColourId,
                             juce::Colours::lime.darker(0.2f));

    addAndMakeVisible(btnPerformanceMode);
    btnPerformanceMode.setButtonText("12-Core Performance");
    btnPerformanceMode.setClickingTogglesState(true);
    btnPerformanceMode.setToggleState(true, juce::dontSendNotification);
    btnPerformanceMode.setColour(juce::ToggleButton::tickColourId,
                                 juce::Colours::cyan);

    // 1. Sync Buffer Slider (0ms to 50ms)
    addAndMakeVisible(sliderSyncBuffer);
    sliderSyncBuffer.setRange(0.0, 50.0, 0.5);
    sliderSyncBuffer.setDefaultValue(20.0);
    sliderSyncBuffer.setSliderStyle(juce::Slider::LinearBar);
    sliderSyncBuffer.setTextValueSuffix(" ms Lookahead");
    addAndMakeVisible(btnBypassLookahead);
    btnBypassLookahead.setButtonText("Zero-Latency Mode (Bypass Buffer)");
    btnBypassLookahead.onClick = [this] {
      updateGroups();
      if (onLookaheadBypassChanged)
        onLookaheadBypassChanged(btnBypassLookahead.getToggleState());
    };

    // 2. Performance Mode Toggle
    addAndMakeVisible(btnMultiCoreMode);
    btnMultiCoreMode.setButtonText("12-Core Pro Mode");

    // SETUP RENDER MODE COMBO (1=Eco, 2=Pro, 3=Software, 4=Auto)
    addAndMakeVisible(cmbRenderMode);
    cmbRenderMode.addItem("Eco Mode (30fps)", 1);
    cmbRenderMode.addItem("Pro Mode (60fps+)", 2);
    cmbRenderMode.addItem("Software (No GPU)", 3);
    cmbRenderMode.addItem("Auto (best available)", 4);
    cmbRenderMode.setSelectedId(4, juce::dontSendNotification);

    cmbRenderMode.onChange = [this] {
      if (onRenderModeChanged)
        onRenderModeChanged(cmbRenderMode.getSelectedId());
    };

    // GPU backend (OpenGL / Vulkan / Metal / Auto - current implementation uses OpenGL)
    addAndMakeVisible(lblGpuBackend);
    lblGpuBackend.setText("GPU backend:", juce::dontSendNotification);
    addAndMakeVisible(cmbGpuBackend);
    {
      juce::StringArray backends = RenderBackend::getAvailableBackends();
      for (int i = 0; i < backends.size(); ++i)
        cmbGpuBackend.addItem(backends[i], i + 1);
      cmbGpuBackend.setSelectedId(1, juce::dontSendNotification);
    }
    cmbGpuBackend.onChange = [this] {
      if (onGpuBackendChanged)
        onGpuBackendChanged(cmbGpuBackend.getText());
    };

    addAndMakeVisible(btnResetMixerOnLoad);
    btnResetMixerOnLoad.setButtonText("Reset Mixer on Load");
    btnResetMixerOnLoad.setClickingTogglesState(true);

    addAndMakeVisible(btnResetMixer);
    btnResetMixer.setButtonText("Reset Mixer Channels");
    btnResetMixer.setColour(juce::TextButton::buttonOnColourId,
                            juce::Colours::red); // Visual Warning

    addAndMakeVisible(btnResetLayout);
    btnResetLayout.setButtonText("Reset Window Layout");
    btnResetLayout.setTooltip("Restore default layout: Full 3×3 grid (Log, Playlist, Mixer | Editor, Sequencer, LFO | Arp, Chords, Macros).");
    btnResetLayout.setColour(juce::TextButton::buttonColourId,
                             juce::Colours::orchid.darker(0.3f));
    btnResetLayout.onClick = [this] {
      if (onLayoutResetRequested)
        onLayoutResetRequested();
    };

    addAndMakeVisible(btnResetTour);
    btnResetTour.setButtonText("Reset Setup Guide");
    btnResetTour.onClick = [this] {
      // 1. Notify MainComponent to reset the tour state
      if (onResetTourRequested)
        onResetTourRequested();

      // 2. Show an alert
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::InfoIcon, "Wizard Reset",
          "The setup guide will appear next time you launch the app.");
    };

    addAndMakeVisible(btnForceGrid);
    btnForceGrid.setButtonText("Force Grid (Auto)");
    btnForceGrid.setClickingTogglesState(true);

    addAndMakeVisible(btnNoteQuantize);
    btnNoteQuantize.setButtonText("Quantize Note (Link)");
    btnNoteQuantize.setClickingTogglesState(true);

    // Advanced Sync Group
    addAndMakeVisible(grpSync);
    grpSync.setText("Advanced Sync");

    addAndMakeVisible(lblLatency);
    addAndMakeVisible(sliderLatency);
    addAndMakeVisible(lblLookahead);
    addAndMakeVisible(sliderLookahead);
    addAndMakeVisible(lblClockOffset);
    addAndMakeVisible(sliderClockOffset);

    lblLatency.setText("Latency:", juce::dontSendNotification);
    sliderLatency.setRange(0.0, 500.0, 1.0);
    sliderLatency.setValue(0.0);
    sliderLatency.setDefaultValue(0.0);
    sliderLatency.setSliderStyle(juce::Slider::LinearBar);
    sliderLatency.onValueChange = [this] {
      if (onLatencyChange)
        onLatencyChange(sliderLatency.getValue());
    };

    lblLookahead.setText("Lookahead (ms):", juce::dontSendNotification);
    sliderLookahead.setRange(2.0, 50.0, 1.0);
    sliderLookahead.setValue(4.0);
    sliderLookahead.setDefaultValue(20.0);
    sliderLookahead.setSliderStyle(juce::Slider::LinearBar);

    lblClockOffset.setText("Clock Offset:", juce::dontSendNotification);
    sliderClockOffset.setRange(-100.0, 100.0, 1.0);
    sliderClockOffset.setValue(0.0);
    sliderClockOffset.setDefaultValue(0.0);
    sliderClockOffset.setSliderStyle(juce::Slider::LinearBar);
    sliderClockOffset.onValueChange = [this] {
      if (onClockOffsetChange)
        onClockOffsetChange(sliderClockOffset.getValue());
    };

    sliderLatency.setDoubleClickReturnValue(true, 0.0);
    sliderLookahead.setDoubleClickReturnValue(true, 20.0);
    sliderClockOffset.setDoubleClickReturnValue(true, 0.0);
    sliderSyncBuffer.setDoubleClickReturnValue(true, 20.0);

    addAndMakeVisible(btnCalibrate);
    btnCalibrate.setButtonText("Calibrate");
    btnCalibrate.setColour(juce::TextButton::buttonColourId,
                           juce::Colours::orange.darker(0.5f));

    // Link & Sync Group (Replaces old Sync group + new controls)
    addAndMakeVisible(grpLink);
    grpLink.setText("Ableton Link & Sync");

    addAndMakeVisible(btnLinkEnable);
    btnLinkEnable.setButtonText("Enable Link");
    btnLinkEnable.setClickingTogglesState(true);
    btnLinkEnable.setColour(juce::TextButton::buttonOnColourId,
                            juce::Colours::orange);

    addAndMakeVisible(btnStartStopSync);
    btnStartStopSync.setButtonText("Start/Stop Sync");
    btnStartStopSync.setClickingTogglesState(true);

    addAndMakeVisible(btnLockBpm);
    btnLockBpm.setButtonText("Lock BPM");
    btnLockBpm.setClickingTogglesState(true);
    btnLockBpm.setColour(juce::TextButton::buttonOnColourId,
                         juce::Colours::red.darker(0.3f));

    addAndMakeVisible(lblQuantum);
    lblQuantum.setText("Quantum:", juce::dontSendNotification);
    addAndMakeVisible(cmbQuantum);
    cmbQuantum.addItem("1 Beat", 1);
    cmbQuantum.addItem("2 Beats", 2);
    cmbQuantum.addItem("4 Beats", 3);
    cmbQuantum.addItem("8 Beats", 4);
    cmbQuantum.setSelectedId(3); // Default 4 beats

    addAndMakeVisible(lblLinkBpm);
    lblLinkBpm.setText("BPM (Link):", juce::dontSendNotification);
    addAndMakeVisible(sliderLinkBpm);
    sliderLinkBpm.setRange((double)Constants::kMinBpm, (double)Constants::kMaxBpm, 0.1);
    sliderLinkBpm.setDefaultValue(Constants::kDefaultBpm);
    sliderLinkBpm.setSliderStyle(juce::Slider::LinearBar);
    sliderLinkBpm.setTextValueSuffix(" bpm");
    sliderLinkBpm.setDoubleClickReturnValue(true, Constants::kDefaultBpm);

    // Advanced OSC Toggle
    addAndMakeVisible(btnOscAdvanced);
    btnOscAdvanced.setButtonText("Edit OSC Addresses >");
    btnOscAdvanced.setClickingTogglesState(true);
    btnOscAdvanced.onClick = [this] {
      bool show = btnOscAdvanced.getToggleState();
      oscAddresses.setVisible(show);
      oscAddresses.addressesVisible = show;
      btnOscAdvanced.setButtonText(show ? "Hide OSC Addresses <"
                                        : "Edit OSC Addresses >");
      resized(); // Triggers layout update
    };

    addAndMakeVisible(oscAddresses);
    oscAddresses.setVisible(false);

    // Wire up the OSC Update Callback
    oscAddresses.onSchemaChanged = [this] {
      triggerAsyncUpdate();
    };
    oscAddresses.onSchemaApplied = [this](const OscNamingSchema &schema) {
      if (onSchemaUpdated)
        onSchemaUpdated(schema);
    };

    // --- NEW: RTP-MIDI SECTION ---
    addAndMakeVisible(grpRtp);
    grpRtp.setText("Network MIDI (RTP)");

    addAndMakeVisible(btnRtpDriver);
    btnRtpDriver.setButtonText("Use OS Driver (Mac/rtpMIDI)");
    btnRtpDriver.setRadioGroupId(101);
    btnRtpDriver.setClickingTogglesState(true);

    addAndMakeVisible(btnRtpInternal);
    btnRtpInternal.setButtonText("Internal Server");
    btnRtpInternal.setRadioGroupId(101);
    btnRtpInternal.setClickingTogglesState(true);

    addAndMakeVisible(btnRtpOff);
    btnRtpOff.setButtonText("Off");
    btnRtpOff.setRadioGroupId(101);
    btnRtpOff.setClickingTogglesState(true);
    btnRtpOff.setToggleState(true,
                             juce::dontSendNotification); // Default Off

    // Wire Callbacks
    btnRtpDriver.onClick = [this] {
      if (onRtpModeChanged)
        onRtpModeChanged(1);
    };
    btnRtpInternal.onClick = [this] {
      if (onRtpModeChanged)
        onRtpModeChanged(2);
    };
    btnRtpOff.onClick = [this] {
      if (onRtpModeChanged)
        onRtpModeChanged(0);
    };

    // --- Threading (worker pool mode; size takes effect on next launch) ---
    addAndMakeVisible(grpThreading);
    grpThreading.setText("Worker Threads");
    addAndMakeVisible(lblThreadingWorkers);
    lblThreadingWorkers.setText("Mode:", juce::dontSendNotification);
    addAndMakeVisible(cmbThreadingMode);
    cmbThreadingMode.addItem("Single thread", 1);
    cmbThreadingMode.addItem("Multi-core (fixed)", 2);
    cmbThreadingMode.addItem("Adaptive (auto)", 3);
    cmbThreadingMode.setSelectedId(3, juce::dontSendNotification); // Default Adaptive
    cmbThreadingMode.onChange = [this] {
      if (onThreadingModeChanged)
        onThreadingModeChanged(cmbThreadingMode.getSelectedId() - 1);
    };

    // --- NEW: LFO MODULATION SECTION ---
    addAndMakeVisible(grpLfo);
    grpLfo.setText("Internal Modulation (LFO)");

    addAndMakeVisible(lblLfoFreq);
    lblLfoFreq.setText("Rate (Hz):", juce::dontSendNotification);
    addAndMakeVisible(sliderLfoFreq);
    sliderLfoFreq.setRange(0.01, 20.0, 0.01);
    sliderLfoFreq.setValue(1.0);
    sliderLfoFreq.setDefaultValue(1.0);
    sliderLfoFreq.setSliderStyle(juce::Slider::LinearBar);

    addAndMakeVisible(lblLfoDepth);
    lblLfoDepth.setText("Depth:", juce::dontSendNotification);
    addAndMakeVisible(sliderLfoDepth);
    sliderLfoDepth.setRange(0.0, 1.0, 0.01);
    sliderLfoDepth.setValue(0.5);
    sliderLfoDepth.setDefaultValue(0.5);
    sliderLfoDepth.setSliderStyle(juce::Slider::LinearBar);

    addAndMakeVisible(lblLfoWave);
    lblLfoWave.setText("Shape:", juce::dontSendNotification);
    addAndMakeVisible(cmbLfoWave);
    cmbLfoWave.addItem("Sine", 1);
    cmbLfoWave.addItem("Triangle", 2);
    cmbLfoWave.addItem("Saw", 3);
    cmbLfoWave.addItem("Square", 4);
    cmbLfoWave.addItem("Random", 5);
    cmbLfoWave.setSelectedId(1);

    auto lfoUpdate = [this] {
      if (onLfoChanged)
        onLfoChanged((float)sliderLfoFreq.getValue(),
                     (float)sliderLfoDepth.getValue(),
                     cmbLfoWave.getSelectedId() - 1);
    };

    sliderLfoFreq.onValueChange = lfoUpdate;
    sliderLfoDepth.onValueChange = lfoUpdate;
    sliderLfoFreq.setDoubleClickReturnValue(true, 1.0);
    sliderLfoDepth.setDoubleClickReturnValue(true, 0.5);
    cmbLfoWave.onChange = lfoUpdate;

    // --- NEW: INPUT DEVICES SECTION (Bluetooth MIDI + Gamepad) ---
    addAndMakeVisible(grpInputDevices);
    grpInputDevices.setText("Extended Input Devices");

    // Bluetooth MIDI
    addAndMakeVisible(btnBluetoothPair);
    btnBluetoothPair.setButtonText("Pair Bluetooth MIDI...");
    btnBluetoothPair.setTooltip("Open OS pairing (or Bluetooth settings on Windows). After pairing, click Scan or MIDI In to see the device.");
    btnBluetoothPair.setColour(juce::TextButton::buttonColourId,
                               juce::Colours::steelblue.darker(0.3f));
    btnBluetoothPair.onClick = [this] {
      if (onBluetoothMidiPair)
        onBluetoothMidiPair();
    };

    addAndMakeVisible(btnScanBluetooth);
    btnScanBluetooth.setButtonText("Scan");
    btnScanBluetooth.setTooltip("Refresh the list of Bluetooth MIDI and controller devices. Enable them in MIDI In above.");
    btnScanBluetooth.onClick = [this] { scanBluetoothMidi(); };

    addAndMakeVisible(lblBluetoothStatus);
    lblBluetoothStatus.setText("No BT MIDI devices connected",
                               juce::dontSendNotification);
    lblBluetoothStatus.setColour(juce::Label::textColourId,
                                 juce::Colours::grey);

    // Gamepad Controls
    addAndMakeVisible(btnGamepadEnable);
    btnGamepadEnable.setButtonText("Enable Gamepad Input");
    btnGamepadEnable.setTooltip("Enable gamepad input (not yet functional; polling stub only).");
    btnGamepadEnable.setClickingTogglesState(true);
    btnGamepadEnable.setColour(juce::TextButton::buttonOnColourId,
                               juce::Colours::limegreen.darker(0.2f));
    addAndMakeVisible(ledGamepad);
    addAndMakeVisible(lblGamepadStatus);
    lblGamepadStatus.setText("Gamepad: Not Connected",
                             juce::dontSendNotification);

    addAndMakeVisible(lblGamepadDeadzone);
    lblGamepadDeadzone.setText("Deadzone:", juce::dontSendNotification);
    addAndMakeVisible(btnShowDiag);
    btnShowDiag.setButtonText("Diagnostics HUD");
    btnShowDiag.setClickingTogglesState(true);

    addAndMakeVisible(sliderGamepadDeadzone);
    sliderGamepadDeadzone.setRange(0.0, 0.5, 0.01);
    sliderGamepadDeadzone.setValue(0.15);
    sliderGamepadDeadzone.setDefaultValue(0.15);
    sliderGamepadDeadzone.setSliderStyle(juce::Slider::LinearBar);
    sliderGamepadDeadzone.onValueChange = [this] {
      if (onGamepadDeadzone)
        onGamepadDeadzone((float)sliderGamepadDeadzone.getValue());
    };
    sliderGamepadDeadzone.setDoubleClickReturnValue(true, 0.15);

    addAndMakeVisible(lblGamepadSensitivity);
    lblGamepadSensitivity.setText("Sensitivity:", juce::dontSendNotification);
    addAndMakeVisible(sliderGamepadSensitivity);
    sliderGamepadSensitivity.setRange(0.2, 3.0, 0.05);
    sliderGamepadSensitivity.setValue(1.0);
    sliderGamepadSensitivity.setDefaultValue(1.0);
    sliderGamepadSensitivity.setSliderStyle(juce::Slider::LinearBar);
    sliderGamepadSensitivity.onValueChange = [this] {
      if (onGamepadSensitivity)
        onGamepadSensitivity((float)sliderGamepadSensitivity.getValue());
    };
    sliderGamepadSensitivity.setDoubleClickReturnValue(true, 1.0);

    addAndMakeVisible(lblGamepadController);
    lblGamepadController.setText("Controller:", juce::dontSendNotification);
    addAndMakeVisible(cmbGamepadController);
    cmbGamepadController.addItem("Xbox", 1);
    cmbGamepadController.addItem("PlayStation", 2);
    cmbGamepadController.addItem("Wii", 3);
    cmbGamepadController.setSelectedId(1, juce::dontSendNotification);
    cmbGamepadController.onChange = [this] {
      if (onGamepadControllerType)
        onGamepadControllerType(cmbGamepadController.getSelectedId() - 1);
    };

    btnGamepadEnable.onClick = [this] {
      updateGroups();
      if (onGamepadEnable)
        onGamepadEnable(btnGamepadEnable.getToggleState());
    };

    btnShowDiag.onClick = [this] {
      if (onDiagToggleChanged)
        onDiagToggleChanged(btnShowDiag.getToggleState());
    };

    // --- QOL: INPUT VALIDATION ---
    edPOut.setInputFilter(
        new juce::TextEditor::LengthAndCharacterRestriction(5, "0123456789"),
        true);
    edPIn.setInputFilter(
        new juce::TextEditor::LengthAndCharacterRestriction(5, "0123456789"),
        true);
    edIp.setInputFilter(
        new juce::TextEditor::LengthAndCharacterRestriction(15, "0123456789."),
        true);

    // Help Section (inside Config)
    addAndMakeVisible(grpHelp);
    addAndMakeVisible(lblHelpText);
    addAndMakeVisible(btnOpenHelp);
    grpHelp.setText("Help");
    lblHelpText.setFont(Fonts::body());
    lblHelpText.setColour(juce::Label::textColourId, Theme::text);
    lblHelpText.setJustificationType(juce::Justification::topLeft);
    lblHelpText.setText(
        "Patchworld Bridge — OSC/MIDI/Link bridge. Quick setup: Network (IP + ports, Connect), "
        "MIDI In/Out, then use Transport and Playlist. For troubleshooting and full usage, click below.",
        juce::dontSendNotification);
    btnOpenHelp.setButtonText("Open Help Guide");
    btnOpenHelp.onClick = [this] {
      if (onOpenHelpRequested)
        onOpenHelpRequested();
    };

    updateGroups();
    setBufferedToImage(true);
  }

  void updateGroups() {
    bool link = btnLinkEnable.getToggleState();
    sliderLinkBpm.setEnabled(link);
    cmbQuantum.setEnabled(link);
    btnStartStopSync.setEnabled(link);
    btnLockBpm.setEnabled(link);

    bool bypass = btnBypassLookahead.getToggleState();
    sliderSyncBuffer.setEnabled(!bypass);

    bool gp = btnGamepadEnable.getToggleState();
    sliderGamepadDeadzone.setEnabled(gp);
    sliderGamepadSensitivity.setEnabled(gp);
    cmbGamepadController.setEnabled(gp);
  }

  // Async Updater Callback for Debouncing
  void handleAsyncUpdate() override {
    // 1. Conflict Check visual
    oscAddresses.validateConflicts();

    // 2. Build full schema and send to engine
    if (onSchemaUpdated) {
      onSchemaUpdated(oscAddresses.getSchema());
    }
  }

  // NEW: Refresh the list of profiles in the dropdown
  void refreshProfileList(const juce::String &selectedName = "") {
    cmbCtrlProfile.clear();
    cmbCtrlProfile.addItem("- Select Profile -", 1);

    auto dir =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("PatchworldBridge")
            .getChildFile("ControllerProfiles");

    if (dir.exists()) {
      auto files = dir.findChildFiles(juce::File::findFiles, false, "*.json");
      int id = 2;
      for (auto &f : files) {
        juce::String name = f.getFileNameWithoutExtension();
        cmbCtrlProfile.addItem(name, id);
        if (name == selectedName) {
          cmbCtrlProfile.setSelectedId(id, juce::dontSendNotification);
        }
        id++;
      }
    }

    if (selectedName.isEmpty())
      cmbCtrlProfile.setSelectedId(1, juce::dontSendNotification);
  }

  // Refresh MIDI Devices in dropdowns (menus are built on click; this updates button labels)
  void refreshMidiDevices() {
    updateMidiButtonLabels();
  }

  /** Update MIDI In/Out button text to show device counts. Call when device list changes (e.g. disconnect). */
  void updateMidiButtonLabels() {
    auto ins = juce::MidiInput::getAvailableDevices();
    auto outs = juce::MidiOutput::getAvailableDevices();
    int nIn = ins.size();
    int nOut = outs.size();
    btnMidiIn.setButtonText(nIn > 0 ? "MIDI In (" + juce::String(nIn) + ")" : "MIDI In");
    btnMidiOut.setButtonText(nOut > 0 ? "MIDI Out (" + juce::String(nOut) + ")" : "MIDI Out");
  }

  void resized() override {
    auto b = getLocalBounds();
    if (b == lastLayoutBounds_)
      return;
    lastLayoutBounds_ = b;
    auto r = b.reduced(20); // More padding overall

    // Title
    lblTitle.setBounds(r.removeFromTop(40));
    r.removeFromTop(10);

    // Helper for rows - Increased height
    auto getRow = [&r]() { return r.removeFromTop(35); };

    // Theme Section
    addAndMakeVisible(grpTheme);
    auto themeArea = r.removeFromTop(160);
    grpTheme.setBounds(themeArea);
    auto theme = themeArea.reduced(15, 25);

    auto tRow1 = theme.removeFromTop(35);
    lblTheme.setBounds(tRow1.removeFromLeft(60));
    cmbTheme.setBounds(tRow1.removeFromLeft(150));

    auto tRow2 = theme.removeFromTop(35);
    lblMidiMap.setBounds(tRow2.removeFromLeft(80));
    cmbMidiMap.setBounds(tRow2.removeFromLeft(150));

    theme.removeFromTop(5);

    auto tRow3 = theme.removeFromTop(35);
    btnImportMap.setBounds(tRow3.removeFromLeft(100).reduced(2));
    btnExportMap.setBounds(tRow3.removeFromLeft(100).reduced(2));
    btnResetMaps.setBounds(tRow3.removeFromRight(140).reduced(2));

    r.removeFromTop(10);

    // App / General: controller profile, render mode, GPU backend inside one group
    auto appArea = r.removeFromTop(195);
    grpApp.setBounds(appArea);
    auto appInner = appArea.reduced(15, 25);

    auto cRow1 = appInner.removeFromTop(35);
    lblCtrlProfile.setBounds(cRow1.removeFromLeft(80));
    cmbCtrlProfile.setBounds(cRow1.removeFromLeft(200));

    appInner.removeFromTop(5);
    auto cRow2 = appInner.removeFromTop(35);
    btnSaveProfile.setBounds(cRow2.removeFromLeft(100).reduced(2));
    btnLoadProfile.setBounds(cRow2.removeFromLeft(100).reduced(2));
    btnDeleteProfile.setBounds(cRow2.removeFromLeft(100).reduced(2));
    lblProfileStatus.setBounds(cRow2.reduced(2));

    appInner.removeFromTop(5);
    auto cRow3 = appInner.removeFromTop(35);
    lblRenderMode.setText("Render:", juce::dontSendNotification);
    lblRenderMode.setBounds(cRow3.removeFromLeft(80));
    cmbRenderMode.setBounds(cRow3.removeFromLeft(180).reduced(2));

    appInner.removeFromTop(5);
    auto cRow3b = appInner.removeFromTop(35);
    lblGpuBackend.setBounds(cRow3b.removeFromLeft(80));
    cmbGpuBackend.setBounds(cRow3b.removeFromLeft(180).reduced(2));

    r.removeFromTop(10);

    // Network & MIDI Configuration Group (Side-by-Side)
    auto sectionRow = r.removeFromTop(200);

    // Split that row into two horizontal halves
    auto netArea =
        sectionRow.removeFromLeft((int)(sectionRow.getWidth() * 0.5f))
            .reduced(5);
    auto midiArea = sectionRow.reduced(5); // The remaining right side

    // Set the bounds for the "White Boxes" (GroupComponents)
    grpNet.setBounds(netArea);
    grpIo.setBounds(midiArea);

    // --- Position Network Controls Inside grpNet ---
    auto net = netArea.reduced(10, 20); // Inner padding
    lblIp.setBounds(net.removeFromTop(30));
    auto ipRow = net.removeFromTop(35);
    edIp.setBounds(ipRow.removeFromLeft(ipRow.getWidth() - 100).reduced(0, 2));
    btnLocalIps.setBounds(ipRow.reduced(2));

    auto portRow = net.removeFromTop(35);
    lblPOut.setBounds(portRow.removeFromLeft(58));
    edPOut.setBounds(portRow.removeFromLeft(60).reduced(2));
    lblPIn.setBounds(portRow.removeFromLeft(52));
    edPIn.setBounds(portRow.removeFromLeft(60).reduced(2));

    net.removeFromTop(5);
    auto connectRow = net.removeFromTop(30);
    btnConnect.setBounds(connectRow.removeFromLeft(120).reduced(2));
    btnOscAddresses.setBounds(connectRow.reduced(2));

    auto ipv6Row = net.removeFromTop(30);
    btnIPv6.setBounds(ipv6Row.reduced(2));

    net.removeFromTop(5);
    auto netOptRow = net.removeFromTop(30);
    btnMulticast.setBounds(
        netOptRow.removeFromLeft((int)(netOptRow.getWidth() * 0.5f)).reduced(2));
    btnZeroConfig.setBounds(netOptRow.reduced(2));

    net.removeFromTop(5);
    btnLowLatency.setBounds(net.removeFromTop(28));

    // --- Position MIDI Controls Inside grpIo ---
    auto io = midiArea.reduced(10, 20);

    auto ioRow1 = io.removeFromTop(35);
    lblIn.setBounds(ioRow1.removeFromLeft(60));
    btnMidiIn.setBounds(ioRow1.removeFromLeft(120));
    btnMidiPorts.setBounds(ioRow1.removeFromLeft(100).reduced(2));
    btnTestMidi.setBounds(ioRow1.removeFromLeft(50).reduced(2));

    io.removeFromTop(5);
    auto ioRow2 = io.removeFromTop(35);
    lblOut.setBounds(ioRow2.removeFromLeft(60));
    btnMidiOut.setBounds(ioRow2.removeFromLeft(150));

    io.removeFromTop(5);
    auto ioRow3 = io.removeFromTop(35);
    lblCh.setBounds(ioRow3.removeFromLeft(60));
    cmbMidiCh.setBounds(ioRow3.removeFromLeft(80));

    ioRow3.removeFromLeft(10);
    btnThru.setBounds(ioRow3.removeFromLeft(60));
    btnClock.setBounds(ioRow3.removeFromLeft(60));

    io.removeFromTop(5);
    auto clockRow = io.removeFromTop(35);
    lblClockSource.setBounds(clockRow.removeFromLeft(90));
    cmbClockSource.setBounds(clockRow.reduced(2));

    auto ioRow4 = io.removeFromTop(35);
    btnBlockMidiOut.setBounds(ioRow4.removeFromLeft(100).reduced(2));
    btnMidiScaling.setBounds(ioRow4.removeFromLeft(125).reduced(2));
    btnDirectInput.setBounds(ioRow4.removeFromLeft(125).reduced(2));

    r.removeFromTop(12);

    // Session & Playback
    auto sessionArea = r.removeFromTop(95);
    grpSession.setBounds(sessionArea);
    auto sessionInner = sessionArea.reduced(15, 22);
    auto sRow1 = sessionInner.removeFromTop(32);
    btnResetMixerOnLoad.setBounds(sRow1.removeFromLeft(140).reduced(2));
    btnResetMixer.setBounds(sRow1.removeFromLeft(140).reduced(2));
    btnForceGrid.setBounds(sRow1.removeFromLeft(120).reduced(2));
    btnNoteQuantize.setBounds(sRow1.removeFromLeft(140).reduced(2));
    btnPerformanceMode.setBounds(sRow1.removeFromLeft(160).reduced(2));

    r.removeFromTop(10);

    // Ableton Link & Sync (Link-only controls)
    auto linkArea = r.removeFromTop(125);
    grpLink.setBounds(linkArea);
    auto linkInner = linkArea.reduced(15, 25);

    auto lRow1 = linkInner.removeFromTop(35);
    btnLinkEnable.setBounds(lRow1.removeFromLeft(120).reduced(2));
    btnStartStopSync.setBounds(lRow1.removeFromLeft(120).reduced(2));
    btnLockBpm.setBounds(lRow1.removeFromLeft(100).reduced(2));

    auto lRow2 = linkInner.removeFromTop(35);
    lblQuantum.setBounds(lRow2.removeFromLeft(70));
    cmbQuantum.setBounds(lRow2.removeFromLeft(100));
    lRow2.removeFromLeft(20);
    lblLinkBpm.setBounds(lRow2.removeFromLeft(80));
    sliderLinkBpm.setBounds(lRow2.removeFromLeft(120));

    r.removeFromTop(10);

    // Advanced Sync (latency, lookahead, clock offset, buffer)
    auto syncArea = r.removeFromTop(220);
    grpSync.setBounds(syncArea);
    auto sync = syncArea.reduced(15, 25);

    auto lRow3 = sync.removeFromTop(35);
    lblLatency.setBounds(lRow3.removeFromLeft(100));
    btnCalibrate.setBounds(lRow3.removeFromRight(80).reduced(2));
    sliderLatency.setBounds(lRow3.removeFromLeft(200));

    auto lRow4 = sync.removeFromTop(35);
    lblLookahead.setBounds(lRow4.removeFromLeft(100));
    sliderLookahead.setBounds(lRow4.removeFromLeft(200));

    auto lRow5 = sync.removeFromTop(35);
    lblClockOffset.setBounds(lRow5.removeFromLeft(100));
    sliderClockOffset.setBounds(lRow5.removeFromLeft(200));

    auto lRow6 = sync.removeFromTop(35);
    lblSyncBuffer.setBounds(lRow6.removeFromLeft(100));
    sliderSyncBuffer.setBounds(lRow6.removeFromLeft(200));
    btnBypassLookahead.setBounds(lRow6.removeFromLeft(250).translated(10, 0));

    // RTP Section (Inserted before Footer)
    auto rtpAreaSub = r.removeFromTop(90);
    grpRtp.setBounds(rtpAreaSub);
    auto rtpContent = rtpAreaSub.reduced(15, 25);

    btnRtpDriver.setBounds(rtpContent.removeFromTop(20));
    rtpContent.removeFromTop(5);
    btnRtpInternal.setBounds(rtpContent.removeFromLeft(120));
    btnRtpOff.setBounds(rtpContent.removeFromLeft(60).translated(10, 0));

    r.removeFromTop(10);

    // Threading (worker pool mode)
    auto threadingArea = r.removeFromTop(55);
    grpThreading.setBounds(threadingArea);
    auto threadingContent = threadingArea.reduced(15, 20);
    lblThreadingWorkers.setBounds(threadingContent.removeFromLeft(50));
    cmbThreadingMode.setBounds(threadingContent.removeFromLeft(160).reduced(2));

    r.removeFromTop(15);

    // LFO Section
    auto lfoArea = r.removeFromTop(120);
    grpLfo.setBounds(lfoArea);
    auto lfo = lfoArea.reduced(15, 25);

    auto lfoRow1 = lfo.removeFromTop(35);
    lblLfoWave.setBounds(lfoRow1.removeFromLeft(60));
    cmbLfoWave.setBounds(lfoRow1.removeFromLeft(120));

    auto lfoRow2 = lfo.removeFromTop(35);
    lblLfoFreq.setBounds(lfoRow2.removeFromLeft(80));
    sliderLfoFreq.setBounds(lfoRow2.removeFromLeft(150));
    lfoRow2.removeFromLeft(20);
    lblLfoDepth.setBounds(lfoRow2.removeFromLeft(60));
    sliderLfoDepth.setBounds(lfoRow2.removeFromLeft(150));

    // Input Devices Section (Bluetooth MIDI)
    auto inputArea = r.removeFromTop(160);
    grpInputDevices.setBounds(inputArea);
    auto inputContent = inputArea.reduced(15, 25);

    auto btRow = inputContent.removeFromTop(35);
    btnBluetoothPair.setBounds(btRow.removeFromLeft(160).reduced(2));
    btnScanBluetooth.setBounds(btRow.removeFromLeft(60).reduced(2));
    lblBluetoothStatus.setBounds(btRow.reduced(5));

    inputContent.removeFromTop(10);
    r.removeFromTop(20);

    // Help Section (inside Config)
    auto helpArea = r.removeFromTop(200);
    grpHelp.setBounds(helpArea);
    auto helpContent = helpArea.reduced(15, 25);
    lblHelpText.setBounds(helpContent.removeFromTop(52));
    btnOpenHelp.setBounds(helpContent.removeFromTop(32).removeFromLeft(160).reduced(2));
    r.removeFromTop(10);

    // Footer
    r.removeFromTop(10);
    btnOscAdvanced.setBounds(r.removeFromTop(30).removeFromRight(180));
    btnResetLayout.setBounds(
        r.removeFromTop(30).removeFromRight(180).translated(-200, 0));
    btnResetTour.setBounds(
        r.removeFromTop(30).removeFromRight(180).translated(-400, 0));
    btnShowDiag.setBounds(
        r.removeFromTop(30).removeFromRight(180).translated(-600, 0));
    oscAddresses.setBounds(r.removeFromTop(600));
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();

    // 1. Transparency for CRT Shader
    // ONLY fill the background if we are NOT in Pro/CRT mode
    if (currentRenderMode != RenderConfig::OpenGL_Perf) {
      // Gradient Background (Top darker to bottom lighter)
      juce::ColourGradient grad(Theme::bgDark.darker(0.1f), bounds.getX(),
                                bounds.getY(), Theme::bgPanel.withAlpha(0.95f),
                                bounds.getX(), bounds.getBottom(), false);
      g.setGradientFill(grad);
      g.fillRect(bounds);
    }
    // Otherwise, do nothing! Let the OpenGL shader defined in
    // renderOpenGL() be the background.

    // 2. Left accent line (subtle glow)
    g.setColour(Theme::accent.withAlpha(0.1f));
    g.fillRect(bounds.withWidth(2.0f));

    // 3. Inner shadow at top
    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.fillRect(bounds.withHeight(3.0f));

    // 4. Outer border with subtle highlight
    g.setColour(juce::Colours::white.withAlpha(0.03f));
    g.drawRect(getLocalBounds().reduced(1), 1);
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.drawRect(getLocalBounds(), 1);
  }

  // --- PUBLIC MEMBERS ---
  juce::Label lblTitle, lblTheme, lblMidiMap, lblRenderMode, lblGpuBackend;
  juce::GroupComponent grpTheme, grpApp, grpNet, grpIo, grpSession, grpSync, grpThreading;
  juce::Label lblIp, lblPOut, lblPIn, lblIn, lblOut, lblCh, lblClockSource,
      lblLatency, lblClockOffset, lblLookahead;
  juce::TextEditor edIp, edPOut, edPIn;
  juce::TextButton btnConnect, btnOscAddresses, btnLocalIps, btnThru, btnClock, btnSplit, btnMidiScaling,
      btnOscAdvanced, btnImportMap, btnExportMap, btnResetMaps, btnSaveProfile,
      btnLoadProfile, btnDeleteProfile, btnResetMixerOnLoad, btnResetMixer,
      btnForceGrid, btnZeroConfig, btnLowLatency, btnMulticast, btnBlockMidiOut,
      btnNoteQuantize, btnDirectInput, btnMidiIn, btnMidiOut, btnMidiPorts, btnTestMidi, btnCalibrate,
      btnIPv6;
  juce::ToggleButton btnBypassLookahead;
  juce::ComboBox cmbTheme, cmbMidiMap, cmbMidiCh, cmbClockSource, cmbQuantum,
      cmbCtrlProfile, cmbRenderMode, cmbGpuBackend, cmbThreadingMode;
  juce::Label lblCtrlProfile, lblProfileStatus;
  ControlHelpers::ResponsiveSlider sliderLatency, sliderClockOffset, sliderLinkBpm, sliderLookahead,
      sliderSyncBuffer;
  juce::Label lblQuantum, lblLinkBpm, lblSyncBuffer, lblThreadingWorkers;
  juce::ToggleButton btnMultiCoreMode, btnPerformanceMode;

  ConnectionLight btLight;
  std::function<void(juce::String, bool)> onLog;

  // RTP Members
  juce::GroupComponent grpRtp;
  juce::TextButton btnRtpDriver, btnRtpInternal, btnRtpOff;

  // LFO Members
  juce::GroupComponent grpLfo;
  juce::Label lblLfoFreq, lblLfoDepth, lblLfoWave;
  ControlHelpers::ResponsiveSlider sliderLfoFreq, sliderLfoDepth;
  juce::ComboBox cmbLfoWave;

  // Input Devices Section
  juce::GroupComponent grpInputDevices;
  juce::TextButton btnBluetoothPair, btnScanBluetooth;
  juce::Label lblBluetoothStatus;
  juce::TextButton btnShowDiag, btnResetTour, btnResetLayout;
  juce::TextButton btnGamepadEnable;
  ConnectionLight ledGamepad;
  juce::Label lblGamepadStatus;
  juce::Label lblGamepadDeadzone, lblGamepadSensitivity, lblGamepadController;
  ControlHelpers::ResponsiveSlider sliderGamepadDeadzone, sliderGamepadSensitivity;
  juce::ComboBox cmbGamepadController;

  // Link/Sync Members
  juce::GroupComponent grpLink;
  juce::TextButton btnLinkEnable, btnStartStopSync, btnLockBpm;

  // Help (inside Config)
  juce::GroupComponent grpHelp;
  juce::Label lblHelpText;
  juce::TextButton btnOpenHelp;

  // --- OSC ADDRESS EDITOR ---
  OscAddressConfig oscAddresses;

  void log(const juce::String &msg, bool isError) {
    if (onLog)
      onLog(msg, isError);
  }

  void setGamepadConnected(bool connected,
                           const juce::String &deviceName = "") {
    ledGamepad.setConnected(connected);
    if (connected) {
      lblGamepadStatus.setText(
          "Gamepad: " + (deviceName.isEmpty() ? "Connected" : deviceName),
          juce::dontSendNotification);
      lblGamepadStatus.setColour(juce::Label::textColourId,
                                 juce::Colours::limegreen);
    } else {
      lblGamepadStatus.setText("Gamepad: Not Connected",
                               juce::dontSendNotification);
      lblGamepadStatus.setColour(juce::Label::textColourId,
                                 juce::Colours::grey);
    }
  }

  void refreshClockSources(const juce::Array<juce::MidiDeviceInfo> &devices,
                           const juce::String &currentId) {
    clockSourceIds.clear();
    cmbClockSource.clear(juce::dontSendNotification);
    cmbClockSource.addItem("Any / Merge (Not Recommended)", 1);
    int selectId = 1;
    for (int i = 0; i < devices.size(); ++i) {
      cmbClockSource.addItem(devices[i].name, i + 2);
      clockSourceIds.add(devices[i].identifier);
      if (devices[i].identifier == currentId)
        selectId = i + 2;
    }
    cmbClockSource.setSelectedId(selectId, juce::dontSendNotification);
  }

  void setBluetoothMidiStatus(const juce::String &status) {
    lblBluetoothStatus.setText(status, juce::dontSendNotification);
    juce::Colour c = juce::Colours::grey;
    if (status.contains("Connected") || status.startsWith("BT MIDI:"))
      c = juce::Colours::limegreen;
    else if (status.contains("Controllers:"))
      c = juce::Colours::lightgrey;
    lblBluetoothStatus.setColour(juce::Label::textColourId, c);
  }

  void setProfileFeedback(const juce::String &msg, bool isError) {
    lblProfileStatus.setText(msg, juce::dontSendNotification);
    lblProfileStatus.setColour(juce::Label::textColourId,
                              isError ? juce::Colours::red : juce::Colours::limegreen);
    startTimer(3000);
  }

  void timerCallback() override {
    stopTimer();
    lblProfileStatus.setText("", juce::dontSendNotification);
  }

  ~ConfigPanel() override {
    stopTimer();
    cancelPendingUpdate();
  }

private:
  int currentRenderMode = 1;
  juce::StringArray clockSourceIds; // Maps cmb item index to device identifier
  juce::Rectangle<int> lastLayoutBounds_{0, 0, 0, 0}; // Skip layout when bounds unchanged
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConfigPanel)
};
