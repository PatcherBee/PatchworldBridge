/*
  ==============================================================================
    Source/Network/OscAirlock.h
    Lock-Free: Uses juce::AbstractFifo (Wait-Free). No CAS livelock.
  ==============================================================================
*/

#pragma once
#include "../Audio/OscTypes.h"
#include <array>
#include <functional>
#include <juce_core/juce_core.h>

class OscAirlock {
public:
  static constexpr int Capacity = 8192; // Power of 2

  OscAirlock() = default;

  /** Optional: called after a successful push (e.g. signal NetworkWorker). Set once at init; safe from audio thread. */
  void setOnPush(std::function<void()> f) { onPush = std::move(f); }

  // Producer (Audio/MIDI Thread)
  bool push(const BridgeEvent &ev) {
    int start1, size1, start2, size2;
    fifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0) {
      buffer[start1] = ev;
      fifo.finishedWrite(1);
      if (onPush)
        onPush();
      return true;
    }
    return false; // Buffer full
  }

  /** Process up to DefaultBatchSize per call to keep audio callback time bounded. */
  static constexpr int DefaultBatchSize = 512;
  // Consumer - Process up to DefaultBatchSize per call (avoids callback spikes when queue is full)
  template <typename ProcessFunction>
  void process(ProcessFunction &&processFn) {
    processBatch(std::forward<ProcessFunction>(processFn), DefaultBatchSize);
  }

  template <typename ProcessFunction>
  void processBatch(ProcessFunction &&processFn, int maxItems) {
    int start1, size1, start2, size2;
    int numReady = fifo.getNumReady();
    if (numReady > maxItems)
      numReady = maxItems;

    fifo.prepareToRead(numReady, start1, size1, start2, size2);

    if (size1 > 0) {
      for (int i = 0; i < size1; ++i)
        processFn(buffer[start1 + i]);
    }
    if (size2 > 0) {
      for (int i = 0; i < size2; ++i)
        processFn(buffer[start2 + i]);
    }
    fifo.finishedRead(size1 + size2);
  }

  void clear() { fifo.reset(); }

  int getNumReady() const { return fifo.getNumReady(); }
  float getPressure() const { return (float)fifo.getNumReady() / Capacity; }

private:
  juce::AbstractFifo fifo{Capacity};
  std::array<BridgeEvent, Capacity> buffer;
  std::function<void()> onPush;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscAirlock)
};
