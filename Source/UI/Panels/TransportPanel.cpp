/*
  ==============================================================================
    Source/UI/Panels/TransportPanel.cpp
  ==============================================================================
*/

#include "TransportPanel.h"
#include "../../Audio/AudioEngine.h"
#include "../../Audio/Metronome.h"
#include "../../Audio/MidiRouter.h"
#include "../../Core/BridgeContext.h"
#include "../PopupMenuOptions.h"
#include "../Theme.h"

TransportPanel::TransportPanel(AudioEngine &eng, MidiRouter &hand,
                               BridgeContext &ctx)
    : engine(eng), handler(hand), context(ctx) {
  // 1. Play/Stop
  addAndMakeVisible(btnPlay);
  btnPlay.setButtonText("PLAY");
  btnPlay.getProperties().set("paramID", "Transport_Play");
  btnPlay.setColour(juce::TextButton::buttonOnColourId, juce::Colours::green);
  btnPlay.setTooltip("Start playback from current position.");

  addAndMakeVisible(btnStop);
  btnStop.setButtonText("STOP");
  btnStop.getProperties().set("paramID", "Transport_Stop");
  btnStop.setTooltip("Stop playback and return to start.");

  // 2. Navigation
  addAndMakeVisible(btnPrev);
  btnPrev.setButtonText("|<");
  btnPrev.setTooltip("Go to previous cue or start.");
  btnPrev.onClick = [this] {
    if (actions.onPrev)
      actions.onPrev();
  };

  addAndMakeVisible(btnSkip);
  btnSkip.setButtonText(">|");
  btnSkip.setTooltip("Go to next cue or end.");
  btnSkip.onClick = [this] {
    if (actions.onSkip)
      actions.onSkip();
  };

  addAndMakeVisible(btnReset);
  btnReset.setButtonText("RST");
  btnReset.setTooltip("Reset transport to start position.");

  // Tempo slider + Tap moved to MainComponent top bar (right of Redo)
  addAndMakeVisible(btnResetBpm);
  btnResetBpm.setButtonText("BPM");
  btnResetBpm.setTooltip("Reset BPM to default (from Config).");

  // Octave -/+ (control .mid/MIDI/OSC and drawn note position; wired in SystemController)
  addAndMakeVisible(btnOctaveMinus);
  btnOctaveMinus.setButtonText("-");
  btnOctaveMinus.setTooltip("Octave down (transpose -12). Affects keyboard, sequencer, and OSC.");
  addAndMakeVisible(btnOctavePlus);
  btnOctavePlus.setButtonText("+");
  btnOctavePlus.setTooltip("Octave up (transpose +12). Affects keyboard, sequencer, and OSC.");

  // Nudge kept as child (hidden) for any code that still references them; tempo nudge available via BPM slider
  addChildComponent(btnNudgeMinus);
  btnNudgeMinus.setButtonText("-");
  btnNudgeMinus.setTooltip("Nudge tempo down (legacy).");
  addChildComponent(btnNudgePlus);
  btnNudgePlus.setButtonText("+");
  btnNudgePlus.setTooltip("Nudge tempo up (legacy).");
  btnNudgeMinus.setVisible(false);
  btnNudgePlus.setVisible(false);

  addAndMakeVisible(btnQuantize);
  btnQuantize.setButtonText("Quant");
  btnQuantize.setClickingTogglesState(true);
  btnQuantize.setTooltip("Toggle note quantize on/off. Right-click grid for options.");

  addAndMakeVisible(btnBlock);
  btnBlock.setButtonText("BLOCK");
  btnBlock.setClickingTogglesState(true);
  btnBlock.setTooltip("Block MIDI output (mute outbound notes/CC).");

  addAndMakeVisible(btnSnapshot);
  btnSnapshot.setButtonText("SNAP");
  btnSnapshot.setTooltip("Save or recall a snapshot of current state.");

  addAndMakeVisible(btnSplit);
  btnSplit.setButtonText("SPLIT");
  btnSplit.setClickingTogglesState(true);
  btnSplit.setTooltip("Split keyboard by note range (right-click for zones).");
  btnSplit.onRightClick = [this] { showSplitMenu(); };

  addAndMakeVisible(btnMetronome);
  btnMetronome.setButtonText("Click");
  btnMetronome.setClickingTogglesState(true);
  btnMetronome.setTooltip("Metronome click on/off. Right-click for volume and sound type.");
  btnMetronome.onRightClick = [this] { showMetronomeMenu(); };
  btnMetronome.onClick = [this] {
    if (context.metronome)
      context.metronome->setEnabled(btnMetronome.getToggleState());
  };

  // Link indicator moved to MainComponent top bar (left of THRU)
}

