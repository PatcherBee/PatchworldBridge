#pragma once
#include "../../Core/TimerHub.h"
#include "../Fonts.h"
#include "../Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>

/**
 * SignalPathLegend - Visual indicator showing which processing lanes are
 * active. Pulses when data flows through Network (OSC), UI (Keyboard), or
 * Engine (Sequencer). Helps users debug and understand the multi-lane SPSC
 * architecture.
 */
class SignalPathLegend : public juce::Component {
public:
  enum Lane { NET, UI, ENG };

  SignalPathLegend() {
    hubId = "SignalLegend_" + std::to_string(reinterpret_cast<int64_t>(this));
    TimerHub::instance().subscribe(
        hubId,
        [this]() {
          if (!isVisible())
            return;
          bool needsRepaint = false;
          auto decay = [&needsRepaint](float &alpha) {
            if (alpha > 0.0f) {
              alpha = std::max(0.0f, alpha - 0.08f);
              needsRepaint = true;
            }
          };
          decay(netAlpha);
          decay(uiAlpha);
          decay(engAlpha);
          if (needsRepaint)
            repaint();
        },
        TimerHub::Rate10Hz); // 10Hz is sufficient for signal pulse decay
  }

  ~SignalPathLegend() override { TimerHub::instance().unsubscribe(hubId); }

  /**
   * Trigger a pulse on the specified lane.
   * Call from your event handlers when data flows through a lane.
   */
  void pulse(Lane lane) {
    switch (lane) {
    case NET:
      netAlpha = 1.0f;
      break;
    case UI:
      uiAlpha = 1.0f;
      break;
    case ENG:
      engAlpha = 1.0f;
      break;
    }
  }

  void paint(juce::Graphics &g) override {
    auto r = getLocalBounds().toFloat();
    float sectionWidth = r.getWidth() / 3.0f;

    drawIndicator(g, r.removeFromLeft(sectionWidth), "OSC", netAlpha,
                  juce::Colours::cyan);
    drawIndicator(g, r.removeFromLeft(sectionWidth), "MIDI", uiAlpha,
                  juce::Colours::lime);
    drawIndicator(g, r.removeFromLeft(sectionWidth), "SEQ", engAlpha,
                  juce::Colours::orange);
  }

private:
  void drawIndicator(juce::Graphics &g, juce::Rectangle<float> area,
                     const juce::String &text, float alpha, juce::Colour col) {
    auto inner = area.reduced(2.0f);

    // Semi-transparent overlay (subtle when in log window)
    g.setColour(col.withAlpha(0.03f + (alpha * 0.2f)));
    g.fillRoundedRectangle(inner, 4.0f);

    // Active border (brighter when active)
    g.setColour(col.withAlpha(0.1f + (alpha * 0.7f)));
    g.drawRoundedRectangle(inner, 4.0f, 1.0f);

    // LED dot
    float dotSize = 6.0f;
    float dotX = inner.getX() + 6.0f;
    float dotY = inner.getCentreY() - (dotSize / 2.0f);
    g.setColour(col.withAlpha(0.2f + (alpha * 0.8f)));
    g.fillEllipse(dotX, dotY, dotSize, dotSize);

    // Text (brighter when active)
    g.setColour(alpha > 0.3f ? juce::Colours::white : col.withAlpha(0.5f));
    g.setFont(Fonts::smallBold());
    g.drawText(text, inner.withTrimmedLeft(16.0f),
               juce::Justification::centred);
  }

  std::string hubId;
  float netAlpha = 0.0f;
  float uiAlpha = 0.0f;
  float engAlpha = 0.0f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalPathLegend)
};
