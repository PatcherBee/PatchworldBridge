/*
  ==============================================================================
    Source/Components/Sequencer.h
    Status: FIXED (Added missing isStepActive method)
  ==============================================================================
*/
#pragma once
#include "Common.h"
#include <JuceHeader.h>

class StepSequencer : public juce::Component {
public:
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
  juce::ComboBox cmbSteps;
  juce::ComboBox cmbSeqChannel; // New Output Channel selection
  int outputChannel = 1;

  juce::Label lblTitle{{}, "Sequencer"};
  juce::OwnedArray<juce::ToggleButton> stepButtons;
  int numSteps = 16, currentStep = -1;
  juce::TextButton btnClear{"Clear"};

  enum Mode { Time, Loop, Roll };
  Mode currentMode = Mode::Time;
  juce::ComboBox cmbMode;

  // Roll/Loop State
  double rollCaptureBeat = 0.0;
  bool isRollActive = false;
  int lastRollFiredStep = -1;

  StepSequencer() {
    addAndMakeVisible(lblTitle);
    lblTitle.setFont(juce::FontOptions(12.0f).withStyle("Bold"));

    addAndMakeVisible(cmbSteps);
    cmbSteps.addItemList({"4", "8", "12", "16"}, 1);
    cmbSteps.setSelectedId(4, juce::dontSendNotification);
    cmbSteps.onChange = [this] {
      rebuildSteps(cmbSteps.getText().getIntValue());
    };

    addAndMakeVisible(cmbMode);
    cmbMode.addItemList({"Time", "Loop", "Roll"}, 1);
    // User requested Loop as default. Index 1=Time, 2=Loop, 3=Roll
    cmbMode.setSelectedId(2, juce::dontSendNotification);
    currentMode = Mode::Loop;
    cmbMode.onChange = [this] {
      currentMode = (Mode)(cmbMode.getSelectedId() - 1);
    };

    // Output Channel Dropdown
    addAndMakeVisible(cmbSeqChannel);
    for (int i = 1; i <= 16; ++i)
      cmbSeqChannel.addItem(juce::String(i), i);
    cmbSeqChannel.setSelectedId(1, juce::dontSendNotification);
    cmbSeqChannel.onChange = [this] {
      outputChannel = cmbSeqChannel.getSelectedId();
    };

    noteSlider.setSliderStyle(juce::Slider::LinearBar);
    noteSlider.setRange(0, 127, 1);
    noteSlider.setValue(60);
    addAndMakeVisible(noteSlider);

    auto setupRoll = [&](juce::TextButton &b, int div) {
      b.setClickingTogglesState(false); // Momentary
      b.setColour(juce::TextButton::buttonOnColourId, Theme::accent);
      b.onStateChange = [this, &b, div] {
        if (b.isMouseButtonDown())
          activeRollDiv = div;
        else
          activeRollDiv = 0;
      };
      addAndMakeVisible(b);
    };

    setupRoll(btnRoll4, 4);
    setupRoll(btnRoll8, 8);
    setupRoll(btnRoll16, 16);
    setupRoll(btnRoll32, 32);

    rebuildSteps(16);

    btnClear.onClick = [this] { clearSteps(); };
    addAndMakeVisible(btnClear);
  }

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
      b->setButtonText(juce::String(i + 1));
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

  Mode getMode() const { return currentMode; }

  void resized() override {
    auto r = getLocalBounds().reduced(2);
    auto header = r.removeFromTop(30);
    lblTitle.setBounds(header.removeFromLeft(70));
    cmbSteps.setBounds(header.removeFromLeft(50));
    cmbMode.setBounds(
        header.removeFromLeft(70).reduced(5, 0)); // Added Mode Menu

    btnClear.setBounds(header.removeFromRight(50).reduced(2));
    noteSlider.setBounds(header.removeFromRight(50).reduced(2, 0));
    cmbSeqChannel.setBounds(
        header.removeFromRight(50).reduced(2, 0)); // Left of Note Slider

    auto rollRow = r.removeFromTop(25);
    int rw = rollRow.getWidth() / 4;
    btnRoll4.setBounds(rollRow.removeFromLeft(rw).reduced(1));
    btnRoll8.setBounds(rollRow.removeFromLeft(rw).reduced(1));
    btnRoll16.setBounds(rollRow.removeFromLeft(rw).reduced(1));
    btnRoll32.setBounds(rollRow.removeFromLeft(rw).reduced(1));

    r.removeFromTop(10); // Gap

    // Beat steps - Larger and lower
    if (stepButtons.size() > 0) {
      int sw = r.getWidth() / stepButtons.size();
      for (int i = 0; i < stepButtons.size(); ++i) {
        stepButtons[i]->setBounds(i * sw, r.getY(), sw, r.getHeight() - 10);
      }
    }
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgDark);
    if (stepButtons.size() > 0 && currentStep >= 0 &&
        currentStep < stepButtons.size()) {
      auto r = stepButtons[currentStep]->getBounds();
      g.setColour(juce::Colours::white.withAlpha(0.2f));
      g.fillRect(r);
      g.setColour(Theme::accent);
      g.drawRect(r, 2.0f);
    }
  }

  void paintOverChildren(juce::Graphics &g) override {
    if (stepButtons.size() > 0 && currentStep >= 0 &&
        currentStep < stepButtons.size()) {
      auto r = stepButtons[currentStep]->getBounds();
      g.setColour(juce::Colours::white.withAlpha(0.3f));
      g.fillRect(r.reduced(1));
    }
  }

  void clearSteps() {
    for (auto *b : stepButtons)
      b->setToggleState(false, juce::dontSendNotification);
  }
};