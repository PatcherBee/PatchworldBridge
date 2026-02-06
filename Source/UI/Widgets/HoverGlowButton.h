/*
  ==============================================================================
    Source/UI/Widgets/HoverGlowButton.h
    Role: Button with smooth hover glow feedback
    OPTIMIZED: Uses HoverGlowManager instead of per-widget 60Hz timers
  ==============================================================================
*/
#pragma once
#include "../ControlHelpers.h"
#include "../Theme.h"
#include "HoverGlowManager.h"
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

class HoverGlowButton : public juce::TextButton, public HoverGlowWidget {
public:
  HoverGlowButton() { HoverGlowManager::instance().registerWidget(this); }
  explicit HoverGlowButton(const juce::String &text) : juce::TextButton(text) {
    HoverGlowManager::instance().registerWidget(this);
  }

  ~HoverGlowButton() override {
    HoverGlowManager::instance().unregisterWidget(this);
  }

  void mouseEnter(const juce::MouseEvent &e) override {
    hoverAlpha = 0.0f;
    juce::TextButton::mouseEnter(e);
  }

  void mouseExit(const juce::MouseEvent &e) override {
    juce::TextButton::mouseExit(e);
  }

  bool shouldAnimate() const override { return isVisible(); }

  bool tickGlow() override {
    bool isHovered = isMouseOver();
    float target = isHovered ? 1.0f : 0.0f;
    float delta = target - hoverAlpha;
    if (std::abs(delta) < 0.01f) {
      if (hoverAlpha != target) {
        hoverAlpha = target;
        repaint();
        return true;
      }
      return false; // Stable, no repaint needed
    }
    hoverAlpha += delta * 0.25f; // Slightly faster since 30Hz
    repaint();
    return true;
  }

  void paintButton(juce::Graphics &g, bool shouldDrawButtonAsHighlighted,
                   bool shouldDrawButtonAsDown) override {
    auto bounds = getLocalBounds().toFloat();
    juce::TextButton::paintButton(g, shouldDrawButtonAsHighlighted,
                                  shouldDrawButtonAsDown);
    if (hoverAlpha > 0.01f) {
      g.setColour(Theme::accent.withAlpha(hoverAlpha * 0.2f));
      g.fillRoundedRectangle(bounds.reduced(1), 4.0f);
      g.setColour(Theme::accent.withAlpha(hoverAlpha * 0.5f));
      g.drawRoundedRectangle(bounds.reduced(1), 4.0f, 1.5f);
    }
  }

private:
  float hoverAlpha = 0.0f;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HoverGlowButton)
};

// Hover glow for sliders
class HoverGlowSlider : public juce::Slider, public HoverGlowWidget {
public:
  HoverGlowSlider() { HoverGlowManager::instance().registerWidget(this); }
  explicit HoverGlowSlider(SliderStyle style) : juce::Slider(style, NoTextBox) {
    HoverGlowManager::instance().registerWidget(this);
  }

  ~HoverGlowSlider() override {
    HoverGlowManager::instance().unregisterWidget(this);
  }

  void mouseEnter(const juce::MouseEvent &e) override {
    hoverAlpha = 0.0f;
    juce::Slider::mouseEnter(e);
  }

  void mouseExit(const juce::MouseEvent &e) override {
    juce::Slider::mouseExit(e);
  }

  bool shouldAnimate() const override { return isVisible(); }

  bool tickGlow() override {
    bool isHovered = isMouseOver();
    float target = isHovered ? 1.0f : 0.0f;
    float delta = target - hoverAlpha;
    if (std::abs(delta) < 0.01f) {
      if (hoverAlpha != target) {
        hoverAlpha = target;
        repaint();
        return true;
      }
      return false;
    }
    hoverAlpha += delta * 0.25f;
    repaint();
    return true;
  }

  void paint(juce::Graphics &g) override {
    juce::Slider::paint(g);
    if (hoverAlpha > 0.01f) {
      auto bounds = getLocalBounds().toFloat();
      g.setColour(Theme::accent.withAlpha(hoverAlpha * 0.15f));
      g.drawRoundedRectangle(bounds.reduced(1), 4.0f, 2.0f);
    }
  }

private:
  float hoverAlpha = 0.0f;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HoverGlowSlider)
};

/** ResponsiveSlider (double-click reset, scroll, modifiers) + hover glow. */
class HoverGlowResponsiveSlider : public ControlHelpers::ResponsiveSlider,
                                  public HoverGlowWidget {
public:
  HoverGlowResponsiveSlider() {
    HoverGlowManager::instance().registerWidget(this);
  }
  explicit HoverGlowResponsiveSlider(const juce::String &name)
      : ControlHelpers::ResponsiveSlider(name) {
    HoverGlowManager::instance().registerWidget(this);
  }

  ~HoverGlowResponsiveSlider() override {
    HoverGlowManager::instance().unregisterWidget(this);
  }

  void paint(juce::Graphics &g) override {
    ControlHelpers::ResponsiveSlider::paint(g);
    if (hoverAlpha > 0.01f) {
      auto bounds = getLocalBounds().toFloat();
      g.setColour(Theme::accent.withAlpha(hoverAlpha * 0.15f));
      g.drawRoundedRectangle(bounds.reduced(1), 4.0f, 2.0f);
    }
  }

  void mouseEnter(const juce::MouseEvent &e) override {
    hoverAlpha = 0.0f;
    ControlHelpers::ResponsiveSlider::mouseEnter(e);
  }
  void mouseExit(const juce::MouseEvent &e) override {
    ControlHelpers::ResponsiveSlider::mouseExit(e);
  }

  bool shouldAnimate() const override { return isVisible(); }

  bool tickGlow() override {
    float target = isMouseOver() ? 1.0f : 0.0f;
    float delta = target - hoverAlpha;
    if (std::abs(delta) < 0.01f) {
      if (hoverAlpha != target) {
        hoverAlpha = target;
        repaint();
        return true;
      }
      return false;
    }
    hoverAlpha += delta * 0.25f;
    repaint();
    return true;
  }

private:
  float hoverAlpha = 0.0f;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HoverGlowResponsiveSlider)
};
