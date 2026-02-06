/*
  ==============================================================================
    Source/Audio/AudioMemoryPool.h
    Role: Fixed-size pool for audio-thread allocations; avoid malloc in processBlock.
  ==============================================================================
*/
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

class AudioMemoryPool {
public:
  static constexpr size_t PoolSize = 64 * 1024; // 64 KB

  /** Allocate from pool (audio thread safe). Returns nullptr if no space. */
  void* allocate(size_t size) {
    size = (size + 7u) & ~7u; // align to 8
    size_t current = offset.load(std::memory_order_relaxed);
    while (current + size <= PoolSize) {
      if (offset.compare_exchange_weak(current, current + size,
                                       std::memory_order_relaxed,
                                       std::memory_order_relaxed))
        return &pool[current];
    }
    return nullptr;
  }

  /** Reset pool (call from message thread or at buffer switch, not from processBlock). */
  void reset() { offset.store(0, std::memory_order_release); }

  size_t used() const { return offset.load(std::memory_order_acquire); }

private:
  alignas(8) std::uint8_t pool[PoolSize];
  std::atomic<size_t> offset{0};
};
