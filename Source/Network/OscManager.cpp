#include "../Network/OscManager.h"
#include "../Audio/OscTypes.h"
#include "../Core/TimerHub.h"
#include "../Core/BridgeContext.h"
#include "../Core/CommandDispatcher.h"
#include "../UI/Panels/MixerPanel.h"
#include "../UI/Theme.h"
#include <cstring>
#include <memory>
#include <shared_mutex>

namespace {
static uint64_t computeMessageFingerprint(const juce::OSCMessage &m) {
  const char *addr = m.getAddressPattern().toString().toRawUTF8();
  uint64_t hash = FastOsc::hashRaw(addr);
  hash = hash * 31 + (uint64_t)m.size();
  for (int i = 0; i < std::min(m.size(), 4); ++i) {
    const auto &arg = m[i];
    if (arg.isFloat32()) {
      float f = arg.getFloat32();
      uint32_t bits;
      std::memcpy(&bits, &f, sizeof(bits));
      hash = hash * 31 + bits;
    } else if (arg.isInt32()) {
      hash = hash * 31 + (uint64_t)arg.getInt32();
    } else if (arg.isString()) {
      hash = hash * 31 + FastOsc::hashRaw(arg.getString().toRawUTF8());
    }
  }
  auto now = juce::Time::getMillisecondCounterHiRes();
  hash ^= (uint64_t)((uint64_t)(now / 50.0) << 40);
  return hash;
}
} // namespace

OscManager::OscManager(BridgeSettings &s, EngineShadowState &ess)
    : settings(s), engineState(ess) {
  resetTrafficCache();
  OscNamingSchema defaultSchema;
  updateSchema(defaultSchema);
  packetCount.store(0);
  lastLogTime = 0.0;
}

OscManager::~OscManager() { disconnect(); }

bool OscManager::connect(const juce::String &targetIp, int portOut, int portIn,
                         bool useIPv6) {
  const juce::ScopedLock sl(oscLock);
  juce::ignoreUnused(useIPv6);

  // 1. Force Clean Slate
  disconnect();

  // 2. Connect Receiver (Bind to 0.0.0.0 aka Any Interface to ensure we hear
  // replies)
  bool rx = false;
  if (juce::OSCReceiver::connect(portIn)) {
    rx = true;
    localPort.store(portIn);
    juce::OSCReceiver::addListener(
        static_cast<juce::OSCReceiver::Listener<
            juce::OSCReceiver::MessageLoopCallback> *>(this));
  }

  // 3. Connect Sender (Target IP)
  bool tx = sender.connect(targetIp, portOut);

  isOscConnected = rx && tx;
  isConnectedFlag = isOscConnected;

  if (!isOscConnected) {
    // If failed, cleanup immediately so we don't leave half-open sockets
    sender.disconnect();
    juce::OSCReceiver::disconnect();
  }

  return isOscConnected;
}

void OscManager::sendControlChange(int chan, int cc, int val) {
  juce::ignoreUnused(chan);
  if (!isConnectedFlag)
    return;

  // Optional: Only send if the value actually changed (Throttling)
  static int lastVal = -1;
  if (val == lastVal)
    return;
  lastVal = val;

  juce::String addr = schema.ccPrefix + juce::String(cc);
  // Use OSCSender convenience overload to avoid constructor issues
  sender.send(juce::OSCAddressPattern(addr), val);
}

bool OscManager::connectMulticast(int portOut) {
  const juce::ScopedLock sl(oscLock);
  bool connected = sender.connect("255.255.255.255", portOut);
  isOscConnected = connected;
  isConnectedFlag = connected;
  return connected;
}

void OscManager::sendHandshake(juce::String version, int numChannels) {
  const juce::ScopedLock sl(oscLock);
  if (!isConnectedFlag)
    return;

  juce::OSCMessage msg("/test");
  msg.addString(version);
  msg.addInt32(numChannels);
  msg.addInt32(Theme::currentThemeId);
  msg.addString(instanceId); // Loopback shield
  sender.send(msg);
}

