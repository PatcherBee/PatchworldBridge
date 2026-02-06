/*
  ==============================================================================
    Source/UI/Widgets/ProKnob.h
    Role: Pro knob with value display, glow, and label.
  ==============================================================================
*/
#pragma once
#include "../Fonts.h"
#include "../PopupMenuOptions.h"
#include "../Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ProKnob : public juce::Slider {
public:
  explicit ProKnob(const juce::String& labelText = "") : label(labelText) {
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  }

  void setLabel(const juce::String& text) { label = text; }
  const juce::String& getLabelText() const { return label; }

  void paint(juce::Graphics& g) override {
    auto bounds = getLocalBounds().toFloat();
    auto labelHeight = label.isEmpty() ? 0.0f : 14.0f;
    auto knobArea = bounds.withTrimmedBottom(labelHeight);

    float radius = juce::jmin(knobArea.getWidth(), knobArea.getHeight()) * 0.45f;
    float centreX = knobArea.getCentreX();
    float centreY = knobArea.getCentreY();

    juce::Rectangle<float> knobRect(centreX - radius - 4.0f, centreY - radius - 2.0f,
                                     (radius + 4.0f) * 2.0f, (radius + 2.0f) * 2.0f);
    Theme::drawControlShadow(g, knobRect, knobRect.getWidth() * 0.5f, 2.0f);

    float startAngle = juce::MathConstants<float>::pi * 1.25f;
    float endAngle = juce::MathConstants<float>::pi * 2.75f;
    float angle = startAngle + (float)valueToProportionOfLength(getValue()) *
                  (endAngle - startAngle);

    // Background track
    juce::Path bgArc;
    bgArc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                        startAngle, endAngle, true);
    g.setColour(Theme::bgMedium);
    g.strokePath(bgArc, juce::PathStrokeType(4.0f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc
    juce::Path valueArc;
    valueArc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                           startAngle, angle, true);
    g.setColour(Theme::accent);
    g.strokePath(valueArc, juce::PathStrokeType(4.0f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Center circle
    float innerRadius = radius * 0.55f;
    g.setColour(Theme::bgPanel);
    g.fillEllipse(centreX - innerRadius, centreY - innerRadius,
                  innerRadius * 2.0f, innerRadius * 2.0f);

    // Pointer
    float pointerLength = innerRadius * 0.8f;
    float pointerX = centreX + std::sin(angle) * pointerLength;
    float pointerY = centreY - std::cos(angle) * pointerLength;
    g.setColour(Theme::accent);
    g.drawLine(centreX, centreY, pointerX, pointerY, 2.5f);

    // Value text when dragging
    if (isMouseButtonDown()) {
      g.setColour(Theme::text);
      g.setFont(Fonts::small());
      g.drawText(getTextFromValue(getValue()), knobArea, juce::Justification::centred);
    }

    if (!label.isEmpty()) {
      g.setColour(Theme::text.withAlpha(0.7f));
      g.setFont(Fonts::small());
      g.drawText(label, bounds.removeFromBottom(labelHeight),
                 juce::Justification::centred);
    }
  }

  void mouseDown(const juce::MouseEvent& e) override {
    if (e.mods.isRightButtonDown()) {
      if (getProperties().getWithDefault("suppressContextMenu", false))
        return;
      juce::PopupMenu m;
      m.addSectionHeader(label.isEmpty() ? "Value" : label);
      m.addItem("Set value...", [this] {
        auto* aw = new juce::AlertWindow(
            "Set value", "Enter value (" + juce::String(getMinimum(), 2) + " to " + juce::String(getMaximum(), 2) + "):",
            juce::MessageBoxIconType::QuestionIcon);
        aw->addTextEditor("val", getTextFromValue(getValue()), "Value:");
        aw->addButton("OK", static_cast<int>(1), juce::KeyPress(juce::KeyPress::returnKey));
        aw->addButton("Cancel", static_cast<int>(0), juce::KeyPress(juce::KeyPress::escapeKey));
        aw->enterModalState(
            true,
            juce::ModalCallbackFunction::create([this, aw](int result) {
              if (result == 1) {
                double v = getValueFromText(aw->getTextEditorContents("val").trim());
                setValue(juce::jlimit(getMinimum(), getMaximum(), v), juce::sendNotification);
              }
              delete aw;
            }),
            false);
      });
      m.showMenuAsync(PopupMenuOptions::forComponent(this));
      return;
    }
    juce::Slider::mouseDown(e);
  }

private:
  juce::String label;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProKnob)
};
