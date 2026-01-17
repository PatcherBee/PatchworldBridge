/*
  ==============================================================================
    Source/Components/Mixer.h
  ==============================================================================
*/
#pragma once
#include "Common.h"
#include <JuceHeader.h>

class MixerContainer : public juce::Component,
                       public juce::DragAndDropContainer {
public:
  struct MixerStrip : public juce::Component,
                      public juce::Slider::Listener,
                      public juce::DragAndDropTarget {
    juce::Slider volSlider;
    juce::TextEditor nameLabel;
    juce::ToggleButton btnActive;
    int channelIndex;
    int visualIndex; // For display order
    std::function<void(int, float)> onLevelChange;
    std::function<void(int, bool)> onActiveChange;

    MixerStrip(int i) : channelIndex(i), visualIndex(i) {
      volSlider.setSliderStyle(juce::Slider::LinearVertical);
      volSlider.setRange(0, 127, 1);
      volSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
      volSlider.addListener(this);
      addAndMakeVisible(volSlider);
      nameLabel.setText(juce::String(i + 1)); // Shows actual MIDI CH
      nameLabel.setFont(juce::FontOptions(12.0f));
      nameLabel.setJustification(juce::Justification::centred);
      nameLabel.setColour(juce::TextEditor::backgroundColourId,
                          juce::Colours::transparentBlack);
      nameLabel.setColour(juce::TextEditor::outlineColourId,
                          juce::Colours::transparentBlack);
      addAndMakeVisible(nameLabel);
      nameLabel.setInterceptsMouseClicks(false, false); // Fix Drag

      btnActive.setToggleState(true, juce::dontSendNotification);
      btnActive.setButtonText("ON");
      btnActive.onClick = [this] {
        if (onActiveChange)
          onActiveChange(channelIndex + 1, btnActive.getToggleState());
        repaint();
      };
      addAndMakeVisible(btnActive);

      trackLabel.setFont(juce::FontOptions(10.0f));
      trackLabel.setJustificationType(juce::Justification::centred);
      trackLabel.setColour(juce::Label::backgroundColourId,
                           juce::Colours::black.withAlpha(0.3f));
      trackLabel.setInterceptsMouseClicks(false, false); // Fix Drag
      addAndMakeVisible(trackLabel);
    }
    juce::Label trackLabel;
    void setTrackName(juce::String n) {
      trackLabel.setText(n, juce::dontSendNotification);
    }
    void sliderValueChanged(juce::Slider *s) override {
      if (onLevelChange)
        onLevelChange(channelIndex + 1, (float)s->getValue());
    }

    // Drag & Drop
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails
                                      &dragSourceDetails) override {
      return dragSourceDetails.description.toString().startsWith(
          "mixer_strip_");
    }
    void itemDropped(const juce::DragAndDropTarget::SourceDetails
                         &dragSourceDetails) override {
      if (auto *container = findParentComponentOfClass<MixerContainer>()) {
        int sourceIndex = dragSourceDetails.description.toString()
                              .fromLastOccurrenceOf("_", false, false)
                              .getIntValue();
        container->swapStrips(sourceIndex, visualIndex);
      }
    }
    void mouseDown(const juce::MouseEvent &e) override {
      juce::DragAndDropContainer *dragContainer =
          juce::DragAndDropContainer::findParentDragContainerFor(this);
      if (dragContainer) {
        dragContainer->startDragging("mixer_strip_" + juce::String(visualIndex),
                                     this);
      }
    }

    void paint(juce::Graphics &g) override {
      auto r = getLocalBounds().reduced(2);
      g.setColour(Theme::bgPanel);
      g.fillRoundedRectangle(r.toFloat(), 4.0f);
      if (btnActive.getToggleState()) {
        g.setColour(Theme::getChannelColor(channelIndex + 1).withAlpha(0.2f));
        g.fillRoundedRectangle(r.toFloat(), 4.0f);
      } else {
        g.setColour(juce::Colours::red.withAlpha(0.1f)); // Mute visual
        g.fillRoundedRectangle(r.toFloat(), 4.0f);
      }
      float level = (float)volSlider.getValue() / 127.0f;
      int h = (int)((volSlider.getHeight()) * level);
      g.setColour(Theme::getChannelColor(channelIndex + 1).withAlpha(0.5f));
      g.fillRect((float)volSlider.getX(), (float)(volSlider.getBottom() - h),
                 (float)volSlider.getWidth(), (float)h);

      // Draw Drag Handle (Top)
      g.setColour(juce::Colours::white.withAlpha(0.3f));
      auto handleArea = getLocalBounds().withHeight(12).reduced(15, 4);
      g.drawLine(handleArea.getX(), handleArea.getY(), handleArea.getRight(),
                 handleArea.getY(), 1.0f);
      g.drawLine(handleArea.getX(), handleArea.getY() + 2,
                 handleArea.getRight(), handleArea.getY() + 2, 1.0f);
      g.drawLine(handleArea.getX(), handleArea.getY() + 4,
                 handleArea.getRight(), handleArea.getY() + 4, 1.0f);
    }
    void resized() override {
      trackLabel.setBounds(0, 0, getWidth(), 14);
      btnActive.setBounds(0, 15, getWidth(), 15);
      nameLabel.setBounds(0, getHeight() - 20, getWidth(), 20);
      volSlider.setBounds(0, 32, getWidth(), getHeight() - 52);
    }
  };

  juce::OwnedArray<MixerStrip> strips;
  std::function<void(int, float)> onMixerActivity;
  std::function<void(int, bool)> onChannelToggle;
  const int stripWidth = 60;

  MixerContainer() {
    for (int i = 0; i < 16; ++i)
      channelMapping[i] = i;

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

  int getMappedChannel(int sourceCh) {
    if (sourceCh < 1 || sourceCh > 16)
      return sourceCh;
    return channelMapping[sourceCh - 1] + 1;
  }

  void swapStrips(int indexA, int indexB) {
    if (indexA == indexB)
      return;
    if (isPositiveAndBelow(indexA, strips.size()) &&
        isPositiveAndBelow(indexB, strips.size())) {
      strips.swap(indexA, indexB);
      for (int i = 0; i < strips.size(); ++i) {
        strips[i]->visualIndex = i;
        channelMapping[strips[i]->channelIndex] = i;
      }
      for (int i = 0; i < strips.size(); ++i) {
        if (strips[i]->nameLabel.getText().containsOnly("0123456789")) {
          strips[i]->nameLabel.setText(juce::String(i + 1),
                                       juce::dontSendNotification);
        }
      }
      resized();
      if (auto *p = getParentComponent())
        p->repaint();
    }
  }

  bool isChannelActive(int ch) {
    if (isPositiveAndBelow(ch - 1, strips.size()))
      return strips[ch - 1]->btnActive.getToggleState();
    return true;
  }

  juce::String getChannelName(int ch) {
    if (ch < 1 || ch > 16)
      return juce::String(ch);
    if (isPositiveAndBelow(ch - 1, strips.size()))
      return strips[ch - 1]->nameLabel.getText();
    return juce::String(ch);
  }

  void removeAllStrips() {
    strips.clear();
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
    resized();
  }

  void resized() override {
    for (int i = 0; i < strips.size(); ++i) {
      strips[i]->setBounds(i * stripWidth, 0, stripWidth, getHeight());
      strips[i]->repaint();
    }
  }

private:
  int channelMapping[16];
};
