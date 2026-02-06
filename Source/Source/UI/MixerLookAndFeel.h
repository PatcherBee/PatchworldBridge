/*
  ==============================================================================
    Source/UI/MixerLookAndFeel.h
    Mixer-specific LookAndFeel: phantom faders, reactive knobs.
    Uses Component Properties (meterLevel, modDepth) for styling.
  ==============================================================================
*/
#pragma once
#include "Theme.h"
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

class MixerLookAndFeel : public juce::LookAndFeel_V4 {
public:
  MixerLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, Theme::bgDark);
    setColour(juce::TextButton::buttonColourId, Theme::bgPanel);
    setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    setColour(juce::Slider::thumbColourId, Theme::accent);
  }

  void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, float rotaryStartAngle,
                        float rotaryEndAngle, juce::Slider &slider) override {
    auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width,
                                          (float)height)
                      .reduced(2.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto toAngle =
        rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto center = bounds.getCentre();

    bool isHover = slider.isMouseOverOrDragging();
    bool isDrag = slider.isMouseButtonDown();

    if (slider.getProperties().contains("modDepth")) {
      float depth = (float)slider.getProperties()["modDepth"];
      if (depth > 0.01f) {
        float modStart = toAngle - (depth * juce::MathConstants<float>::pi);
        float modEnd = toAngle + (depth * juce::MathConstants<float>::pi);
        juce::Path p;
        p.addCentredArc(center.x, center.y, radius + 4.0f, radius + 4.0f, 0.0f,
                        modStart, modEnd, true);
        g.setColour(Theme::accent.withAlpha(0.3f));
        g.strokePath(p, juce::PathStrokeType(2.0f));
      }
    }

    juce::Path track;
    track.addCentredArc(center.x, center.y, radius, radius, 0.0f,
                        rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(Theme::bgDark.darker(0.5f));
    g.strokePath(track,
                 juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc(center.x, center.y, radius, radius, 0.0f,
                           rotaryStartAngle, toAngle, true);
    g.setColour(isHover ? Theme::accent.brighter(0.2f) : Theme::accent);
    g.strokePath(valueArc,
                 juce::PathStrokeType(isHover ? 5.0f : 4.0f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
    if (isHover) {
      g.setColour(Theme::accent.withAlpha(0.2f));
      g.strokePath(valueArc,
                   juce::PathStrokeType(12.0f, juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
    }

    if (isDrag) {
      g.setColour(juce::Colours::white);
      g.setFont(juce::FontOptions(14.0f));
      g.drawText(juce::String(slider.getValue(), 1), bounds,
                 juce::Justification::centred);
    }
  }

  void drawLinearSlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, [[maybe_unused]] float minSliderPos,
                        [[maybe_unused]] float maxSliderPos,
                        const juce::Slider::SliderStyle style,
                        juce::Slider &slider) override {
    auto trackWidth = 6.0f;

    g.setColour(Theme::bgDark.darker(0.3f));
    g.fillRoundedRectangle(x + (width - trackWidth) * 0.5f, (float)y,
                            trackWidth, (float)height, 3.0f);

    if (slider.getProperties().contains("meterLevel")) {
      float level = (float)slider.getProperties()["meterLevel"];
      if (level > 0.01f) {
        float meterH = (float)height * level;
        auto meterRect = juce::Rectangle<float>(
            x + (width - trackWidth) * 0.5f, (float)y + (float)height - meterH,
            trackWidth, meterH);
        g.setGradientFill(juce::ColourGradient(
            Theme::accent.withAlpha(0.8f), meterRect.getCentreX(),
            meterRect.getY(), Theme::accent.withAlpha(0.0f),
            meterRect.getCentreX(), meterRect.getBottom(), false));
        g.fillRoundedRectangle(meterRect, 3.0f);
        g.setColour(Theme::accent.withAlpha(0.2f));
        g.fillRoundedRectangle(meterRect.expanded(4.0f, 0.0f), 6.0f);
      }
    }

    if (style == juce::Slider::LinearVertical) {
      auto thumbW = 16.0f;
      auto thumbH = 8.0f;
      juce::Point<float> startPos(x + width * 0.5f, y + height - 4.0f);
      g.setColour(slider.findColour(juce::Slider::thumbColourId));
      g.fillRoundedRectangle(startPos.x - thumbW * 0.5f,
                              sliderPos - thumbH * 0.5f, thumbW, thumbH, 2.0f);
      if (slider.isMouseOverOrDragging()) {
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawRoundedRectangle(startPos.x - thumbW * 0.5f,
                               sliderPos - thumbH * 0.5f, thumbW, thumbH, 2.0f,
                               1.0f);
      }
    }
  }

  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &backgroundColour,
                            bool isMouseOverButton,
                            bool isButtonDown) override {
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    auto baseColor = isButtonDown ? Theme::accent : backgroundColour;
    g.setColour(baseColor);
    g.fillRoundedRectangle(bounds, 4.0f);

    if (button.getProperties().contains("auroraPhase")) {
      float phase = (float)button.getProperties()["auroraPhase"];
      float x = bounds.getX() + bounds.getWidth() * phase;
      g.setGradientFill(juce::ColourGradient(
          juce::Colours::white.withAlpha(0.3f), x, bounds.getY(),
          juce::Colours::transparentWhite, x, bounds.getBottom(), true));
      g.fillRoundedRectangle(bounds, 4.0f);
    }

    if (isMouseOverButton) {
      g.setColour(juce::Colours::white.withAlpha(0.1f));
      g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }
  }
};