void OscManager::sendPanicOsc() {
  const juce::ScopedLock sl(oscLock);
  if (!isConnectedFlag)
    return;

  juce::OSCMessage m("/s");
  m.addInt32(1);
  m.addString(instanceId);
  sender.send(m);
}

void OscManager::disconnect() {
  const juce::ScopedLock sl(oscLock);
  zeroConfigEnabled.store(false);
  if (!hubId.empty()) {
    TimerHub::instance().unsubscribe(hubId);
    hubId.clear();
  }
  broadcastSender.disconnect();
  juce::OSCReceiver::disconnect();
  sender.disconnect();
  isOscConnected = false;
  isConnectedFlag = false;
}

void OscManager::setZeroConfig(bool enable) {
  zeroConfigEnabled.store(enable);
  if (enable) {
    if (hubId.empty()) {
      hubId = "OscManager_zeroConfig_" + juce::Uuid().toDashedString().toStdString();
      TimerHub::instance().subscribe(hubId, [this] { tickZeroConfig(); },
                                     TimerHub::Rate0_33Hz);
    }
  } else {
    if (!hubId.empty()) {
      TimerHub::instance().unsubscribe(hubId);
      hubId.clear();
    }
    broadcastSender.disconnect();
  }
}

void OscManager::updateSchema(const OscNamingSchema &newSchema) {
  auto newMap = OscLookup::createMapFromSchema(newSchema);
  auto newSchemaPtr = std::make_shared<OscNamingSchema>(newSchema);
  std::atomic_store(&currentRoutingTable, newMap);
  schemaSwapper.updateSchema(newSchemaPtr);

  // Pre-calculate addresses for all 16 channels to avoid string mallocs on hot
  // path
  const juce::ScopedLock sl(oscLock);
  schema = newSchema; // Update local copy
  noteAddrCache.clear();
  noteAddrCache.reserve(17);
  noteAddrCache.push_back(""); // 0-th index dummy
  for (int i = 1; i <= 16; ++i) {
    noteAddrCache.push_back(schema.outNotePrefix + juce::String(i) +
                            schema.outNoteSuffix);
  }
}

void OscManager::registerCustomMixerAddresses(MixerPanel *mixer) {
  const juce::ScopedLock sl(oscLock);
  channelOverrides.clear();
  customLookup.clear();

  if (!mixer)
    return;

  for (auto *s : mixer->strips) {
    int ch = s->channelIndex + 1;
    if (s->customOscIn.isNotEmpty()) {
      channelOverrides[ch] = s->customOscIn;
      customLookup[oscHash(s->customOscIn.toRawUTF8())] = ch;
    }
  }
}

void OscManager::sendNoteOn(int ch, int note, float value) {
  if (!isOscConnected)
    return;

  // Address: /ch{X}note
  // OPTIMIZATION: Use pre-calculated cache
  juce::String addr;
  if (ch >= 1 && ch < (int)noteAddrCache.size()) {
    addr = noteAddrCache[ch];
  } else {
    addr = schema.outNotePrefix + juce::String(ch) + schema.outNoteSuffix;
  }

  juce::OSCMessage m(addr);
  m.addInt32(note);

  // Multi-Arg: Add velocity as second argument
  if (useIntegerScaling)
    m.addInt32((int)(value * 127.0f));
  else
    m.addFloat32(value);

  juce::OSCBundle b;
  b.addElement(m);
  sendBundle(b);

  if (onLog) {
    int vel = useIntegerScaling ? (int)(value * 127.0f) : (int)(value * 100.0f);
    auto fn = onLog;
    juce::String a = addr;
    juce::MessageManager::callAsync([fn, a, note, vel] {
      if (fn)
        fn("OSC: " + a + " " + juce::String(note) + " " + juce::String(vel),
           false);
    });
  }
}

