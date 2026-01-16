/*
  ==============================================================================
    Source/Components/Controls.h
  ==============================================================================
*/
#pragma once
#include "Common.h"
#include <JuceHeader.h>

class OscAddressConfig : public juce::Component {
public:
  juce::Label lblTitle{{}, "OSC Addresses"};
  juce::Label lblGui{{}, "GUI Control"};
  juce::Label lN{{}, "Note:"}, lV{{}, "Velocity:"}, lOff{{}, "Off:"},
      lCC{{}, "CC#:"}, lCCV{{}, "CCVal:"}, lP{{}, "Pitch:"}, lPr{{}, "Touch:"};
  juce::TextEditor eN, eV, eOff, eCC, eCCV, eP, ePr;
  juce::Label lPlay{{}, "Play:"}, lStop{{}, "Stop:"}, lRew{{}, "Rew:"},
      lLoop{{}, "Loop:"};
  juce::Label lTap{{}, "Tap:"}, lOctUp{{}, "Oct+:"}, lOctDn{{}, "Oct-:"};
  juce::TextEditor ePlay, eStop, eRew, eLoop, eTap, eOctUp, eOctDn;

  OscAddressConfig() {
    addAndMakeVisible(lblTitle);
    lblTitle.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
    setup(lN, eN, "/ch{X}note");
    setup(lV, eV, "/ch{X}nvalue");
    setup(lOff, eOff, "/ch{X}noteoff");
    setup(lCC, eCC, "/ch{X}cc");
    setup(lCCV, eCCV, "/ch{X}ccvalue");
    setup(lP, eP, "/ch{X}pitch");
    setup(lPr, ePr, "/ch{X}pressure");
    addAndMakeVisible(lblGui);
    lblGui.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    setup(lPlay, ePlay, "/play");
    setup(lStop, eStop, "/stop");
    setup(lRew, eRew, "/rewind");
    setup(lLoop, eLoop, "/loop");
    setup(lTap, eTap, "/tap");
    setup(lOctUp, eOctUp, "/octup");
    setup(lOctDn, eOctDn, "/octdown");
  }
  void setup(juce::Label &l, juce::TextEditor &e, juce::String def) {
    addAndMakeVisible(l);
    addAndMakeVisible(e);
    e.setText(def);
  }
  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgPanel.withAlpha(0.95f));
    g.setColour(Theme::accent);
    g.drawRect(getLocalBounds(), 2);
  }
  void resized() override {
    auto r = getLocalBounds().reduced(20);
    lblTitle.setBounds(r.removeFromTop(30));
    auto addRow = [&](juce::Label &l, juce::TextEditor &e) {
      auto row = r.removeFromTop(25);
      l.setBounds(row.removeFromLeft(60));
      e.setBounds(row);
      r.removeFromTop(5);
    };
    addRow(lN, eN);
    addRow(lV, eV);
    addRow(lOff, eOff);
    addRow(lCC, eCC);
    addRow(lCCV, eCCV);
    addRow(lP, eP);
    addRow(lPr, ePr);
    r.removeFromTop(15);
    lblGui.setBounds(r.removeFromTop(25));
    addRow(lPlay, ePlay);
    addRow(lStop, eStop);
    addRow(lRew, eRew);
    addRow(lLoop, eLoop);
    addRow(lTap, eTap);
    addRow(lOctUp, eOctUp);
    addRow(lOctDn, eOctDn);
  }
};

class ControlPage : public juce::Component {
public:
  struct GenericControl : public juce::Component {
    juce::Slider slider;
    juce::TextButton button;
    juce::TextEditor addrBox;
    bool isSlider = true;
    std::function<void(juce::String, float)> onAction;

    GenericControl(bool sliderMode, juce::String defaultAddr)
        : isSlider(sliderMode) {
      addrBox.setText(defaultAddr);
      addrBox.setColour(juce::TextEditor::backgroundColourId,
                        juce::Colours::black);
      addrBox.setColour(juce::TextEditor::outlineColourId, juce::Colours::grey);
      addAndMakeVisible(addrBox);

      if (isSlider) {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 15);
        slider.setRange(0.0, 1.0, 0.01);
        slider.onValueChange = [this] {
          if (onAction)
            onAction(addrBox.getText(), (float)slider.getValue());
        };
        addAndMakeVisible(slider);
      } else {
        button.setButtonText("Trig");
        button.onClick = [this] {
          if (onAction)
            onAction(addrBox.getText(), 1.0f);
        };
        addAndMakeVisible(button);
      }
    }
    void resized() override {
      auto r = getLocalBounds().reduced(2);
      addrBox.setBounds(r.removeFromBottom(20));
      if (isSlider)
        slider.setBounds(r);
      else
        button.setBounds(r.reduced(5));
    }
  };

  juce::OwnedArray<GenericControl> controls;

  ControlPage() {
    // Create 4 Sliders and 4 Buttons
    for (int i = 0; i < 4; ++i)
      controls.add(
          new GenericControl(true, "/ctrl/slider/" + juce::String(i + 1)));
    for (int i = 0; i < 4; ++i)
      controls.add(
          new GenericControl(false, "/ctrl/button/" + juce::String(i + 1)));

    for (auto *c : controls)
      addAndMakeVisible(c);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(20);
    auto sliderRow = r.removeFromTop(r.getHeight() / 2);
    int w = r.getWidth() / 4;

    for (int i = 0; i < 4; ++i)
      controls[i]->setBounds(sliderRow.removeFromLeft(w));
    for (int i = 4; i < 8; ++i)
      controls[i]->setBounds(r.removeFromLeft(w));
  }
};
