#pragma once
#include "../Audio/OscTypes.h"
#include "../Network/OscManager.h"
#include <juce_core/juce_core.h>
#include <juce_osc/juce_osc.h>
#include <vector>

class NetworkWorker : public juce::Thread {
public:
  NetworkWorker(OscAirlock &outRef, OscAirlock &inRef, OscManager &oscRef)
      : juce::Thread("OSC_Worker"), outputAirlock(outRef), inputAirlock(inRef),
        osc(oscRef) {
    // Initialize default cache
    rebuildCache();
  }

  ~NetworkWorker() override {
    signalThreadShouldExit();
    workSignal.signal();
    stopThread(2000);
  }

  // Called by UI Thread (ConfigPanel)
  void setSchema(const OscNamingSchema &newSchema) {
    // Thread-safe swap: Lock, Copy, Rebuild
    const juce::ScopedWriteLock sl(schemaLock);
    activeSchema = newSchema;
    rebuildCache(); // Rebuilds the fast vector of OSCAddressPatterns
  }

  void pushEvent(const BridgeEvent &e) { outputAirlock.push(e); }

  // Bundle fragmentation limit — prevents oversized UDP packets
  static constexpr int MAX_ITEMS_PER_BUNDLE = 32;

  std::atomic<bool> hasSentPacket{false};
  std::atomic<uint32_t> pulseCount{0}; // Counts PACKETS (activity)
  std::atomic<uint32_t> heartbeat{0};  // Counts LOOPS (thread health)

  // Wake up signal for the thread loop
  juce::WaitableEvent workSignal;

  uint32_t getPulseCount() const { return pulseCount.load(); }
  uint32_t getHeartbeat() const { return heartbeat.load(); }

  void run() override;

  // MANUAL NETWORK POLL (For Eco/Single-Thread Mode)
  // Returns true if any work was done
  bool runSingleCycle();

private:
  OscAirlock &outputAirlock;
  OscAirlock &inputAirlock;
  OscManager &osc;

  // The Schema and its protection
  juce::ReadWriteLock schemaLock;
  OscNamingSchema activeSchema;

  // FAST CACHE: Vector of pre-validated OSC Address Patterns
  // Indexed by Channel (0-16)
  std::vector<juce::OSCAddressPattern> noteAddrCache;
  std::vector<juce::OSCAddressPattern> noteOffAddrCache;
  std::vector<juce::OSCAddressPattern> ccAddrCache;
  std::vector<juce::OSCAddressPattern> pitchAddrCache;
  std::vector<juce::OSCAddressPattern> aftertouchAddrCache;
  std::vector<juce::OSCAddressPattern> susAddrCache;
  std::vector<juce::OSCAddressPattern> programChangeAddrCache;
  std::vector<juce::OSCAddressPattern> polyAftertouchAddrCache;

  juce::OSCAddressPattern playAddrCache{"/play"};
  juce::OSCAddressPattern stopAddrCache{"/stop"};

  // Helper to regenerate the vectors
  void rebuildCache() {
    noteAddrCache.clear();
    noteOffAddrCache.clear();
    ccAddrCache.clear();
    pitchAddrCache.clear();
    aftertouchAddrCache.clear();
    susAddrCache.clear();
    programChangeAddrCache.clear();
    polyAftertouchAddrCache.clear();

    // Generate addresses for channels 0-16
    for (int i = 0; i <= 16; ++i) {
      noteAddrCache.emplace_back(activeSchema.getAddress(
          activeSchema.outNotePrefix, i, activeSchema.outNoteSuffix));
      noteOffAddrCache.emplace_back(activeSchema.getAddress(
          activeSchema.outNotePrefix, i, activeSchema.outNoteOff));
      ccAddrCache.emplace_back(activeSchema.getAddress(
          activeSchema.outNotePrefix, i, activeSchema.outCC));
      pitchAddrCache.emplace_back(activeSchema.getAddress(
          activeSchema.outNotePrefix, i, activeSchema.outPitch));
      aftertouchAddrCache.emplace_back(activeSchema.getAddress(
          activeSchema.outNotePrefix, i, activeSchema.outPressure));
      susAddrCache.emplace_back(activeSchema.getAddress(
          activeSchema.outNotePrefix, i, activeSchema.outSus));
      programChangeAddrCache.emplace_back(activeSchema.getAddress(
          activeSchema.outNotePrefix, i, activeSchema.outProgramChange));
      polyAftertouchAddrCache.emplace_back(activeSchema.getAddress(
          activeSchema.outNotePrefix, i, activeSchema.outPolyAftertouch));
    }

    playAddrCache = juce::OSCAddressPattern(activeSchema.playAddr);
    stopAddrCache = juce::OSCAddressPattern(activeSchema.stopAddr);
  }

  juce::OSCMessage bridgeEventToOsc(const BridgeEvent &e);
};