/* Source/Services/MidiTransformer.h */
#pragma once
#include <cmath>
#include <juce_core/juce_core.h>
#include <vector>

/**
 * MidiTransformer: Filters and modifies incoming MIDI messages
 * before they reach the main handlers.
 */
class MidiTransformer {
public:
  struct Settings {
    float deadzone = 0.0f;
    int channelOverride = -1;   // -1 = off
    float velocityCurve = 1.0f; // 1.0 = linear
  } settings;

  /**
   * Processes a MIDI message and returns the transformed version.
   * Returns an empty message if it should be dropped (e.g. deadzone, SysEx).
   */
  juce::MidiMessage process(const juce::MidiMessage &m) {
    // Drop SysEx - large allocations, lock-free queue unsafe
    if (m.isSysEx())
      return {};

    if (m.isController()) {
      float val = m.getControllerValue() / 127.0f;
      // Drop jittery near-zero CCs if deadzone is active
      if (settings.deadzone > 0.001f && val < settings.deadzone && val > 0.0f) {
        return {};
      }
    }

    auto msg = m;
    if (settings.channelOverride != -1) {
      msg.setChannel(settings.channelOverride);
    }

    // Velocity scaling/curving logic could be added here
    // For now, satisfy the caller's requirement for a return value.
    return msg;
  }
};

class ScaleQuantizer {
public:
  enum Scale { Chromatic, Major, Minor, Pentatonic, Dorian, Blues };

  struct Settings {
    Scale scale = Chromatic;
    int root = 0; // 0=C, 1=C#, etc.
    bool enabled = false;
  } settings;

  int quantize(int note) {
    if (!settings.enabled || settings.scale == Chromatic)
      return note;

    int octave = note / 12;
    int pitchClass = note % 12;

    // Normalize to root
    int relative = (pitchClass - settings.root + 12) % 12;

    const auto &intervals = getIntervals(settings.scale);

    // Find closest valid interval
    int bestMatch = intervals[0];
    int minDist = 12;

    for (int interval : intervals) {
      int dist = std::abs(relative - interval);
      if (dist < minDist) {
        minDist = dist;
        bestMatch = interval;
      }
    }

    // Reconstruct
    int outPitch = (bestMatch + settings.root) % 12;
    return juce::jlimit(0, 127, (octave * 12) + outPitch);
  }

private:
  const std::vector<int> &getIntervals(Scale s) {
    static const std::vector<int> major = {0, 2, 4, 5, 7, 9, 11};
    static const std::vector<int> minor = {0, 2, 3, 5, 7, 8, 10};
    static const std::vector<int> pent = {0, 3, 5, 7, 10};
    static const std::vector<int> dorian = {0, 2, 3, 5, 7, 9, 10};
    static const std::vector<int> blues = {0, 3, 5, 6, 7, 10}; // Blues scale
    static const std::vector<int> chrom = {0, 1, 2, 3, 4,  5,
                                           6, 7, 8, 9, 10, 11};

    switch (s) {
    case Major:
      return major;
    case Minor:
      return minor;
    case Pentatonic:
      return pent;
    case Dorian:
      return dorian;
    case Blues:
      return blues;
    default:
      return chrom;
    }
  }
};
