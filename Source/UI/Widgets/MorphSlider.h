/* Source/Components/MorphSlider.h */
#pragma once
#include "../Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

class MorphSlider : public juce::Slider {
public:
  MorphSlider()
      : Slider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox) {
    setRange(0.0, 1.0);
  }

  void paint(juce::Graphics &g) override {
    auto r = getLocalBounds().toFloat().reduced(2);

    // Gradient Track
    juce::ColourGradient grad(juce::Colours::cyan, r.getX(), r.getCentreY(),
                              juce::Colours::magenta, r.getRight(),
                              r.getCentreY(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(r.withY(r.getCentreY() - 2.0f).withHeight(4.0f),
                           2.0f);

    // Thumb
    float val = (float)getValue();
    float thumbX = r.getX() + val * r.getWidth();

    g.setColour(juce::Colours::white);
    g.fillEllipse(thumbX - 6, r.getCentreY() - 6, 12, 12);
    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.drawEllipse(thumbX - 6, r.getCentreY() - 6, 12, 12, 1.0f);
  }
};