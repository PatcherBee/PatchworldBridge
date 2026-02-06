/*
  ==============================================================================
    Source/UI/Panels/ArpeggiatorPanel.h
    Role: Standalone arpeggiator controls panel (extracted from SidebarPanel.h)
  ==============================================================================
*/
#pragma once
#include "../ControlHelpers.h"
#include "../Theme.h"
#include "../Fonts.h"
#include "../Widgets/ProKnob.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

class ArpeggiatorPanel : public juce::Component {
public:
  juce::ComboBox cmbArpPattern;
  ProKnob knobArpSpeed{"Rate"}, knobArpVel{"Vel"}, knobArpGate{"Gate"};
  ControlHelpers::ResponsiveSlider sliderArpOctave;
  juce::TextButton btnArpLatch, btnArpSync, btnBlockBpm;
  juce::TextButton btnArpOn;  // Tiny ON: when enabled, all MIDI in/keyboard runs through arp (Rate/Vel/Gate from dials)

  std::function<void(int, int, int, int, float)> onArpUpdate;
  std::function<void(bool)> onBpmBlockChanged;
  std::function<void(bool)> onArpOnChanged;

  /** Live indicator: phase 0–1 within current bar when playing (for beat/phase bar). */
  void setLivePhase(float phase01) {
    phase01 = juce::jlimit(0.0f, 1.0f, phase01);
    if (std::abs(phase01 - livePhase_) > 0.001f) {
      livePhase_ = phase01;
      repaint();
    }
  }

  ArpeggiatorPanel() {
    addAndMakeVisible(cmbArpPattern);
    cmbArpPattern.addItemList({"Up", "Down", "UpDown", "DownUp", "Random",
                               "Chord", "Diverge", "Play Order"},
                              1);
    cmbArpPattern.setSelectedId(1);

    auto setupKnob = [this](ProKnob &k, double min, double max, double def) {
      addAndMakeVisible(k);
      k.setRange(min, max, (max - min > 10 ? 1 : 0.01));
      k.setValue(def);
      k.setDoubleClickReturnValue(true, def);
    };
    setupKnob(knobArpSpeed, 1, 32, 16);
    setupKnob(knobArpVel, 0, 127, 100);
    setupKnob(knobArpGate, 0.1, 1.0, 0.5);
    knobArpSpeed.getProperties().set("paramID", "Arp_Rate");
    knobArpVel.getProperties().set("paramID", "Arp_Vel");
    knobArpGate.getProperties().set("paramID", "Arp_Gate");

    addAndMakeVisible(sliderArpOctave);
    sliderArpOctave.setSliderStyle(juce::Slider::LinearBar);
    sliderArpOctave.setRange(1, 4, 1);
    sliderArpOctave.setValue(1);
    sliderArpOctave.setDefaultValue(1);
    sliderArpOctave.setTextValueSuffix(" Oct");
    sliderArpOctave.getProperties().set("paramID", "Arp_Octave");

    auto setupBtn = [this](juce::TextButton &b, juce::String t,
                           juce::Colour c) {
      addAndMakeVisible(b);
      b.setButtonText(t);
      b.setClickingTogglesState(true);
      b.setColour(juce::TextButton::buttonOnColourId, c);
    };

    setupBtn(btnArpLatch, "Latch", juce::Colours::blue.darker(0.2f));
    setupBtn(btnArpSync, "Sync", juce::Colours::orange.darker(0.2f));
    setupBtn(btnBlockBpm, "Lock BPM", juce::Colours::red.darker(0.3f));
    btnArpLatch.setTooltip("Latch: hold notes after key release so arp keeps playing.");
    btnArpSync.setTooltip("Sync: lock arp rate to Link tempo (recommended when using Link).");
    btnBlockBpm.setTooltip("Lock BPM: prevent arp sync from changing project tempo when enabled.");

    addAndMakeVisible(btnArpOn);
    btnArpOn.setButtonText("ON");
    btnArpOn.setClickingTogglesState(true);
    btnArpOn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::green.darker(0.2f));
    btnArpOn.setTooltip("Route all MIDI in / keyboard / virtual through arp (Rate, Vel, Gate from dials)");
    btnArpOn.onClick = [this] {
      if (onArpOnChanged)
        onArpOnChanged(btnArpOn.getToggleState());
    };

