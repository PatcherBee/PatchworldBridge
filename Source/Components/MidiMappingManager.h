/*
  ==============================================================================
    Source/Components/MidiMappingManager.h
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <functional>
#include <map>

struct MappingUpdate {
  char paramID[64]; // Fixed size for lock-free safety
  float value;
};

struct MidiSource {
  int channel = -1;
  int ccNumber = -1;
  int noteNumber = -1;
  bool isCC = true;

  bool operator<(const MidiSource &other) const {
    return std::tie(channel, ccNumber, noteNumber, isCC) <
           std::tie(other.channel, other.ccNumber, other.noteNumber,
                    other.isCC);
  }
};

class MidiMappingManager : public juce::MidiInputCallback,
                           public juce::AsyncUpdater {
public:
  MidiMappingManager() : fifo(1024) {}

  // Callback to update the actual UI/Parameters in MainComponent
  std::function<void(juce::String, float)> setParameterValueCallback;

  // --- Learn Mode Control ---
  void setLearnModeActive(bool active) {
    if (learnModeActive != active) {
      learnModeActive = active;
      targetParamID.clear();
      triggerAsyncUpdate(); // Notify GUI if needed
    }
  }

  bool isLearnModeActive() const { return learnModeActive; }

  void setSelectedParameterForLearning(const juce::String &paramID) {
    targetParamID = paramID;
    DBG("Learn Mode: Waiting for MIDI input for parameter: " + targetParamID);
    // Could trigger repaint of overlay here if we had ref
  }

  juce::String getSelectedParameter() const { return targetParamID; }

  // --- MIDI Input Handling ---
  void handleIncomingMidiMessage(juce::MidiInput *source,
                                 const juce::MidiMessage &message) override {
    // 1. Filter
    if (!message.isController() && !message.isNoteOn())
      return;

    MidiSource incomingSource;
    incomingSource.channel = message.getChannel();
    incomingSource.isCC = message.isController();
    if (incomingSource.isCC)
      incomingSource.ccNumber = message.getControllerNumber();
    else
      incomingSource.noteNumber = message.getNoteNumber();

    const juce::ScopedLock sl(mappingLock);

    // 2. Learning?
    if (learnModeActive && targetParamID.isNotEmpty()) {
      activeMappings[incomingSource] = targetParamID;
      DBG("Mapped Ch" << incomingSource.channel << " CC"
                      << incomingSource.ccNumber << " to " << targetParamID);

      // Clear target to confirm learned
      // targetParamID.clear(); // User might want to re-learn? Usually
      // auto-clear is better feedback.
      targetParamID.clear();
      triggerAsyncUpdate(); // Signal completion?
    }
    // 3. Performing?
    else {
      auto it = activeMappings.find(incomingSource);
      if (it != activeMappings.end()) {
        juce::String mappedParamID = it->second;
        float normalizedValue = 0.0f;
        if (incomingSource.isCC)
          normalizedValue = message.getControllerValue() / 127.0f;
        else
          normalizedValue = message.getVelocity() / 127.0f;

        // Bridge to Main Thread if needed, or call directly if thread-safe
        // setParameterValueCallback is likely sticking to MessageManager?
        // No, this is MIDI thread. We should use AsyncUpdater or atomic/FIFO if
        // heavy. For simple UI sliders, callAsync is safe enough for low
        // traffic. Or if MainComponent's bridge just updates a
        // ValueTree/atomic, it's fine. We'll use callAsync for now to ensure
        // thread safety with GUI.
        // Optimizing with Lock-Free FIFO (User Request)
        // Avoid callAsync allocations on MIDI thread
        int start1, size1, start2, size2;
        fifo.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 > 0) {
          mappedParamID.copyToUTF8(updateBuffer[start1].paramID, 64);
          updateBuffer[start1].paramID[63] = 0; // Null terminate safety
          updateBuffer[start1].value = normalizedValue;
        } else if (size2 > 0) {
          mappedParamID.copyToUTF8(updateBuffer[start2].paramID, 64);
          updateBuffer[start2].paramID[63] = 0;
          updateBuffer[start2].value = normalizedValue;
        }
        fifo.finishedWrite(1);
        triggerAsyncUpdate();
      }
    }
  }

  // --- Persistence ---
  void saveMappingsToJSON(juce::DynamicObject *root) {
    const juce::ScopedLock sl(mappingLock);
    juce::Array<juce::var> mappingArray;

    for (auto const &[source, paramID] : activeMappings) {
      juce::DynamicObject *entry = new juce::DynamicObject();
      entry->setProperty("paramID", paramID);
      entry->setProperty("channel", source.channel);
      entry->setProperty("isCC", source.isCC);
      if (source.isCC)
        entry->setProperty("cc", source.ccNumber);
      else
        entry->setProperty("note", source.noteNumber);
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
      auto *entry = entryVar.getDynamicObject();
      if (!entry)
        continue;

      MidiSource source;
      source.channel = (int)entry->getProperty("channel");
      source.isCC = (bool)entry->getProperty("isCC");
      if (source.isCC)
        source.ccNumber = (int)entry->getProperty("cc");
      else
        source.noteNumber = (int)entry->getProperty("note");

      juce::String pid = entry->getProperty("paramID").toString();
      if (pid.isNotEmpty())
        activeMappings[source] = pid;
    }
  }

  void clearMappings() {
    const juce::ScopedLock sl(mappingLock);
    activeMappings.clear();
  }

private:
  bool learnModeActive = false;
  juce::String targetParamID = "";
  std::map<MidiSource, juce::String> activeMappings;
  juce::CriticalSection mappingLock;

  // Lock-Free Queue Members
  juce::AbstractFifo fifo;
  std::array<MappingUpdate, 1024> updateBuffer;

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
      setParameterValueCallback(juce::String(update.paramID), update.value);
    }
  }
};
