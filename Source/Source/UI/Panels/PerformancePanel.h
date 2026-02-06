/*
  ==============================================================================
    Source/UI/Panels/PerformancePanel.h
    Role: Main performance view (Piano Roll + Keyboards + Macro Controls)
  ==============================================================================
*/
#pragma once
#include "../Panels/SequencerPanel.h"
#include "../Panels/SpliceEditor.h"
#include "../Theme.h"
#include "../Widgets/PianoRoll.h"
#include "../Widgets/PlayView.h"
#include "../Widgets/TimelineComponent.h"
#include "../Widgets/WheelComponent.h"
#include "Core/BridgeContext.h"

#include <juce_graphics/juce_graphics.h>

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

class BridgeContext;
class ComplexPianoRoll;
class SequencerPanel;

class PerformancePanel : public juce::Component, private juce::Timer {

public:
  // Two modes: Play (PlayView falling notes) and Edit (SpliceEditor)
  enum class ViewMode { Play, Edit };

  PerformancePanel(juce::MidiKeyboardState &keyState, SequencerPanel &seq,
                   BridgeContext &ctx)
      : keyboardState(keyState), sequencer(seq), trackGrid(keyState),
        context(ctx),
        horizontalKeyboard(keyState,
                           juce::MidiKeyboardComponent::horizontalKeyboard),
        verticalKeyboard(
            keyState,
            juce::MidiKeyboardComponent::verticalKeyboardFacingRight) {

    trackGrid.keyboardComp = &horizontalKeyboard;
    playView.setKeyboardComponent(&horizontalKeyboard);

    setWantsKeyboardFocus(true);
    addAndMakeVisible(trackGrid);
    addAndMakeVisible(playView);
    addAndMakeVisible(spliceEditor);
    // Note: sequencer is no longer a child here — it lives in its own
    // ModuleWindow
    addAndMakeVisible(horizontalKeyboard);
    addAndMakeVisible(verticalKeyboard);
    addAndMakeVisible(pitchWheel);
    addAndMakeVisible(modWheel);

    // Internal wiring
    spliceEditor.setContext(&ctx);
    spliceEditor.onNotesChanged = [this] { syncNotesToPlayView(); };

    trackGrid.onRequestRepaint = [this] {
      context.repaintCoordinator.markDirty(RepaintCoordinator::PianoRoll);
    };

    // Keyboard-piano roll scroll sync: when piano roll scrolls, update keyboard
    // visible range
    spliceEditor.onScrollChanged = [this](float scrollY) {
      if (currentMode != ViewMode::Edit)
        return;
      float nh = spliceEditor.getNoteHeight();
      if (nh <= 0.1f)
        return;
      float viewH = (float)spliceEditor.getHeight();
      int lowestNote = (int)(127.0f - (scrollY + viewH) / nh);
      verticalKeyboard.setLowestVisibleKey(juce::jlimit(0, 115, lowestNote));
    };
    spliceEditor.onZoomChanged = [this](float percent) { showZoomFeedback(percent); };

    // --- MODE TOGGLE BUTTON ---
    addAndMakeVisible(btnViewMode);
    btnViewMode.setButtonText("Play");
    btnViewMode.setClickingTogglesState(true);
    btnViewMode.onClick = [this] {
      setViewMode(btnViewMode.getToggleState() ? ViewMode::Edit
                                               : ViewMode::Play);
    };

    // Initialize Default View (Play mode - falling notes)
    setViewMode(ViewMode::Play);

    addAndMakeVisible(timeline);

    // Zoom feedback overlay (Ctrl+wheel): show "Zoom XX%" then fade out
    addChildComponent(lblZoomFeedback);
    lblZoomFeedback.setVisible(false);
    lblZoomFeedback.setJustificationType(juce::Justification::centred);
    lblZoomFeedback.setColour(juce::Label::textColourId, Theme::text);
    lblZoomFeedback.setColour(juce::Label::backgroundColourId, juce::Colour(0xe016161d));

    // Scale dropdowns removed from piano roll header (user request: remove two
    // dropdowns left of Edit)

    // Defaults
    trackGrid.setColour(juce::TextEditor::backgroundColourId,
                        Theme::bgDark.brighter(0.05f));

    // Timeline Interactivity
    timeline.onSeek = [this](double beat) {
      if (onTimelineSeek)
        onTimelineSeek(beat);
    };

    // Default mode (Play = falling notes)
    setViewMode(ViewMode::Play);
  }

  void updatePlayhead(double beat, double /*ppq*/) {
    timeline.setPlayhead(beat);

    if (currentMode == ViewMode::Play) {
      trackGrid.showPlayhead = false;
      spliceEditor.setPlayheadBeat(beat);
      playView.setCurrentBeat(beat);
      std::set<int> active;
      for (int ch = 1; ch <= 16; ++ch) {
        for (int i = 0; i < 128; ++i) {
          if (keyboardState.isNoteOn(ch, i))
            active.insert(i);
        }
      }
      playView.setActiveNotes(active);
    } else {
      trackGrid.showPlayhead = false;
      spliceEditor.setPlayheadBeat(beat);
      playView.setCurrentBeat(beat);
    }
  }

