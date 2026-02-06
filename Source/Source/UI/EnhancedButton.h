/*
  ==============================================================================
    Source/UI/EnhancedButton.h
    Role: Button with hover glow and press animation (roadmap 6.6)
  ==============================================================================
*/
#pragma once

#include "Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * AnimatedButton: TextButton with scale animation on press and hover glow.
 */
class AnimatedButton : public juce::TextButton {
public:
  AnimatedButton() = default;
  explicit AnimatedButton(const juce::String &name) : juce::TextButton(name) {}

  void paintButton(juce::Graphics &g, bool highlighted, bool down) override {
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    // Base color (with toggle state support)
    auto baseColour = getToggleState()
                          ? findColour(buttonOnColourId)
                          : findColour(buttonColourId);

    // Scale animation on press
    float scale = down ? 0.96f : (highlighted ? 1.02f : 1.0f);
    auto transform = juce::AffineTransform::scale(
        scale, scale, bounds.getCentreX(), bounds.getCentreY());
    g.addTransform(transform);

    // Color interpolation
    auto targetColour = down ? Theme::buttonPressed(baseColour)
                             : (highlighted ? Theme::buttonHover(baseColour) : baseColour);

    // Main fill
    g.setColour(targetColour);
    g.fillRoundedRectangle(bounds, 4.0f);

    // Hover glow
    if (highlighted && !down) {
      g.setColour(Theme::accent.withAlpha(0.15f));
      g.fillRoundedRectangle(bounds, 4.0f);
    }

    // Border
    g.setColour(highlighted ? Theme::accent : Theme::accent.withAlpha(0.3f));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    // Text with subtle shadow
    auto textBounds = getLocalBounds();
    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.setFont(getFont());
    g.drawText(getButtonText(), textBounds.translated(1, 1), juce::Justification::centred);
    g.setColour(findColour(down ? textColourOnId : textColourOffId));
    g.drawText(getButtonText(), textBounds, juce::Justification::centred);
  }

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnimatedButton)
};

/**
 * IconButton: Small button with icon character (for toolbars).
 */
class IconButton : public juce::TextButton {
public:
  IconButton() = default;
  explicit IconButton(const juce::String &icon) : juce::TextButton(icon) {
    setButtonText(icon);
  }

  void paintButton(juce::Graphics &g, bool highlighted, bool down) override {
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    // Background
    auto baseColour = getToggleState() ? Theme::accent.darker(0.3f) : Theme::bgPanel;
    g.setColour(down ? baseColour.darker(0.2f)
                     : (highlighted ? baseColour.brighter(0.1f) : baseColour));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Hover ring
    if (highlighted) {
      g.setColour(Theme::accent.withAlpha(0.4f));
      g.drawRoundedRectangle(bounds, 4.0f, 1.5f);
    }

    // Icon
    g.setColour(getToggleState() ? Theme::accent : Theme::text);
    g.setFont(juce::FontOptions(bounds.getHeight() * 0.6f));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
  }

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IconButton)
};
