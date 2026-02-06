#pragma once
#include <array>
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "OscTypes.h" // For BridgeEvent

class MidiScheduler {
public:
  struct ScheduledEvent {
    BridgeEvent event;
    double scheduledBeat = 0.0;
    bool isActive = false;
  };

  static constexpr int MAX_POOL_SIZE = 1024;
  static constexpr int MAX_DUE_PER_BLOCK = 128;
  static constexpr int COMMAND_QUEUE_CAPACITY = 512;

  MidiScheduler();

  // Producer (network / message thread): lock-free push. No lock held.
  void scheduleNoteOff(int ch, int note, double beat);
  void scheduleEvent(const BridgeEvent &e, double beat);
  void clear();
  void allNotesOff();

  // Consumer (audio thread only): processBlock drains command queue then pool.
  void processBlock(juce::MidiBuffer &outputBuffer, int numSamples, double bpm,
                    double sampleRate);

  void processDueEvents(double beat,
                        std::function<void(const BridgeEvent &)> callback);

  void forceResetTime() { lastProcessedBeat = -1.0; }

private:
  enum class Command : juce::uint8 { Schedule, Clear, AllNotesOff };
  struct CommandItem {
    Command cmd = Command::Schedule;
    BridgeEvent event{};
    double beat = 0.0;
  };

  void drainCommandQueue();

  std::array<ScheduledEvent, MAX_POOL_SIZE> pool;
  double currentBeat = 0.0;
  double lastProcessedBeat = 0.0;

  juce::AbstractFifo commandFifo{COMMAND_QUEUE_CAPACITY};
  std::array<CommandItem, COMMAND_QUEUE_CAPACITY> commandBuffer;
};