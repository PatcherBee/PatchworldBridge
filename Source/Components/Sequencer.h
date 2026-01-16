/*
  ==============================================================================
    Source/Components/Sequencer.h
    Status: FIXED (Restored Track Logic for MIDI Files)
  ==============================================================================
*/
#pragma once
#include "Common.h"
#include <JuceHeader.h>

class StepSequencer : public juce::Component {
public:
  // --- RESTORED TRACK STRUCTURE ---
  struct Track {
    int channel;
    int program;
    juce::String name;
  };
  std::vector<Track> activeTracks;

  juce::TextButton btnRoll4{"1/4"}, btnRoll8{"1/8"}, btnRoll16{"1/16"},
      btnRoll32{"1/32"};
  int activeRollDiv = 0;
  juce::Slider noteSlider;
  juce::ComboBox cmbSteps, cmbRate;
  juce::Label lblTitle{{}, "Sequencer"};
  juce::OwnedArray<juce::ToggleButton> stepButtons;
  int numSteps = 16, currentStep = -1;
  juce::TextButton btnClear{"Clear Log"};

  StepSequencer() {
    addAndMakeVisible(lblTitle);
    lblTitle.setFont(juce::FontOptions(12.0f).withStyle("Bold"));

    addAndMakeVisible(cmbSteps);
    cmbSteps.addItemList({"4", "8", "12", "16"}, 1);
    cmbSteps.setSelectedId(4, juce::dontSendNotification);
    cmbSteps.onChange = [this] {
      rebuildSteps(cmbSteps.getText().getIntValue());
    };

    addAndMakeVisible(cmbRate);
    cmbRate.addItem("1/1", 1);
    cmbRate.addItem("1/2", 2);
    cmbRate.addItem("1/4", 3);
    cmbRate.addItem("1/8", 4);
    cmbRate.addItem("1/16", 5);
    cmbRate.addItem("1/32", 6);
    cmbRate.setSelectedId(3, juce::dontSendNotification);

    noteSlider.setSliderStyle(juce::Slider::LinearBar);
    noteSlider.setRange(0, 127, 1);
    noteSlider.setValue(60);
    addAndMakeVisible(noteSlider);

    auto setupRoll = [&](juce::TextButton &b, int div) {
      b.setClickingTogglesState(true);
      b.setRadioGroupId(101);
      b.setColour(juce::TextButton::buttonOnColourId, Theme::accent);
      b.onClick = [this, &b, div] {
        activeRollDiv = b.getToggleState() ? div : 0;
      };
      addAndMakeVisible(b);
    };

    setupRoll(btnRoll4, 4);
    setupRoll(btnRoll8, 8);
    setupRoll(btnRoll16, 16);
    setupRoll(btnRoll32, 32);

    rebuildSteps(16);
  }

  // --- RESTORED METHOD ---
  void addTrack(int ch, int prog, juce::String name) {
    activeTracks.push_back({ch, prog, name});
    repaint();
  }

  void rebuildSteps(int count) {
    stepButtons.clear();
    numSteps = count;
    for (int i = 0; i < numSteps; ++i) {
      auto *b = stepButtons.add(new juce::ToggleButton());
      b->setColour(juce::ToggleButton::tickColourId, Theme::accent);
      addAndMakeVisible(b);
    }
    resized();
  }

  void setActiveStep(int step) {
    currentStep = step;
    juce::MessageManager::callAsync([this] { repaint(); });
  }

  bool isStepActive(int step) {
    if (step >= 0 && step < stepButtons.size())
      return stepButtons[step]->getToggleState();
    return false;
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgDark);

    // Draw the Track Buttons (The part you said was "fucked")
    if (activeTracks.empty()) {
      g.setColour(juce::Colours::grey);
      g.drawText("No MIDI Tracks", getLocalBounds().removeFromBottom(40),
                 juce::Justification::centred);
    } else {
      int tw = getWidth() / juce::jmax(1, (int)activeTracks.size());
      auto r = getLocalBounds().removeFromBottom(30);
      for (int i = 0; i < activeTracks.size(); ++i) {
        auto trackRect = r.removeFromLeft(tw).reduced(2);
        g.setColour(Theme::getChannelColor(activeTracks[i].channel));
        g.fillRoundedRectangle(trackRect.toFloat(), 4.0f);
        g.setColour(juce::Colours::white);
        g.drawText(activeTracks[i].name, trackRect,
                   juce::Justification::centred);
      }
    }
  }

  void resized() override {
    auto r = getLocalBounds().reduced(2);
    auto header = r.removeFromTop(25);
    lblTitle.setBounds(header.removeFromLeft(70));
    cmbSteps.setBounds(header.removeFromLeft(45));
    cmbRate.setBounds(header.removeFromLeft(55));

    auto rollRow = r.removeFromTop(22);
    int rw = rollRow.getWidth() / 4;
    btnRoll4.setBounds(rollRow.removeFromLeft(rw).reduced(1));
    btnRoll8.setBounds(rollRow.removeFromLeft(rw).reduced(1));
    btnRoll16.setBounds(rollRow.removeFromLeft(rw).reduced(1));
    btnRoll32.setBounds(rollRow.removeFromLeft(rw).reduced(1));

    r.removeFromBottom(35); // Room for track buttons
    if (stepButtons.size() > 0) {
      int sw = r.getWidth() / stepButtons.size();
      for (int i = 0; i < stepButtons.size(); ++i)
        stepButtons[i]->setBounds(i * sw, r.getY(), sw, r.getHeight());
    }
  }
};