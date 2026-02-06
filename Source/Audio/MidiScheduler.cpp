#include "MidiScheduler.h"
#include "../Core/PlatformGuard.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

MidiScheduler::MidiScheduler() {
  for (auto &slot : pool)
    slot.isActive = false;
  currentBeat = 0.0;
  lastProcessedBeat = 0.0;
}

void MidiScheduler::scheduleNoteOff(int ch, int note, double beat) {
  BridgeEvent e;
  e.type = EventType::NoteOff;
  e.channel = static_cast<juce::uint8>(ch);
  e.noteOrCC = static_cast<juce::uint8>(note);
  e.source = EventSource::EngineSequencer;
  scheduleEvent(e, beat);
}

void MidiScheduler::scheduleEvent(const BridgeEvent &e, double beat) {
  int s1, n1, s2, n2;
  commandFifo.prepareToWrite(1, s1, n1, s2, n2);
  if (n1 > 0) {
    commandBuffer[s1].cmd = Command::Schedule;
    commandBuffer[s1].event = e;
    commandBuffer[s1].beat = beat;
    commandFifo.finishedWrite(1);
  }
  // If queue full, drop (producer never blocks)
}

void MidiScheduler::clear() {
  int s1, n1, s2, n2;
  commandFifo.prepareToWrite(1, s1, n1, s2, n2);
  if (n1 > 0) {
    commandBuffer[s1].cmd = Command::Clear;
    commandFifo.finishedWrite(1);
  }
}

void MidiScheduler::allNotesOff() {
  int s1, n1, s2, n2;
  commandFifo.prepareToWrite(1, s1, n1, s2, n2);
  if (n1 > 0) {
    commandBuffer[s1].cmd = Command::AllNotesOff;
    commandFifo.finishedWrite(1);
  }
}

void MidiScheduler::drainCommandQueue() {
  int ready = commandFifo.getNumReady();
  if (ready <= 0)
    return;
  int s1, n1, s2, n2;
  commandFifo.prepareToRead(ready, s1, n1, s2, n2);
  auto apply = [this](CommandItem &item) {
    if (item.cmd == Command::Clear) {
      for (auto &slot : pool)
        slot.isActive = false;
      currentBeat = 0.0;
      lastProcessedBeat = 0.0;
    } else if (item.cmd == Command::AllNotesOff) {
      for (auto &slot : pool)
        slot.isActive = false;
    } else if (item.cmd == Command::Schedule) {
      for (auto &slot : pool) {
        if (!slot.isActive) {
          slot.event = item.event;
          slot.scheduledBeat = item.beat;
          slot.isActive = true;
          return;
        }
      }
      double oldest = 1e18;
      int oldestIdx = 0;
      for (int idx = 0; idx < MAX_POOL_SIZE; ++idx) {
        if (pool[idx].scheduledBeat < oldest) {
          oldest = pool[idx].scheduledBeat;
          oldestIdx = idx;
        }
      }
      pool[oldestIdx].event = item.event;
      pool[oldestIdx].scheduledBeat = item.beat;
      pool[oldestIdx].isActive = true;
    }
  };
  for (int i = 0; i < n1; ++i)
    apply(commandBuffer[s1 + i]);
  for (int i = 0; i < n2; ++i)
    apply(commandBuffer[s2 + i]);
  commandFifo.finishedRead(n1 + n2);
}

void MidiScheduler::processBlock(juce::MidiBuffer &outputBuffer, int numSamples,
                                 double bpm, double sampleRate) {
  juce::ScopedNoDenormals noDenormals;
  drainCommandQueue();

  struct DueEvent {
    int sampleOffset = 0;
    BridgeEvent event{};
  };
  std::array<DueEvent, MAX_DUE_PER_BLOCK> dueEvents;
  int dueCount = 0;

  double startBeat = lastProcessedBeat;
  double endBeat =
      startBeat + ((bpm / 60.0) / sampleRate) * (double)numSamples;
  bool isJump = std::abs(endBeat - startBeat) > 1.0;
  const double epsilon = 0.0001;
  const double secondsPerBeat = 60.0 / bpm;

  for (auto &slot : pool) {
    if (dueCount >= MAX_DUE_PER_BLOCK)
      break;
    if (!slot.isActive)
      continue;
    if ((slot.scheduledBeat >= startBeat - epsilon &&
         slot.scheduledBeat < endBeat - epsilon) ||
        (isJump && slot.scheduledBeat < endBeat - epsilon)) {
      double beatDelta = slot.scheduledBeat - startBeat;
      int sampleOffset = (int)((beatDelta * secondsPerBeat) * sampleRate);
      sampleOffset = juce::jlimit(0, numSamples - 1, sampleOffset);
      dueEvents[dueCount].sampleOffset = sampleOffset;
      dueEvents[dueCount].event = slot.event;
      dueCount++;
      slot.isActive = false;
    }
  }
  lastProcessedBeat = endBeat;
  currentBeat = endBeat;

  for (int i = 0; i < dueCount; ++i) {
    const auto &e = dueEvents[i].event;
    int sampleOffset = dueEvents[i].sampleOffset;
    juce::MidiMessage m;
    if (e.type == EventType::NoteOn)
      m = juce::MidiMessage::noteOn(e.channel, (int)e.noteOrCC, e.value);
    else if (e.type == EventType::NoteOff)
      m = juce::MidiMessage::noteOff(e.channel, (int)e.noteOrCC);
    else if (e.type == EventType::ControlChange)
      m = juce::MidiMessage::controllerEvent(e.channel, (int)e.noteOrCC,
                                             (int)(e.value * 127.0f));
    if (m.getRawDataSize() > 0)
      outputBuffer.addEvent(m, sampleOffset);
  }
}

void MidiScheduler::processDueEvents(
    double beat, std::function<void(const BridgeEvent &)> callback) {
  drainCommandQueue();
  for (auto &slot : pool) {
    if (slot.isActive && slot.scheduledBeat <= beat) {
      callback(slot.event);
      slot.isActive = false;
    }
  }
}