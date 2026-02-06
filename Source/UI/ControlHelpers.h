/*
  ==============================================================================
    Source/UI/ControlHelpers.h
    Role: Shared behavior for sliders/knobs — fine control, double-click reset,
          scroll wheel (roadmap 6.1–6.5). Use ResponsiveSlider or attach to existing.
  ==============================================================================
*/
#pragma once

#include "Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ControlHelpers {

/** Sensitivity multiplier: Shift = fine, Ctrl = ultra-fine, Alt = coarse. */
inline float modifierSensitivity(const juce::ModifierKeys &mods) {
  if (mods.isShiftDown())
    return 0.1f;
  if (mods.isCtrlDown())
    return 0.01f;
  if (mods.isAltDown())
    return 5.0f;
  return 1.0f;
}

/** Slider with double-click reset, scroll wheel, and modifier-based drag sensitivity. */
class ResponsiveSlider : public juce::Slider {
public:
  ResponsiveSlider() = default;
  explicit ResponsiveSlider(const juce::String &name) : juce::Slider(name) {}

  void setDefaultValue(double value) { defaultValue = value; }
  double getDefaultValue() const { return defaultValue; }

  void mouseDoubleClick(const juce::MouseEvent &) override {
    setValue(defaultValue, juce::sendNotificationSync);
  }

  void mouseDown(const juce::MouseEvent &e) override {
    dragStartValue = getValue();
    dragStartY = e.getMouseDownY();
    int baseSens = 250;
    float mult = modifierSensitivity(e.mods);
    setMouseDragSensitivity((int)(baseSens / mult));
    juce::Slider::mouseDown(e);
  }

  void mouseDrag(const juce::MouseEvent &e) override {
    if (e.mods.isShiftDown()) {
      float delta = (float)(dragStartY - e.y) / 400.0f;
      double range = getMaximum() - getMinimum();
      setValue(juce::jlimit(getMinimum(), getMaximum(),
                            dragStartValue + delta * range),
               juce::sendNotificationSync);
      return;
    }
    juce::Slider::mouseDrag(e);
  }

  void mouseWheelMove(const juce::MouseEvent &e,
                      const juce::MouseWheelDetails &wheel) override {
    double step = getInterval() > 0 ? getInterval()
                                    : (getMaximum() - getMinimum()) / 100.0;
    double delta = -wheel.deltaY * step * 5.0 * modifierSensitivity(e.mods);
    setValue(juce::jlimit(getMinimum(), getMaximum(), getValue() + delta),
             juce::sendNotificationSync);
  }

private:
  double defaultValue = 0.0;
  double dragStartValue = 0.0;
  int dragStartY = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResponsiveSlider)
};

/** Zoom slider (50-200%) with robust keyboard parsing and slower, precise mouse drag. */
class ZoomSlider : public ResponsiveSlider {
public:
  ZoomSlider() {
    setRange(0.5, 2.0, 0.01);
    setNumDecimalPlacesToDisplay(0);
    setTextValueSuffix("%");
    setWantsKeyboardFocus(true);
  }

  void mouseDown(const juce::MouseEvent &e) override {
    ResponsiveSlider::mouseDown(e);
    // Slower, more precise drag: higher sensitivity = less change per pixel
    setMouseDragSensitivity(800);
  }

  double getValueFromText(const juce::String &text) override {
    juce::String t = text.trim().trimCharactersAtStart("% \t").trimCharactersAtEnd("% \t");
    if (t.isEmpty()) return 1.0;
    double v = t.getDoubleValue();
    if (v >= 50.0 && v <= 200.0)
      return juce::jlimit(0.5, 2.0, v / 100.0);
    if (v >= 0.5 && v <= 2.0)
      return v;
    return juce::jlimit(0.5, 2.0, v / 100.0);
  }

  juce::String getTextFromValue(double value) override {
    return juce::String((int)(value * 100.0)) + "%";
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZoomSlider)
};

} // namespace ControlHelpers
