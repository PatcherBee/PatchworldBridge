/*
  ==============================================================================
    Source/Audio/ChordDetector.h
    Role: Real-time chord detection from active notes (roadmap 13.3)
  ==============================================================================
*/
#pragma once

#include <juce_core/juce_core.h>
#include <set>
#include <vector>

class ChordDetector {
public:
  struct DetectedChord {
    juce::String name;      // "Cmaj7", "Dm", "G7", etc.
    juce::String root;      // "C", "D", etc.
    juce::String quality;   // "maj", "min", "dim", "aug", "7", etc.
    int rootNote = -1;      // MIDI note number of root
    float confidence = 0.0f;
    std::set<int> notes;
  };

  /** Detect chord from active MIDI notes. */
  DetectedChord detect(const std::set<int> &activeNotes) const {
    DetectedChord result;
    if (activeNotes.size() < 2)
      return result;

    // Get pitch classes (0-11) and find intervals
    std::vector<int> pitchClasses;
    for (int note : activeNotes) {
      int pc = note % 12;
      if (std::find(pitchClasses.begin(), pitchClasses.end(), pc) == pitchClasses.end())
        pitchClasses.push_back(pc);
    }
    std::sort(pitchClasses.begin(), pitchClasses.end());

    if (pitchClasses.empty())
      return result;

    // Try each pitch class as potential root
    float bestConfidence = 0.0f;
    for (int root : pitchClasses) {
      // Calculate intervals from this root
      std::vector<int> intervals;
      for (int pc : pitchClasses) {
        int interval = (pc - root + 12) % 12;
        intervals.push_back(interval);
      }
      std::sort(intervals.begin(), intervals.end());

      // Match against known chord patterns
      auto match = matchPattern(intervals);
      if (match.second > bestConfidence) {
        bestConfidence = match.second;
        result.quality = match.first;
        result.rootNote = root;
        result.root = noteNames[root];
        result.confidence = match.second;
      }
    }

    if (bestConfidence > 0.5f) {
      result.name = result.root + result.quality;
      result.notes = activeNotes;
    } else {
      result.name = "";
    }

    return result;
  }

private:
  static inline const char *noteNames[] = {"C", "C#", "D", "D#", "E", "F",
                                            "F#", "G", "G#", "A", "A#", "B"};

  /** Match interval pattern to chord quality. Returns (quality, confidence). */
  std::pair<juce::String, float> matchPattern(const std::vector<int> &intervals) const {
    // Common chord patterns: {intervals} -> quality
    if (containsAll(intervals, {0, 4, 7})) {
      if (containsAll(intervals, {0, 4, 7, 11}))
        return {"maj7", 1.0f};
      if (containsAll(intervals, {0, 4, 7, 10}))
        return {"7", 1.0f};
      return {"maj", 0.9f}; // Major triad
    }
    if (containsAll(intervals, {0, 3, 7})) {
      if (containsAll(intervals, {0, 3, 7, 10}))
        return {"m7", 1.0f};
      return {"m", 0.9f}; // Minor triad
    }
    if (containsAll(intervals, {0, 3, 6})) {
      if (containsAll(intervals, {0, 3, 6, 9}))
        return {"dim7", 1.0f};
      if (containsAll(intervals, {0, 3, 6, 10}))
        return {"m7b5", 1.0f};
      return {"dim", 0.85f};
    }
    if (containsAll(intervals, {0, 4, 8}))
      return {"aug", 0.85f};
    if (containsAll(intervals, {0, 5, 7}))
      return {"sus4", 0.8f};
    if (containsAll(intervals, {0, 2, 7}))
      return {"sus2", 0.8f};
    if (containsAll(intervals, {0, 4, 7, 9}))
      return {"6", 0.9f};
    if (containsAll(intervals, {0, 3, 7, 9}))
      return {"m6", 0.9f};

    return {"", 0.0f};
  }

  bool containsAll(const std::vector<int> &set, std::initializer_list<int> required) const {
    for (int r : required) {
      if (std::find(set.begin(), set.end(), r) == set.end())
        return false;
    }
    return true;
  }
};
