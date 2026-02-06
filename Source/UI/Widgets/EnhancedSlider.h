/*
  ==============================================================================
    Source/UI/Widgets/EnhancedSlider.h
    Role: Slider with snap, modifier-sensitive drag, and visual feedback
  ==============================================================================
*/
#pragma once

#include "../Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

class EnhancedSlider : public juce::Slider {
public:
  EnhancedSlider() {
    setVelocityBasedMode(true);
    setVelocityModeParameters(1.0, 1, 0.0, false);
  }

  /** Fine control when Shift key held (reduced sensitivity). */
  void mouseDrag(const juce::MouseEvent &e) override {
    if (e.mods.isShiftDown()) {
      setVelocityModeParameters(0.5, 1, 0.0, false);
    } else {
      setVelocityModeParameters(1.0, 1, 0.0, false);
    }
    juce::Slider::mouseDrag(e);
  }

  double getValueFromText(const juce::String &text) override {
    return juce::Slider::getValueFromText(text);
  }
  juce::String getTextFromValue(double value) override {
    return juce::Slider::getTextFromValue(value);
  }

  double snapValue(double attemptedValue, DragMode mode) override {
    if (!snapEnabled)
      return attemptedValue;
    double range = getMaximum() - getMinimum();
    if (range <= 0)
      return attemptedValue;
    double snapPoints[] = {
        getMinimum(),
        getMinimum() + range * 0.25,
        getMinimum() + range * 0.5,
        getMinimum() + range * 0.75,
        getMaximum()
    };
    double threshold = range * 0.02;
    for (double snap : snapPoints) {
      if (std::abs(attemptedValue - snap) < threshold)
        return snap;
    }
    return attemptedValue;
  }

  void setSnapEnabled(bool enabled) { snapEnabled = enabled; }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();
    if (isMouseOver()) {
      g.setColour(Theme::accent.withAlpha(0.1f));
      g.fillRoundedRectangle(bounds.expanded(2), 4.0f);
    }
    juce::Slider::paint(g);
    if (isMouseButtonDown()) {
      g.setColour(Theme::accent.withAlpha(0.2f));
      g.drawRoundedRectangle(bounds, 4.0f, 2.0f);
    }
  }

private:
  bool snapEnabled = true;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnhancedSlider)
};