void OscManager::sendNoteOff(int ch, int note) {
  const juce::ScopedLock sl(oscLock);
  if (!isOscConnected)
    return;

  auto info = noteTracker.processNoteOff(ch, note, schemaSwapper);
  int mappedPitch = info.first;
  auto localSchemaPtr = info.second;

  if (mappedPitch == -1)
    return;

  if (auto localSchema = info.second) {
    juce::String addr = localSchema->getAddress(localSchema->notePrefix, ch,
                                                localSchema->noteOffSuffix);

    juce::OSCMessage m(addr);
    m.addInt32(note);
    m.addInt32(sequenceCounter++);
    m.addString(instanceId);
    sender.send(m);

    if (onLog) {
      auto fn = onLog;
      juce::String a = addr;
      juce::MessageManager::callAsync([fn, a, note] {
        if (fn)
          fn("OSC: " + a + " " + juce::String(note) + " off", false);
      });
    }
  }
}

void OscManager::sendPing(double timestamp) {
  const juce::ScopedLock sl(oscLock);
  if (!isOscConnected)
    return;

  juce::OSCMessage m("/sys/ping");
  m.addFloat32((float)timestamp);
  m.addString(instanceId);
  sender.send(m);
}

void OscManager::sendCC(int ch, int cc, float value) {
  if (!isOscConnected)
    return;

  // Address: /ch{X}cc
  juce::String addr = schema.outNotePrefix + juce::String(ch) + schema.outCC;

  juce::OSCMessage m(addr);
  m.addInt32(cc);

  // Multi-Arg: Value
  if (useIntegerScaling)
    m.addInt32((int)(value * 127.0f));
  else
    m.addFloat32(value);

  juce::OSCBundle b;
  b.addElement(m);
  sendBundle(b);

  if (onLog) {
    int v = useIntegerScaling ? (int)(value * 127.0f) : (int)(value * 100.0f);
    auto fn = onLog;
    juce::MessageManager::callAsync([fn, ch, cc, v] {
      if (fn)
        fn("OSC: /ch" + juce::String(ch) + "cc " + juce::String(cc) + " " +
               juce::String(v),
           false);
    });
  }
}

void OscManager::sendPitch(int ch, float value) {
  const juce::ScopedLock sl(oscLock);
  if (!isOscConnected)
    return;

  auto info = schemaSwapper.getSchemaForNoteOn();
  auto &localSchema = *info.first;
  juce::String addr = localSchema.getAddress(localSchema.pitchPrefix, ch,
                                             localSchema.pitchSuffix);

  juce::OSCMessage m(addr);
  m.addFloat32(value);
  m.addInt32(sequenceCounter++);
  m.addString(instanceId);
  sender.send(m);
}

void OscManager::sendAftertouch(int ch, int note, float value) {
  juce::ignoreUnused(note);
  const juce::ScopedLock sl(oscLock);
  if (!isOscConnected)
    return;

  auto info = schemaSwapper.getSchemaForNoteOn();
  auto &localSchema = *info.first;
  juce::String addr = localSchema.getAddress(localSchema.notePrefix, ch,
                                             localSchema.aftertouchSuffix);

  juce::OSCMessage m(addr);
  m.addFloat32(value);
  m.addInt32(sequenceCounter++);
  m.addString(instanceId);
  sender.send(m);
}

void OscManager::sendProgramChange(int ch, int program) {
  if (!isOscConnected)
    return;
  juce::String addr = schema.outNotePrefix + juce::String(ch) + schema.outProgramChange;
  juce::OSCMessage m(addr);
  m.addInt32(program);
  sender.send(m);
}

void OscManager::sendPolyAftertouch(int ch, int note, float value) {
  if (!isOscConnected)
    return;
  juce::String addr = schema.outNotePrefix + juce::String(ch) + schema.outPolyAftertouch;
  juce::OSCMessage m(addr);
  m.addInt32(note);
  m.addFloat32(value);
  sender.send(m);
}

void OscManager::sendBpm(double bpm) {
  if (!isOscConnected)
    return;
  juce::String addr = schema.bpmAddr.isEmpty() ? "/clock/bpm" : schema.bpmAddr;
  juce::OSCMessage m(addr);
  m.addFloat32((float)bpm);
  sender.send(m);
}

void OscManager::sendFloat(const juce::String &address, float value) {
  const juce::ScopedLock sl(oscLock);
  if (!isOscConnected)
    return;

  juce::OSCMessage m(address);
  m.addFloat32(value);
  m.addInt32(sequenceCounter++);
  m.addString(instanceId);
  sender.send(m);
}

