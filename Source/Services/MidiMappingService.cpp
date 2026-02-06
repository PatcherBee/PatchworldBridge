#include "../Services/MidiMappingService.h"
#include "../Core/Common.h"

// ==============================================================================
// MidiMappingService Implementation
// ==============================================================================

MidiMappingService::MidiMappingService() : fifo(1024) {}

void MidiMappingService::rebuildFastLookup() {
  fastLookup.clear();
  for (size_t i = 0; i < mappings.size(); ++i) {
    auto &m = mappings[i];
    uint32_t key = (m.source.channel << 16) | (m.source.isCC ? 0x8000 : 0) |
                   (m.source.isCC ? m.source.ccNumber : m.source.noteNumber);
    fastLookup[key].push_back((int)i);
  }
}

juce::String MidiMappingService::getHardwareNameForCC(int channel, int cc) {
  juce::ignoreUnused(channel);
  if (cc == 1)
    return "Mod Wheel";
  if (cc == 7)
    return "Volume";
  if (cc == 10)
    return "Pan";
  if (cc == 11)
    return "Expression";
  if (cc == 64)
    return "Sustain";

  if (cc >= 22 && cc <= 29)
    return "Akai Knob " + juce::String(cc - 21);
  if (cc >= 70 && cc <= 77)
    return "MPK Mini K" + juce::String(cc - 69);
  if (cc >= 48 && cc <= 56)
    return "APC Fader " + juce::String(cc - 47);

  return "CC " + juce::String(cc);
}

float MidiMappingService::applyCurve(float input, MappingEntry::Curve curve) {
  // Clamp input to valid range
  input = juce::jlimit(0.0f, 1.0f, input);

  switch (curve) {
  case MappingEntry::Linear:
    return input;
  case MappingEntry::Log:
    // Logarithmic: fast start, slow end (like audio faders)
    // log10(1 + 9x) / log10(10) maps 0→0, 1→1 with true log curve
    return std::log10(1.0f + 9.0f * input);
  case MappingEntry::Exp:
    // Exponential: slow start, fast end (like pitch bend)
    return input * input;
  case MappingEntry::S_Curve:
    // Smooth S-curve (sine-based for natural feel)
    return 0.5f - 0.5f * std::cos(input * juce::MathConstants<float>::pi);
  default:
    return input;
  }
}

uint32_t MidiMappingService::getLastLearnTime(const juce::String &paramID) {
  const juce::ScopedLock sl(stateLock);
  return lastMappingTime.count(paramID) ? lastMappingTime[paramID] : 0;
}

MappingEntry *MidiMappingService::getEntryAtRow(int row) {
  const juce::ScopedReadLock sl(mappingLock);
  if (row < 0 || row >= (int)mappings.size())
    return nullptr;
  return &mappings[row];
}

float MidiMappingService::processValue(float input0to1, const MappingEntry &e) {
  float val = e.inverted ? (1.0f - input0to1) : input0to1;
  val = applyCurve(val, e.curve);
  return e.target.minRange + (val * (e.target.maxRange - e.target.minRange));
}

void MidiMappingService::setLearnModeActive(bool active) {
  const juce::ScopedLock sl(stateLock);
  state = active ? LearnState::LearnPending : LearnState::Normal;
  if (!active)
    pendingLearnParams.clear();
  if (!active)
    publishChanges(120.0);
  triggerAsyncUpdate();
}

void MidiMappingService::clearLearnQueue() {
  const juce::ScopedLock sl(stateLock);
  pendingLearnParams.clear();
  triggerAsyncUpdate();
}

void MidiMappingService::resetAllHookStates() {
  const juce::ScopedReadLock sl(mappingLock);
  for (const auto &m : mappings) {
    m.isHooked = false;
  }
}

bool MidiMappingService::isLearnModeActive() const {
  return state != LearnState::Normal;
}

void MidiMappingService::setSelectedParameterForLearning(
    const juce::String &paramID) {
  const juce::ScopedLock sl(stateLock);
  pendingLearnParams.clear();
  pendingLearnParams.push_back(paramID);
  state = LearnState::AwaitingMIDI;
  lastLearnValue = -1;
  hasWiggled = false;
  if (onMidiLogCallback)
    onMidiLogCallback("! Queued for MIDI Learn: " + paramID);
}

