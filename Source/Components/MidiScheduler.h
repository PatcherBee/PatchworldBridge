/*
  ==============================================================================
    Source/Components/MidiScheduler.h
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <vector>


class MidiScheduler {
public:
  struct ScheduledNote {
    int channel;
    int note;
    double releaseTimeMs;
  };

  struct VirtualNote {
    int channel;
    int note;
    double releaseTime;
  };

  std::vector<ScheduledNote> scheduledNotes;
  std::vector<VirtualNote> activeVirtualNotes;
  juce::CriticalSection lock;

  // Schedule a note off event
  void scheduleNoteOff(int channel, int note, double releaseTimeMs) {
    juce::ScopedLock sl(lock);
    scheduledNotes.push_back({channel, note, releaseTimeMs});
  }

  // Schedule a virtual note (for internal synths or visualizations)
  void scheduleVirtualNote(int channel, int note, double releaseTime) {
    // No lock needed for virtual notes if only accessed from main/timer,
    // but safer to keep separate or locked if needed.
    // Assuming single threaded access mostly or simple timer.
    activeVirtualNotes.push_back({channel, note, releaseTime});
  }

  // Process notes that are due to turn off
  // Returns a list of notes to turn off so the MainComponent can handle the
  // actual MIDI/OSC commands
  std::vector<ScheduledNote> processDueNotes(double currentStatsMs) {
    std::vector<ScheduledNote> dueNotes;
    {
      juce::ScopedLock sl(lock);
      for (auto it = scheduledNotes.begin(); it != scheduledNotes.end();) {
        if (currentStatsMs >= it->releaseTimeMs) {
          dueNotes.push_back(*it);
          it = scheduledNotes.erase(it);
        } else {
          ++it;
        }
      }
    }
    return dueNotes;
  }

  // Process virtual notes (returns list)
  std::vector<VirtualNote> processDueVirtualNotes(double currentStatsMs) {
    std::vector<VirtualNote> dueNotes;
    for (auto it = activeVirtualNotes.begin();
         it != activeVirtualNotes.end();) {
      if (currentStatsMs >= it->releaseTime) {
        dueNotes.push_back(*it);
        it = activeVirtualNotes.erase(it);
      } else {
        ++it;
      }
    }
    return dueNotes;
  }

  void clear() {
    juce::ScopedLock sl(lock);
    scheduledNotes.clear();
    activeVirtualNotes.clear();
  }
};