TransportPanel::~TransportPanel() {}

void TransportPanel::setExternalTransportRefs(juce::TextButton *undoBtn,
                                              juce::TextButton *redoBtn,
                                              juce::TextButton *linkBtn,
                                              juce::Component *linkIndicatorComp) {
  externalUndo_ = undoBtn;
  externalRedo_ = redoBtn;
  externalLink_ = linkBtn;
  externalLinkIndicator_ = linkIndicatorComp;
}

void TransportPanel::showMetronomeMenu() {
  if (!context.metronome)
    return;
  juce::PopupMenu m;
  m.addSectionHeader("Metronome Click");

  juce::PopupMenu volMenu;
  for (int pct : {25, 50, 75, 100}) {
    float v = pct / 100.0f;
    volMenu.addItem(juce::String(pct) + "%", true,
                    std::abs(context.metronome->getVolume() - v) < 0.05f,
                    [this, v] { context.metronome->setVolume(v); });
  }
  m.addSubMenu("Volume", volMenu);

  juce::PopupMenu typeMenu;
  typeMenu.addItem("Sine", true,
                  context.metronome->getClickType() == Metronome::Sine,
                  [this] { context.metronome->setClickType(Metronome::Sine); });
  typeMenu.addItem("Tick", true,
                  context.metronome->getClickType() == Metronome::Tick,
                  [this] { context.metronome->setClickType(Metronome::Tick); });
  typeMenu.addItem("Beep", true,
                  context.metronome->getClickType() == Metronome::Beep,
                  [this] { context.metronome->setClickType(Metronome::Beep); });
  m.addSubMenu("Sound type", typeMenu);

  m.showMenuAsync(PopupMenuOptions::forComponent(this));
}

void TransportPanel::showSplitMenu() {
  auto *router = context.midiRouter.get();
  if (!router)
    return;
  juce::PopupMenu m;
  m.addSectionHeader("Split zones (0-127)");
  int nz = router->getSplitNumZones();
  auto zoneLabel = [](int zones) {
    if (zones == 2) return "2 zones (0-63 \u2192 Ch1, 64-127 \u2192 Ch2)";
    if (zones == 3) return "3 zones (0-42 \u2192 Ch1, 43-84 \u2192 Ch2, 85-127 \u2192 Ch3)";
    return "4 zones (0-31 \u2192 Ch1, 32-63 \u2192 Ch2, 64-95 \u2192 Ch3, 96-127 \u2192 Ch4)";
  };
  m.addItem(zoneLabel(2), true, nz == 2, [router] { router->setSplitNumZones(2); });
  m.addItem(zoneLabel(3), true, nz == 3, [router] { router->setSplitNumZones(3); });
  m.addItem(zoneLabel(4), true, nz == 4, [router] { router->setSplitNumZones(4); });
  m.addSeparator();
  m.addSectionHeader("Zone output channel");
  for (int z = 0; z < nz; ++z) {
    juce::PopupMenu chMenu;
    for (int ch = 1; ch <= 16; ++ch) {
      chMenu.addItem("Ch " + juce::String(ch), true,
                     router->getSplitZoneChannel(z) == ch,
                     [router, z, ch] { router->setSplitZoneChannel(z, ch); });
    }
    int low = (z == 0) ? 0 : (128 * z / nz);
    int high = (z + 1 == nz) ? 127 : (128 * (z + 1) / nz - 1);
    m.addSubMenu("Zone " + juce::String(z + 1) + " (notes " + juce::String(low) + "-" + juce::String(high) + ") \u2192 Ch" + juce::String(router->getSplitZoneChannel(z)), chMenu);
  }
  m.showMenuAsync(PopupMenuOptions::forComponent(this));
}

