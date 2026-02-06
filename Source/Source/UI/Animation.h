/*
  ==============================================================================
    Source/UI/Animation.h
    Role: Shared animation helpers (fade, slide, scale) and durations.
    For more advanced curves (spring, bounce) see JUCE 8+ ValueAnimatorBuilder.
  ==============================================================================
*/
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Animation {
constexpr int defaultDurationMs = 150;
constexpr float defaultEaseOut = 0.4f;

/** Easing curve constants for animateComponent (ease-in, ease-out). */
namespace Easing {
  constexpr float linear = 0.5f;
  constexpr float easeOut = 0.4f;
  constexpr float easeIn = 0.6f;
  constexpr float sharpOut = 0.25f;
  constexpr float softIn = 0.75f;
} // namespace Easing

/** Animate component alpha (0 = invisible, 1 = opaque). */
inline void fade(juce::Component& c, float targetAlpha,
                 int durationMs = defaultDurationMs) {
  if (targetAlpha >= 0.99f)
    juce::Desktop::getInstance().getAnimator().fadeIn(&c, durationMs);
  else if (targetAlpha <= 0.01f)
    juce::Desktop::getInstance().getAnimator().fadeOut(&c, durationMs);
  else
    c.setAlpha(targetAlpha);
}

/** Animate component position to target (in parent coordinates). */
inline void slide(juce::Component& c, juce::Point<int> target,
                 int durationMs = defaultDurationMs) {
  juce::Desktop::getInstance().getAnimator().animateComponent(
      &c, c.getBounds().withPosition(target), 1.0f, durationMs, false,
      defaultEaseOut, defaultEaseOut);
}

/** Animate component bounds (position + size). */
inline void animateBounds(juce::Component& c, juce::Rectangle<int> targetBounds,
                          int durationMs = defaultDurationMs) {
  juce::Desktop::getInstance().getAnimator().animateComponent(
      &c, targetBounds, 1.0f, durationMs, false, defaultEaseOut, defaultEaseOut);
}

/** Animate bounds with custom easing (e.g. Easing::sharpOut for snappier end). */
inline void animateBoundsWithEasing(juce::Component& c,
                                    juce::Rectangle<int> targetBounds,
                                    int durationMs = defaultDurationMs,
                                    float easeIn = Easing::easeIn,
                                    float easeOut = Easing::easeOut) {
  juce::Desktop::getInstance().getAnimator().animateComponent(
      &c, targetBounds, 1.0f, durationMs, false, easeIn, easeOut);
}
} // namespace Animation
