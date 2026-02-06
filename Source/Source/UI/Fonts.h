/*
  ==============================================================================
    Source/UI/Fonts.h
    Role: Centralized font definitions (roadmap 9.5 / 4.2)
  ==============================================================================
*/
#pragma once

#include <juce_graphics/juce_graphics.h>

/**
 * Fonts: Centralized font factory for consistent typography.
 * 
 * Usage:
 *   g.setFont(Fonts::header());
 *   g.setFont(Fonts::body());
 *   g.setFont(Fonts::mono());
 */
namespace Fonts {

inline juce::Font header() {
  return juce::FontOptions(14.0f).withStyle("Bold");
}

inline juce::Font headerLarge() {
  return juce::FontOptions(18.0f).withStyle("Bold");
}

inline juce::Font body() {
  return juce::FontOptions(12.0f);
}

inline juce::Font bodyBold() {
  return juce::FontOptions(12.0f).withStyle("Bold");
}

inline juce::Font small() {
  return juce::FontOptions(10.0f);
}

inline juce::Font smallBold() {
  return juce::FontOptions(10.0f).withStyle("Bold");
}

inline juce::Font mono() {
  return juce::FontOptions("Consolas", 12.0f, juce::Font::plain);
}

inline juce::Font monoSmall() {
  return juce::FontOptions("Consolas", 10.0f, juce::Font::plain);
}

inline juce::Font icon(float size = 14.0f) {
  return juce::FontOptions(size);
}

} // namespace Fonts
