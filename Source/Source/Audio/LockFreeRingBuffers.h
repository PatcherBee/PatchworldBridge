/*
  ==============================================================================
    Source/Engine/LockFreeRingBuffers.h
    Role: Decouple Audio/Network threads from UI thread allocations.
  ==============================================================================
*/
#pragma once
#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <mutex>

// --- 1. LOGGING BUFFER ---
// Prevents string allocations on the audio thread.
// We pass an Integer Code and one or two optional values.
struct LogEntry {
  enum class Code {
    None,
    MidiInput,
    MidiOutput,
    OscIn,
    OscOut,
    TransportPlay,
    TransportStop,
    LinkEnabled,
    LinkDisabled,
    Error,
    Custom
  };
  Code code = Code::None;
  int val1 = 0;
  float val2 = 0.0f;
  // We intentionally avoid juce::String here.
  // If Custom text is absolutely needed, we'd need a fixed size char array
  // but typically we can map codes to static strings in the UI thread.
};

class LogBuffer {
public:
  static constexpr int capacity = 512;
  juce::AbstractFifo fifo{capacity};
  std::array<LogEntry, capacity> buffer;

  // AUDIO THREAD SAFE
  void push(LogEntry::Code c, int v1 = 0, float v2 = 0.0f) {
    int s1, n1, s2, n2;
    fifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 > 0) {
      buffer[s1] = {c, v1, v2};
      fifo.finishedWrite(n1);
    }
  }

  template <typename ProcessFunction>
  void process(ProcessFunction &&processFn) {
    int s1, n1, s2, n2;
    fifo.prepareToRead(fifo.getNumReady(), s1, n1, s2, n2);
    if (n1 > 0) {
      for (int i = 0; i < n1; ++i)
        processFn(buffer[s1 + i]);
    }
    if (n2 > 0) {
      for (int i = 0; i < n2; ++i)
        processFn(buffer[s2 + i]);
    }
    fifo.finishedRead(n1 + n2);
  }
};

// --- 2. VISUAL FEEDBACK BUFFER ---
// Prevents callAsync() for Note On/Off/CC flashes
struct VisualEvent {
  enum class Type { NoteOn, NoteOff, CC, MixerFlash };
  VisualEvent(Type t = Type::NoteOn, int ch = 0, int n = 0, float v = 0.0f)
      : type(t), channel(ch), noteOrCC(n), value(v) {}
  Type type = Type::NoteOn;
  int channel;
  int noteOrCC;
  float value;
};

// 100% Lock-Free SPSC-style Visual Buffer (Lossy when full)
// Audio thread pushes; UI thread reads. If full, we DROP the event.
// Visuals are secondary to audio—never spin-wait on the audio thread.
class VisualBuffer {
public:
  static constexpr int capacity = 1024;
  juce::AbstractFifo fifo{capacity};
  std::array<VisualEvent, capacity> buffer;
  juce::SpinLock writeLock;

  // Audio/Network Thread: Pushes event. Returns immediately if full (drops).
  // Lock protects multi-producer (audio + network) from corrupting fifo indexes.
  void push(const VisualEvent &e) {
    const juce::SpinLock::ScopedLockType sl(writeLock);
    int s1, n1, s2, n2;
    fifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 > 0) {
      buffer[s1] = e;
      fifo.finishedWrite(1);
    }
  }

  template <typename ProcessFunction>
  void process(ProcessFunction &&processFn) {
    int s1, n1, s2, n2;
    fifo.prepareToRead(fifo.getNumReady(), s1, n1, s2, n2);
    if (n1 > 0) {
      for (int i = 0; i < n1; ++i)
        processFn(buffer[s1 + i]);
    }
    if (n2 > 0) {
      for (int i = 0; i < n2; ++i)
        processFn(buffer[s2 + i]);
    }
    fifo.finishedRead(n1 + n2);
  }
};

// --- 3. MIDI SEND QUEUE ---
// Multi-producer (audio + message thread), single-consumer (dedicated drain thread).
// Avoids sendMessageNow() from audio thread. Wake-on-data + short timeout for low latency.
class MidiSendQueue {
public:
  static constexpr int capacity = 256;
  juce::AbstractFifo fifo{capacity};
  std::array<juce::MidiMessage, capacity> buffer;
  juce::SpinLock writeLock;
  std::mutex notifyMutex;
  std::condition_variable notifyCond;

  // Any thread (including audio): enqueue for later send. Drops if full. Wakes drain thread.
  void push(const juce::MidiMessage &m) {
    const juce::SpinLock::ScopedLockType sl(writeLock);
    int s1, n1, s2, n2;
    fifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 > 0) {
      buffer[s1] = m;
      fifo.finishedWrite(1);
      notifyCond.notify_one();
    }
  }

  // Drain thread: wait up to timeout for data, then return. Call process() after.
  void waitForData(std::chrono::microseconds timeout) {
    std::unique_lock<std::mutex> lock(notifyMutex);
    notifyCond.wait_for(lock, timeout);
  }

  // Wake drain thread (e.g. for shutdown). Safe to call from any thread.
  void wakeDrain() { notifyCond.notify_one(); }

  // Drain thread only: invoke for each queued message. Call after waitForData() or when ready.
  template <typename ProcessFunction>
  void process(ProcessFunction &&processFn) {
    int s1, n1, s2, n2;
    fifo.prepareToRead(fifo.getNumReady(), s1, n1, s2, n2);
    if (n1 > 0) {
      for (int i = 0; i < n1; ++i)
        processFn(buffer[s1 + i]);
    }
    if (n2 > 0) {
      for (int i = 0; i < n2; ++i)
        processFn(buffer[s2 + i]);
    }
    fifo.finishedRead(n1 + n2);
  }
};