juce::String MidiMappingService::getSelectedParameter() const {
  const juce::ScopedLock sl(stateLock);
  return pendingLearnParams.empty() ? "" : pendingLearnParams.front();
}

std::vector<juce::String> MidiMappingService::getLearnQueue() const {
  const juce::ScopedLock sl(stateLock);
  return pendingLearnParams;
}

bool MidiMappingService::isMessageMapped(const juce::MidiMessage &message) {
  if (!message.isController() && !message.isNoteOn())
    return false;
  uint32_t key = (message.getChannel() << 16) |
                 (message.isController() ? 0x8000 : 0) |
                 (message.isController() ? message.getControllerNumber()
                                         : message.getNoteNumber());
  const juce::ScopedReadLock sl(mappingLock);
  return fastLookup.find(key) != fastLookup.end();
}

bool MidiMappingService::isParameterMapped(const juce::String &paramID) {
  const juce::ScopedReadLock sl(mappingLock);
  for (const auto &m : mappings) {
    if (m.target.paramID == paramID)
      return true;
  }
  return false;
}

int MidiMappingService::getCCForParam(const juce::String &paramID) const {
  const juce::ScopedReadLock sl(mappingLock);
  for (const auto &m : mappings) {
    if (m.target.paramID == paramID && m.source.isCC)
      return m.source.ccNumber;
  }
  return -1;
}

std::vector<juce::String> MidiMappingService::getActiveMappingList() {
  const juce::ScopedReadLock sl(mappingLock);
  std::vector<juce::String> list;
  for (const auto &m : mappings) {
    juce::String src = m.source.isCC
                           ? "CC " + juce::String(m.source.ccNumber)
                           : "Note " + juce::String(m.source.noteNumber);
    juce::String ch = " (Ch " + juce::String(m.source.channel) + ")";
    list.push_back(src + ch + " -> " + m.target.paramID);
  }
  return list;
}

juce::StringArray MidiMappingService::getAllMappableParameters() const {
  juce::StringArray params;
  params.add("Transport_BPM");
  params.add("Transport_Play");
  params.add("Transport_Stop");
  for (int i = 1; i <= 16; ++i) {
    juce::String prefix = "Mixer_" + juce::String(i);
    params.add(prefix + "_Vol");
    params.add(prefix + "_On");
    params.add(prefix + "_Solo");
    params.add(prefix + "_Mute");
    params.add(prefix + "_Pan");
  }
  params.add("Main_Pitch");
  params.add("Main_Mod");
  params.add("Sequencer_Swing");
  params.add("Sequencer_Rate");
  for (int i = 1; i <= 3; ++i) {
    params.add("Macro_Fader_" + juce::String(i));
    params.add("Macro_Btn_" + juce::String(i));
  }
  for (int i = 0; i < 32; ++i) {
    params.add("Vis_" + juce::String(i));
  }
  params.add("LFO_Rate");
  params.add("LFO_Depth");
  params.add("LFO_Shape");
  params.add("LFO_Attack");
  params.add("LFO_Decay");
  params.add("LFO_Sustain");
  params.add("LFO_Release");
  params.add("Arp_Rate");
  params.add("Arp_Vel");
  params.add("Arp_Gate");
  params.add("Arp_Octave");
  return params;
}

void MidiMappingService::setCurveForParam(const juce::String &paramID,
                                          MappingEntry::Curve c) {
  const juce::ScopedWriteLock sl(mappingLock);
  for (auto &m : mappings) {
    if (m.target.paramID == paramID)
      m.curve = c;
  }
}

void MidiMappingService::setInvertedForParam(const juce::String &paramID,
                                             bool inverted) {
  const juce::ScopedWriteLock sl(mappingLock);
  for (auto &m : mappings) {
    if (m.target.paramID == paramID)
      m.inverted = inverted;
  }
}

void MidiMappingService::setLayerForParam(const juce::String &paramID,
                                          int layer) {
  const juce::ScopedWriteLock sl(mappingLock);
  for (auto &m : mappings) {
    if (m.target.paramID == paramID)
      m.layer = layer;
  }
}

void MidiMappingService::publishChanges(double currentBpm) {
  const juce::ScopedReadLock sl(mappingLock);
  publishChangesInternal(currentBpm);
}