void TransportPanel::paint(juce::Graphics &g) {
  g.fillAll(Theme::bgDark.brighter(0.05f));
  g.setColour(Theme::grid);
  g.drawRect(getLocalBounds(), 1);
}

void TransportPanel::resized() {
  auto r = getLocalBounds().reduced(4);
  const int btnW = 40;
  const int playW = 52;
  const int gap = 6;
  const int tx = getX(), ty = getY();

  // Left: Prev, Stop, Play, Skip, RST, Undo, Redo (Undo/Redo from external refs when set)
  btnPrev.setBounds(r.removeFromLeft(btnW).reduced(1));
  r.removeFromLeft(1);
  btnStop.setBounds(r.removeFromLeft(btnW).reduced(1));
  r.removeFromLeft(1);
  btnPlay.setBounds(r.removeFromLeft(playW).reduced(1));
  r.removeFromLeft(1);
  btnSkip.setBounds(r.removeFromLeft(btnW).reduced(1));
  r.removeFromLeft(1);
  btnReset.setBounds(r.removeFromLeft(btnW).reduced(1));
  r.removeFromLeft(gap);
  if (externalUndo_) {
    auto slot = r.removeFromLeft(btnW).reduced(1);
    externalUndo_->setBounds(slot.getX() + tx, slot.getY() + ty, slot.getWidth(), slot.getHeight());
    r.removeFromLeft(1);
  }
  if (externalRedo_) {
    auto slot = r.removeFromLeft(btnW).reduced(1);
    externalRedo_->setBounds(slot.getX() + tx, slot.getY() + ty, slot.getWidth(), slot.getHeight());
  }
  r.removeFromLeft(gap);

  // Link, Link indicator (external), then Octave -/+
  if (externalLink_) {
    auto slot = r.removeFromLeft(40).reduced(1);
    externalLink_->setBounds(slot.getX() + tx, slot.getY() + ty, slot.getWidth(), slot.getHeight());
    r.removeFromLeft(2);
  }
  if (externalLinkIndicator_) {
    auto slot = r.removeFromLeft(88).reduced(2);
    externalLinkIndicator_->setBounds(slot.getX() + tx, slot.getY() + ty, slot.getWidth(), slot.getHeight());
  }
  r.removeFromLeft(gap);

  // -, +, Block, Snap, Split (left of Click)
  btnOctaveMinus.setBounds(r.removeFromLeft(btnW).reduced(1));
  r.removeFromLeft(1);
  btnOctavePlus.setBounds(r.removeFromLeft(btnW).reduced(1));
  r.removeFromLeft(gap);
  btnBlock.setBounds(r.removeFromLeft(50).reduced(2));
  r.removeFromLeft(2);
  btnSnapshot.setBounds(r.removeFromLeft(50).reduced(2));
  r.removeFromLeft(2);
  btnSplit.setBounds(r.removeFromLeft(50).reduced(2));
  r.removeFromLeft(gap);

  // Click, then Quant (right)
  btnQuantize.setBounds(r.removeFromRight(48).reduced(2));
  r.removeFromRight(2);
  btnMetronome.setBounds(r.removeFromRight(52).reduced(2));
}

void TransportPanel::setNumLinkPeers(int n) {
  juce::ignoreUnused(n);
  // Link indicator and tooltip live on MainComponent top bar
}
