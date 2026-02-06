//////////////////////////////////////////////////////////////////////
// FILE: Source/Engine/NoteTracker.h
//////////////////////////////////////////////////////////////////////

#pragma once
#include "../Audio/LockFreeRingBuffers.h"
#include "../Network/OscSchemaSwapper.h"
#include <array>
#include <atomic>
#include <juce_core/juce_core.h>

class NoteTracker {
public:
  NoteTracker() { clearAll(); }

  // --- Audio/Network Thread API ---

  // Register a Note On: We map (Input Ch/Note) -> (Output Pitch)
  // Returns the Output Pitch that was stored.
  void processNoteOn(int ch, int inputNote, int outputPitch, float velocity,
                     OscSchemaSwapper &swapper) {
    const juce::SpinLock::ScopedLockType sl(getLock(ch, inputNote));

    int idx = getSlotIndex(ch, inputNote);
    auto &slot = activeNotes[idx];

    slot.outputPitch = outputPitch; // REMEMBER THIS PITCH
    auto schemaInfo = swapper.getSchemaForNoteOn();
    slot.schemaGen = schemaInfo.second;
    slot.velocity = velocity;
    slot.isActive = true;

    // Visual feedback uses the OUTPUT pitch to match what we hear
    visualBuffer.push({VisualEvent::Type::NoteOn, ch, outputPitch, velocity});
  }

  // Process Note Off: Look up what we played for this input note
  // Returns {StoredOutputPitch, Schema}
  std::pair<int, std::shared_ptr<OscNamingSchema>>
  processNoteOff(int ch, int inputNote, OscSchemaSwapper &swapper) {
    const juce::SpinLock::ScopedLockType sl(getLock(ch, inputNote));

    int idx = getSlotIndex(ch, inputNote);
    auto &slot = activeNotes[idx];

    if (!slot.isActive) {
      // Failsafe: If we missed the NoteOn, assume 1:1 mapping
      // This prevents "stuck off" logic if state is lost
      return {inputNote, swapper.getSchemaForNoteOn().first};
    }

    int p = slot.outputPitch; // RETRIEVE THE REMEMBERED PITCH
    auto schema = swapper.getSchemaForGeneration(slot.schemaGen);

    slot.isActive = false;
    slot.outputPitch = -1;

    visualBuffer.push({VisualEvent::Type::NoteOff, ch, p, 0.0f});

    return {p, schema};
  }

  void clearAll() {
    for (auto &slot : activeNotes) {
      slot.isActive = false;
      slot.outputPitch = -1;
    }
  }

  VisualBuffer &getVisualBuffer() { return visualBuffer; }

private:
  struct NoteSlot {
    int outputPitch = -1; // The actual pitch we sent to OSC/Audio
    uint64_t schemaGen = 0;
    float velocity = 0.0f;
    bool isActive = false;
  };

  static constexpr int NumChannels = 17;
  static constexpr int NumNotes = 128;
  static constexpr int TotalSlots = NumChannels * NumNotes;

  std::array<NoteSlot, TotalSlots> activeNotes;
  std::array<juce::SpinLock, NumChannels> locks;
  VisualBuffer visualBuffer;

  inline int getSlotIndex(int ch, int note) const {
    // Safety wrap
    return (juce::jlimit(0, 16, ch) * 128) + juce::jlimit(0, 127, note);
  }

  inline juce::SpinLock &getLock(int ch, int note) {
    juce::ignoreUnused(note);
    return locks[ch % NumChannels];
  }
};