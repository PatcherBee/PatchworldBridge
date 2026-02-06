/*
  ==============================================================================
    Source/Components/WheelComponent.h
    Role: Pitch bend and mod wheel components (vertical sliders)
  ==============================================================================
*/
#pragma once
#include "../Theme.h"
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

class WheelComponent : public juce::Slider {
public:
  WheelComponent() {
    setSliderStyle(juce::Slider::LinearVertical);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setRange(0.0, 1.0, 0.01);
    setValue(0.0);
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    // Background track
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Value bar
    float normValue =
        (float)((getValue() - getMinimum()) / (getMaximum() - getMinimum()));
    float barHeight = bounds.getHeight() * normValue;
    auto valueRect = bounds.removeFromBottom(barHeight).reduced(2.0f, 0.0f);

    g.setColour(juce::Colours::cyan.withAlpha(0.8f));
    g.fillRoundedRectangle(valueRect, 2.0f);

    // Center line for pitch wheel (if range includes negative)
    if (getMinimum() < 0.0) {
      float centerY = bounds.getY() + bounds.getHeight() * 0.5f;
      g.setColour(juce::Colours::white.withAlpha(0.3f));
      g.drawHorizontalLine((int)centerY, bounds.getX(), bounds.getRight());
    }
  }
};
