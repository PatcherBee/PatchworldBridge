/*
  ==============================================================================
    Source/Audio/CueManager.h
    Cue point system for transport navigation.
  ==============================================================================
*/
#pragma once
#include <algorithm>
#include <juce_core/juce_core.h>
#include <vector>

struct CuePoint {
  juce::String name;
  double beat = 0.0;
  juce::Colour colour = juce::Colours::orange;
  int id = 0;
};

class CueManager {
public:
  void addCue(const juce::String &name, double beat,
              juce::Colour col = juce::Colours::orange) {
    CuePoint cue{name, beat, col, nextId++};
    cues.push_back(cue);
    std::sort(cues.begin(), cues.end(),
              [](const auto &a, const auto &b) { return a.beat < b.beat; });
  }

  void removeCue(int id) {
    cues.erase(std::remove_if(cues.begin(), cues.end(),
                              [id](const auto &c) { return c.id == id; }),
               cues.end());
  }

  void clear() { cues.clear(); }

  const std::vector<CuePoint> &getCues() const { return cues; }

  double getNextCueBeat(double currentBeat) const {
    for (const auto &cue : cues) {
      if (cue.beat > currentBeat + 0.01)
        return cue.beat;
    }
    return -1.0;
  }

  double getPrevCueBeat(double currentBeat) const {
    double result = -1.0;
    for (const auto &cue : cues) {
      if (cue.beat < currentBeat - 0.01)
        result = cue.beat;
      else
        break;
    }
    return result;
  }

  const CuePoint *getCue(int index) const {
    if (index >= 0 && index < (int)cues.size())
      return &cues[index];
    return nullptr;
  }

private:
  std::vector<CuePoint> cues;
  int nextId = 1;
};
