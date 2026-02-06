/*
  ==============================================================================
    Source/Core/RepaintCoordinator.h
    Role: Batches repaint requests to avoid storms of independent repaints.
  ==============================================================================
*/
#pragma once

#include <atomic>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

class RepaintCoordinator {
public:
  enum DirtyBit : uint32_t {
    PianoRoll = 1 << 0,
    Keyboard = 1 << 1,
    Mixer = 1 << 2,
    Sequencer = 1 << 3,
    Transport = 1 << 4,
    Playhead = 1 << 5,
    VelocityLane = 1 << 6,
    Automation = 1 << 7,
    Log = 1 << 8,
    Dashboard = 1 << 9,  // Module windows moved/resized; need full repaint to clear ghosting
  };

  void markDirty(DirtyBit bit) {
    dirtyFlags_.fetch_or(bit, std::memory_order_relaxed);
  }

  void flush(std::function<void(uint32_t)> handler) {
    uint32_t flags = dirtyFlags_.exchange(0, std::memory_order_acquire);
    lastFlushHadDirty_.store(flags != 0, std::memory_order_relaxed);
    if (flags != 0 && handler)
      handler(flags);
  }

  void flushAll(std::function<void(uint32_t)> handler) {
    uint32_t flags = dirtyFlags_.exchange(0xFFFF, std::memory_order_acquire);
    lastFlushHadDirty_.store(flags != 0, std::memory_order_relaxed);
    if (flags != 0 && handler)
      handler(flags);
  }

  /** True if the last flush had any dirty regions (use to skip GL/Vulkan repaint when false). */
  bool hadDirtyLastFlush() const {
    return lastFlushHadDirty_.load(std::memory_order_relaxed);
  }

private:
  std::atomic<uint32_t> dirtyFlags_{0};
  std::atomic<bool> lastFlushHadDirty_{false};
};
