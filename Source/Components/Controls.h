/*
  ==============================================================================
    Source/Components/Controls.h
    Updated: Poly Aftertouch Support
  ==============================================================================
*/
#pragma once
#include "Common.h"
#include <JuceHeader.h>

class OscAddressConfig : public juce::Component {
public:
  juce::Label lblTitle{{}, "OSC Addresses"};
  juce::TextEditor ePlay, eStop, eRew, eLoop, eTap, eOctUp, eOctDn, ePanic;
  juce::Label lblGui{{}, "GUI Control"};

  // TX
  juce::Label lTXn{{}, "TX Note:"}, lTXv{{}, "TX Vel:"}, lTXoff{{}, "TX Off:"},
      lTXcc{{}, "TX CC:"}, lTXccv{{}, "TX CC Val:"}, lTXp{{}, "TX Pitch:"},
      lTXpr{{}, "TX Press:"}, lTXpoly{{}, "TX PolyAT:"}; // Added PolyAT
  juce::TextEditor eTXn, eTXv, eTXoff, eTXcc, eTXccv, eTXp, eTXpr, eTXpoly;

  // RX
  juce::Label lRXn{{}, "RX Note:"}, lRXnv{{}, "RX Vel:"},
      lRXnoff{{}, "RX Off:"};
  juce::TextEditor eRXn, eRXnv, eRXnoff;

  juce::TextEditor eVol1, eVol2;

  juce::Label lRXc{{}, "RX CC #:"}, lRXcv{{}, "RX CC Val:"},
      lRXwheel{{}, "RX Wheel:"}, lRXpress{{}, "RX Press:"},
      lRXpoly{{}, "RX PolyAT:"};
  juce::TextEditor eRXc, eRXcv, eRXwheel, eRXpress, eRXpoly; // Added PolyAT

  juce::Label lPlay{{}, "Play:"}, lStop{{}, "Stop:"}, lRew{{}, "Rew:"},
      lLoop{{}, "Loop:"}, lTap{{}, "Tap:"}, lOctUp{{}, "Oct+:"},
      lOctDn{{}, "Oct-:"}, lPanic{{}, "Panic:"};

  juce::Label lMixVol{{}, "Mixer Vol:"}, lMixMute{{}, "Mixer Mute:"},
      lArpS{{}, "Arp Spd:"}, lArpV{{}, "Arp Vel:"};
  juce::TextEditor eMixVol, eMixMute, eArpS, eArpV;

  OscAddressConfig() {
    addAndMakeVisible(lblTitle);
    lblTitle.setFont(juce::FontOptions(16.0f).withStyle("Bold"));

    // TX Addresses
    setup(lTXn, eTXn, "/ch{X}note");
    setup(lTXv, eTXv, "/ch{X}nvalue");
    setup(lTXoff, eTXoff, "/ch{X}noteoff");
    setup(lTXcc, eTXcc, "/ch{X}cc");
    setup(lTXccv, eTXccv, "/ch{X}ccvalue");
    setup(lTXp, eTXp, "/ch{X}pitch");
    setup(lTXpr, eTXpr, "/ch{X}pressure");
    setup(lTXpoly, eTXpoly, "/ch{X}pressure"); // Default TX Poly

    // RX Addresses
    setup(lRXn, eRXn, "/ch{X}n");
    setup(lRXnv, eRXnv, "/ch{X}nv");
    setup(lRXnoff, eRXnoff, "/ch{X}noff");
    setup(lRXc, eRXc, "/ch{X}c");
    setup(lRXcv, eRXcv, "/ch{X}cv");
    setup(lRXwheel, eRXwheel, "/ch{X}wheel");
    setup(lRXpress, eRXpress, "/ch{X}press");
    setup(lRXpoly, eRXpoly, "/ch{X}press"); // Default RX Poly

    addAndMakeVisible(lblGui);
    lblGui.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    setup(lPlay, ePlay, "/play");
    setup(lStop, eStop, "/stop");
    setup(lRew, eRew, "/rewind");
    setup(lLoop, eLoop, "/loop");
    setup(lTap, eTap, "/tap");
    setup(lOctUp, eOctUp, "/octup");
    setup(lOctDn, eOctDn, "/octdown");
    setup(lPanic, ePanic, "/panic");

    setup(lMixVol, eMixVol, "/mix/{X}/vol");
    setup(lMixMute, eMixMute, "/mix/{X}/mute");
    setup(lArpS, eArpS, "/arp/speed");
    setup(lArpV, eArpV, "/arp/velocity");

    addAndMakeVisible(eVol1);
    eVol1.setText("/ch1/vol");
    addAndMakeVisible(eVol2);
    eVol2.setText("/ch2/vol");

    setSize(450, 950);
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
      l.setBounds(row.removeFromLeft(70)); // Widened label
      e.setBounds(row);
      r.removeFromTop(5);
    };

    addRow(lTXn, eTXn);
    addRow(lTXv, eTXv);
    addRow(lTXoff, eTXoff);
    addRow(lTXcc, eTXcc);
    addRow(lTXccv, eTXccv);
    addRow(lTXp, eTXp);
    addRow(lTXpr, eTXpr);
    addRow(lTXpoly, eTXpoly); // Added

    r.removeFromTop(10);

    addRow(lRXn, eRXn);
    addRow(lRXnv, eRXnv);
    addRow(lRXnoff, eRXnoff);
    addRow(lRXc, eRXc);
    addRow(lRXcv, eRXcv);
    addRow(lRXwheel, eRXwheel);
    addRow(lRXpress, eRXpress);
    addRow(lRXpoly, eRXpoly); // Added

    r.removeFromTop(15);

    lblGui.setBounds(r.removeFromTop(25));
    addRow(lPlay, ePlay);
    addRow(lStop, eStop);
    addRow(lRew, eRew);
    addRow(lLoop, eLoop);
    addRow(lTap, eTap);
    addRow(lOctUp, eOctUp);
    addRow(lOctDn, eOctDn);
    addRow(lPanic, ePanic);

    r.removeFromTop(10);
    addRow(lMixVol, eMixVol);
    addRow(lMixMute, eMixMute);
    addRow(lArpS, eArpS);
    addRow(lArpV, eArpV);
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
    auto r = getLocalBounds().reduced(15, 20);
    auto sliderRow = r.removeFromTop(r.getHeight() / 2);
    int w = r.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
      controls[i]->setBounds(sliderRow.removeFromLeft(w).reduced(4, 0));
    for (int i = 4; i < 8; ++i)
      controls[i]->setBounds(r.removeFromLeft(w).reduced(4, 0));
  }
};