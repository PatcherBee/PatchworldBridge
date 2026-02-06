/*
  ==============================================================================
    Source/Core/UIWatchdog.h
    Role: Detects UI freezes by checking elapsed time since last markAlive().
  ==============================================================================
*/
#pragma once

#include <atomic>
#include <juce_core/juce_core.h>

#include "LogService.h"

class UIWatchdog {
public:
  /** Call from main UI tick (e.g. TimerHub High60Hz) to mark UI responsive. */
  static void markAlive() {
    lastUIUpdate.store(juce::Time::getMillisecondCounterHiRes(),
                       std::memory_order_relaxed);
  }

  /** Call from a low-rate timer (e.g. 1Hz); logs if no markAlive for > threshold ms. */
  static void check() {
    double now = juce::Time::getMillisecondCounterHiRes();
    double elapsed = now - lastUIUpdate.load(std::memory_order_relaxed);
    if (elapsed > thresholdMs) {
      LogService::instance().error("UI WATCHDOG: No update for "
                                   + juce::String(static_cast<int>(elapsed))
                                   + " ms (possible freeze).");
    }
  }

  static constexpr double thresholdMs = 5000.0;

private:
  inline static std::atomic<double> lastUIUpdate{0.0};
};
