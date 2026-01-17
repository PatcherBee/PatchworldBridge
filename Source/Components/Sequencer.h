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
  juce::ComboBox cmbSteps;
  juce::Label lblTitle{{}, "Sequencer"};
  juce::OwnedArray<juce::ToggleButton> stepButtons;
  int numSteps = 16, currentStep = -1;
  juce::TextButton btnClear{"Clear"};

  StepSequencer() {
    addAndMakeVisible(lblTitle);
    lblTitle.setFont(juce::FontOptions(12.0f).withStyle("Bold"));

    addAndMakeVisible(cmbSteps);
    cmbSteps.addItemList({"4", "8", "12", "16"}, 1);
    cmbSteps.setSelectedId(4, juce::dontSendNotification);
    cmbSteps.onChange = [this] {
      rebuildSteps(cmbSteps.getText().getIntValue());
    };

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

    btnClear.onClick = [this] { clearSteps(); };
    addAndMakeVisible(btnClear);
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

  void resized() override {
    auto r = getLocalBounds().reduced(2);
    auto header = r.removeFromTop(25);
    lblTitle.setBounds(header.removeFromLeft(70));
    cmbSteps.setBounds(header.removeFromLeft(90)); // Widened
    // cmbRate removed
    btnClear.setBounds(header.removeFromRight(60).reduced(2));

    // User requested: "missing the root not slider I askedd for that was there
    // in last build"
    noteSlider.setBounds(header.removeFromRight(80).reduced(2, 0));

    auto rollRow = r.removeFromTop(22);
    int rw = rollRow.getWidth() / 4;
    btnRoll4.setBounds(rollRow.removeFromLeft(rw).reduced(1));
    btnRoll8.setBounds(rollRow.removeFromLeft(rw).reduced(1));
    btnRoll16.setBounds(rollRow.removeFromLeft(rw).reduced(1));
    btnRoll32.setBounds(rollRow.removeFromLeft(rw).reduced(1));

    r.removeFromBottom(35); // Room for track buttons
    if (stepButtons.size() > 0) {
      int sw = r.getWidth() / stepButtons.size();
      for (int i = 0; i < stepButtons.size(); ++i) {
        auto *b = stepButtons[i];
        b->setBounds(i * sw, r.getY(), sw, r.getHeight());
        // Simple visual feedback for "visual beat step indicators"
        // If currentStep matches i, we could highlight it. But ToggleButton
        // doesn't support dual state easily without lookandfeel. We will just
        // rely on standard painting or adding a component behind it? Actually,
        // let's just use the paint method to draw a highlight since the buttons
        // effectively cover the area. Wait, buttons consume clicks. Let's make
        // the buttons transparent? No, they track state. We can draw a border
        // around the active step in paint(), ensuring buttons are slightly
        // smaller or transparent? Or just set the toggle state? No, that clears
        // the sequence. Let's try custom painting in the button? Too complex.
        // Let's just draw a marker ABOVE the button row or BELOW it.
        // The user says "missing the visual beat step indicators".
        // I'll add a 'paintOverChildren' or just draw in paint and make sure
        // buttons are transparent? Simplest: Draw a rectangle in paint() at the
        // position of the current step.
      }
    }
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgDark);

    // Draw Active Step Highlight over buttons
    if (stepButtons.size() > 0 && currentStep >= 0 &&
        currentStep < stepButtons.size()) {
      auto r = stepButtons[currentStep]->getBounds();
      g.setColour(juce::Colours::white.withAlpha(0.2f));
      g.fillRect(r);
      g.setColour(Theme::accent);
      g.drawRect(r, 2.0f);
    }
  }

  // Ensure highlight is visible over buttons
  void paintOverChildren(juce::Graphics &g) override {
    if (stepButtons.size() > 0 && currentStep >= 0 &&
        currentStep < stepButtons.size()) {
      auto r = stepButtons[currentStep]->getBounds();
      g.setColour(juce::Colours::white.withAlpha(0.4f)); // Increased from 0.2
      g.fillRect(r);
      g.setColour(Theme::accent);
      g.drawRect(r, 2.0f);
    }
  }

  void clearSteps() {
    for (auto *b : stepButtons)
      b->setToggleState(false, juce::dontSendNotification);
  }
};