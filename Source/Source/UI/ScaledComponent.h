/*
  ==============================================================================
    Source/UI/ScaledComponent.h
    Role: Base component that applies display DPI scale; rescales on display change.
  ==============================================================================
*/
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Base for components that should scale with display DPI.
 *  Override applyScale() to resize children or adjust layout when scale changes.
 */
class ScaledComponent : public juce::Component {
public:
  ScaledComponent() = default;

  void parentHierarchyChanged() override {
    juce::Component::parentHierarchyChanged();
    updateScaleFromDisplay();
  }

  /** Override to apply scale to children (e.g. setTransform, resize, font size). */
  virtual void applyScale(float scale) { juce::ignoreUnused(scale); }

  float getCurrentScale() const { return currentScale; }

private:
  void updateScaleFromDisplay() {
    if (auto* display = juce::Desktop::getInstance().getDisplays()
                           .getDisplayForRect(getScreenBounds())) {
      float newScale = static_cast<float>(display->scale);
      if (std::abs(newScale - currentScale) > 0.01f) {
        currentScale = newScale;
        applyScale(currentScale);
      }
    }
  }

  float currentScale = 1.0f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScaledComponent)
};
