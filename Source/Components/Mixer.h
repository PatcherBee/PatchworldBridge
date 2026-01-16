/*
  ==============================================================================
    Source/Components/Mixer.h
  ==============================================================================
*/
#pragma once
#include "Common.h"
#include <JuceHeader.h>


class MixerContainer : public juce::Component {
public:
  struct MixerStrip : public juce::Component, public juce::Slider::Listener {
    juce::Slider volSlider;
    juce::TextEditor nameLabel;
    juce::ToggleButton btnActive;
    int channelIndex;
    std::function<void(int, float)> onLevelChange;
    std::function<void(int, bool)> onActiveChange;
    MixerStrip(int i) : channelIndex(i) {
      volSlider.setSliderStyle(juce::Slider::LinearVertical);
      volSlider.setRange(0, 127, 1);
      volSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
      volSlider.addListener(this);
      addAndMakeVisible(volSlider);
      nameLabel.setText(juce::String(i + 1));
      nameLabel.setFont(juce::FontOptions(12.0f));
      nameLabel.setJustification(juce::Justification::centred);
      nameLabel.setColour(juce::TextEditor::backgroundColourId,
                          juce::Colours::transparentBlack);
      nameLabel.setColour(juce::TextEditor::outlineColourId,
                          juce::Colours::transparentBlack);
      addAndMakeVisible(nameLabel);
      btnActive.setToggleState(true, juce::dontSendNotification);
      btnActive.setButtonText("ON");
      btnActive.onClick = [this] {
        if (onActiveChange)
          onActiveChange(channelIndex + 1, btnActive.getToggleState());
      };
      addAndMakeVisible(btnActive);
    }
    void sliderValueChanged(juce::Slider *s) override {
      if (onLevelChange)
        onLevelChange(channelIndex + 1, (float)s->getValue());
    }
    void paint(juce::Graphics &g) override {
      auto r = getLocalBounds().reduced(2);
      g.setColour(Theme::bgPanel);
      g.fillRoundedRectangle(r.toFloat(), 4.0f);
      if (btnActive.getToggleState()) {
        g.setColour(Theme::getChannelColor(channelIndex + 1).withAlpha(0.2f));
        g.fillRoundedRectangle(r.toFloat(), 4.0f);
      }
      float level = (float)volSlider.getValue() / 127.0f;
      int h = (int)((volSlider.getHeight()) * level);
      g.setColour(Theme::getChannelColor(channelIndex + 1).withAlpha(0.5f));
      g.fillRect((float)volSlider.getX(), (float)(volSlider.getBottom() - h),
                 (float)volSlider.getWidth(), (float)h);
    }
    void resized() override {
      nameLabel.setBounds(0, getHeight() - 20, getWidth(), 20);
      btnActive.setBounds(0, 2, getWidth(), 15);
      volSlider.setBounds(0, 20, getWidth(), getHeight() - 40);
    }
  };

  juce::OwnedArray<MixerStrip> strips;
  std::function<void(int, float)> onMixerActivity;
  std::function<void(int, bool)> onChannelToggle;
  const int stripWidth = 60;
  MixerContainer() {
    for (int i = 0; i < 16; ++i) {
      auto *s = strips.add(new MixerStrip(i));
      s->onLevelChange = [this](int ch, float val) {
        if (onMixerActivity)
          onMixerActivity(ch, val);
      };
      s->onActiveChange = [this](int ch, bool active) {
        if (onChannelToggle)
          onChannelToggle(ch, active);
      };
      addAndMakeVisible(s);
    }
  }
  juce::String getChannelName(int ch) {
    if (ch < 1 || ch > 16)
      return juce::String(ch);
    return strips[ch - 1]->nameLabel.getText();
  }
  void resized() override {
    for (int i = 0; i < 16; ++i)
      strips[i]->setBounds(i * stripWidth, 0, stripWidth, getHeight());
  }
};
