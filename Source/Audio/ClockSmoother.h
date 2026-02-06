#pragma once

#include <array>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

class ClockSmoother {
public:
  static constexpr int HISTORY_SIZE = 48;
  static constexpr double MIN_BPM = 20.0;
  static constexpr double MAX_BPM = 300.0;

  double stableBpm = 120.0;
  bool isLocked = false;
  double latestJitter = 0.0;

  ClockSmoother();
  void reset();
  bool onMidiClockByte(double timestampMs);

  double getBpm() const { return stableBpm; }
  bool getIsLocked() const { return isLocked; }
  double getJitterMs() const { return latestJitter; }
  double getLastPulseTime() const { return lastTickTime; }
  double getConfidence() const;

private:
  void calculateBpm();

  std::array<double, HISTORY_SIZE> history{};
  int writeIndex = 0;
  int count = 0;
  double runningSum = 0.0;
  double variance = 0.0;
  double lastTickTime = 0.0;
  int outlierCount = 0;
};