void OscManager::sendMidiAsOsc(const juce::MidiMessage &m, int overrideChannel,
                               bool splitMode) {
  if (!isOscConnected)
    return;

  // Reentrancy guard -- prevent recursive calls from split logic
  thread_local bool reentrancyGuard = false;
  if (reentrancyGuard)
    return;
  struct GuardScope {
    bool &guard;
    GuardScope(bool &g) : guard(g) { guard = true; }
    ~GuardScope() { guard = false; }
  } scope(reentrancyGuard);

  auto sendTo = [this, &m](int ch) {
    if (ch < 1 || ch > 16)
      ch = 1;

    if (m.isNoteOn())
      sendNoteOn(ch, m.getNoteNumber(), m.getVelocity() / 127.0f);
    else if (m.isNoteOff())
      sendNoteOff(ch, m.getNoteNumber());
    else if (m.isController())
      sendCC(ch, m.getControllerNumber(), m.getControllerValue() / 127.0f);
    else if (m.isPitchWheel())
      sendPitch(ch, (float)(m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isChannelPressure())
      sendAftertouch(ch, 0, m.getChannelPressureValue() / 127.0f);
    else if (m.isProgramChange())
      sendProgramChange(ch, m.getProgramChangeNumber());
    else if (m.isAftertouch())
      sendPolyAftertouch(ch, m.getNoteNumber(), m.getAfterTouchValue() / 127.0f);
  };

  int baseCh = (overrideChannel != -1) ? overrideChannel : m.getChannel();
  if (baseCh <= 0)
    baseCh = 1;

  if (splitMode && baseCh == 1) {
    if (m.isNoteOnOrOff()) {
      int n = m.getNoteNumber();
      // Ch1: notes 0-64, Ch2: notes 65-127
      sendTo(n <= 64 ? 1 : 2);
    } else {
      sendTo(1);
      sendTo(2);
    }
  } else {
    sendTo(baseCh);
  }
}

void OscManager::sendBundle(const juce::OSCBundle &b) {
  if (!isOscConnected)
    return;

  // 1. RECORD OUTGOING HASHES (The Anti-Echo System)
  {
    const juce::ScopedLock hl(gatekeeperLock);
    double now = juce::Time::getMillisecondCounterHiRes();
    for (const auto &elem : b) {
      if (elem.isMessage()) {
        auto &m = elem.getMessage();
        lastMessageTime[computeMessageFingerprint(m)] = now;
      }
    }
  }

  // 2. Network Send (Always Fast)
  sender.send(b);

  // 3. Logging (Throttled, callAsync for thread safety vs OpenGL)
  auto now = juce::Time::getMillisecondCounterHiRes();
  if (now - lastLogTime > 250.0) {
    lastLogTime = now;
    if (onLog) {
      int count = packetCount.exchange(0);
      if (count > 0) {
        auto cb = onLog;
        juce::MessageManager::callAsync([cb, count] {
          if (cb)
            cb("OSC OUT: " + juce::String(count) + " pkts", false);
        });
      }
    }
  } else {
    packetCount += b.size();
  }
}

void OscManager::recordSentMessage(const juce::String &address, float value) {
  juce::ignoreUnused(address);
  packetCount++;
  if (logBuffer) {
    logBuffer->push(LogEntry::Code::OscOut, 0, value);
  }
}

void OscManager::oscMessageReceived(const juce::OSCMessage &message) {
  // 1. ABSOLUTE SELF-IDENTIFICATION CHECK
  for (const auto &arg : message) {
    if (arg.isString() && arg.getString() == instanceId) {
      return;
    }
  }

  const juce::String addr = message.getAddressPattern().toString();
  auto now = juce::Time::getMillisecondCounterHiRes();
  uint64_t hash = computeMessageFingerprint(message);

  // 2. RATE LIMITER (Gatekeeper)
  {
    const juce::ScopedLock sl(gatekeeperLock);
    if (lastMessageTime.count(hash)) {
      if ((now - lastMessageTime[hash]) < 50.0) {
        return; // DROP ECHO
      }
    }
  }

  // --- GLOBAL ADDRESSES (no channel prefix) ---
  if (addr == "/clock/bpm" || addr == "/tempo" ||
      (schema.bpmAddr.isNotEmpty() && addr == schema.bpmAddr)) {
    if (message.size() >= 1) {
      float bpm = message[0].isFloat32() ? message[0].getFloat32()
                : message[0].isInt32() ? (float)message[0].getInt32()
                : 120.0f;
      if (inputAirlock) {
        inputAirlock->push(BridgeEvent(EventType::SystemCommand,
                                       EventSource::NetworkOsc, 0,
                                       (int)CommandID::SetBpm, bpm));
      }
    }
    return;
  }

  // Parse Address (bridge schema: /chN + suffix for notes/CC/pitch/etc.)
  int channel = 0;
  juce::String suffix;

  if (addr.startsWith(schema.inNotePrefix)) {
    int prefixLen = schema.inNotePrefix.length();
    int suffixStart = prefixLen;
    while (suffixStart < addr.length() &&
           juce::CharacterFunctions::isDigit(addr[suffixStart])) {
      suffixStart++;
    }
    channel = addr.substring(prefixLen, suffixStart).getIntValue();
    suffix = addr.substring(suffixStart);
  } else {
    // Unknown address: pass to generic handler for full OSC / future use
    // (e.g. Patchworld string addresses, other apps, custom paths)
    if (onUnknownOscMessage)
      onUnknownOscMessage(message);
    return;
  }

  // --- 1. NOTE ON (/n) with AUTO-OFF ---
  if (suffix == schema.inNoteSuffix) { // "n"
    if (message.size() >= 1) {
      int note = 0;
      float velocity = 0.0f;

      if (message[0].isInt32())
        note = message[0].getInt32();
      else if (message[0].isFloat32())
        note = (int)message[0].getFloat32();

      if (message.size() >= 2) {
        if (message[1].isFloat32())
          velocity = juce::jlimit(0.0f, 1.0f, message[1].getFloat32());
        else if (message[1].isInt32())
          velocity = juce::jlimit(0.0f, 1.0f, (float)message[1].getInt32() / 127.0f);
      } else {
        velocity = 0.8f;
      }

      if (inputAirlock) {
        inputAirlock->push(BridgeEvent(EventType::NoteOn,
                                       EventSource::NetworkOsc, channel, note,
                                       velocity));
      }

      // Auto-Off: second argument 0–127 maps to duration 0–2.5s; we create our own note-offs (no hung notes)
      double durationMs = velocity * 2500.0;
      if (scheduleOffCallback) {
        scheduleOffCallback(channel, note, durationMs);
      }
    }
    return;
  }

  // --- 1b. NOTE OFF (/noff) ---
  if (suffix == schema.inNoteOff) { // "noff"
    if (message.size() >= 1) {
      int note = 0;
      if (message[0].isInt32())
        note = message[0].getInt32();
      else if (message[0].isFloat32())
        note = (int)message[0].getFloat32();

      if (inputAirlock) {
        inputAirlock->push(BridgeEvent(
            EventType::NoteOff, EventSource::NetworkOsc, channel, note, 0.0f));
      }
    }
    return;
  }

  // --- 2. CC (/chXcc): two args = CC#, value. Normalize to 0-1 for DAW MIDI out.
  if (suffix == schema.inCC) { // "c"
    if (message.size() >= 2) {
      int cc = 0;
      float val = 0.0f;

      if (message[0].isInt32())
        cc = message[0].getInt32();

      if (message[1].isFloat32())
        val = juce::jlimit(0.0f, 1.0f, message[1].getFloat32());
      else if (message[1].isInt32())
        val = juce::jlimit(0.0f, 1.0f, (float)message[1].getInt32() / 127.0f);

      if (inputAirlock) {
        inputAirlock->push(BridgeEvent(EventType::ControlChange,
                                       EventSource::NetworkOsc, channel, cc,
                                       val));
      }
    }
    return;
  }

  // --- 3. SUSTAIN (/s) ---
  if (suffix == schema.inSus) { // "s"
    if (message.size() >= 1) {
      float val = 0.0f;
      if (message[0].isFloat32())
        val = message[0].getFloat32();
      else if (message[0].isInt32())
        val = (float)message[0].getInt32(); // 0 or 1 usually

      // Sustain is CC 64
      if (inputAirlock) {
        // Normalize 0-1 range if needed, or assume raw 0-127?
        // Typically /s sends 0.0 or 1.0 (float)
        inputAirlock->push(BridgeEvent(EventType::ControlChange,
                                       EventSource::NetworkOsc, channel, 64,
                                       val));
      }
    }
    return;
  }

  // --- 4. WHEEL (/wheel) ---
  if (suffix == schema.inWheel) { // "wheel"
    if (message.size() >= 1) {
      float val = 0.0f;
      if (message[0].isFloat32())
        val = message[0].getFloat32();

      if (inputAirlock) {
        inputAirlock->push(BridgeEvent(
            EventType::PitchBend, EventSource::NetworkOsc, channel, 0, val));
      }
    }
    return;
  }

  // --- 5. AFTERTOUCH/PRESSURE (/press) ---
  if (suffix == schema.inPress) { // "press"
    if (message.size() >= 1) {
      float val = 0.0f;
      if (message[0].isFloat32())
        val = message[0].getFloat32();

      if (inputAirlock) {
        inputAirlock->push(BridgeEvent(
            EventType::Aftertouch, EventSource::NetworkOsc, channel, 0, val));
      }
    }
    return;
  }

  // --- 6. PROGRAM CHANGE (/pc) ---
  if (suffix == schema.inProgramChange || suffix == "pc") {
    if (message.size() >= 1) {
      int program = message[0].isInt32() ? message[0].getInt32()
                  : message[0].isFloat32() ? (int)message[0].getFloat32()
                  : 0;
      if (inputAirlock) {
        inputAirlock->push(BridgeEvent(EventType::ProgramChange,
                                       EventSource::NetworkOsc, channel,
                                       program, 0.0f));
      }
    }
    return;
  }

  // --- 7. POLY AFTERTOUCH (/pat) ---
  if (suffix == schema.inPolyAftertouch || suffix == "pat") {
    if (message.size() >= 2) {
      int note = message[0].isInt32() ? message[0].getInt32()
               : message[0].isFloat32() ? (int)message[0].getFloat32()
               : 0;
      float pressure = message[1].isFloat32() ? message[1].getFloat32()
                    : message[1].isInt32() ? (float)message[1].getInt32() / 127.0f
                    : 0.0f;
      if (inputAirlock) {
        inputAirlock->push(BridgeEvent(EventType::PolyAftertouch,
                                       EventSource::NetworkOsc, channel, note,
                                       pressure));
      }
    }
    return;
  }

  // Unknown suffix or unhandled address: pass to generic handler for full OSC
  if (onUnknownOscMessage)
    onUnknownOscMessage(message);
}

void OscManager::oscBundleReceived(const juce::OSCBundle &bundle) {
  processBundleRecursive(bundle);
}

void OscManager::processBundleRecursive(const juce::OSCBundle &bundle) {
  for (const auto &elem : bundle) {
    if (elem.isMessage())
      oscMessageReceived(elem.getMessage());
    else if (elem.isBundle())
      processBundleRecursive(elem.getBundle());
  }
}

void OscManager::checkConnectionHealth() {
  // DISABLE AUTOMATIC PING/HEARTBEAT
  // We do not want background chatter.
  return;
}

void OscManager::tickZeroConfig() {
  if (zeroConfigEnabled.load()) {
    int ourPort = localPort.load();
    if (ourPort > 0 &&
        broadcastSender.connect("255.255.255.255", 5550)) {
      juce::OSCMessage m("/sys/discovery");
      m.addString(instanceId);
      m.addInt32(ourPort);
      broadcastSender.send(m);
      broadcastSender.disconnect();
    }
  }
}
