/*
  ==============================================================================
    Source/UI/Widgets/Indicators.h
    FIXED: Renamed trigger() to activate() to match MainComponent usage.
  ==============================================================================
*/
#pragma once
#include "../../Core/TimerHub.h"
#include "../Theme.h"
#include <atomic>
#include <juce_core/juce_core.h>
#include <string>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

// --- 1. Connection Status Light ---
class ConnectionLight : public juce::Component, public juce::TooltipClient {
public:
  bool isConnected = false;
  juce::String tooltipString;

  void setConnected(bool connected) {
    if (isConnected != connected) {
      isConnected = connected;
      repaint();
    }
  }

  void setTooltip(const juce::String &t) { tooltipString = t; }
  juce::String getTooltip() override { return tooltipString; }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();
    float d = juce::jmin(bounds.getWidth(), bounds.getHeight()) - 8.0f;
    auto circle = bounds.withSizeKeepingCentre(d, d);
    g.setColour(isConnected ? juce::Colours::lime : juce::Colours::red);
    g.fillEllipse(circle);
    if (isConnected) {
      g.setColour(juce::Colours::lime.withAlpha(0.4f));
      g.drawEllipse(circle, 2.0f);
    }
  }
};

// --- 2. MIDI Activity Light ---
class MidiIndicator : public juce::Component,
                      public juce::TooltipClient {
public:
  MidiIndicator() {
    hubId = "MidiInd_" + std::to_string(reinterpret_cast<int64_t>(this));
    TimerHub::instance().subscribe(
        hubId,
        [this]() {
          if (!isVisible()) return;
          if (triggered.exchange(false, std::memory_order_relaxed))
            level = 1.0f;
          if (level > 0.001f) {
            level *= 0.85f;
            if (level < 0.01f) level = 0.0f;
            repaint();
          }
        },
        TimerHub::Low15Hz);
  }

  ~MidiIndicator() override {
    TimerHub::instance().unsubscribe(hubId);
  }

  // FIXED: Renamed from trigger() to activate()  // Thread-Safe Trigger (Called
  // from Audio/Network Threads)
  void activate() { triggered.store(true, std::memory_order_relaxed); }

  void setTooltip(const juce::String &t) { tooltipString = t; }
  juce::String getTooltip() override { return tooltipString; }

  void paint(juce::Graphics &g) override {
    auto r = getLocalBounds().reduced(2).toFloat();
    g.setColour(Theme::bgPanel.darker(0.2f));
    g.fillRoundedRectangle(r, 2.0f);

    if (level > 0.01f) {
      g.setColour(Theme::accent.withAlpha(level));
      g.fillRoundedRectangle(r, 2.0f);
      g.setColour(Theme::accent.withAlpha(level * 0.5f));
      g.drawRoundedRectangle(r.expanded(2.0f), 3.0f, 1.0f);
    }
  }

private:
  std::string hubId;
  juce::String tooltipString;
  float level = 0.0f;
  std::atomic<bool> triggered{false};
};