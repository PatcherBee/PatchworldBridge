#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

// ==============================================================================
// 0. Zero-Alloc Raw MIDI Bitmask (Wait-Free Integer Pipe)
// ==============================================================================

// Compile-Time FNV-1a Hash (Moved from OscUtils.h)
static constexpr juce::uint64 oscHash(const char *str) {
  juce::uint64 hash = 0xcbf29ce484222325;
  while (*str) {
    hash ^= static_cast<juce::uint64>(*str++);
    hash *= 0x100000001b3;
  }
  return hash;
}

struct FastOsc {
  static constexpr juce::uint64 offset_basis = 0xcbf29ce484222325;
  static constexpr juce::uint64 prime = 0x100000001b3;
  static constexpr juce::uint64 hashRaw(const char *str) {
    juce::uint64 hash = offset_basis;
    while (*str) {
      hash ^= static_cast<juce::uint64>(*str++);
      hash *= prime;
    }
    return hash;
  }
  static juce::uint64 hashString(const juce::String &s) {
    return hashRaw(s.toRawUTF8());
  }
};

// Pack 3 MIDI bytes into a uint32_t to bypass juce::MidiMessage ref-counting
// and heap allocation on the hot path. 100% wait-free, zero cache contention.
struct RawMidi {
  static juce::uint32 pack(const juce::MidiMessage &m) {
    auto *data = m.getRawData();
    int size = m.getRawDataSize();
    juce::uint32 packed = (juce::uint32)data[0];
    if (size > 1)
      packed |= ((juce::uint32)data[1] << 8);
    if (size > 2)
      packed |= ((juce::uint32)data[2] << 16);
    return packed;
  }

  static juce::MidiMessage unpack(juce::uint32 packed) {
    // Explicit cast to int to resolve ambiguity between constructors
    return juce::MidiMessage((int)(packed & 0xFF), (int)((packed >> 8) & 0xFF),
                             (int)((packed >> 16) & 0xFF));
  }
};

// ==============================================================================
// 1. Naming Schema
// ==============================================================================
// Defines the rules for generating addresses
struct OscNamingSchema {
  // --- SENDING TO NETWORK (OUT) ---
  juce::String outNotePrefix = "/ch";
  juce::String outNoteSuffix = "note";
  juce::String outVelSuffix = "nvalue";
  juce::String outNoteOff = "noteoff";
  juce::String outCC = "cc";
  juce::String outCCVal = "ccvalue";
  juce::String outPitch = "pitch";
  juce::String outPressure = "pressure";
  juce::String outSus = "sus";

  // --- RECEIVING FROM NETWORK (IN) ---
  juce::String inNotePrefix = "/ch";
  juce::String inNoteSuffix = "n";
  juce::String inVelSuffix = "nv";
  juce::String inNoteOff = "noff";
  juce::String inCC = "c";
  juce::String inWheel = "wheel";
  juce::String inPress = "press";
  juce::String inSus = "s";
  juce::String inProgramChange = "pc";
  juce::String inPolyAftertouch = "pat";

  juce::String outProgramChange = "pc";
  juce::String outPolyAftertouch = "pat";
  juce::String bpmAddr = "/clock/bpm";

  // --- LEGACY/UI COMPATIBILITY ---
  juce::String notePrefix = "/ch";
  juce::String noteSuffix = "note";
  juce::String noteOffSuffix = "noteoff";
  juce::String ccPrefix = "/ch";
  juce::String ccSuffix = "cc";
  juce::String pitchPrefix = "/ch";
  juce::String pitchSuffix = "pitch";
  juce::String aftertouchSuffix = "pressure";
  juce::String playAddr = "/play";
  juce::String stopAddr = "/stop";

  // Helper: Build address
  juce::String getAddress(const juce::String &prefix, int channel,
                          const juce::String &suffix) const {
    return prefix + juce::String(channel) + suffix;
  }
};

