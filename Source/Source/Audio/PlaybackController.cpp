/*
  ==============================================================================
    Source/Audio/PlaybackController.cpp
    Role: Midi File Playback (Implementation)
  ==============================================================================
*/

#include "../Audio/PlaybackController.h"
#include "../Core/BridgeContext.h" // For context
#include "../UI/Panels/MidiPlaylist.h"
#include "../UI/Panels/MixerPanel.h"
#include "../UI/Widgets/PianoRoll.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

PlaybackController::PlaybackController(AudioEngine *e, BridgeContext *ctx)
    : engine(*e), context(ctx) {
  // Initialize
}

void PlaybackController::handleSequenceEnd() {
  // Runs on audio/callback thread: re-check playlist and play mode when track ends
  if (!playlist)
    return;

  if (playlist->playMode == MidiPlaylist::LoopOne) {
    engine.resetTransportForLoop();
  } else if (playlist->playMode == MidiPlaylist::LoopAll) {
    juce::MessageManager::callAsync([]() {
      auto *ctx = BridgeContext::getLivingContext();
      if (!ctx || !ctx->playbackController) return;
      ctx->playbackController->skipToNextOrWrapToFirst();
    });
  } else {
    juce::MessageManager::callAsync([]() {
      auto *ctx = BridgeContext::getLivingContext();
      if (!ctx || !ctx->playbackController) return;
      ctx->playbackController->stopPlayback();
    });
  }
}

void PlaybackController::startPlayback() {
  engine.resetTransportForLoop();
  engine.play();

  if (onLog)
    onLog("Transport: PLAY", false);
}

void PlaybackController::stopPlayback() {
  bool wasPlaying = engine.getIsPlaying();

  engine.stop();

  if (scheduler) {
    scheduler->clear();
    if (engine.onMidiEvent)
      engine.onMidiEvent(juce::MidiMessage::allNotesOff(1));
  }

  if (wasPlaying) {
    if (onLog)
      onLog("Transport: STOP", false);
  } else {
    engine.resetTransport();
    if (onLog)
      onLog("Transport: Rewind to Start", false);
  }
}

void PlaybackController::skipToNext() {
  if (playlist && playlist->currentIndex < playlist->files.size() - 1) {
    playAfterLoad = true;
    playlist->currentIndex++;
    juce::File nextFile(playlist->files[playlist->currentIndex]);
    loadMidiFile(nextFile);
    playlist->selectFileAtIndex(playlist->currentIndex);
  }
}

void PlaybackController::skipToNextOrWrapToFirst() {
  if (!playlist || playlist->files.isEmpty())
    return;
  playAfterLoad = true;
  if (playlist->currentIndex < playlist->files.size() - 1) {
    playlist->currentIndex++;
  } else {
    playlist->currentIndex = 0;
  }
  juce::File nextFile(playlist->files[playlist->currentIndex]);
  loadMidiFile(nextFile);
  playlist->selectFileAtIndex(playlist->currentIndex);
}

void PlaybackController::skipToPrevious() {
  if (playlist && playlist->currentIndex > 0) {
    playAfterLoad = true;
    playlist->currentIndex--;
    juce::File prevFile(playlist->files[playlist->currentIndex]);
    loadMidiFile(prevFile);
    playlist->selectFileAtIndex(playlist->currentIndex);
  }
}