void MidiMappingService::publishChangesInternal(double currentBpm) {
  auto next = std::make_shared<MappingSnapshot>();
  next->captureBpm = currentBpm;
  next->generation = lastGeneration++;
  // Assumes lock is already held by caller!
  for (const auto &m : mappings) {
    uint32_t key = (m.source.channel << 16) | (m.source.isCC ? 0x8000 : 0) |
                   (m.source.isCC ? m.source.ccNumber : m.source.noteNumber);
    next->routes[key] = m.target.paramID;
  }
  std::atomic_store(&activeSnapshot, next);
}

void MidiMappingService::setParameterValue(const juce::String &paramID,
                                           float value) {
  {
    const juce::ScopedLock sl(stateLock);
    lastKnownSoftwareValues[paramID] = value;
    lastUISetTimeMs[paramID] = juce::Time::getMillisecondCounter();
  }
  if (setParameterValueCallback) {
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) {
      setParameterValueCallback(paramID, value);
    } else {
      auto cb = setParameterValueCallback;
      juce::String id = paramID;
      juce::MessageManager::callAsync([cb, id, value] {
        if (cb)
          cb(id, value);
      });
    }
  }
}

bool MidiMappingService::handleLearnInput(const juce::MidiMessage &message) {
  const juce::ScopedLock sl(stateLock);
  if (state == LearnState::AwaitingMIDI && !pendingLearnParams.empty()) {
    if (message.isController() || message.isNoteOn()) {
      stateLock.exit();
      handleIncomingMidiMessage(nullptr, message);
      stateLock.enter();
      return true;
    }
  }
  return false;
}

