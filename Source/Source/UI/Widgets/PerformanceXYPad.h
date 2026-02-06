/* Source/Components/PerformanceXYPad.h */
#pragma once
#include "../Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

class PerformanceXYPad : public juce::Component {
public:
  std::function<void(float, float)> onPositionChanged;

  PerformanceXYPad() { setOpaque(false); }

  void paint(juce::Graphics &g) override {
    auto r = getLocalBounds().toFloat();
    g.setColour(Theme::bgPanel.withAlpha(0.4f));
    g.fillRoundedRectangle(r, 6.0f);

    g.setColour(Theme::accent.withAlpha(0.1f));
    g.drawRoundedRectangle(r, 6.0f, 1.0f);

    // Target Crosshair
    float cx = r.getX() + xVal * r.getWidth();
    float cy = r.getY() + (1.0f - yVal) * r.getHeight();

    g.setColour(Theme::accent.withAlpha(0.3f));
    g.drawHorizontalLine((int)cy, r.getX(), r.getRight());
    g.drawVerticalLine((int)cx, r.getY(), r.getBottom());

    g.setColour(Theme::accent);
    g.fillEllipse(cx - 5, cy - 5, 10, 10);
    g.setColour(juce::Colours::white);
    g.drawEllipse(cx - 5, cy - 5, 10, 10, 1.5f);

    g.setFont(10.0f);
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawText("X: CC 74  Y: CC 1", r.reduced(5),
               juce::Justification::bottomRight);
  }

  void mouseDown(const juce::MouseEvent &e) override { handleMouse(e); }
  void mouseDrag(const juce::MouseEvent &e) override { handleMouse(e); }

private:
  void handleMouse(const juce::MouseEvent &e) {
    xVal = juce::jlimit(0.0f, 1.0f, (float)e.x / getWidth());
    yVal = juce::jlimit(0.0f, 1.0f, 1.0f - ((float)e.y / getHeight()));
    if (onPositionChanged)
      onPositionChanged(xVal, yVal);
    repaint();
  }

  float xVal = 0.5f, yVal = 0.5f;
};