void PlaybackController::loadMidiFile(const juce::File &file) {
  stopPlayback();

  auto self = shared_from_this();
  juce::Thread::launch([self, file] {
    if (!self)
      return;
    // A. Heavy Lifting (Disk I/O + Parsing)
    juce::MidiFile mf;
    juce::FileInputStream stream(file);
    if (!stream.openedOk() || !mf.readFrom(stream)) {
      juce::String err = "Could not load \"" + file.getFileName()
          + "\". File may be missing or not a valid MIDI file.";
      if (self->onLog)
        juce::MessageManager::callAsync([self, err]() {
          if (!BridgeContext::getLivingContext() || !self) return;
          if (self->onLog) self->onLog(err, true);
        });
      return;
    }

    juce::MidiMessageSequence seq;
    double ppq = 960.0;

    // Convert ticks
    if (mf.getTimeFormat() > 0)
      ppq = mf.getTimeFormat();

    // Merge Tracks
    for (int i = 0; i < mf.getNumTracks(); ++i) {
      seq.addSequence(*mf.getTrack(i), 0);
    }
    seq.updateMatchedPairs();

    // B. Scan for Meta Events (Tempo & Time Signature)
    double fileBpm = -1.0;
    int timeSigNum = 4;
    int timeSigDen = 4;

    for (int i = 0; i < seq.getNumEvents(); ++i) {
      auto &msg = seq.getEventPointer(i)->message;
      if (msg.isTempoMetaEvent() && fileBpm < 0) {
        fileBpm = msg.getTempoSecondsPerQuarterNote() > 0.0
                      ? 60.0 / msg.getTempoSecondsPerQuarterNote()
                      : -1.0;
      }

      int num, den;
      if (msg.isTimeSignatureMetaEvent()) {
        msg.getTimeSignatureInfo(num, den);
        timeSigNum = num;
        timeSigDen = den;
      }
    }

    if (!self)
      return;
    if (fileBpm > 0.0)
      self->engine.setBpm(fileBpm);
    self->engine.setTimeSignature(timeSigNum, timeSigDen);
    self->engine.setSequence(seq, ppq, fileBpm);

    // C. Extract track names (Track Name meta-event + channel from first NoteOn)
    std::map<int, juce::String> channelNames;
    for (int t = 0; t < mf.getNumTracks(); ++t) {
      const auto *track = mf.getTrack(t);
      juce::String trackName = "";
      int trackChannel = -1;

      for (int e = 0; e < track->getNumEvents(); ++e) {
        const auto &m = track->getEventPointer(e)->message;

        if (m.isTrackNameEvent()) {
          trackName = m.getTextFromTextMetaEvent();
        }
        if (trackChannel == -1 && m.isNoteOn()) {
          trackChannel = m.getChannel();
        }
        if (trackName.isNotEmpty() && trackChannel > 0)
          break;
      }

      if (trackChannel >= 1 && trackChannel <= 16 && trackName.isNotEmpty()) {
        channelNames[trackChannel] = trackName;
      }
    }

    // D. Build sequence for trackGrid and calculate total beats
    double totalBeats = 0.0;
    if (seq.getNumEvents() > 0) {
      double lastTick = seq.getEndTime();
      totalBeats = lastTick / ppq;
    }

    juce::MessageManager::callAsync([self, seq, file, channelNames, fileBpm,
                                     totalBeats, ppq]() {
      if (!BridgeContext::getLivingContext() || !self)
        return;
      // Ensure playback starts from beginning: reset on message thread after
      // setSequence (which ran on loader thread) so position is definitely 0
      self->engine.stop();
      self->engine.resetTransport();
      self->loadedFileBpm = (fileBpm > 0.0) ? fileBpm : 120.0;
      self->hasFileLoaded = true;
      if (self->trackGrid)
        self->trackGrid->setSequence(seq);

      if (self->spliceEditor) {
        std::vector<EditableNote> editNotes;
        for (int i = 0; i < seq.getNumEvents(); ++i) {
          auto *ev = seq.getEventPointer(i);
          if (ev->message.isNoteOn()) {
            EditableNote n;
            n.noteNumber = ev->message.getNoteNumber();
            n.velocity = ev->message.getFloatVelocity();
            n.channel = ev->message.getChannel();
            n.startBeat = ev->message.getTimeStamp() / ppq;

            // Find note off
            auto *pair = seq.getEventPointer(seq.getIndexOfMatchingKeyUp(i));
            if (pair) {
              double endBeat = pair->message.getTimeStamp() / ppq;
              n.durationBeats = endBeat - n.startBeat;
            } else {
              n.durationBeats = 1.0;
            }
            editNotes.push_back(n);
          }
        }
        self->spliceEditor->setNotes(editNotes);
        self->spliceEditor->setPlayheadBeat(0.0);  // Ensure playhead shows start
        self->spliceEditor->repaint();  // Force redraw so notes appear immediately
      }

      if (self->onLengthUpdate)
        self->onLengthUpdate(totalBeats);
      if (fileBpm > 0.0 && self->onBpmUpdate)
        self->onBpmUpdate(fileBpm);

      if (self->mixer) {
        self->mixer->resetMapping(true);
        for (auto const &[ch, name] : channelNames) {
          if (ch >= 1 && ch <= 16)
            self->mixer->setChannelName(ch - 1, name);
        }
      }

      if (self->onLog)
        self->onLog("Loaded: " + file.getFileName(), false);

      if (self->context)
        self->context->appState.addRecentMidiFile(file.getFullPathName());

      if (self->playAfterLoad) {
        self->engine.resetTransportForLoop();
        self->engine.play();
        self->playAfterLoad = false;
      }

      self->prepareNextTrack();
    });
  });
}

