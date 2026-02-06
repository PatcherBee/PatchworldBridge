/*
  ==============================================================================
    Source/Audio/LockFreeUtilities.h
    Role: Lock-free data structures for audio thread (roadmap 12.2)
  ==============================================================================
*/
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace LockFree {

/**
 * SPSC Queue: Single-Producer Single-Consumer lock-free queue.
 * Use for audio thread -> UI thread communication.
 */
template <typename T, size_t Capacity>
class SPSCQueue {
public:
  bool push(const T &item) {
    size_t current = writeIndex.load(std::memory_order_relaxed);
    size_t next = increment(current);

    if (next == readIndex.load(std::memory_order_acquire)) {
      return false; // Full
    }

    buffer[current] = item;
    writeIndex.store(next, std::memory_order_release);
    return true;
  }

  bool pop(T &item) {
    size_t current = readIndex.load(std::memory_order_relaxed);

    if (current == writeIndex.load(std::memory_order_acquire)) {
      return false; // Empty
    }

    item = buffer[current];
    readIndex.store(increment(current), std::memory_order_release);
    return true;
  }

  bool isEmpty() const {
    return readIndex.load(std::memory_order_relaxed) ==
           writeIndex.load(std::memory_order_relaxed);
  }

  size_t size() const {
    size_t w = writeIndex.load(std::memory_order_relaxed);
    size_t r = readIndex.load(std::memory_order_relaxed);
    return (w >= r) ? (w - r) : (Capacity - r + w);
  }

private:
  size_t increment(size_t idx) const { return (idx + 1) % Capacity; }

  std::array<T, Capacity> buffer;
  std::atomic<size_t> writeIndex{0};
  std::atomic<size_t> readIndex{0};
};

/**
 * AtomicValue: Double-buffered atomic value for complex types.
 * Writer updates one buffer while reader uses the other.
 */
template <typename T>
class AtomicValue {
public:
  void store(const T &value) {
    int writeIdx = 1 - readIndex.load(std::memory_order_relaxed);
    buffers[writeIdx] = value;
    readIndex.store(writeIdx, std::memory_order_release);
  }

  T load() const {
    int idx = readIndex.load(std::memory_order_acquire);
    return buffers[idx];
  }

private:
  std::array<T, 2> buffers;
  std::atomic<int> readIndex{0};
};

/**
 * Seqlock: Sequence-locked value for single writer, multiple readers.
 * Reader retries if write occurred during read.
 */
template <typename T>
class Seqlock {
public:
  void store(const T &value) {
    sequence.fetch_add(1, std::memory_order_release);
    data = value;
    sequence.fetch_add(1, std::memory_order_release);
  }

  T load() const {
    T result;
    uint32_t seq0, seq1;
    do {
      seq0 = sequence.load(std::memory_order_acquire);
      result = data;
      seq1 = sequence.load(std::memory_order_acquire);
    } while (seq0 != seq1 || (seq0 & 1)); // Retry if write in progress
    return result;
  }

private:
  T data;
  std::atomic<uint32_t> sequence{0};
};

/**
 * RealtimeSafeFloat: Atomic float with exponential smoothing.
 */
class RealtimeSafeFloat {
public:
  void setTarget(float value) {
    target.store(value, std::memory_order_relaxed);
  }

  float process(float smoothingFactor = 0.99f) {
    float t = target.load(std::memory_order_relaxed);
    float c = current.load(std::memory_order_relaxed);
    float newVal = c + (t - c) * (1.0f - smoothingFactor);
    current.store(newVal, std::memory_order_relaxed);
    return newVal;
  }

  float get() const { return current.load(std::memory_order_relaxed); }

private:
  std::atomic<float> target{0.0f};
  std::atomic<float> current{0.0f};
};

} // namespace LockFree
