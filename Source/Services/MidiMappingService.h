#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <map>
#include <unordered_map>
#include <vector>

#include "../Audio/OscTypes.h"

// Forward declarations
class BridgeContext;

// --- Helper Structs for Thread-Safe Queue ---
struct MappingUpdate {
  char paramID[64]; // Fixed size for lock-free FIFO
  float value;
};

// PRO: Mapping Snapshot for Read-Copy-Update (Triple-Buffering)
struct MappingSnapshot {
  double captureBpm = 120.0;
  uint64_t generation = 0;
  bool isHighPerformance = true;
  // Key: (channel << 16) | (isCC ? 0x8000 : 0) | (ccOrNote)
  std::map<uint32_t, juce::String> routes;
};

// Start Fresh Structs
struct MidiSource {
  int channel = -1;
  int ccNumber = -1;
  int noteNumber = -1;
  bool isCC = true;

  // Equality for Vector search
  bool operator==(const MidiSource &other) const {
    return channel == other.channel && isCC == other.isCC &&
           ccNumber == other.ccNumber && noteNumber == other.noteNumber;
  }
};

struct MappingTarget {
  juce::String paramID;
  float minRange = 0.0f;
  float maxRange = 1.0f;
};

struct MappingEntry {
  MidiSource source;
  MappingTarget target;

  // Concrete User-Facing details
  juce::String controllerName = "Unnamed controller";

  float minVal = 0.0f;
  float maxVal = 1.0f;
  bool active = true;

  enum Curve { Linear, Log, Exp, S_Curve };
  Curve curve = Linear;
  bool inverted = false;
  int layer = 0; // 0=Default, 1=Shifted

  // JSON Serialization
  juce::DynamicObject *toDynamicObject() const {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("name", controllerName);
    obj->setProperty("param_id", target.paramID);
    obj->setProperty("type", source.isCC ? "CC" : "Note");
    obj->setProperty("ch", source.channel);
    obj->setProperty("idx", source.isCC ? source.ccNumber : source.noteNumber);
    obj->setProperty("min", target.minRange);
    obj->setProperty("max", target.maxRange);
    obj->setProperty("min_map", minVal);
    obj->setProperty("max_map", maxVal);
    obj->setProperty("cc", source.isCC ? source.ccNumber : -1);
    obj->setProperty("curve", (int)curve);
    obj->setProperty("inverted", inverted);
    obj->setProperty("layer", layer);
    return obj;
  }

  // Soft Takeover / Pickup Mode state
  mutable bool isHooked = false;
  mutable float lastMidiValue =
      -1.0f; // Last MIDI value received (for pickup mode)
};

class MidiMappingService : public juce::MidiInputCallback,
                           public juce::AsyncUpdater {
public:
  enum class LearnState { Normal, LearnPending, AwaitingMIDI };

  MidiMappingService();

  // 1. DECLARE LOCKS AND FLAGS FIRST
  juce::ReadWriteLock mappingLock;
  std::atomic<bool> isDirty{false};

  // 2. DATA STRUCTURES
  std::vector<MappingEntry> mappings;
  std::map<uint32_t, std::vector<int>> fastLookup;
  std::map<juce::String, float> lastKnownSoftwareValues;
  std::map<juce::String, uint32_t> lastMappingTime;
  /** When a param was last set by UI (mouse drag); skip MIDI feedback for a
   * short window to prevent jump. */
  std::map<juce::String, uint32_t> lastUISetTimeMs;

  void rebuildFastLookup();

  // 3. THE HARDWARE AUTO-NAMER (Static helper)
  static juce::String getHardwareNameForCC(int channel, int cc);

  // 4. CURVE MATH
  static float applyCurve(float input, MappingEntry::Curve curve);

  uint32_t getLastLearnTime(const juce::String &paramID);

  MappingEntry *getEntryAtRow(int row);

  static float processValue(float input0to1, const MappingEntry &e);

  std::function<void(juce::String, float)> setParameterValueCallback;
  std::function<void(juce::String, float)> onHardwarePositionChanged;
  std::function<void(juce::String)> onMidiLogCallback;
  std::function<void()> onMappingChanged;
  std::function<float(juce::String)> getParameterValue;

  // --- State Control ---
  void setLearnModeActive(bool active);
  void clearLearnQueue();
  void resetAllHookStates();
  bool isLearnModeActive() const;

  // Pickup Mode (prevents value jumps on MIDI controller reconnect/switch)
  void setPickupModeEnabled(bool enabled) { pickupModeEnabled = enabled; }
  bool getPickupModeEnabled() const { return pickupModeEnabled; }

  void setSelectedParameterForLearning(const juce::String &paramID);
  juce::String getSelectedParameter() const;
  std::vector<juce::String> getLearnQueue() const;

  // --- UI/Query Helpers ---
  bool isMessageMapped(const juce::MidiMessage &message);
  bool isParameterMapped(const juce::String &paramID);
  /** Returns the CC number for a param if it is mapped to a CC, otherwise -1.
   */
  int getCCForParam(const juce::String &paramID) const;
  std::vector<juce::String> getActiveMappingList();

  // --- Core Processing ---
  juce::StringArray getAllMappableParameters() const;

  // Advanced Configuration Setters
  void setCurveForParam(const juce::String &paramID, MappingEntry::Curve c);
  void setInvertedForParam(const juce::String &paramID, bool inverted);
  void setLayerForParam(const juce::String &paramID, int layer);

  // The "Published" snapshot for the Audio Thread
  std::shared_ptr<MappingSnapshot> activeSnapshot;

  void publishChanges(double currentBpm);
  // Internal helper to avoid self-deadlock when lock is already held
  void publishChangesInternal(double currentBpm);
  void setParameterValue(const juce::String &paramID, float value);

  bool handleLearnInput(const juce::MidiMessage &message);
  void handleIncomingMidiMessage(juce::MidiInput *source,
                                 const juce::MidiMessage &message) override;

  // --- Data Management ---
  void removeMappingForParam(const juce::String &paramID);
  void resetMappings();
  void saveMappingsToJSON(juce::DynamicObject *root);
  void loadMappingsFromJSON(const juce::var &jsonVar);
  /** Returns true if save succeeded. */
  bool saveMappingsToFile(const juce::File &f);
  /** Returns true if load succeeded (file existed and parsed). */
  bool loadMappingsFromFile(const juce::File &f);
  void saveMappingsToInternalFile();

private:
  // --- LOCKS ---
  juce::CriticalSection stateLock;

  LearnState state = LearnState::Normal;
  std::vector<juce::String> pendingLearnParams;

  juce::AbstractFifo fifo;
  std::array<MappingUpdate, 1024> updateBuffer;

  int modifierCC = 64;
  bool isShiftHeld = false;
  uint64_t lastGeneration = 0;

  // Wiggle Detection state
  int lastLearnValue = -1;
  bool hasWiggled = false;

  // Pickup Mode
  bool pickupModeEnabled = true; // Default enabled to prevent jumps

  void handleAsyncUpdate() override;
  void processQueueBlock(int start, int size);
};