  void setViewMode(ViewMode m) {
    currentMode = m;

    // Sync toggle button with mode
    btnViewMode.setToggleState(m == ViewMode::Edit, juce::dontSendNotification);
    btnViewMode.setButtonText(m == ViewMode::Play ? "Play" : "Edit");

    bool isEdit = (m == ViewMode::Edit);

    trackGrid.setVisible(false);
    playView.setVisible(!isEdit);
    spliceEditor.setVisible(isEdit);
    spliceEditor.setViewMode(isEdit ? SpliceEditor::ViewMode::Edit : SpliceEditor::ViewMode::Play);

    // Sync notes to Play view when switching to Play
    if (!isEdit) {
      playView.setNotes(spliceEditor.getNotes());
      if (context.engine)
        playView.setBpm(context.engine->getBpm());
    }

    // Layout first so SpliceEditor/PlayView get correct bounds before building render state (avoids black frame)
    resized();

    if (isEdit) {
      spliceEditor.toFront(false);
      spliceEditor.pushRenderState();  // After resized() so state uses correct dimensions
    }
    btnViewMode.toFront(false);

    spliceEditor.repaint();
    playView.repaint();
    context.repaintCoordinator.markDirty(RepaintCoordinator::PianoRoll);
    context.repaintCoordinator.markDirty(RepaintCoordinator::Dashboard);
    repaint();
  }

  ViewMode getViewMode() const { return currentMode; }

  /** Sync notes from engine/editor to Play view. */
  void syncNotesToPlayView() {
    playView.setNotes(spliceEditor.getNotes());
    if (context.engine)
      playView.setBpm(context.engine->getBpm());
  }

  // Duplicate implementation blocks removed.
  // The correct implementations are already present earlier in the file or
  // should be consolidated. Logic is cleaner this way.

  void mouseWheelMove(const juce::MouseEvent &e,
                      const juce::MouseWheelDetails &wheel) override {
    if (currentMode == ViewMode::Play && (e.mods.isCtrlDown() || e.mods.isCommandDown())) {
      float delta =
          (wheel.deltaY > 0) ? 1.08f : (wheel.deltaY < 0 ? 1.0f / 1.08f : 1.0f);
      playZoomFactor_ = juce::jlimit(0.32f, 2.0f, playZoomFactor_ * delta);
      playView.setScrollSpeedScale(1.0f);
      showZoomFeedback(playZoomFactor_ * 100.0f);
      resized();
      repaint();
      return;
    }
    juce::Component::mouseWheelMove(e, wheel);
  }

  void timerCallback() override {
    if (zoomFeedbackTicksLeft > 0) {
      --zoomFeedbackTicksLeft;
      if (zoomFeedbackTicksLeft == 0) {
        lblZoomFeedback.setVisible(false);
        stopTimer();
      }
    }
  }

  void resized() override {
    auto area = getLocalBounds();

    // 1. Top row: Timeline (left) + Play/Edit button (top right of timeline so it doesn't block content)
    auto timelineRow = area.removeFromTop(24);
    const int btnW = 52, btnH = 20;
    const int btnMargin = 6;
    btnViewMode.setBounds(timelineRow.getRight() - btnW - btnMargin,
                          timelineRow.getY() + (timelineRow.getHeight() - btnH) / 2,
                          btnW, btnH);
    timeline.setBounds(timelineRow.reduced(2).withTrimmedRight(btnW + btnMargin));
    btnViewMode.toFront(false);

    bool isEdit = (currentMode == ViewMode::Edit);

    // 2. Bottom: Wheels + keyboard in Play mode (zoom out = smaller bar = more grid visible)
    int bottomHeight = isEdit ? 0 : juce::jmax(24, juce::roundToInt(60.0f * playZoomFactor_));
    auto bottomArea = area.removeFromBottom(bottomHeight);
    if (!isEdit) {
      pitchWheel.setVisible(true);
      modWheel.setVisible(true);
      auto leftWheelArea = bottomArea.removeFromLeft(60).reduced(2);
      pitchWheel.setBounds(leftWheelArea.removeFromLeft(28));
      modWheel.setBounds(leftWheelArea.removeFromLeft(28));
      horizontalKeyboard.setVisible(true);
      horizontalKeyboard.setEnabled(true);
      horizontalKeyboard.setBounds(bottomArea.reduced(2));
      verticalKeyboard.setVisible(false);
    }

    // 3. Main Area
    auto mainArea = area.reduced(2);
    auto zoomOverlayArea = mainArea;

    if (isEdit) {
      // --- EDIT MODE: SpliceEditor (with built-in piano strip) only - no
      // duplicate keyboard ---
      const int wheelWidth = 18;
      pitchWheel.setVisible(true);
      modWheel.setVisible(true);
      verticalKeyboard.setVisible(
          false); // SpliceEditor has its own piano strip
      horizontalKeyboard.setVisible(false);
      auto leftCol = mainArea.removeFromLeft(wheelWidth * 2);
      pitchWheel.setBounds(leftCol.removeFromLeft(wheelWidth));
      modWheel.setBounds(leftCol);
      spliceEditor.setBounds(mainArea);
      spliceEditor.setInterceptsMouseClicks(true, true);
      int nh = juce::jmax(8, mainArea.getHeight() / 32);
      spliceEditor.setNoteHeightFromKeyboard(128 * nh);
      if (spliceEditor.onScrollChanged)
        spliceEditor.onScrollChanged(spliceEditor.getScrollY());
      playView.setBounds(0, 0, 0, 0);
    } else {
      // --- PLAY MODE: PlayView falling notes + bottom keyboard only ---
      verticalKeyboard.setVisible(false);
      playView.setBounds(mainArea);
      spliceEditor.setBounds(0, 0, 0, 0);
      spliceEditor.setInterceptsMouseClicks(false, false);
      trackGrid.setBounds(0, 0, 0, 0);    // Legacy hidden
      // Sync PlayView key range with horizontal keyboard so notes align
      int lowest = horizontalKeyboard.getLowestVisibleKey();
      float kw = horizontalKeyboard.getKeyWidth();
      int keyCount = (kw > 0.1f)
                         ? juce::jlimit(1, 88,
                                        (int)(horizontalKeyboard.getWidth() /
                                              kw * 12.0f / 7.0f))
                         : 49;
      int keysToShow = juce::jlimit(1, 88, (int)((float)keyCount / playZoomFactor_));
      playView.setKeyRange(lowest, keysToShow);
    }

    // Zoom feedback overlay: centre of main content, above other content
    if (zoomOverlayArea.getWidth() >= 80 && zoomOverlayArea.getHeight() >= 24) {
      auto zoomRect = zoomOverlayArea.withSizeKeepingCentre(90, 26);
      zoomRect.setY(zoomOverlayArea.getY() + 12);
      lblZoomFeedback.setBounds(zoomRect);
      lblZoomFeedback.toFront(false);
    }
  }

