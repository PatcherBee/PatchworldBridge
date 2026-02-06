#pragma once
#include "../Core/TimerHub.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * LatencyCalibrator: MIDI loopback round-trip measurement.
 *
 * Usage:
 *   1. Wire onSendPing to your MIDI output path.
 *   2. Call startMeasurement() to begin (sends N pings).
 *   3. Call receivePong() each time you detect the ping note coming back.
 *   4. After N samples, onResult fires with average RTT in ms.
 *
 * Ping message: Channel 16, Note 127, Velocity 1 (unlikely to conflict).
 *
 * Must be held in shared_ptr so delayed callbacks can safely check liveness.
 */
class LatencyCalibrator : public std::enable_shared_from_this<LatencyCalibrator> {
public:
  static constexpr int kPingChannel = 16;
  static constexpr int kPingNote = 127;
  static constexpr int kPingVelocity = 1;
  static constexpr int kDefaultSamples = 8;
  static constexpr double kTimeoutMs = 2000.0;

  std::function<void(const juce::MidiMessage &)> onSendPing;
  std::function<void(double avgMs)> onResult;

  ~LatencyCalibrator() { unsubscribeTimeout(); }

  void startMeasurement(int numSamples = kDefaultSamples) {
    samples.clear();
    targetSamples = numSamples;
    measuring = true;
    sendNextPing();
  }

  // Call this when you detect the ping note returning on input.
  // Returns true if this was a calibration ping (consume the message).
  bool receivePong(const juce::MidiMessage &m) {
    if (!measuring)
      return false;
    if (m.getChannel() != kPingChannel || m.getNoteNumber() != kPingNote)
      return false;

    unsubscribeTimeout();
    double rtt = juce::Time::getMillisecondCounterHiRes() - pingStartTime;
    samples.push_back(rtt);

    if ((int)samples.size() >= targetSamples) {
      measuring = false;
      double avg = 0.0;
      for (auto s : samples)
        avg += s;
      avg /= (double)samples.size();
      if (onResult)
        onResult(avg);
    } else {
      // Small delay before next ping; SafePointer-style: weak_ptr so callback is safe if calibrator is destroyed
      std::weak_ptr<LatencyCalibrator> wp = shared_from_this();
      juce::Timer::callAfterDelay(50, [wp] {
        if (auto p = wp.lock())
          p->sendNextPing();
      });
    }
    return true;
  }

  bool isMeasuring() const { return measuring; }

  // Check if a message is a calibration ping (for filtering on input)
  static bool isCalibrationPing(const juce::MidiMessage &m) {
    return m.isNoteOn() && m.getChannel() == kPingChannel &&
           m.getNoteNumber() == kPingNote &&
           m.getVelocity() == kPingVelocity;
  }

private:
  bool measuring = false;
  int targetSamples = kDefaultSamples;
  double pingStartTime = 0.0;
  std::vector<double> samples;

  void sendNextPing() {
    pingStartTime = juce::Time::getMillisecondCounterHiRes();
    if (onSendPing) {
      onSendPing(
          juce::MidiMessage::noteOn(kPingChannel, kPingNote, (juce::uint8)kPingVelocity));
    }
    subscribeTimeout();
  }

  void subscribeTimeout() {
    if (timeoutHubId.empty()) {
      timeoutHubId = "LatencyCalibrator_" + juce::Uuid().toDashedString().toStdString();
      TimerHub::instance().subscribe(timeoutHubId, [this] { onTimeout(); },
                                     TimerHub::Rate0_5Hz);
    }
  }

  void unsubscribeTimeout() {
    if (!timeoutHubId.empty()) {
      TimerHub::instance().unsubscribe(timeoutHubId);
      timeoutHubId.clear();
    }
  }

  void onTimeout() {
    unsubscribeTimeout();
    samples.push_back(kTimeoutMs); // Record as timeout
    if ((int)samples.size() >= targetSamples) {
      measuring = false;
      double avg = 0.0;
      for (auto s : samples)
        avg += s;
      avg /= (double)samples.size();
      if (onResult)
        onResult(avg);
    } else {
      sendNextPing();
    }
  }

  std::string timeoutHubId;
};
