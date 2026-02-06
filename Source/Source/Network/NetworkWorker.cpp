/*
  ==============================================================================
    Source/Network/NetworkWorker.cpp
    Role: NetworkWorker Implementation
  ==============================================================================
*/

#include "../Network/NetworkWorker.h"
#include "../Core/PlatformGuard.h"

void NetworkWorker::run() {
  setPriority(juce::Thread::Priority::high);

  int idleCounter = 0;

  while (!threadShouldExit()) {
    // 1. Alive Signal
    heartbeat.fetch_add(1, std::memory_order_relaxed);

    // 2. Process
    bool busy = runSingleCycle();

    // 3. Adaptive Sleep (CPU Saver)
    if (busy) {
      idleCounter = 0;
      juce::Thread::yield(); // Yield to allow other threads to push data
    } else {
      idleCounter++;
      // If we've been idle for a while, sleep longer (Eco Mode)
      if (idleCounter > 10) {
        workSignal.wait(5); // 5ms = 200Hz (Plenty for control data)
      } else {
        workSignal.wait(1); // 1ms burst mode
      }
    }
  }
}

bool NetworkWorker::runSingleCycle() {
  // --- DRAIN UNDER PRESSURE ---
  // When queue is > 95% full, drain with larger batches instead of clearing
  // so OSC keeps sending and recovers without dropping everything.
  int batchSize = 32;
  if (outputAirlock.getPressure() > 0.95f)
    batchSize = 256;

  if (outputAirlock.getNumReady() > 0) {
    juce::OSCBundle bundle;
    int count = 0;
    size_t estimatedSize = 0;
    const size_t MTU_SAFE_LIMIT = 1024;

    const juce::ScopedReadLock sl(schemaLock);

    outputAirlock.processBatch(
        [this, &bundle, &count, &estimatedSize, MTU_SAFE_LIMIT](const BridgeEvent &e) {
          if (e.type == EventType::None)
            return;
          if (e.type != EventType::VisualParam &&
              (e.channel < 1 || e.channel > 16))
            return;

          juce::OSCMessage m = bridgeEventToOsc(e);
          juce::String addr = m.getAddressPattern().toString();
          if (addr.isEmpty())
            return;

          size_t msgSize = addr.length() + 16;
          if (estimatedSize + msgSize > MTU_SAFE_LIMIT)
            return;

          bundle.addElement(m);
          estimatedSize += msgSize;
          count++;
        },
        batchSize);

    if (count > 0) {
      try {
        osc.sendBundle(bundle);
        hasSentPacket.store(true, std::memory_order_relaxed);
        pulseCount.fetch_add(1, std::memory_order_relaxed);
        return true;
      } catch (...) {
        // Catch socket errors to prevent thread death
      }
    }
  }
  return false; // Idle
}

juce::OSCMessage NetworkWorker::bridgeEventToOsc(const BridgeEvent &e) {
  // 1. Global/Transport
  if (e.type == EventType::Transport) {
    return juce::OSCMessage(e.value > 0.5f ? playAddrCache : stopAddrCache,
                            1.0f);
  }

  // 2. Bound Check
  if (e.channel < 1 || e.channel > 16)
    return juce::OSCMessage(juce::OSCAddressPattern("/null"));

  // 2b. VisualParam (LFO, playhead, etc.) - from Engine via OscAirlock
  if (e.type == EventType::VisualParam) {
    return juce::OSCMessage(
        "/visual/" + juce::String((int)e.noteOrCC), (float)e.value);
  }

  // 3. Lookup from Cache
  switch (e.type) {
  case EventType::NoteOn:
    return juce::OSCMessage(noteAddrCache[e.channel], (int)e.noteOrCC,
                            (float)e.value);
  case EventType::NoteOff:
    return juce::OSCMessage(noteOffAddrCache[e.channel], (int)e.noteOrCC);
  case EventType::ControlChange:
    // Special Case: Sustain (CC 64)
    if (e.noteOrCC == 64) {
      return juce::OSCMessage(susAddrCache[e.channel], (float)e.value);
    }
    return juce::OSCMessage(ccAddrCache[e.channel], (int)e.noteOrCC,
                            (float)e.value);
  case EventType::PitchBend:
    return juce::OSCMessage(pitchAddrCache[e.channel], (float)e.value);
  case EventType::Aftertouch:
    return juce::OSCMessage(aftertouchAddrCache[e.channel], (float)e.value);
  case EventType::PolyAftertouch:
    return juce::OSCMessage(polyAftertouchAddrCache[e.channel],
                            (int)e.noteOrCC, (float)e.value);
  case EventType::ProgramChange:
    return juce::OSCMessage(programChangeAddrCache[e.channel], (int)e.noteOrCC);
  default:
    return juce::OSCMessage(juce::OSCAddressPattern("/null"));
  }
}