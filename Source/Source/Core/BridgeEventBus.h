/*
  ==============================================================================
    Source/Core/BridgeEventBus.h
    Role: Central bus for BridgeEvent delivery. Ensures all subscribers
    (network, log, etc.) receive events without manual callback wiring.

    CRITICAL FIX: Uses lock-free FIFO to prevent audio thread blocking.
  ==============================================================================
*/
#pragma once

#include "../Audio/OscTypes.h"
#include "../Core/TimerHub.h"
#include <array>
#include <functional>
#include <juce_core/juce_core.h>
#include <vector>

class BridgeEventBus {
public:
  using Listener = std::function<void(const BridgeEvent &)>;
  using ListenerId = int;

  static BridgeEventBus &instance() {
    static BridgeEventBus bus;
    return bus;
  }

  /** Subscribe to all bridge events. Returns ID for unsubscribe.
   *  Thread-safe but uses lock (NOT for audio thread). */
  ListenerId subscribe(Listener listener) {
    juce::ScopedLock sl(lock_);
    int id = nextId_++;
    listeners_.push_back({id, std::move(listener)});
    return id;
  }

  void unsubscribe(ListenerId id) {
    juce::ScopedLock sl(lock_);
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(),
                       [id](const Entry &e) { return e.id == id; }),
        listeners_.end());
  }

  /** DEPRECATED: Use push() from audio thread instead.
   *  This method still exists for non-realtime callers but copies listeners. */
  void emit(const BridgeEvent &e) {
    std::vector<Listener> copy;
    {
      juce::ScopedLock sl(lock_);
      copy.reserve(listeners_.size());
      for (const auto &entry : listeners_)
        copy.push_back(entry.listener);
    }
    for (auto &fn : copy)
      fn(e);
  }

  /** Lock-free push for audio/MIDI thread. Events broadcast asynchronously. */
  void push(const BridgeEvent &e) {
    int start1, size1, start2, size2;
    fifo_.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0) {
      eventBuffer_[start1] = e;
      fifo_.finishedWrite(1);
    }
    // If FIFO full, event is dropped (audio thread never blocks)
  }

private:
  BridgeEventBus() {
    hubId_ = "BridgeEventBus_" + juce::Uuid().toDashedString().toStdString();
    // 10Hz = ~100ms broadcast latency (acceptable for network/log, not audio)
    TimerHub::instance().subscribe(
        hubId_, [this] { broadcast(); }, TimerHub::Rate10Hz);
  }

  ~BridgeEventBus() { TimerHub::instance().unsubscribe(hubId_); }

  void broadcast() {
    int start1, size1, start2, size2;
    fifo_.prepareToRead(fifo_.getNumReady(), start1, size1, start2, size2);

    std::vector<Listener> copy;
    {
      juce::ScopedLock sl(lock_);
      copy.reserve(listeners_.size());
      for (const auto &entry : listeners_)
        copy.push_back(entry.listener);
    }

    auto process = [this, &copy](int idx) {
      const BridgeEvent &e = eventBuffer_[idx];
      for (auto &fn : copy)
        fn(e);
    };

    if (size1 > 0)
      for (int i = 0; i < size1; ++i)
        process(start1 + i);
    if (size2 > 0)
      for (int i = 0; i < size2; ++i)
        process(start2 + i);

    fifo_.finishedRead(size1 + size2);
  }

  struct Entry {
    ListenerId id;
    Listener listener;
  };

  // Lock-free FIFO (audio thread → broadcaster thread)
  static constexpr int Capacity = 512;
  juce::AbstractFifo fifo_{Capacity};
  std::array<BridgeEvent, Capacity> eventBuffer_;
  std::string hubId_;

  // Listener management (still uses lock, only for subscribe/unsubscribe)
  std::vector<Entry> listeners_;
  juce::CriticalSection lock_;
  int nextId_ = 1;
};
