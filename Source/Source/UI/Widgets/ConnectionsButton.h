/*
  ==============================================================================
    Source/UI/Widgets/ConnectionsButton.h
    Button that shows a cog (settings) icon beside "Connections" text.
  ==============================================================================
*/
#pragma once
#include "../Theme.h"
#include "../Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>

/** Button with cog icon + "Connections" label for the main menu bar. */
class ConnectionsButton : public juce::Button {
public:
  ConnectionsButton() : juce::Button("Connections") {
    setButtonText("Connections");
  }

  void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                  bool shouldDrawButtonAsDown) override {
    auto r = getLocalBounds().toFloat();
    auto& lf = getLookAndFeel();

    lf.drawButtonBackground(g, *this,
                            findColour(juce::TextButton::buttonColourId),
                            shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    g.setColour(findColour(juce::TextButton::textColourOffId));
    g.setFont(Fonts::body());

    const float iconW = 18.0f;
    const float gap = 4.0f;
    auto iconRect = r.reduced(4.0f).withWidth(iconW);
    auto textRect = r.reduced(4.0f).withTrimmedLeft(iconW + gap);

    drawCog(g, iconRect);

    g.drawFittedText(getButtonText(), textRect.toNearestInt(),
                     juce::Justification::centredLeft, 1);
  }

private:
  static void drawCog(juce::Graphics& g, juce::Rectangle<float> area) {
    const int numTeeth = 10;
    const float outerR = 1.0f;
    const float innerR = 0.72f;
    float cx = 0.0f, cy = 0.0f;
    juce::Path p;
    for (int i = 0; i <= 2 * numTeeth; ++i) {
      float angle = (float)i * juce::MathConstants<float>::pi / (float)numTeeth;
      float r = (i & 1) ? innerR : outerR;
      float x = cx + r * std::cos(angle);
      float y = cy - r * std::sin(angle);
      if (i == 0)
        p.startNewSubPath(x, y);
      else
        p.lineTo(x, y);
    }
    p.closeSubPath();
    float sz = juce::jmin(area.getWidth(), area.getHeight());
    p.scaleToFit(-1.0f, -1.0f, 2.0f, 2.0f, true);
    g.fillPath(p, juce::AffineTransform::scale(sz * 0.5f)
                               .translated(area.getCentreX(), area.getCentreY()));
  }
};
