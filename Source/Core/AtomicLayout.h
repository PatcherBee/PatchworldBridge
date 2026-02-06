/*
  ==============================================================================
    Source/Core/AtomicLayout.h
    Role: Thread-safe layout state for UI/resize coordination.
    STATUS: UNUSED. No includes or usages in Source. Reserved for future
    use (e.g. Performance panel / splice editor reading layout off-thread).
  ==============================================================================
*/
#pragma once

#include <atomic>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

struct AtomicLayoutState {
  int pianoRollWidth = 0;
  int keyboardHeight = 0;
  float noteHeight = 16.0f;
  float pixelsPerBeat = 60.0f;
};

class AtomicLayout {
public:
  AtomicLayout() { state_.store(new AtomicLayoutState()); }

  ~AtomicLayout() {
    auto *p = state_.exchange(nullptr);
    if (p)
      delete p;
  }

  void update(const AtomicLayoutState &newState) {
    auto *fresh = new AtomicLayoutState(newState);
    auto *old = state_.exchange(fresh);
    juce::MessageManager::callAsync([old]() {
      if (old)
        delete old;
    });
  }

  AtomicLayoutState read() const {
    auto *p = state_.load(std::memory_order_acquire);
    return p ? *p : AtomicLayoutState();
  }

private:
  std::atomic<AtomicLayoutState *> state_;
};
