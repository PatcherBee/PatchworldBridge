/*
  ==============================================================================
    Source/Core/OptimizationCache.h
    Role: Zero-alloc caches for audio thread (noteToHz LUT, velocity curve,
          pre-cached OSC address strings)
  ==============================================================================
*/
#pragma once
#include <array>
#include <cmath>
#include <cstring>
#include <juce_core/juce_core.h>

struct MidiMath {
  static inline float noteToHz[128];
  static inline float velocityCurve[128];
  static bool initialized;

  static void initialize() {
    if (initialized)
      return;
    for (int i = 0; i < 128; ++i) {
      noteToHz[i] = 440.0f * std::pow(2.0f, (i - 69.0f) / 12.0f);
      velocityCurve[i] = std::pow(i / 127.0f, 2.0f);
    }
    initialized = true;
  }

  static float noteToFrequency(int note) {
    if (!initialized)
      initialize();
    return noteToHz[juce::jlimit(0, 127, note)];
  }

  static float velocityToGain(int vel) {
    if (!initialized)
      initialize();
    return velocityCurve[juce::jlimit(0, 127, vel)];
  }
};

inline bool MidiMath::initialized = false;

// ==============================================================================
// Pre-cached OSC Address Strings (Zero-alloc on audio thread)
// ==============================================================================
struct OscAddressCache {
  static constexpr int MaxChannels = 16;
  static constexpr int MaxAddressLen = 64;

  struct ChannelAddresses {
    char noteOn[MaxAddressLen];
    char noteOff[MaxAddressLen];
    char cc[MaxAddressLen];
    char pitch[MaxAddressLen];
    char pressure[MaxAddressLen];
    char volume[MaxAddressLen];
    char pan[MaxAddressLen];
  };

  static inline std::array<ChannelAddresses, MaxChannels> channels;
  static inline bool initialized = false;

  static void initialize(const char* prefix = "/ch") {
    if (initialized)
      return;

    for (int ch = 0; ch < MaxChannels; ++ch) {
      int channelNum = ch + 1;
      snprintf(channels[ch].noteOn, MaxAddressLen, "%s%d/note", prefix, channelNum);
      snprintf(channels[ch].noteOff, MaxAddressLen, "%s%d/noteoff", prefix, channelNum);
      snprintf(channels[ch].cc, MaxAddressLen, "%s%d/cc", prefix, channelNum);
      snprintf(channels[ch].pitch, MaxAddressLen, "%s%d/pitch", prefix, channelNum);
      snprintf(channels[ch].pressure, MaxAddressLen, "%s%d/pressure", prefix, channelNum);
      snprintf(channels[ch].volume, MaxAddressLen, "%s%d/vol", prefix, channelNum);
      snprintf(channels[ch].pan, MaxAddressLen, "%s%d/pan", prefix, channelNum);
    }
    initialized = true;
  }

  static const char* getNoteOnAddr(int channel) {
    if (!initialized)
      initialize();
    return channels[juce::jlimit(0, MaxChannels - 1, channel - 1)].noteOn;
  }

  static const char* getNoteOffAddr(int channel) {
    if (!initialized)
      initialize();
    return channels[juce::jlimit(0, MaxChannels - 1, channel - 1)].noteOff;
  }

  static const char* getCCAddr(int channel) {
    if (!initialized)
      initialize();
    return channels[juce::jlimit(0, MaxChannels - 1, channel - 1)].cc;
  }

  static const char* getPitchAddr(int channel) {
    if (!initialized)
      initialize();
    return channels[juce::jlimit(0, MaxChannels - 1, channel - 1)].pitch;
  }

  static const char* getVolumeAddr(int channel) {
    if (!initialized)
      initialize();
    return channels[juce::jlimit(0, MaxChannels - 1, channel - 1)].volume;
  }

  static const char* getPanAddr(int channel) {
    if (!initialized)
      initialize();
    return channels[juce::jlimit(0, MaxChannels - 1, channel - 1)].pan;
  }

  // Re-initialize with custom prefix (call from UI thread only!)
  static void updatePrefix(const char* newPrefix) {
    initialized = false;
    initialize(newPrefix);
  }
};

// ==============================================================================
// Combined Optimization Cache Singleton
// ==============================================================================
class OptimizationCache {
public:
  static OptimizationCache& getInstance() {
    static OptimizationCache instance;
    return instance;
  }

  void initializeAll() {
    MidiMath::initialize();
    OscAddressCache::initialize();
  }

  float midiToHz(int note) const {
    return MidiMath::noteToFrequency(note);
  }

  float velocityCurve(int velocity) const {
    return MidiMath::velocityToGain(velocity);
  }

  const char* oscNoteOn(int channel) const {
    return OscAddressCache::getNoteOnAddr(channel);
  }

  const char* oscCC(int channel) const {
    return OscAddressCache::getCCAddr(channel);
  }

private:
  OptimizationCache() {
    initializeAll();
  }
};
