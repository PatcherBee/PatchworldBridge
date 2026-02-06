#pragma once
#include "../Audio/OscTypes.h"
#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_osc/juce_osc.h>
#include <memory>
#include <unordered_map>
#include <vector>

// --- 3. Project Headers ---
#include "../Audio/LockFreeRingBuffers.h"
#include "../Audio/NoteTracker.h"
#include "../Core/BridgeSettings.h"
#include "../Network/OscAirlock.h"
#include "../Core/TimerHub.h"
#include "../Network/OscLookup.h"
#include "../Network/OscSchemaSwapper.h"
#include "../Services/DeferredDeleter.h"

// Forward declarations
class MixerPanel;

class OscManager : public juce::OSCReceiver,
                   public juce::OSCReceiver::Listener<
                       juce::OSCReceiver::MessageLoopCallback> {
public:
  OscManager(BridgeSettings &s, EngineShadowState &ess);
  ~OscManager();

  // Connectivity
  bool connect(const juce::String &targetIp, int portOut, int portIn,
               bool useIPv6 = false);
  void disconnect();
  void setZeroConfig(bool enable);
  bool isConnected() const { return isConnectedFlag; }
  void enableBroadcasting() { isBroadcasting.store(true); }
  bool connectMulticast(int portOut);

  // Sending
  void sendBundle(const juce::OSCBundle &b);
  void sendNoteOn(int ch, int note, float value);
  void sendNoteOff(int ch, int note);
  void sendCC(int ch, int cc, float value);
  void sendPitch(int ch, float value);
  void sendAftertouch(int ch, int note, float value);
  void sendProgramChange(int ch, int program);
  void sendPolyAftertouch(int ch, int note, float value);
  void sendBpm(double bpm);
  void sendFloat(const juce::String &address, float value);
  void sendControlChange(int chan, int cc, int val); // For UI overrides
  void sendMidiAsOsc(const juce::MidiMessage &m, int overrideChannel = -1,
                     bool splitMode = false);

  // Configuration
  void updateSchema(const OscNamingSchema &newSchema);
  void setInputAirlock(OscAirlock *lock) { inputAirlock = lock; }
  void registerCustomMixerAddresses(MixerPanel *mixer);
  void setScalingMode(bool useInt) { useIntegerScaling = useInt; }
  void setDeleter(DeferredDeleter *d) { deferredDeleter = d; }

  // Additional Sending
  void sendHandshake(juce::String version, int numChannels);
  void sendPanicOsc();
  void sendPing(double timestamp);
  void recordSentMessage(const juce::String &address, float value);

  // Callbacks
  std::function<void(juce::String, bool)> onLog;
  std::function<void(int)> onLatencyMeasured;
  std::function<void(int, int, double)> scheduleOffCallback;

  /** Optional handler for any OSC message that does not match the bridge schema
   *  (e.g. addresses with strings, custom paths). For future/other uses beyond
   *  Patchworld; Patchworld currently uses basic OSC and may support string
   *  addresses etc. later. Called on the OSC receiver thread. */
  std::function<void(const juce::OSCMessage &)> onUnknownOscMessage;

  // Receiver
  void oscMessageReceived(const juce::OSCMessage &message) override;
  void oscBundleReceived(const juce::OSCBundle &bundle) override;

private:
  void tickZeroConfig();
  BridgeSettings &settings;
  EngineShadowState &engineState;

  juce::OSCSender sender;
  bool isConnectedFlag = false;
  std::atomic<bool> isBroadcasting{false};
  std::atomic<int> localPort{0};
  std::atomic<int> sequenceCounter{0};
  std::atomic<uint32_t> packetCount{0};

  OscAirlock *inputAirlock = nullptr;
  LogBuffer *logBuffer = nullptr;
  DeferredDeleter *deferredDeleter = nullptr;

  OscNamingSchema schema;
  bool isOscConnected = false;

  // High Performance Engine
  OscSchemaSwapper schemaSwapper;
  std::shared_ptr<OscLookup::RouteMap> currentRoutingTable;
  juce::CriticalSection oscLock;
  juce::CriticalSection gatekeeperLock;
  NoteTracker noteTracker;
  std::vector<juce::String> noteAddrCache;

  // OPTIMIZATION: Deduplication cache to prevent flooding MIDI/OSC
  // [Channel][CC] = Last Value
  float lastInboundCC[17][128];
  float lastOutboundCC[17][128];

  // Helper to reset cache on connect/disconnect
  void resetTrafficCache() {
    for (int c = 0; c < 17; ++c) {
      for (int i = 0; i < 128; ++i) {
        lastInboundCC[c][i] = -1.0f;
        lastOutboundCC[c][i] = -1.0f;
      }
    }
  }

  std::unordered_map<uint64_t, double> lastMessageTime;
  double lastLogTime = 0.0;
  const juce::String instanceId;
  bool useIntegerScaling = false;

  // Map Channel+Note -> Pre-calculated Hash
  std::unordered_map<int, uint64_t> outputHashCache;

  // DYNAMIC OVERRIDES
  std::unordered_map<int, juce::String> channelOverrides;
  std::unordered_map<uint64_t, int> customLookup; // Hash -> Channel

  std::atomic<bool> zeroConfigEnabled{false};
  juce::OSCSender broadcastSender;

  void processBundleRecursive(const juce::OSCBundle &bundle);
  void checkConnectionHealth();

  std::string hubId;
};