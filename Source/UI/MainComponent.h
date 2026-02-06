
//////////////////////////////////////////////////////////////////////
// FILE: UI\MainComponent.h
//////////////////////////////////////////////////////////////////////

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>
#include <string>

#include "../Core/BridgeContext.h"
#include "../Core/BridgeListener.h"
#include "../Core/GLContextManager.h"
#include "../Core/SystemController.h"
#include "UI/RenderBackend.h"
#if PATCHWORLD_VULKAN_SUPPORT
#include "UI/VulkanContext.h"
#endif
#include "../Core/TimerHub.h"
#include "../UI/Theme.h"

// Panels
#include "Panels/ArpeggiatorPanel.h"
#include "Panels/ChordGeneratorPanel.h"
#include "Panels/ConfigControls.h"
#include "Panels/ConfigPanel.h"
#include "Panels/HeaderPanel.h"
#include "Panels/LfoGeneratorPanel.h"
#include "Panels/MidiPlaylist.h"
#include "Panels/MixerPanel.h"
#include "Panels/NetworkConfigPanel.h"
#include "Panels/PerformancePanel.h"
#include "Panels/SequencerPanel.h"
#include "Panels/StatusBar.h"
#include "Panels/TrafficMonitor.h"
#include "Panels/TransportPanel.h"

// Widgets
#include "UI/CustomMenuLookAndFeel.h"
#include "UI/MixerLookAndFeel.h"
#include "Widgets/CRTBackground.h"
#include "Widgets/ConnectionsButton.h"
#include "Widgets/DiagnosticOverlay.h"
#include "Widgets/DynamicBackground.h"
#include "Widgets/LayoutChoiceWizard.h"
#include "Widgets/LinkBeatIndicator.h"
#include "Widgets/MeterBarRenderer.h"
#include "Widgets/MidiLearnOverlay.h"
#include "Widgets/ModuleWindow.h"
#include "Widgets/PianoRoll.h"
#include "Widgets/SetupWizard.h"

// Helper Class for Macros is defined in ConfigControls.h
// class MacroControls; // No forward declare needed if header included

