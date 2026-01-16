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
  juce::TextButton btnRoll4{"1/4"}, btnRoll8{"1/8"}, btnRoll16{"1/16"},
      btnRoll32{"1/32"};
  int activeRollDiv = 0; // 0 = off, 4, 8, 16, 32
  juce::Slider noteSlider;
  juce::ComboBox cmbSteps, cmbRate;
  juce::Label lblTitle{{}, "Sequencer"};
  juce::OwnedArray<juce::ToggleButton> stepButtons;
  int numSteps = 16, currentStep = -1;
  juce::TextButton btnClear{"Clear"};

  StepSequencer() {
    addAndMakeVisible(lblTitle);
    lblTitle.setFont(juce::FontOptions(12.0f).withStyle("Bold"));

    // Steps Combo
    addAndMakeVisible(cmbSteps);
    cmbSteps.addItemList({"4", "8", "12", "16"}, 1);
    cmbSteps.setSelectedId(4, juce::dontSendNotification);
    cmbSteps.onChange = [this] {
      rebuildSteps(cmbSteps.getText().getIntValue());
    };

    // Rate Combo
    addAndMakeVisible(cmbRate);
    cmbRate.addItem("1/1", 1);
    cmbRate.addItem("1/2", 2);
    cmbRate.addItem("1/4", 3);
    cmbRate.addItem("1/8", 4);
    cmbRate.addItem("1/16", 5);
    cmbRate.addItem("1/32", 6);
    cmbRate.setSelectedId(5, juce::dontSendNotification);

    // Roll Buttons (Momentary)
    auto setupRoll = [&](juce::TextButton &b, int div) {
      b.setClickingTogglesState(false); // Momentary
      b.setColour(juce::TextButton::buttonOnColourId, Theme::accent);
      b.onStateChange = [this, div, &b] {
        if (b.isDown()) {
          activeRollDiv = div;
        } else {
          // Only clear if this button was the one active (prevents clearing if
          // sliding between buttons, though simple check is fine)
          if (activeRollDiv == div)
            activeRollDiv = 0;
        }
      };
      addAndMakeVisible(b);
    };
    setupRoll(btnRoll4, 1);
    setupRoll(btnRoll8, 2);
    setupRoll(btnRoll16, 4);
    setupRoll(btnRoll32, 8);

    // Root Note Slider
    addAndMakeVisible(noteSlider);
    noteSlider.setRange(36, 72, 1);
    noteSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    noteSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 30, 20);
    noteSlider.textFromValueFunction = [](double value) {
      return juce::MidiMessage::getMidiNoteName((int)value, true, true, 3);
    };
    noteSlider.setValue(60, juce::sendNotificationSync);

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
    repaint();
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
      // Use the bounds of the first step button to find the start Y
      if (stepButtons.size() > 0) {
        auto stepBounds = stepButtons[0]->getBounds();
        g.fillRect((float)(currentStep * stepW), (float)stepBounds.getY(),
                   stepW, (float)stepBounds.getHeight());
      }
    }
  }

  void resized() override {
    auto r = getLocalBounds().reduced(5);

    // Header Row: Dropdowns, Note, Clear
    auto head = r.removeFromTop(30);
    lblTitle.setBounds(head.removeFromLeft(75));
    cmbSteps.setBounds(head.removeFromLeft(65).reduced(2));
    cmbRate.setBounds(head.removeFromLeft(75).reduced(2));

    // Root Note & Clear positioned at top-right
    btnClear.setBounds(head.removeFromRight(50).reduced(2));
    noteSlider.setBounds(head.removeFromRight(100).reduced(2));

    // Momentary Roll Row
    auto rollRow = r.removeFromTop(30);
    int rollW = rollRow.getWidth() / 4;
    btnRoll4.setBounds(rollRow.removeFromLeft(rollW).reduced(2));
    btnRoll8.setBounds(rollRow.removeFromLeft(rollW).reduced(2));
    btnRoll16.setBounds(rollRow.removeFromLeft(rollW).reduced(2));
    btnRoll32.setBounds(rollRow.removeFromLeft(rollW).reduced(2));

    // Bottom: Large Beat Steps
    if (numSteps > 0) {
      r.removeFromTop(5);
      float w = (float)r.getWidth() / numSteps;
      for (int i = 0; i < numSteps; ++i)
        stepButtons[i]->setBounds((int)(r.getX() + i * w), r.getY() + 10,
                                  (int)w, r.getHeight() - 15);
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
  void mouseDown(const juce::MouseEvent &e) override {
    if (activeTracks.empty())
      return;
    int cols = 4;
    int itemW = getWidth() / cols;
    int itemH = 30;

    int col = e.x / itemW;
    int row = e.y / itemH;
    int index = row * cols + col;

    if (index >= 0 && index < (int)activeTracks.size()) {
      int ch = activeTracks[index].channel;
      // Trigger a default note on this channel
      keyboardState.noteOn(ch, 60, 0.8f);

      // Auto off after 100ms
      juce::Timer::callAfterDelay(
          100, [this, ch] { keyboardState.noteOff(ch, 60, 0.0f); });
    }
  }
  void resized() override {}
};
