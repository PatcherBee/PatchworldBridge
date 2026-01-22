/*
  ==============================================================================
    Source/Components/MidiMappingManager.h
    Status: VERIFIED COMPLETE (Split Locks, FIFO, JSON Support)
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <map>
#include <tuple>
#include <vector>

// --- Helper Structs for Thread-Safe Queue ---
struct MappingUpdate {
  char paramID[64]; // Fixed size for lock-free FIFO
  float value;
};

struct MidiSource {
  int channel = -1;
  int ccNumber = -1;
  int noteNumber = -1;
  bool isCC = true;

  // Required for std::map key
  bool operator<(const MidiSource &other) const {
    return std::tie(channel, ccNumber, noteNumber, isCC) <
           std::tie(other.channel, other.ccNumber, other.noteNumber,
                    other.isCC);
  }
};

struct MappingTarget {
  juce::String paramID;
  float minRange = 0.0f;
  float maxRange = 1.0f;
};

class MidiMappingManager : public juce::MidiInputCallback,
                           public juce::AsyncUpdater {
public:
  enum class LearnState { Normal, LearnPending, AwaitingMIDI };

  // Initialize FIFO with buffer size
  MidiMappingManager() : fifo(1024) {}

  // --- Callbacks (Assigned by MainComponent) ---
  std::function<void(juce::String, float)> setParameterValueCallback;
  std::function<void(juce::String)> onMidiLogCallback;
  std::function<void()> onMappingChanged; // NEW: Notify when mapping is added

  // --- State Control ---
  void setLearnModeActive(bool active) {
    const juce::ScopedLock sl(stateLock);
    state = active ? LearnState::LearnPending : LearnState::Normal;
    if (!active)
      targetParamID.clear();
    triggerAsyncUpdate(); // Notify UI if needed
  }

  bool isLearnModeActive() const {
    // Safe read for UI state check
    return state != LearnState::Normal;
  }

  void setSelectedParameterForLearning(const juce::String &paramID) {
    const juce::ScopedLock sl(stateLock);
    if (state == LearnState::LearnPending) {
      targetParamID = paramID;
      state = LearnState::AwaitingMIDI;

      // Log to console/UI immediately
      if (onMidiLogCallback)
        onMidiLogCallback("! Waiting for MIDI to map: " + paramID);
    }
  }

  juce::String getSelectedParameter() const {
    const juce::ScopedLock sl(stateLock);
    return targetParamID;
  }

  // --- Core MIDI Processing (Realtime Thread) ---
  void handleIncomingMidiMessage(juce::MidiInput *source,
                                 const juce::MidiMessage &message) override {
    // Filter invalid messages immediately
    if (!message.isController() && !message.isNoteOn())
      return;

    // 1. Snapshot State (Short Lock)
    LearnState currentState;
    juce::String currentTarget;
    {
      const juce::ScopedLock sl(stateLock);
      currentState = state;
      currentTarget = targetParamID;
    }

    // 2. Identify Source
    MidiSource incomingSource;
    incomingSource.channel = message.getChannel();
    incomingSource.isCC = message.isController();
    if (incomingSource.isCC)
      incomingSource.ccNumber = message.getControllerNumber();
    else
      incomingSource.noteNumber = message.getNoteNumber();

    // 3. Mode A: LEARNING
    if (currentState == LearnState::AwaitingMIDI &&
        currentTarget.isNotEmpty()) {
      {
        const juce::ScopedLock ml(mappingLock); // Lock Map only for write
        activeMappings[incomingSource] = {currentTarget, 0.0f, 1.0f};
      }

      // Log success (Async to avoid blocking audio thread)
      juce::String mappingLog =
          "! Mapped: " + currentTarget + " to " +
          (incomingSource.isCC
               ? "CC " + juce::String(incomingSource.ccNumber)
               : "Note " + juce::String(incomingSource.noteNumber));

      juce::MessageManager::callAsync([this, mappingLog] {
        if (onMidiLogCallback)
          onMidiLogCallback(mappingLog);
      });

      // Fire immediate update so the UI snaps to the current HW position
      float immediateVal = incomingSource.isCC
                               ? (message.getControllerValue() / 127.0f)
                               : (message.getVelocity() / 127.0f);
      if (setParameterValueCallback) {
        juce::MessageManager::callAsync([this, currentTarget, immediateVal] {
          setParameterValueCallback(currentTarget, immediateVal);
        });
      }

      // Notify that a mapping has changed (for auto-save)
      if (onMappingChanged) {
        juce::MessageManager::callAsync([this] { onMappingChanged(); });
      }

      // Reset State
      {
        const juce::ScopedLock sl(stateLock);
        targetParamID.clear();
        state = LearnState::LearnPending;
      }
      triggerAsyncUpdate();
      return;
    }

    // 4. Mode B: PERFORMANCE (Mapped Control)
    // Only lock mappingLock here (Read access)
    const juce::ScopedLock ml(mappingLock);
    auto it = activeMappings.find(incomingSource);

    if (it != activeMappings.end()) {
      const auto &target = it->second;

      // Calculate Value (0.0 to 1.0)
      float rawVal = incomingSource.isCC
                         ? (message.getControllerValue() / 127.0f)
                         : (message.getVelocity() / 127.0f);
      float finalVal =
          target.minRange + rawVal * (target.maxRange - target.minRange);

      // Push to FIFO for Main Thread
      int start1, size1, start2, size2;
      fifo.prepareToWrite(1, start1, size1, start2, size2);
      if (size1 > 0) {
        target.paramID.copyToUTF8(updateBuffer[start1].paramID, 64);
        updateBuffer[start1].paramID[63] = 0; // Null terminate
        updateBuffer[start1].value = finalVal;
        fifo.finishedWrite(1);
        triggerAsyncUpdate(); // Wake up Main Thread
      }
    }
  }

  // --- Persistence (JSON) ---
  void saveMappingsToJSON(juce::DynamicObject *root) {
    const juce::ScopedLock sl(mappingLock);
    juce::Array<juce::var> mappingArray;

    for (auto const &[source, target] : activeMappings) {
      auto *entry = new juce::DynamicObject();
      entry->setProperty("parameter_id", target.paramID);
      entry->setProperty("midi_type", source.isCC ? "CC" : "Note");
      entry->setProperty("channel", source.channel);
      entry->setProperty("index",
                         source.isCC ? source.ccNumber : source.noteNumber);
      entry->setProperty("min_range", target.minRange);
      entry->setProperty("max_range", target.maxRange);
      mappingArray.add(juce::var(entry));
    }
    root->setProperty("mappings", mappingArray);
  }

  void loadMappingsFromJSON(const juce::var &mappingsVar) {
    const juce::ScopedLock sl(mappingLock);
    activeMappings.clear();

    auto *arr = mappingsVar.getArray();
    if (!arr)
      return;

    for (auto &entryVar : *arr) {
      if (auto *entry = entryVar.getDynamicObject()) {
        MidiSource source;
        source.channel = (int)entry->getProperty("channel");
        juce::String type = entry->getProperty("midi_type").toString();
        source.isCC = (type == "CC");
        int index = (int)entry->getProperty("index");

        if (source.isCC)
          source.ccNumber = index;
        else
          source.noteNumber = index;

        MappingTarget target;
        target.paramID = entry->getProperty("parameter_id").toString();
        target.minRange = (float)entry->getProperty("min_range");
        target.maxRange = (float)entry->getProperty("max_range");

        // Fix legacy files with 0-0 range
        if (target.maxRange == 0.0f && target.minRange == 0.0f)
          target.maxRange = 1.0f;

        if (target.paramID.isNotEmpty()) {
          activeMappings[source] = target;
        }
      }
    }
  }

  void resetMappings() {
    const juce::ScopedLock sl(mappingLock);
    activeMappings.clear();
  }

private:
  // Thread Safety
  LearnState state = LearnState::Normal;
  juce::String targetParamID = "";

  std::map<MidiSource, MappingTarget> activeMappings;
  juce::CriticalSection mappingLock; // Lock 1: Protects the map
  juce::CriticalSection
      stateLock; // Lock 2: Protects Learn State (Prevents UI/Audio deadlock)

  // FIFO for parameter updates
  juce::AbstractFifo fifo;
  std::array<MappingUpdate, 1024> updateBuffer;

  // AsyncUpdater Callback (Runs on Main Thread)
  void handleAsyncUpdate() override {
    int start1, size1, start2, size2;
    fifo.prepareToRead(fifo.getNumReady(), start1, size1, start2, size2);

    if (size1 > 0)
      processQueueBlock(start1, size1);
    if (size2 > 0)
      processQueueBlock(start2, size2);

    fifo.finishedRead(size1 + size2);
  }

  void processQueueBlock(int start, int size) {
    if (!setParameterValueCallback)
      return;

    for (int i = 0; i < size; ++i) {
      auto &update = updateBuffer[start + i];
      // Call into MainComponent to move sliders
      setParameterValueCallback(juce::String(update.paramID), update.value);
    }
  }
};