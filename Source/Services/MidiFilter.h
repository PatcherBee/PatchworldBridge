/*
  ==============================================================================
    Source/Services/MidiFilter.h
    Channel/note filtering for MIDI routing.
  ==============================================================================
*/
#pragma once
#include <cstdint>
#include <juce_core/juce_core.h>

struct MidiFilter {
  uint16_t channelMask = 0xFFFF; // bit N = channel N+1 enabled
  int lowNote = 0;
  int highNote = 127;
  int transpose = 0;
  int forceChannel = -1; // -1 = no remap, 1-16 = force to channel

  bool shouldPass(int channel, int note) const {
    if (channel < 1 || channel > 16)
      return false;
    if (!(channelMask & (1 << (channel - 1))))
      return false;
    if (note < lowNote || note > highNote)
      return false;
    return true;
  }

  int processNote(int note) const {
    return juce::jlimit(0, 127, note + transpose);
  }

  int processChannel(int channel) const {
    if (forceChannel >= 1 && forceChannel <= 16)
      return forceChannel;
    return channel;
  }
};