void MidiMappingService::handleIncomingMidiMessage(
    juce::MidiInput *source, const juce::MidiMessage &message) {
  juce::ignoreUnused(source);
  if (!message.isController() && !message.isNoteOn())
    return;

  if (state != LearnState::Normal || onMidiLogCallback) {
    juce::String activity;
    if (message.isController())
      activity = "MIDI IN: Ch " + juce::String(message.getChannel()) + " CC " +
                 juce::String(message.getControllerNumber()) +
                 " [Val: " + juce::String(message.getControllerValue()) + "]";
    else if (message.isNoteOn())
      activity = "MIDI IN: Ch " + juce::String(message.getChannel()) +
                 " Note " + juce::String(message.getNoteNumber());

    if (activity.isNotEmpty() && onMidiLogCallback) {
      auto cb = onMidiLogCallback;
      juce::MessageManager::callAsync([cb, activity] {
        if (cb)
          cb(activity);
      });
    }
  }

  if (message.isController() && message.getControllerNumber() == modifierCC) {
    bool newShift = (message.getControllerValue() >= 64);
    if (isShiftHeld != newShift) {
      isShiftHeld = newShift;
    }
    return;
  }

  uint32_t midiKey = (message.getChannel() << 16) |
                     (message.isController() ? 0x8000 : 0) |
                     (message.isController() ? message.getControllerNumber()
                                             : message.getNoteNumber());

  LearnState currentState;
  juce::String currentTargetID;
  {
    const juce::ScopedLock sl(stateLock);
    currentState = state;
    if (!pendingLearnParams.empty())
      currentTargetID = pendingLearnParams.front();
  }

  if (currentState == LearnState::AwaitingMIDI &&
      currentTargetID.isNotEmpty()) {
    if (message.isController()) {
      int val = message.getControllerValue();
      if (lastLearnValue == -1) {
        lastLearnValue = val;
        hasWiggled = true; // Accept first CC immediately (reduced friction)
      } else if (!hasWiggled && std::abs(val - lastLearnValue) < 2) {
        return; // Minimal wiggle (2) to confirm user intent
      } else {
        hasWiggled = true;
      }
    }
    {
      removeMappingForParam(currentTargetID);
      const juce::ScopedWriteLock ml(mappingLock);
      MappingEntry newEntry;
      newEntry.source = {message.getChannel(),
                         message.isController() ? message.getControllerNumber()
                                                : -1,
                         message.isNoteOn() ? message.getNoteNumber() : -1,
                         message.isController()};
      newEntry.target = {currentTargetID, 0.0f, 1.0f};

      if (newEntry.source.isCC)
        newEntry.controllerName = getHardwareNameForCC(
            newEntry.source.channel, newEntry.source.ccNumber);
      else
        newEntry.controllerName =
            "Note " + juce::String(newEntry.source.noteNumber);

      mappings.push_back(newEntry);
      rebuildFastLookup();

      const juce::ScopedLock sl(stateLock);
      lastMappingTime[currentTargetID] = juce::Time::getMillisecondCounter();
      isDirty.store(true);
      lastMappingTime[currentTargetID] = juce::Time::getMillisecondCounter();
      isDirty.store(true);
      publishChangesInternal(120.0);
      triggerAsyncUpdate();
      triggerAsyncUpdate();
    }
    {
      const juce::ScopedLock sl(stateLock);
      if (!pendingLearnParams.empty())
        pendingLearnParams.erase(pendingLearnParams.begin());
      if (pendingLearnParams.empty())
        state = LearnState::Normal;
    }
    return;
  }

  const juce::ScopedReadLock ml(mappingLock);
  auto it = fastLookup.find(midiKey);
  if (it != fastLookup.end()) {
    uint32_t nowMs = juce::Time::getMillisecondCounter();
    constexpr uint32_t kUIFeedbackSuppressMs = 200;
    float rawVal = message.isController()
                       ? (message.getControllerValue() / 127.0f)
                       : (message.getVelocity() / 127.0f);
    for (int idx : it->second) {
      if (!juce::isPositiveAndBelow(idx, (int)mappings.size()))
        continue;
      auto &entry = mappings[idx];
      // Skip applying MIDI if this param was just set by UI (mouse drag) to
      // prevent jump
      {
        const juce::ScopedLock sl(stateLock);
        auto uiIt = lastUISetTimeMs.find(entry.target.paramID);
        if (uiIt != lastUISetTimeMs.end() &&
            (nowMs - uiIt->second) < kUIFeedbackSuppressMs)
          continue;
      }
      float mappedRawVal = rawVal;
      if (entry.minVal != 0.0f || entry.maxVal != 1.0f) {
        mappedRawVal = entry.minVal + (rawVal * (entry.maxVal - entry.minVal));
      }
      // Pickup Mode: prevent value jumps when MIDI controller value differs
      // from parameter
      if (pickupModeEnabled && !entry.isHooked) {
        float currentVal = 0.0f;
        if (getParameterValue)
          currentVal = getParameterValue(entry.target.paramID);
        else {
          const juce::ScopedLock sl(stateLock);
          currentVal = lastKnownSoftwareValues[entry.target.paramID];
        }

        // Store last MIDI value for pickup detection
        if (entry.lastMidiValue < 0.0f) {
          entry.lastMidiValue = rawVal;
        }

        // Hook when MIDI value crosses current parameter value (pickup)
        bool crossedValue =
            (entry.lastMidiValue <= currentVal && rawVal >= currentVal) ||
            (entry.lastMidiValue >= currentVal && rawVal <= currentVal);

        if (crossedValue || std::abs(rawVal - currentVal) < 0.05f) {
          entry.isHooked = true;
          entry.lastMidiValue = rawVal;
        } else {
          // Update last MIDI value and notify UI of hardware position
          entry.lastMidiValue = rawVal;
          if (onHardwarePositionChanged) {
            auto cb = onHardwarePositionChanged;
            juce::String id = entry.target.paramID;
            juce::MessageManager::callAsync([cb, id, rawVal] {
              if (cb)
                cb(id, rawVal);
            });
          }
          continue;
        }
      }
      float finalVal = processValue(rawVal, entry);
      int s1, n1, s2, n2;
      fifo.prepareToWrite(1, s1, n1, s2, n2);
      if (n1 > 0) {
        entry.target.paramID.copyToUTF8(updateBuffer[s1].paramID, 64);
        updateBuffer[s1].value = finalVal;
        fifo.finishedWrite(1);
        triggerAsyncUpdate();
      }
    }
  }
}

void MidiMappingService::handleAsyncUpdate() {
  if (isDirty.exchange(false))
    saveMappingsToInternalFile();
  if (onMappingChanged)
    onMappingChanged();
  int start1, size1, start2, size2;
  fifo.prepareToRead(fifo.getNumReady(), start1, size1, start2, size2);
  if (size1 > 0)
    processQueueBlock(start1, size1);
  if (size2 > 0)
    processQueueBlock(start2, size2);
  fifo.finishedRead(size1 + size2);
}

