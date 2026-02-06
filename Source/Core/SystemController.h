/*
  ==============================================================================

    SystemController.h
    Role: Wiring harness and central logic coordinator.

  ==============================================================================
*/

#pragma once

#include "BridgeContext.h"
#include "TransportViewModel.h"
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

// Forward Declarations
class MainComponent;
class SystemController;

/** Refreshes Undo/Redo button state immediately when undo stack changes. */
struct UndoButtonRefresher : juce::ChangeListener {
  SystemController *controller = nullptr;
  void changeListenerCallback(juce::ChangeBroadcaster *) override;
};

class SystemController {
public:
  SystemController(BridgeContext &ctx) : context(ctx) {}
  ~SystemController();

  /** Used by UndoButtonRefresher async callback to avoid use-after-free. */
  static SystemController *getLivingInstance();

  // Pass by reference to avoid copying
  void bindInterface(MainComponent &mainUI);
  /** When fullUpdate is false, only tick + playback-critical (playhead, trackGrid, sequencer); heavy UI at ~15 Hz. */
  void processUpdates(bool fullUpdate = true);

  // Window Layout Persistence
  void saveWindowLayout();
  void restoreWindowLayout();
  void resetWindowLayout();
  /** Apply a named preset (Minimal, Full); saves to preset if first time. */
  void applyLayoutPreset(const juce::String& name);
  /** Restore from a saved preset XML. */
  void restoreLayoutFromXml(const juce::String& xmlStr);
  /** Get current layout as XML string (after saveWindowLayout). */
  juce::String getCurrentLayoutXml() const;

  // Event Handlers (Called by MainComponent forwarding)
  bool handleGlobalKeyPress(const juce::KeyPress &key);
  void handleFileDrop(const juce::StringArray &files);
  void handleSliderTouch(const juce::String &paramID);
  void handleSliderRelease(const juce::String &paramID);

  /** Refresh Undo/Redo button states (called by UndoButtonRefresher). */
  void refreshUndoRedoButtons();

  /** Sync ConfigPanel Thru/Block/Split from backend (call when showing Config view). */
  void refreshConfigPanelFromBackend();

  /** Sync TransportPanel Thru/Block/Split from backend (call when showing Dashboard). */
  void refreshTransportFromBackend();

  /** Wire an extra sequencer panel (slot 1..3) to engine and view model. */
  void wireExtraSequencer(class SequencerPanel *panel, int slot);

  BridgeContext &getContext() { return context; }
  MainComponent *getUi() { return ui; }

private:
  BridgeContext &context;

  // ViewModel for transport (MVC/MVP)
  std::unique_ptr<TransportViewModel> transportViewModel;

  // UI Reference
  MainComponent *ui = nullptr;

  // Immediate Undo/Redo button refresh (no timer delay)
  UndoButtonRefresher undoButtonRefresher;

  // Internal Logic Bindings
  void bindGlobalNavigation(MainComponent &ui);
  void bindHeader(MainComponent &ui); // New: Network & MIDI Setup
  void bindTransport(MainComponent &ui);
  void bindSidebar(MainComponent &ui);
  void bindConfig(MainComponent &ui);
  void bindMixer(MainComponent &ui);
  void bindMappingManager(MainComponent &ui);
  void bindPerformance(MainComponent &ui);
  void bindControlPage(MainComponent &ui);
  void bindOscConfig(MainComponent &ui);
  void bindMacros(MainComponent &ui);
  void bindChordGenerator(MainComponent &ui);
  void bindLfoPatching(MainComponent &ui);
  void bindOscLog(MainComponent &ui);
  void bindPlaybackController(MainComponent &ui);
  void bindShortcuts(MainComponent &ui);
  void showShortcutsPanel(MainComponent &mainUI);

  // Helper State
  std::unique_ptr<juce::FileChooser> fileChooser;

  // Right-click "Change message" for controls (MIDI/OSC override)
  struct ControlMessageMenuListener : juce::MouseListener {
    juce::String paramID;
    bool isButton = false;
    juce::Component *attachedTo = nullptr;
    std::function<void(juce::String, bool, juce::Component *)> onRightClick;
    void mouseDown(const juce::MouseEvent &e) override {
      if (e.mods.isRightButtonDown() && onRightClick)
        onRightClick(paramID, isButton, e.eventComponent);
    }
  };
  std::vector<std::unique_ptr<ControlMessageMenuListener>> controlMenuListeners;
  void showControlMessageMenu(juce::String paramID, bool isButton,
                              juce::Component *target);
  juce::PopupMenu buildChangeMessageSubmenu(juce::String paramID, bool isButton);
  void showControlMessageDialog(juce::String paramID, int type);

  // LFO Patching: click any control with paramID when patching mode is on
  struct LfoPatchClickListener : juce::MouseListener {
    MainComponent *main = nullptr;
    void mouseDown(const juce::MouseEvent &e) override;
  };
  std::unique_ptr<LfoPatchClickListener> lfoPatchClickListener;
};