/*
  ==============================================================================
    Source/Engine/LockFreeMidiPipe.h
    Role: Thread-safe, lock-free MIDI transfer from UI/Loading -> Audio Thread
  ==============================================================================
*/

#pragma once
// --- 1. C++ Standard Library ---
#include <array>

// --- 2. JUCE Framework ---
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

class LockFreeMidiPipe {
public:
  // Capacity must be power of 2 for AbstractFifo optimization
  static constexpr int Capacity = 4096;

  LockFreeMidiPipe() : fifo(Capacity) {}

  // ==============================================================================
  // PRODUCER (UI Thread / File Loading Thread)
  // Safe to call from the Message Thread
  // ==============================================================================
  void push(const juce::MidiMessage &m) {
    int start1, size1, start2, size2;

    // Ask FIFO for space to write 1 item
    fifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0) {
      buffer[start1] = m;
    } else if (size2 > 0) {
      buffer[start2] = m;
    }

    // Commit the write (makes it visible to consumer)
    fifo.finishedWrite(size1 + size2);
  }

  // Batch push optimization (for loading entire files)
  void pushBatch(const juce::MidiBuffer &bufferToAdd) {
    for (const auto metadata : bufferToAdd) {
      push(metadata.getMessage());
    }
  }

  // ==============================================================================
  // CONSUMER (Audio Thread)
  // Safe to call inside getNextAudioBlock / processBlock
  // ==============================================================================
  void popAllTo(juce::MidiBuffer &destination) {
    int start1, size1, start2, size2;

    // Ask FIFO what data is available
    fifo.prepareToRead(fifo.getNumReady(), start1, size1, start2, size2);

    // Copy first contiguous block
    for (int i = 0; i < size1; ++i) {
      // We strip timestamps (set to 0) for immediate processing,
      // or you can preserve m.getTimeStamp() if your logic relies on absolute
      // time. For a "Pipe", immediate (0) usually means "process now".
      // NOTE: destination MUST have enough pre-allocated capacity (Fix C)
      // to avoid allocations on the Audio Thread.
      destination.addEvent(buffer[start1 + i], 0);
    }

    // Copy wrapped block (circular buffer wrap-around)
    for (int i = 0; i < size2; ++i) {
      destination.addEvent(buffer[start2 + i], 0);
    }

    // Mark as read so space can be reused
    fifo.finishedRead(size1 + size2);
  }

private:
  juce::AbstractFifo fifo;
  std::array<juce::MidiMessage, Capacity> buffer;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LockFreeMidiPipe)
};