void MidiMappingService::processQueueBlock(int start, int size) {
  if (!setParameterValueCallback)
    return;
  for (int i = 0; i < size; ++i) {
    auto &update = updateBuffer[start + i];
    setParameterValueCallback(juce::String(update.paramID), update.value);
  }
}

void MidiMappingService::removeMappingForParam(const juce::String &paramID) {
  const juce::ScopedWriteLock ml(mappingLock);
  mappings.erase(std::remove_if(mappings.begin(), mappings.end(),
                                [&](const MappingEntry &e) {
                                  return e.target.paramID == paramID;
                                }),
                 mappings.end());
  rebuildFastLookup();
}

void MidiMappingService::resetMappings() {
  const juce::ScopedWriteLock sl(mappingLock);
  mappings.clear();
  fastLookup.clear();
  mappings.clear();
  fastLookup.clear();
  publishChangesInternal(120.0);
  triggerAsyncUpdate();
  triggerAsyncUpdate();
}

void MidiMappingService::saveMappingsToJSON(juce::DynamicObject *root) {
  const juce::ScopedReadLock sl(mappingLock);
  juce::Array<juce::var> arr;
  for (const auto &m : mappings) {
    arr.add(juce::var(m.toDynamicObject()));
  }
  root->setProperty("mappings", arr);
}

void MidiMappingService::loadMappingsFromJSON(const juce::var &jsonVar) {
  const juce::ScopedWriteLock sl(mappingLock);
  mappings.clear();
  auto *arr = jsonVar.getArray();
  if (!arr)
    return;

  for (auto &v : *arr) {
    auto *obj = v.getDynamicObject();
    if (!obj)
      continue;
    MappingEntry entry;
    entry.target.paramID = obj->getProperty("param_id").toString();
    entry.target.minRange =
        obj->hasProperty("min") ? (float)obj->getProperty("min") : 0.0f;
    entry.target.maxRange =
        obj->hasProperty("max") ? (float)obj->getProperty("max") : 1.0f;
    if (entry.target.paramID.isEmpty() && obj->hasProperty("parameter_id"))
      entry.target.paramID = obj->getProperty("parameter_id").toString();

    entry.source.channel = (int)obj->getProperty("ch");
    if (entry.source.channel == 0 && obj->hasProperty("channel"))
      entry.source.channel = (int)obj->getProperty("channel");
    juce::String type =
        obj->hasProperty("type") ? obj->getProperty("type").toString() : "";
    if (type.isEmpty() && obj->hasProperty("midi_type"))
      type = obj->getProperty("midi_type").toString();
    entry.source.isCC = (type == "CC");
    int idx = 0;
    if (obj->hasProperty("idx"))
      idx = (int)obj->getProperty("idx");
    else if (obj->hasProperty("index"))
      idx = (int)obj->getProperty("index");
    if (entry.source.isCC)
      entry.source.ccNumber = idx;
    else
      entry.source.noteNumber = idx;

    if (entry.target.paramID.isNotEmpty()) {
      if (obj->hasProperty("curve"))
        entry.curve = (MappingEntry::Curve)(int)obj->getProperty("curve");
      if (obj->hasProperty("inverted"))
        entry.inverted = obj->getProperty("inverted");
      mappings.push_back(entry);
    }
  }
  rebuildFastLookup();
  for (auto &m : mappings)
    m.isHooked = false;
  for (auto &m : mappings)
    m.isHooked = false;
  publishChangesInternal(120.0);
  isDirty.store(false);
  isDirty.store(false);
}

bool MidiMappingService::saveMappingsToFile(const juce::File &f) {
  auto *obj = new juce::DynamicObject();
  saveMappingsToJSON(obj);
  return f.replaceWithText(juce::JSON::toString(juce::var(obj)));
}

bool MidiMappingService::loadMappingsFromFile(const juce::File &f) {
  if (!f.existsAsFile())
    return false;
  auto json = juce::JSON::parse(f);
  if (json.isVoid() || json.isUndefined())
    return false;
  if (auto *obj = json.getDynamicObject()) {
    loadMappingsFromJSON(obj->getProperty("mappings"));
    return true;
  }
  return false;
}

void MidiMappingService::saveMappingsToInternalFile() {
  juce::File profileDir =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("PatchworldBridge")
          .getChildFile("Profiles");
  if (!profileDir.exists())
    profileDir.createDirectory();
  saveMappingsToFile(profileDir.getChildFile("_mappings.json"));
}
