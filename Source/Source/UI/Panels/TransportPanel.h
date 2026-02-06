/*
  ==============================================================================
    Source/UI/Panels/TransportPanel.h
    Role: Playback controls, BPM slider, and Sync stats.
  ==============================================================================
*/
#pragma once
#include "../ControlHelpers.h"
#include "../Theme.h"
#include "../Widgets/HoverGlowButton.h"
#include "../Widgets/LinkBeatIndicator.h"
#include <cmath>
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

class AudioEngine;
class MidiRouter;
class BridgeContext;

/** TextButton that shows a context menu on right-click without toggling. */
class MetronomeClickButton : public juce::TextButton {
public:
  std::function<void()> onRightClick;
  void mouseDown(const juce::MouseEvent &e) override {
    if (e.mods.isRightButtonDown()) {
      if (onRightClick)
        onRightClick();
      return;
    }
    juce::TextButton::mouseDown(e);
  }
};

/** TextButton that shows a context menu on right-click without toggling. */
class SplitButtonWithMenu : public juce::TextButton {
public:
  std::function<void()> onRightClick;
  void mouseDown(const juce::MouseEvent &e) override {
    if (e.mods.isRightButtonDown()) {
      if (onRightClick)
        onRightClick();
      return;
    }
    juce::TextButton::mouseDown(e);
  }
};

class TransportPanel : public juce::Component {
public:
  TransportPanel(AudioEngine &eng, MidiRouter &hand, BridgeContext &ctx);
  ~TransportPanel() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  /** Link peer count is shown on MainComponent's linkIndicator. */
  void setNumLinkPeers(int n);

  /** Optional refs to MainComponent's Undo, Redo, Link, linkIndicator for layout in transport row (Dashboard). */
  void setExternalTransportRefs(juce::TextButton *undoBtn, juce::TextButton *redoBtn,
                               juce::TextButton *linkBtn, juce::Component *linkIndicatorComp);

  struct {
    std::function<void()> onPlay, onStop, onPrev, onSkip, onReset;
  } actions;

  std::function<void(double)> onBpmChange;
  std::function<void(double)> onNudge;

  /** BPM slider: double-click opens value for typing (no reset). Used in MainComponent top bar. */
  struct BpmSlider : HoverGlowResponsiveSlider {
    void mouseDoubleClick(const juce::MouseEvent &) override {
      showTextBox();
    }
  };
  HoverGlowButton btnPlay, btnStop, btnPrev, btnSkip, btnReset;
  juce::TextButton btnResetBpm,
      btnNudgeMinus, btnNudgePlus, btnQuantize;
  juce::TextButton btnOctaveMinus, btnOctavePlus;
  juce::TextButton btnBlock, btnSnapshot;
  SplitButtonWithMenu btnSplit;
  MetronomeClickButton btnMetronome;

private:
  void showMetronomeMenu();
  void showSplitMenu();
  AudioEngine &engine;
  MidiRouter &handler;
  BridgeContext &context;
  juce::TextButton *externalUndo_ = nullptr;
  juce::TextButton *externalRedo_ = nullptr;
  juce::TextButton *externalLink_ = nullptr;
  juce::Component *externalLinkIndicator_ = nullptr;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportPanel)
};
