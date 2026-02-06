/*
  ==============================================================================
    Source/Core/AudioWatchdog.h
    Monitors audio thread for stalls and triggers panic if detected.
  ==============================================================================
*/
#pragma once
#include "TimerHub.h"
#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>

class AudioWatchdog {
public:
  std::atomic<int> callbackCounter{0};
  int lastCounter = 0;
  std::function<void()> onStallCallback;

  AudioWatchdog(std::function<void()> onStall) : onStallCallback(onStall) {
    hubId = "AudioWatchdog_" + juce::Uuid().toDashedString().toStdString();
    TimerHub::instance().subscribe(hubId, [this] { tick(); },
                                   TimerHub::Low1Hz);
  }

  ~AudioWatchdog() { TimerHub::instance().unsubscribe(hubId); }

  // Call this inside processBlock from audio thread
  void pet() { callbackCounter.fetch_add(1, std::memory_order_relaxed); }

private:
  void tick() {
    int current = callbackCounter.load(std::memory_order_relaxed);
    if (current == lastCounter) {
      if (onStallCallback)
        onStallCallback();
    }
    lastCounter = current;
  }

  std::string hubId;
};
