/*
  ==============================================================================
    Source/Services/StateRecycler.h
    SAFE VERSION: Offloads destruction to background thread if pool is full.
  ==============================================================================
*/
#pragma once
#include "DeferredDeleter.h"
#include <juce_core/juce_core.h>
#include <memory>
#include <stack>


template <typename T> class StateRecycler {
public:
  StateRecycler(size_t maxSize = 8) : maxPoolSize(maxSize) {}

  // Set the deleter reference (Call this in your Constructor!)
  void setDeleter(DeferredDeleter *d) { deleter = d; }

  ~StateRecycler() {
    const juce::SpinLock::ScopedLockType sl(lock);
    while (!pool.empty())
      pool.pop();
  }

  // UI Thread: Get a clean object
  std::shared_ptr<T> checkout() {
    const juce::SpinLock::ScopedLockType sl(lock);
    if (pool.empty()) {
      return std::make_shared<T>(); // Allocate new if empty
    }
    auto obj = pool.top();
    pool.pop();
    return obj;
  }

  // Audio Thread: Recycle old object
  void recycle(std::shared_ptr<T> oldState) {
    if (!oldState)
      return;

    // 1. Try to return to pool (Fastest)
    {
      const juce::SpinLock::ScopedLockType sl(lock);
      if (pool.size() < maxPoolSize) {
        pool.push(std::move(oldState));
        return;
      }
    }

    // 2. Pool is full? Send to Trash Bin (Background Thread)
    // This prevents 'free()' from running on the Audio Thread.
    if (deleter) {
      deleter->deleteAsync(std::move(oldState));
      return;
    }
    // 3. No deleter: allow one overflow to avoid free() on audio thread.
    // Caller must setDeleter() when used from audio thread for hard limit.
    const juce::SpinLock::ScopedLockType sl2(lock);
    pool.push(std::move(oldState));
  }

private:
  juce::SpinLock lock;
  std::stack<std::shared_ptr<T>> pool;
  size_t maxPoolSize;
  DeferredDeleter *deleter = nullptr;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StateRecycler)
};