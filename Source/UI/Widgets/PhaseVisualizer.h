/*
  ==============================================================================
    Source/Components/PhaseVisualizer.h
    Extracted for better compilation dependency management.
  ==============================================================================
*/
#pragma once
#ifndef PHASE_VISUALIZER_H
#define PHASE_VISUALIZER_H

#include "../UI/Theme.h"
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

class PhaseVisualizer : public juce::Component {
public:
  double phase = 0.0;
  void setPhase(double p, double q = 4.0) {
    if (q > 0.0)
      phase = p / q;
    else
      phase = 0.0;
    repaint();
  }
  void paint(juce::Graphics &g) override {
    g.fillAll(juce::Colours::black.withAlpha(0.2f));
    auto r = getLocalBounds().toFloat();
    auto c = r.getCentre();
    float radius = juce::jmin(r.getWidth(), r.getHeight()) * 0.4f;

    g.setColour(Theme::grid);
    g.drawEllipse(c.x - radius, c.y - radius, radius * 2, radius * 2, 2.0f);

    float angle = (float)(phase * 2.0 * juce::MathConstants<double>::pi);
    juce::Line<float> line(c, c.getPointOnCircumference(radius, angle));

    g.setColour(Theme::accent);
    g.drawLine(line, 3.0f);

    // Draw dot
    g.setColour(juce::Colours::white);
    g.fillEllipse(line.getEnd().x - 3, line.getEnd().y - 3, 6, 6);
  }
};

#endif // PHASE_VISUALIZER_H
