#pragma once
#include "../UI/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

class SetupWizard : public juce::Component {
public:
  std::function<void()> onFinished;

  SetupWizard() {
    setAlwaysOnTop(true);

    addAndMakeVisible(btnNext);
    btnNext.setButtonText("Next >");
    btnNext.setColour(juce::TextButton::buttonColourId, Theme::accent);
    btnNext.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    btnNext.onClick = [this] { advanceStep(); };

    addAndMakeVisible(btnSkip);
    btnSkip.setButtonText("Skip Tour");
    btnSkip.setColour(juce::TextButton::buttonColourId,
                      juce::Colours::transparentBlack);
    btnSkip.setColour(juce::TextButton::textColourOffId,
                      juce::Colours::white.withAlpha(0.5f));
    btnSkip.onClick = [this] { finish(); };
  }

  void setStep(const juce::String &title, const juce::String &text,
               juce::Rectangle<int> focusArea) {
    stepTitle = title;
    stepText = text;
    targetArea = focusArea;
    repaint();
  }

  // Call this from MainComponent to set the target zones
  void setHighlights(juce::Rectangle<int> extBtn, juce::Rectangle<int> logArea,
                     juce::Rectangle<int> connectBtn) {
    areas = {extBtn, connectBtn, logArea};
    if (currentStep == 0)
      updateContent();
  }

  void paint(juce::Graphics &g) override {
    int w = getWidth();
    int h = getHeight();

    // 1. Spotlight Effect (Dim Background)
    if (!targetArea.isEmpty()) {
      g.saveState();
      g.excludeClipRegion(targetArea);
      g.fillAll(juce::Colours::black.withAlpha(0.85f));
      g.restoreState();

      // Animated Spotlight Border
      float time = (float)juce::Time::getMillisecondCounter() / 200.0f;
      float pulse = 3.0f + std::sin(time) * 1.0f;

      g.setColour(Theme::accent);
      g.drawRect(targetArea.expanded(4), (int)pulse);

      // Connect line to text box
      g.drawLine((float)targetArea.getCentreX(),
                 (float)targetArea.getBottom() + 4.0f, (float)w / 2.0f,
                 (float)h - 180.0f, 2.0f);
    } else {
      g.fillAll(juce::Colours::black.withAlpha(0.85f));
    }

    // 2. Info Panel (Bottom Center)
    auto panelRect = juce::Rectangle<float>((float)w / 2.0f - 250.0f,
                                            (float)h - 200.0f, 500.0f, 150.0f);
    Theme::drawStylishPanel(g, panelRect, Theme::bgPanel, 10.0f);

    // 3. Text
    g.setColour(Theme::accent);
    g.setFont(juce::FontOptions(24.0f).withStyle("Bold"));
    g.drawText(stepTitle,
               panelRect.withTrimmedTop(15.0f).withHeight(30.0f).toNearestInt(),
               juce::Justification::centred);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(16.0f));
    g.drawFittedText(stepText, panelRect.reduced(20.0f, 50.0f).toNearestInt(),
                     juce::Justification::centred, 3);

    // 4. Progress Dots
    int dots = 3;
    int dotSize = 8;
    int startX = w / 2 - ((dots * 20) / 2);
    for (int i = 0; i < dots; ++i) {
      g.setColour(i == currentStep ? Theme::accent : juce::Colours::grey);
      g.fillEllipse((float)startX + (float)(i * 20), (float)h - 185.0f,
                    (float)dotSize, (float)dotSize);
    }
  }

  void resized() override {
    btnNext.setBounds(getWidth() / 2 + 150, getHeight() - 80, 80, 30);
    btnSkip.setBounds(getWidth() / 2 - 230, getHeight() - 80, 80, 30);
  }

private:
  juce::TextButton btnNext, btnSkip;
  juce::String stepTitle, stepText;
  juce::Rectangle<int> targetArea;
  std::vector<juce::Rectangle<int>> areas;
  int currentStep = 0;

  void updateContent() {
    if (currentStep == 0) {
      setStep("External Sync",
              "Click 'EXT' to sync the bridge to an external MIDI "
              "Clock.\nGreat for drum machines and DAWs.",
              areas.size() > 0 ? areas[0] : juce::Rectangle<int>());
    } else if (currentStep == 1) {
      setStep("OSC Connection",
              "Enter your Patchworld IP and click Connect.\nThe status light "
              "will turn Green when active.",
              areas.size() > 1 ? areas[1] : juce::Rectangle<int>());
    } else if (currentStep == 2) {
      setStep("Traffic Monitor",
              "Watch this area for incoming signals.\nBlue = OSC, Green = "
              "MIDI, Orange = Sequencer.",
              areas.size() > 2 ? areas[2] : juce::Rectangle<int>());
      btnNext.setButtonText("Finish");
    }
  }

  void advanceStep() {
    currentStep++;
    if (currentStep >= 3) {
      finish();
    } else {
      updateContent();
    }
  }

  void finish() {
    setVisible(false);
    if (onFinished)
      onFinished();
  }
};