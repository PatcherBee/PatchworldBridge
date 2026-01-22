/*
  ==============================================================================
    Source/Components/Common.h
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include <cmath>

struct Theme {
  static juce::Colour bgDark;
  static juce::Colour bgPanel;
  static juce::Colour accent;
  static juce::Colour grid;
  static juce::Colour text;

  static juce::Colour getChannelColor(int ch) {
    return juce::Colour::fromHSV(
        ((ch - 1) * 0.618f) - std::floor((ch - 1) * 0.618f), 0.7f, 0.95f, 1.0f);
  }

  // NEW: Helper to draw stylish panels with gradients and glass highlights
  static void drawStylishPanel(juce::Graphics &g, juce::Rectangle<float> area,
                               juce::Colour baseColour, float cornerSize) {
    // 1. Draw the Main Gradient (Darker at the bottom for depth)
    juce::ColourGradient cg(baseColour.brighter(0.05f), area.getX(),
                            area.getY(), baseColour.darker(0.15f), area.getX(),
                            area.getBottom(), false);
    g.setGradientFill(cg);
    g.fillRoundedRectangle(area, cornerSize);

    // 2. Draw a subtle "Glass" highlight at the top
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.fillRoundedRectangle(area.removeFromTop(area.getHeight() * 0.4f),
                           cornerSize);

    // 3. Draw a stylish thin outline
    g.setColour(baseColour.brighter(0.2f).withAlpha(0.4f));
    g.drawRoundedRectangle(area.expanded(0, area.getHeight() * 0.4f),
                           cornerSize, 1.2f);
  }
};

inline juce::Colour Theme::bgDark = juce::Colour::fromString("FF0A0A14");
inline juce::Colour Theme::bgPanel = juce::Colour::fromString("FF1A1A2E");
inline juce::Colour Theme::accent = juce::Colour::fromString("FF00E5FF");
inline juce::Colour Theme::grid = juce::Colour::fromString("FF2A2A4A");
inline juce::Colour Theme::text = juce::Colours::white;

struct PhaseVisualizer : public juce::Component {
  double currentPhase = 0.0;
  double quantum = 4.0;
  void setPhase(double p, double q) {
    currentPhase = p;
    quantum = (q > 0) ? q : 4.0;
    repaint();
  }
  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    int numSteps = (int)quantum;
    if (numSteps < 1)
      numSteps = 1;

    float spacing = 4.0f;
    float stepW = (bounds.getWidth() - (numSteps - 1) * spacing) / numSteps;
    float stepH = bounds.getHeight();

    for (int i = 0; i < numSteps; ++i) {
      juce::Rectangle<float> rect(bounds.getX() + i * (stepW + spacing),
                                  bounds.getY(), stepW, stepH);
      bool isActive = (i == (int)std::floor(currentPhase) % numSteps);

      if (isActive) {
        // High-gloss active step
        juce::Colour activeCol = Theme::accent;
        g.setColour(activeCol);
        g.fillRoundedRectangle(rect, 4.0f);

        // Inner glow/highlight
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.fillRoundedRectangle(rect.reduced(stepW * 0.1f, stepH * 0.1f), 2.0f);
      } else {
        // Dim inactive step
        g.setColour(Theme::bgPanel.brighter(0.05f));
        g.fillRoundedRectangle(rect, 4.0f);
        g.setColour(Theme::grid.withAlpha(0.3f));
        g.drawRoundedRectangle(rect, 4.0f, 1.0f);
      }
    }
  }
};

class ConnectionLight : public juce::Component {
public:
  bool isConnected = false;
  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();
    float d = juce::jmin(bounds.getWidth(), bounds.getHeight()) - 8.0f;
    auto circle = bounds.withSizeKeepingCentre(d, d);
    g.setColour(isConnected ? juce::Colours::lime : juce::Colours::red);
    g.fillEllipse(circle);
    if (isConnected) {
      g.setColour(juce::Colours::lime.withAlpha(0.6f));
      g.drawEllipse(circle, 2.0f);
    }
  }
};