  void paint(juce::Graphics &g) override {
    // just a background
    g.fillAll(Theme::bgPanel);
  }

  bool keyPressed(const juce::KeyPress &key) override {
    if (key == juce::KeyPress::tabKey) {
      setViewMode(currentMode == ViewMode::Play ? ViewMode::Edit
                                                : ViewMode::Play);
      return true;
    }
    if (!key.getModifiers().isAnyModifierKeyDown()) {
      if (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z') {
        if (onOctaveShift)
          onOctaveShift(-1);
        return true;
      }
      if (key.getKeyCode() == 'x' || key.getKeyCode() == 'X') {
        if (onOctaveShift)
          onOctaveShift(1);
        return true;
      }
    }
    return false;
  }

  // Method to handle "Seek" requests from SpliceEditor if needed
  void internalSeek(double b) {
    if (onTimelineSeek)
      onTimelineSeek(b);
  }

private:
  BridgeContext &context;
  ViewMode currentMode = ViewMode::Play;
  float playZoomFactor_ = 0.42f; // Ctrl+wheel in Play mode: 0.32 (max zoom out) – 2.0 (zoom in). Default 0.42 = zoom out to see more grid/keyboard at once.

  juce::Label lblZoomFeedback;
  int zoomFeedbackTicksLeft = 0;
  static constexpr int zoomFeedbackDurationMs = 1500;
  static constexpr int zoomFeedbackTimerHz = 50;

  void showZoomFeedback(float percent) {
    lblZoomFeedback.setText("Zoom " + juce::String(juce::roundToInt(percent)) + "%",
                            juce::dontSendNotification);
    lblZoomFeedback.setVisible(true);
    lblZoomFeedback.toFront(false);
    zoomFeedbackTicksLeft = (zoomFeedbackDurationMs + zoomFeedbackTimerHz - 1) / zoomFeedbackTimerHz;
    if (!isTimerRunning())
      startTimer(zoomFeedbackTimerHz);
  }

  void onSeek(double targetBeat) {
    if (context.engine) {
      double ppq = context.engine->getTicksPerQuarter();
      double lengthBeats = context.engine->getLoopLengthTicks() / ppq;

      if (targetBeat < 0)
        targetBeat = 0;
      if (lengthBeats > 0.0)
        targetBeat = juce::jmin(targetBeat, lengthBeats);

      if (onTimelineSeek)
        onTimelineSeek(targetBeat);
    }
  };

  // --- Public Members ---
public:
  CustomKeyboard horizontalKeyboard, verticalKeyboard;
  WheelComponent pitchWheel, modWheel;
  TimelineComponent timeline;
  juce::TextButton btnViewMode;
  SpliceEditor spliceEditor;
  PlayView playView;

  // References to logic
  juce::MidiKeyboardState &keyboardState;

  // OWNED MEMBER now, no longer a reference to MainComponent stuff
  ComplexPianoRoll trackGrid;

  SequencerPanel &sequencer;
  std::function<void(float)> onProbabilityChange;
  std::function<void(int)> onSequencerChannelChange;

  std::function<void(double)> onTimelineSeek;
  std::function<void(int)> onOctaveShift; // Z/X keys (no UI buttons)

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerformancePanel)
};