class MainComponent : public juce::AudioAppComponent,
                      public juce::ChangeListener,
                      public juce::FileDragAndDropTarget,
                      public juce::OpenGLRenderer,
                      public juce::MidiKeyboardState::Listener {
public:
  MainComponent();
  ~MainComponent() override;

  // --- Audio Lifecycle ---
  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void
  getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
  void releaseResources() override;

  // --- GUI Lifecycle ---
  void paint(juce::Graphics &g) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress &key) override;
  void mouseDown(const juce::MouseEvent &e) override;
  void mouseDrag(const juce::MouseEvent &e) override;
  void mouseUp(const juce::MouseEvent &e) override;

  // --- OpenGL Lifecycle ---
  void newOpenGLContextCreated() override;
  void renderOpenGL() override;
  void openGLContextClosing() override;

  /** GPU context loss recovery: call from timer to flush deferred resize. */
  void flushPendingResize();

  // --- Public UI Methods ---
  enum class AppView {
    Dashboard,
    Control,
    OSC_Config
  }; // Help moved into Config
  void setView(AppView v);
  AppView getCurrentView() const { return currentView; }
  /** Scroll Config viewport so the OSC Addresses section is visible (e.g. from
   * Connections menu). */
  void scrollConfigToOscAddresses();
  void toggleMidiLearnOverlay(bool show);
  MidiLearnOverlay *getMidiLearnOverlay() { return midiLearnOverlay.get(); }

  // Logic helpers (Restored)
  void onLogMessage(const juce::String &msg, bool isError);
  void handleRenderModeChange(int mode);
  /** Applies theme to main LookAndFeel and mixer LookAndFeel. */
  void applyThemeToAllLookAndFeels(int themeId);
  /** Persist mappings and app state before app closes (called from Connections
   * / close). */
  void saveStateBeforeShutdown();
  bool isMidiLearnMode = false;

  // MidiKeyboardState::Listener (Virtual Keyboard -> MIDI)
  void handleNoteOn(juce::MidiKeyboardState *, int midiChannel,
                    int midiNoteNumber, float velocity) override;
  void handleNoteOff(juce::MidiKeyboardState *, int midiChannel,
                     int midiNoteNumber, float velocity) override;

  // ChangeListener (Audio device hot-swap)
  void changeListenerCallback(juce::ChangeBroadcaster *source) override;

  // File Drag
  bool isInterestedInFileDrag(const juce::StringArray &files) override;
  void filesDropped(const juce::StringArray &files, int x, int y) override;

  // --- Public UI Elements (Fixed Panels) ---
  std::unique_ptr<HeaderPanel> headerPanel;
  std::unique_ptr<NetworkConfigPanel> networkConfigPanel;
  std::unique_ptr<TransportPanel> transportPanel;
  std::unique_ptr<ConfigPanel> configPanel;
  std::unique_ptr<ControlPage> controlPage;
  std::unique_ptr<OscAddressConfig> oscConfigPanel;

  // --- Content Panels (Owned, wrapped by ModuleWindows) ---
  std::unique_ptr<PerformancePanel> performancePanel;
  std::unique_ptr<MidiPlaylist> playlist;
  std::unique_ptr<TrafficMonitor> logPanel;
  std::unique_ptr<ArpeggiatorPanel> arpPanel;
  std::unique_ptr<ChordGeneratorPanel> chordPanel;
  MacroControls macroControls;
  LfoGeneratorPanel lfoGeneratorPanel;

  // --- ModuleWindow Wrappers (The MDI containers) ---
  std::unique_ptr<ModuleWindow> winEditor;    // Wraps PerformancePanel
  std::unique_ptr<ModuleWindow> winMixer;     // Wraps context->mixer
  std::unique_ptr<ModuleWindow> winSequencer; // Wraps context->sequencer
  std::unique_ptr<ModuleWindow> winPlaylist;  // Wraps MidiPlaylist
  std::unique_ptr<ModuleWindow> winLog;       // Wraps TrafficMonitor
  std::unique_ptr<ModuleWindow> winArp;       // Wraps ArpeggiatorPanel
  std::unique_ptr<ModuleWindow> winMacros;    // Wraps MacroControls
  std::unique_ptr<ModuleWindow> winChords;    // Wraps ChordGeneratorPanel
  std::unique_ptr<ModuleWindow> winLfoGen;    // Wraps LfoGeneratorPanel
  std::unique_ptr<ModuleWindow>
      winControl; // Wraps ControlPage (Control menu opens this)

  // Extra add/remove module instances (Sequencer +, LFO +)
  std::vector<std::unique_ptr<juce::Component>> extraModulePanels;

  std::vector<std::unique_ptr<ModuleWindow>> extraModuleWindows;
  void removeExtraModuleWindow(ModuleWindow *win);

  // --- Detached Windows (Phase 9) ---
  class DetachedWindow;
  std::map<juce::String, std::unique_ptr<DetachedWindow>> detachedWindows;
  void detachModuleWindow(ModuleWindow *win);
  void reattachModuleWindow(const juce::String &moduleName);

  // Global Navigation
  juce::TextButton btnDash; // Single toggle: shows "Config" on Dashboard,
                            // "Dashboard" on Config
  juce::TextButton btnPanic, btnMidiLearn, btnExtSyncMenu, btnThru;
  ConnectionsButton btnMenu;
  juce::TextButton btnUndo, btnRedo;
  // BPM/Tap in top bar (right of Redo)
  TransportPanel::BpmSlider tempoSlider;
  juce::Label lblBpm;
  juce::TextButton btnTap;
  juce::TextButton btnResetBpm;
  // Link in top bar (left of THRU)
  juce::TextButton btnLink;
  LinkBeatIndicator linkIndicator;

  // Tooltips (400ms so they show on hover without needing a click)
  juce::TooltipWindow tooltipWindow{this, 400};

  // Public Helpers for Layout
  juce::Viewport configViewport;
  StatusBarComponent statusBar;
  StatusBarComponent &getStatusBar() { return statusBar; }
  BridgeContext *getContext() const { return context.get(); }
  bool isPlaying() const;
  /** For SystemController::bindLfoPatching — LFO slot index to paramID (e.g.
   * Macro_Fader_1). */
  std::vector<std::pair<int, juce::String>> &getLfoPatches() {
    return lfoPatches_;
  }

  /** Show all main module windows (Editor, Mixer, Sequencer, etc.) and bring to
   * front. */
  void showAllModules();
  /** Hide all main module windows. */
  void hideAllModules();

  /** Repaint components based on RepaintCoordinator dirty bits (called from
   * flush handler). */
  void repaintDirtyRegions(uint32_t dirtyBits);

  std::function<void(juce::Component *)> onMenuClicked;
  std::function<void(int)>
      onRenderModeChangedInternal; // Syncs UI (ConfigPanel) when mode changes
                                   // internally

private:
  // --- Init phases (constructor is a short sequence of these) ---
  void initContextAndNetworkPanel();
  void initPanels();
  void initLookAndFeels();
  void initModuleWindows();
  void wireModuleWindowCallbacks();
  void wireHeaderAndViewSwitching();
  void wireTransportAndStatusBar();
  void applyLayoutAndRestore();
  void wireOscLogAndConfigSync();
  void wirePlaybackController();
  void wireMappingManager();
  void wireLfoPatching();
  void initEngineAndStartServices();
  void startAudioAndVBlank();
  void handleVBlank();

