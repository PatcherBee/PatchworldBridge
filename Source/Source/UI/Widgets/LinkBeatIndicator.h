#pragma once
#include "../../Core/TimerHub.h"
#include "../Fonts.h"
#include "../Theme.h"
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>

class LinkBeatIndicator : public juce::Component, public juce::TooltipClient {
public:
  LinkBeatIndicator() {
    hubId = "LinkBeat_" + std::to_string(reinterpret_cast<int64_t>(this));
    TimerHub::instance().subscribe(
        hubId,
        [this]() {
          if (!isVisible())
            return;
          double phase = phaseAtomic.load(std::memory_order_relaxed);
          int stepIndex = (int)std::floor(phase);
          int currentStep = (int)std::floor(currentPhase);
          if (stepIndex != currentStep) {
            currentPhase = phase;
            repaint();
          }
        },
        TimerHub::Rate10Hz); // 10Hz is sufficient for beat step display
  }

  ~LinkBeatIndicator() override { TimerHub::instance().unsubscribe(hubId); }

  void setTooltip(const juce::String &t) { tooltipString = t; }
  juce::String getTooltip() override { return tooltipString; }

  void setPhase(double phase) {
    phaseAtomic.store(phase, std::memory_order_relaxed);
  }

  void setCurrentBeat(double beat, double newQuantum) {
    if (newQuantum > 0.0) {
      quantum = newQuantum;
      setPhase(std::fmod(beat, newQuantum));
    }
  }

  void setQuantum(double q) {
    if (q >= 1.0 && std::abs(quantum - q) > 0.1) {
      quantum = q;
      repaint();
    }
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();
    int steps = juce::jmax(1, (int)std::round(quantum));
    float stepWidth = bounds.getWidth() / (float)steps;

    // 1. Draw grid slots (beat steps) - adaptive time signature
    for (int i = 0; i < steps; ++i) {
      float x = (float)i * stepWidth;
      bool isFirstBeat = (i == 0);
      g.setColour(isFirstBeat ? Theme::accent.withAlpha(0.25f)
                              : Theme::grid.withAlpha(0.2f));
      g.fillRect(x, 0.0f, stepWidth, bounds.getHeight());
      g.setColour(Theme::grid.withAlpha(0.3f));
      g.drawRect(x, 0.0f, stepWidth, bounds.getHeight(), 1.0f);
    }

    // 2. Draw current beat step (discrete, not smooth) - first beat distinct
    int currentBeatIndex =
        juce::jlimit(0, steps - 1, (int)std::floor(currentPhase) % steps);
    float headX = (float)currentBeatIndex * stepWidth;

    bool isFirstBeat = (currentBeatIndex == 0);
    g.setColour(isFirstBeat ? Theme::accent.brighter(0.2f) : Theme::accent);
    g.fillRoundedRectangle(headX + 1.0f, 2.0f, stepWidth - 2.0f,
                           bounds.getHeight() - 4.0f, 4.0f);

    // 3. Draw beat number
    int displayBeat = currentBeatIndex + 1;
    g.setColour(juce::Colours::white);
    g.setFont(Fonts::body());
    g.drawText(juce::String(displayBeat), (int)headX, 0, (int)stepWidth,
               (int)bounds.getHeight(), juce::Justification::centred, false);
  }

private:
  juce::String tooltipString;
  std::string hubId;
  std::atomic<double> phaseAtomic{0.0};
  double currentPhase = 0.0;
  double quantum = 4.0;
};