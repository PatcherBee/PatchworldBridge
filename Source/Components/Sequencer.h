/*
  ==============================================================================
    Source/Components/Sequencer.h
  ==============================================================================
*/
#pragma once
#include "Common.h"
#include <JuceHeader.h>


class StepSequencer : public juce::Component {
public:
  juce::ComboBox cmbSteps, cmbRate;
  juce::Label lblTitle{{}, "Sequencer"};
  juce::Slider noteSlider;
  juce::Label lblNote{{}, "Root:"};
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
    addAndMakeVisible(cmbRate);
    cmbRate.addItem("1/1", 1);
    cmbRate.addItem("1/2", 2);
    cmbRate.addItem("1/4", 3);
    cmbRate.addItem("1/8", 4);
    cmbRate.addItem("1/16", 5);
    cmbRate.addItem("1/32", 6);
    cmbRate.setSelectedId(5, juce::dontSendNotification);

    addAndMakeVisible(noteSlider);
    noteSlider.setRange(36, 72, 1);
    noteSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    noteSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);

    noteSlider.textFromValueFunction = [](double value) {
      return juce::MidiMessage::getMidiNoteName((int)value, true, true, 3);
    };
    noteSlider.setValue(60, juce::sendNotificationSync);

    addAndMakeVisible(lblNote);
    lblNote.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(btnClear);
    btnClear.onClick = [this] {
      for (auto *b : stepButtons)
        b->setToggleState(false, juce::dontSendNotification);
    };

    rebuildSteps(16);
  }
  void rebuildSteps(int count) {
    numSteps = count;
    stepButtons.clear();
    for (int i = 0; i < numSteps; ++i) {
      auto *b = stepButtons.add(new juce::ToggleButton(juce::String(i + 1)));
      b->setColour(juce::ToggleButton::tickColourId, Theme::accent);
      addAndMakeVisible(b);
    }
    resized();
  }
  void setActiveStep(int s) {
    if (currentStep != s) {
      currentStep = s % numSteps;
      repaint();
    }
  }
  bool isStepActive(int s) {
    if (s >= 0 && s < stepButtons.size())
      return stepButtons[s]->getToggleState();
    return false;
  }
  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgPanel);
    g.setColour(juce::Colours::grey);
    g.drawRect(getLocalBounds(), 1);
    if (numSteps > 0 && currentStep >= 0) {
      float stepW = (float)getWidth() / numSteps;
      g.setColour(Theme::accent.withAlpha(0.6f));
      g.fillRect((float)(currentStep * stepW), 30.0f, stepW,
                 (float)(getHeight() - 30));
    }
  }
  void resized() override {
    auto r = getLocalBounds().reduced(2);
    auto head = r.removeFromTop(30);
    btnClear.setBounds(head.removeFromRight(50).reduced(2));
    lblTitle.setBounds(head.removeFromLeft(70));
    cmbSteps.setBounds(head.removeFromLeft(60));
    cmbRate.setBounds(head.removeFromLeft(70));
    head.removeFromLeft(10);
    lblNote.setBounds(head.removeFromLeft(40));
    noteSlider.setBounds(head.removeFromLeft(100));
    if (numSteps > 0) {
      float w = (float)r.getWidth() / numSteps;
      for (int i = 0; i < numSteps; ++i)
        stepButtons[i]->setBounds((int)(r.getX() + i * w), r.getY(), (int)w,
                                  r.getHeight());
    }
  }
};

class ComplexPianoRoll : public juce::Component {
public:
  juce::MidiKeyboardState &keyboardState;
  struct ChannelInfo {
    int channel;
    int program;
    juce::String name;
  };
  std::vector<ChannelInfo> activeTracks;
  bool isVerticalStripMode = false;

  ComplexPianoRoll(juce::MidiKeyboardState &s) : keyboardState(s) {}

  void setPixelsPerSecond(float) {}
  void setVisualTranspose(int) {}
  void setPlayhead(float) {}

  void setVerticalStripMode(bool b) {
    isVerticalStripMode = b;
    repaint();
  }

  void loadSequence(const juce::MidiMessageSequence &s) {
    activeTracks.clear();
    std::map<int, int> chProgs;
    std::set<int> chActives;
    for (auto *ev : s) {
      if (ev->message.isNoteOn())
        chActives.insert(ev->message.getChannel());
      if (ev->message.isProgramChange())
        chProgs[ev->message.getChannel()] =
            ev->message.getProgramChangeNumber();
    }
    for (int ch : chActives) {
      int prog = chProgs.count(ch) ? chProgs[ch] : -1;
      juce::String name = "CH " + juce::String(ch);
      if (prog >= 0)
        name += " (P" + juce::String(prog) + ")";
      if (ch == 10)
        name = "DRUMS";
      activeTracks.push_back({ch, prog, name});
    }
    repaint();
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgDark);

    if (isVerticalStripMode) {
      g.setColour(Theme::grid);
      int step = getHeight() / 12;
      for (int i = 0; i < 12; ++i) {
        g.drawHorizontalLine(i * step, 0, getWidth());
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.fillRect(0, i * step, getWidth(), step - 1);
        g.setColour(Theme::grid);
      }
      g.drawRect(getLocalBounds(), 1);
      return;
    }

    if (activeTracks.empty()) {
      g.setColour(Theme::text.withAlpha(0.5f));
      g.drawText("No MIDI File Loaded", getLocalBounds(),
                 juce::Justification::centred, true);
      return;
    }

    int cols = 4;
    int itemW = getWidth() / cols;
    int itemH = 30;

    for (int i = 0; i < activeTracks.size(); ++i) {
      int row = i / cols;
      int col = i % cols;
      auto r = juce::Rectangle<int>(col * itemW, row * itemH, itemW, itemH)
                   .reduced(2);
      g.setColour(Theme::getChannelColor(activeTracks[i].channel));
      g.fillRoundedRectangle(r.toFloat(), 4.0f);
      g.setColour(juce::Colours::black.withAlpha(0.5f));
      g.drawRect(r);
      g.setColour(juce::Colours::white);
      g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
      g.drawText(activeTracks[i].name, r, juce::Justification::centred, true);
    }
  }
  void resized() override {}
};