void PlaybackController::unloadMidiFile() {
  engine.stop();
  hasFileLoaded = false;
  loadedFileBpm = 120.0;
  juce::MidiMessageSequence emptySeq;
  engine.setSequence(emptySeq, 960.0);

  if (trackGrid)
    trackGrid->setSequence(emptySeq);
  if (spliceEditor)
    spliceEditor->setNotes({});

  if (onLog)
    onLog("Unloaded track.", false);
}

void PlaybackController::pausePlayback() {
  if (engine.getIsPlaying()) {
    engine.pause();
    if (onLog)
      onLog("Transport: PAUSED", false);
  }
}

void PlaybackController::resumePlayback() {
  if (engine.getIsPaused()) {
    engine.resume();
    if (onLog)
      onLog("Transport: RESUMED", false);
  } else if (!engine.getIsPlaying()) {
    startPlayback();
  }
}

void PlaybackController::clearTrackAndGrids() {
  unloadMidiFile();
  engine.setBpm(120.0);
  if (onBpmUpdate)
    onBpmUpdate(120.0);
  if (sequencer)
    sequencer->clearAllSteps();
  if (spliceEditor) {
    spliceEditor->setNotes({});
    spliceEditor->deselectAll();
    spliceEditor->repaint();
  }
  if (onReset)
    onReset();
  if (onLog)
    onLog("Reset: Cleared track and grids.", false);
}

void PlaybackController::prepareNextTrack() {
  if (!playlist || playlist->playMode != MidiPlaylist::LoopAll)
    return;

  // 1. Calculate next safely on Message Thread (Fix G)
  int nextIdx = (playlist->currentIndex + 1) % playlist->files.size();
  juce::File nextFileToLoad(playlist->files[nextIdx]);

  if (!nextFileToLoad.existsAsFile())
    return;

  auto self = shared_from_this();
  juce::Thread::launch([self, nextFileToLoad] {
    if (!self)
      return;
    juce::MidiFile mf;
    juce::FileInputStream stream(nextFileToLoad);
    if (!stream.openedOk() || !mf.readFrom(stream))
      return;

    juce::MidiMessageSequence seq;
    for (int i = 0; i < mf.getNumTracks(); ++i)
      seq.addSequence(*mf.getTrack(i), 0);
    seq.updateMatchedPairs();

    double ppq = (mf.getTimeFormat() > 0) ? mf.getTimeFormat() : 960.0;

    self->engine.queueNextSequence(seq, ppq);
  });
}

void PlaybackController::handleIncomingMidiMessage(
    juce::MidiInput *source, const juce::MidiMessage &message) {
  juce::ignoreUnused(source, message);
  // No-op or handle external sync/transport control here
}
