/*
  ==============================================================================
    Source/Core/CommandDispatcher.h
    Central "Switchboard" for app-wide commands.
  ==============================================================================
*/
#pragma once
#include <functional>
#include <juce_core/juce_core.h>

// Forward declarations
class AudioEngine;
class MidiRouter;
class MixerPanel;
class MixerViewModel;
class OscManager;
class PlaybackController;
class SequencerPanel;
class SequencerViewModel;

enum class CommandID {
  TransportPlay,
  TransportStop,
  TransportReset,
  MixerMuteToggle,
  SequencerRandomize,
  Panic,
  SetBpm,
  SetScaleQuantization,
  PlaylistNext,
  PlaylistPrev
};

struct CommandDispatcher {
  AudioEngine *engine = nullptr;
  MidiRouter *router = nullptr;
  MixerPanel *mixer = nullptr;
  MixerViewModel *mixerViewModel = nullptr;
  OscManager *oscManager = nullptr;
  PlaybackController *playback = nullptr;
  SequencerPanel *sequencer = nullptr;
  SequencerViewModel *sequencerViewModel = nullptr;

  // Declaration only - Implementation in CommandDispatcher.cpp
  void trigger(CommandID cmd, float value = 0.0f, int channel = 0);
};