    btnBlockBpm.onClick = [this] {
      if (onBpmBlockChanged)
        onBpmBlockChanged(btnBlockBpm.getToggleState());
    };

    auto notify = [this] {
      if (onArpUpdate)
        onArpUpdate((int)knobArpSpeed.getValue(),
                    (int)knobArpVel.getValue(), cmbArpPattern.getSelectedId(),
                    (int)sliderArpOctave.getValue(),
                    (float)knobArpGate.getValue());
    };

    knobArpSpeed.onValueChange = notify;
    knobArpVel.onValueChange = notify;
    sliderArpOctave.onValueChange = notify;
    knobArpGate.onValueChange = notify;
    cmbArpPattern.onChange = notify;
  }

  void resized() override {
    const int pad = 6;
    const int rowGap = 6;
    auto r = getLocalBounds().reduced(pad);
    livePhaseArea_ = r.removeFromBottom(6).reduced(0, 2);
    int w = r.getWidth();

    // Top row: Pattern dropdown | Sync | Octave — fixed heights, flexible widths
    auto topRow = r.removeFromTop(24);
    int patternW = juce::jmin(100, w / 4);
    int syncW = 44;
    int octaveW = juce::jmin(70, w - patternW - syncW - 16);
    int gap = 4;
    cmbArpPattern.setBounds(topRow.removeFromLeft(patternW).reduced(2));
    topRow.removeFromLeft(gap);
    btnArpSync.setBounds(topRow.removeFromLeft(syncW).reduced(2));
    topRow.removeFromLeft(gap);
    sliderArpOctave.setBounds(topRow.removeFromLeft(octaveW).reduced(2));

    r.removeFromTop(rowGap);

    // Knob row: Rate, Vel (slightly smaller), Gate (same size) — centred
    auto knobRow = r.removeFromTop(76);
    int knobTotal = knobRow.getWidth();
    const int gateKnobW = juce::jmax(36, (knobTotal - (3 + 1) * 4) / 3);
    const int rateVelKnobW = juce::jmax(30, gateKnobW - 5);  // Slightly smaller than Gate
    int totalKnobW = rateVelKnobW + 4 + rateVelKnobW + 4 + gateKnobW;
    int knobStart = (knobTotal - totalKnobW) / 2;
    if (knobStart > 0)
      knobRow.removeFromLeft(knobStart);
    knobArpSpeed.setBounds(knobRow.removeFromLeft(rateVelKnobW).reduced(2));
    knobRow.removeFromLeft(4);
    knobArpVel.setBounds(knobRow.removeFromLeft(rateVelKnobW).reduced(2));
    knobRow.removeFromLeft(4);
    knobArpGate.setBounds(knobRow.removeFromLeft(gateKnobW).reduced(2));

    r.removeFromTop(rowGap + 4);

    // Bottom row: Latch, Lock BPM, tiny ON button
    auto btnRow = r.removeFromTop(24);
    const int latchW = 42, lockBpmW = 56, arpOnSize = 18;
    btnArpLatch.setBounds(btnRow.removeFromLeft(latchW).reduced(2));
    btnRow.removeFromLeft(gap);
    btnBlockBpm.setBounds(btnRow.removeFromLeft(lockBpmW).reduced(2));
    btnRow.removeFromLeft(gap);
    btnArpOn.setBounds(btnRow.removeFromLeft(arpOnSize).reduced(1).withHeight(arpOnSize));
  }

  void paint(juce::Graphics& g) override {
    juce::Component::paint(g);
    if (livePhaseArea_.getHeight() < 4) return;
    g.setColour(Theme::bgPanel.darker(0.2f));
    g.fillRoundedRectangle(livePhaseArea_.toFloat(), 2.0f);
    if (btnArpOn.getToggleState() && livePhase_ >= 0.0f) {
      int w = (int)(livePhaseArea_.getWidth() * livePhase_ + 0.5f);
      if (w > 0) {
        g.setColour(Theme::accent.withAlpha(0.7f));
        g.fillRoundedRectangle(livePhaseArea_.withWidth(w).toFloat(), 2.0f);
      }
    }
  }

private:
  float livePhase_ = -1.0f;
  juce::Rectangle<int> livePhaseArea_;
};
