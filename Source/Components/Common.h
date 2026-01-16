/*
  ==============================================================================
    Source/Components/Common.h
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>

struct Theme {
  static const juce::Colour bgDark;
  static const juce::Colour bgPanel;
  static const juce::Colour accent;
  static const juce::Colour grid;
  static const juce::Colour text;

  static juce::Colour getChannelColor(int ch) {
    return juce::Colour::fromHSV(
        ((ch - 1) * 0.618f) - std::floor((ch - 1) * 0.618f), 0.7f, 0.95f, 1.0f);
  }
};

inline const juce::Colour Theme::bgDark = juce::Colour::fromString("FF121212");
inline const juce::Colour Theme::bgPanel = juce::Colour::fromString("FF1E1E1E");
inline const juce::Colour Theme::accent = juce::Colour::fromString("FF007ACC");
inline const juce::Colour Theme::grid = juce::Colour::fromString("FF333333");
inline const juce::Colour Theme::text = juce::Colours::white;

struct PhaseVisualizer : public juce::Component {
  double currentPhase = 0.0;
  double quantum = 4.0;
  void setPhase(double p, double q) {
    currentPhase = p;
    quantum = (q > 0) ? q : 4.0;
    repaint();
  }
  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();
    int numBlocks = (int)quantum;
    if (numBlocks < 1)
      numBlocks = 1;
    float blockWidth = (bounds.getWidth() - (numBlocks - 1) * 2.0f) / numBlocks;
    int activeBlockIndex = (int)std::floor(currentPhase) % numBlocks;
    for (int i = 0; i < numBlocks; ++i) {
      auto blockRect =
          juce::Rectangle<float>(bounds.getX() + i * (blockWidth + 2.0f),
                                 bounds.getY(), blockWidth, bounds.getHeight());
      if (i == activeBlockIndex) {
        g.setColour(Theme::accent);
        g.fillRoundedRectangle(blockRect, 3.0f);
      } else {
        g.setColour(Theme::bgPanel.brighter(0.1f));
        g.fillRoundedRectangle(blockRect, 3.0f);
        g.setColour(Theme::grid);
        g.drawRoundedRectangle(blockRect, 3.0f, 1.0f);
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
