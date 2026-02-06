#pragma once
#include "../../Core/Diagnostics.h"
#include "../../Core/TimerHub.h"
#include "../Theme.h"
#include "../Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>

class DiagnosticOverlay : public juce::Component {
public:
  DiagnosticOverlay(DiagnosticData &data) : diagData(data) {
    setInterceptsMouseClicks(false, false);
    hubId = "DiagOverlay_" + std::to_string(reinterpret_cast<int64_t>(this));
    TimerHub::instance().subscribe(
        hubId,
        [this]() {
          if (isVisible())
            repaint();
        },
        TimerHub::Low15Hz);  // ~15Hz refresh
  }

  ~DiagnosticOverlay() override {
    TimerHub::instance().unsubscribe(hubId);
  }

  void paint(juce::Graphics &g) override {
    auto b = getLocalBounds().toFloat();

    // 1. Vaporwave HUD Background
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(b, 6.0f);
    g.setColour(Theme::accent.withAlpha(0.8f));
    g.drawRoundedRectangle(b, 6.0f, 1.5f);

    // 2. Draw Metrics
    g.setColour(juce::Colours::white);
    g.setFont(Fonts::bodyBold());
    int y = 5;

    auto drawMetric = [&](juce::String label, juce::String val,
                          juce::Colour col) {
      g.setColour(juce::Colours::white.withAlpha(0.7f));
      g.drawText(label, 10, y, 70, 20, juce::Justification::left);
      g.setColour(col);
      g.drawText(val, 80, y, 60, 20, juce::Justification::right);
      y += 18;
    };

    drawMetric("CPU:", juce::String(diagData.cpuUsage.load(), 1) + "%",
               diagData.cpuUsage.load() > 70.0f ? juce::Colours::red
                                                : juce::Colours::cyan);
    drawMetric("OSC/s:", juce::String(diagData.oscPacketsPerSec.load()),
               juce::Colours::lime);
    drawMetric("Jitter:", juce::String(diagData.midiJitterMs.load(), 2) + "ms",
               juce::Colours::yellow);
    drawMetric("VOICES:", juce::String(diagData.activeVoices.load()),
               Theme::accent);
  }

private:
  std::string hubId;
  DiagnosticData &diagData;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiagnosticOverlay)
};
