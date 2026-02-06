#pragma once
#include "../Audio/AudioEngine.h"
#include <functional>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

// Forward Declarations
class MixerPanel;
class ComplexPianoRoll;
class MidiPlaylist;
class MidiScheduler;
class AudioEngine;
class BridgeContext;
#include "../UI/Panels/SpliceEditor.h"

class PlaybackController : public juce::MidiInputCallback,
                           public std::enable_shared_from_this<PlaybackController> {
public:
  PlaybackController(AudioEngine *e, BridgeContext *ctx);
  void setScheduler(MidiScheduler *s) { scheduler = s; }

  // MidiInputCallback override
  void handleIncomingMidiMessage(juce::MidiInput *source,
                                 const juce::MidiMessage &message) override;

  // Core Actions
  void loadMidiFile(const juce::File &file);
  void unloadMidiFile();
  void startPlayback();
  void stopPlayback();
  void pausePlayback();
  void resumePlayback();
  void clearTrackAndGrids();

  // Logic
  void handleSequenceEnd();
  void skipToNext();
  void skipToNextOrWrapToFirst();
  void skipToPrevious();

  // Gapless
  void prepareNextTrack();

  // Setters
  void setMixer(MixerPanel *m) { mixer = m; }
  void setTrackGrid(ComplexPianoRoll *g) { trackGrid = g; }

  void setPlaylist(MidiPlaylist *p) { playlist = p; }
  void setSpliceEditor(SpliceEditor *se) { spliceEditor = se; }
  void setSequencer(class SequencerPanel *s) { sequencer = s; }

  // Callbacks
  std::function<void(juce::String, bool)> onLog;
  std::function<void(double)> onBpmUpdate;
  std::function<void(double)> onLengthUpdate;
  std::function<void()> onReset;

private:
  AudioEngine &engine;
  BridgeContext *context = nullptr;
  MixerPanel *mixer = nullptr;
  ComplexPianoRoll *trackGrid = nullptr;
  SpliceEditor *spliceEditor = nullptr;
  MidiPlaylist *playlist = nullptr;
  MidiScheduler *scheduler = nullptr;
  class SequencerPanel *sequencer = nullptr;

  juce::CriticalSection fileLock;

  bool playAfterLoad = false;
  double loadedFileBpm = 120.0;
  bool hasFileLoaded = false;
public:
  double getLoadedFileBpm() const { return loadedFileBpm; }
  bool hasLoadedFile() const { return hasFileLoaded; }
};
