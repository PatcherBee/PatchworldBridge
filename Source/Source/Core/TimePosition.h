/*
  ==============================================================================
    Source/Core/TimePosition.h
    Bar:Beat:Tick display utility.
  ==============================================================================
*/
#pragma once
#include <juce_core/juce_core.h>

struct TimePosition {
  int bar = 1;
  int beat = 1;
  int tick = 0;

  juce::String toString() const {
    return juce::String::formatted("%03d:%d:%03d", bar, beat, tick);
  }
};

inline TimePosition ticksToPosition(double ticks, double ppq, int beatsPerBar = 4) {
  double beats = ticks / ppq;
  int totalBeats = (int)beats;
  double fractionalBeat = beats - totalBeats;

  TimePosition pos;
  pos.bar = (totalBeats / beatsPerBar) + 1;
  pos.beat = (totalBeats % beatsPerBar) + 1;
  pos.tick = (int)(fractionalBeat * ppq);

  return pos;
}

inline TimePosition beatsToPosition(double beats, int beatsPerBar = 4) {
  int totalBeats = (int)beats;
  double fractionalBeat = beats - totalBeats;

  TimePosition pos;
  pos.bar = (totalBeats / beatsPerBar) + 1;
  pos.beat = (totalBeats % beatsPerBar) + 1;
  pos.tick = (int)(fractionalBeat * 960.0); // Assume 960 ppq for tick display

  return pos;
}