private:
  // --- Tooltip Timer ---
  class TooltipTimer {
  public:
    TooltipTimer(MainComponent &owner) : main(owner) {
      hubId = "TooltipTimer_" + juce::Uuid().toDashedString().toStdString();
      TimerHub::instance().subscribe(
          hubId, [this] { tick(); }, TimerHub::Medium30Hz);
    }

    ~TooltipTimer() { TimerHub::instance().unsubscribe(hubId); }

  private:
    void tick() {
      auto *comp = juce::Desktop::getInstance()
                       .getMainMouseSource()
                       .getComponentUnderMouse();
      if (comp != nullptr) {
        juce::String tip;
        if (auto *tc = dynamic_cast<juce::TooltipClient *>(comp))
          tip = tc->getTooltip();

        if (tip.isEmpty()) {
          juce::String name = comp->getName();
          juce::String paramID =
              comp->getProperties().getWithDefault("paramID", "").toString();
          if (paramID.isNotEmpty())
            tip = name + " (" + paramID + ")";
          else if (name.isNotEmpty())
            tip = name;
        }

        if (tip.isNotEmpty() && tip != lastTip) {
          main.getStatusBar().setText(tip, juce::dontSendNotification);
          lastTip = tip;
        } else if (tip.isEmpty() && lastTip.isNotEmpty()) {
          main.getStatusBar().setText("Ready", juce::dontSendNotification);
          lastTip.clear();
        }
      }
    }

    MainComponent &main;
    juce::String lastTip;
    std::string hubId;
  };

  std::unique_ptr<TooltipTimer> tooltipTimer;

  // --- THE CORE ---
  std::unique_ptr<BridgeContext> context;
  std::unique_ptr<SystemController> sysController;

  // --- UI Infrastructure ---
  juce::OpenGLContext openGLContext;
#if PATCHWORLD_VULKAN_SUPPORT
  std::unique_ptr<VulkanContext> vulkanContext;
#endif
  CRTBackground crtBackground;
  MeterBarRenderer meterBarRenderer;
  DynamicBackground dynamicBg;

  // V-Blank sync: drives UI at monitor refresh rate (no timer drift/tearing)
  std::unique_ptr<juce::VBlankAttachment> vBlankAttachment;
  double lastFrameTime = 0.0;
  bool isResizing = false;
  bool vBlankWasAnimating = false;

  // Resize-safe: defer layout during playback to avoid glitches
  std::atomic<bool> resizePending{false};
  juce::Rectangle<int> pendingResizeBounds;
  juce::CriticalSection resizeLock;

  // GPU context loss recovery
  GLContextManager glContextManager;
  std::atomic<bool> isGpuAvailable{true};
  std::atomic<bool> showGpuUnavailableMessage{
      false}; // true only when context was lost, not when user chose Software
  float cachedDisplayScale = 1.0f;

  // Overlays
  std::unique_ptr<MidiLearnOverlay> midiLearnOverlay;
  std::unique_ptr<DiagnosticOverlay> diagOverlay;
  SetupWizard setupWizard;
  LayoutChoiceWizard layoutChoiceWizard;

  juce::ImageComponent logoView;

  void applyLayout(juce::Rectangle<int> area);
  void setupComponentCaching();

  // State
  AppView currentView = AppView::Dashboard;
  int currentRenderMode = 0; // 0=Pro (60FPS+Shaders), 1=Eco (30FPS, no shaders)
  bool backgroundFillPending_ = false; // set when module moved; paint() fills
                                       // partial region to clear ghosting

  // Multi-select box and background-drag for ModuleWindows
  bool isBoxSelecting = false;
  bool isBackgroundDragging = false;
  juce::Point<int> boxSelectStart;
  juce::Point<int> lastBackgroundDragPos;
  juce::Rectangle<int> selectionBox;

  /** Right-click on empty dashboard: show Add Modules context menu. */
  void showAddModulesContextMenu(const juce::MouseEvent &e);

  // LFO patching: (lfoIndex 0..3, paramID) for 4 LFO slots
  std::vector<std::pair<int, juce::String>> lfoPatches_;
  double lfoPhase_[4] = {0.0, 0.0, 0.0, 0.0};
  void updateLfoPatches(float dtSec);

  // LookAndFeel for macro dials
  std::unique_ptr<FancyDialLF> fancyDialLF;
  // LookAndFeel for mixer (phantom faders, reactive knobs)
  std::unique_ptr<MixerLookAndFeel> mixerLookAndFeel;
  std::unique_ptr<CustomMenuLookAndFeel> menuLookAndFeel;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};