// ==============================================================================
// 3. Data Packet (Wait-Free Event)
// ==============================================================================
// 1. Source Identification (The "Tag")
enum class EventSource : juce::uint8 {
  None = 0,
  HardwareMidi,   // From physical MIDI Input
  NetworkOsc,     // From incoming UDP/OSC packets
  UserInterface,  // From on-screen faders/buttons
  EngineSequencer // From the internal step sequencer
};

// 2. Event Types
enum class EventType : juce::uint8 {
  None = 0,
  NoteOn,
  NoteOff,
  ControlChange,
  PitchBend,
  Transport,
  SystemCommand,
  Panic,
  Aftertouch,
  PolyAftertouch,
  ProgramChange,
  PlaylistCommand,
  VisualParam
};

// ==============================================================================
// 3. Data Packet (Wait-Free Event)
// ==============================================================================
struct BridgeEvent {
  // Aliases for compatibility with existing code during transition
  using Type = EventType;
  using Source = EventSource;

  // Constants mapping for existing code using BridgeEvent::NoteOn etc.
  static constexpr EventType NoteOn = EventType::NoteOn;
  static constexpr EventType NoteOff = EventType::NoteOff;
  static constexpr EventType CC = EventType::ControlChange;
  static constexpr EventType Pitch = EventType::PitchBend;
  static constexpr EventType Transport = EventType::Transport;
  static constexpr EventType Panic = EventType::Panic;
  static constexpr EventType Unknown = EventType::None;
  static constexpr EventType Aftertouch = EventType::Aftertouch;
  static constexpr EventType PlaylistCommand = EventType::PlaylistCommand;
  static constexpr EventType VisualParam = EventType::VisualParam;
  static constexpr EventType SystemCommand = EventType::SystemCommand;

  static constexpr EventSource Internal = EventSource::EngineSequencer;
  static constexpr EventSource Hardware = EventSource::HardwareMidi;
  static constexpr EventSource Network = EventSource::NetworkOsc;
  static constexpr EventSource UserInterface = EventSource::UserInterface;

  // Explicit constructor to prevent conversion errors
  BridgeEvent(EventType t = EventType::None, EventSource s = EventSource::None,
              int ch = 0, int ncc = 0, float v = 0.0f)
      : type(t), source(s), channel((juce::uint8)ch),
        noteOrCC((juce::uint8)ncc), value(v) {
    timestampUs =
        (juce::int64)(juce::Time::getMillisecondCounterHiRes() * 1000.0);
    // Do NOT memset the whole address array. It's expensive.
    // Just null-terminate the first char to mark it empty.
    oscAddress[0] = '\0';
  }

  // 1. HOT DATA (Accessed frequently in loops)
  // Grouping these together improves read speed and alignment
  EventType type = EventType::None;       // 1 byte
  EventSource source = EventSource::None; // 1 byte
  juce::uint8 channel = 0;                // 1 byte
  juce::uint8 noteOrCC = 0;               // 1 byte
  float value = 0.0f;                     // 4 bytes
  juce::int64 timestampUs = 0;            // 8 bytes

  // Total Hot Data: 16 bytes. Fits perfectly in 1/4 of a cache line.

  // 2. COLD DATA (Only accessed for OSC Network messages)
  // Size reduced to 48 bytes.
  // 16 (Hot) + 48 (Cold) = 64 bytes exactly.
  // This is the "Golden Size" for CPU cache lines.
  char oscAddress[48];

  // Helper to check if this event originated externally
  bool isRemote() const { return source == EventSource::NetworkOsc; }

  // FIX: For compatibility with old code using receiveTime
  double getReceiveTime() const { return (double)timestampUs / 1000.0; }
};

// ==============================================================================
// 2. Routing Definition (The Value mapped to the Hash)
// ==============================================================================
struct OscRoute {
  OscRoute() = default;

  // Constructor to handle the initializer lists in OscLookup.h
  OscRoute(BridgeEvent::Type t, int ch, int ncc = 0, float vs = 1.0f)
      : type(t), channel(ch), noteOrCC(ncc), valueScale(vs) {}

  BridgeEvent::Type type = BridgeEvent::Unknown;
  int channel = 0;
  int noteOrCC = 0;
  float valueScale = 1.0f;
};
