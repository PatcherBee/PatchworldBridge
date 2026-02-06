/*
  ==============================================================================
    Source/Audio/CountInManager.h
    Pre-playback count-in (e.g. 1 bar before transport starts).
  ==============================================================================
*/
#pragma once
#include <functional>
#include <juce_core/juce_core.h>

class CountInManager {
public:
  void startCountIn(int bars = 1, int beatsPerBar = 4) {
    totalBeats = bars * beatsPerBar;
    remainingBeats = totalBeats;
    isActive = true;
    lastBeatInt = -1;
  }

  void stop() {
    isActive = false;
    remainingBeats = 0;
  }

  // Call from audio thread - returns true when count-in is complete
  bool process(double currentBeat, double bpm) {
    juce::ignoreUnused(bpm);
    if (!isActive)
      return true;

    int beatInt = (int)currentBeat;
    if (beatInt != lastBeatInt && beatInt >= 0) {
      lastBeatInt = beatInt;
      remainingBeats--;

      if (onCountBeat) {
        int beatInBar = (totalBeats - remainingBeats) % 4;
        bool isDownbeat = (beatInBar == 0);
        onCountBeat(remainingBeats, isDownbeat);
      }

      if (remainingBeats <= 0) {
        isActive = false;
        if (onCountInComplete)
          onCountInComplete();
        return true;
      }
    }
    return false;
  }

  bool isCounting() const { return isActive; }
  int getBeatsRemaining() const { return remainingBeats; }

  std::function<void(int remaining, bool isDownbeat)> onCountBeat;
  std::function<void()> onCountInComplete;

private:
  bool isActive = false;
  int totalBeats = 4;
  int remainingBeats = 0;
  int lastBeatInt = -1;
};
