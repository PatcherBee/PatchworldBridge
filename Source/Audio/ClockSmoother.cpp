#include "../Audio/ClockSmoother.h"

ClockSmoother::ClockSmoother() { reset(); }

void ClockSmoother::reset() {
  history.fill(0.0);
  writeIndex = 0;
  count = 0;
  runningSum = 0.0;
  variance = 0.0;
  stableBpm = 120.0;
  lastTickTime = 0.0;
  isLocked = false;
  outlierCount = 0;
  latestJitter = 0.0;
}

bool ClockSmoother::onMidiClockByte(double timestampMs) {
  double now = timestampMs > 0.0 ? timestampMs : juce::Time::getMillisecondCounterHiRes();

  if (lastTickTime > 0.0) {
    double interval = now - lastTickTime;

    // Sanity check - reject invalid intervals
    if (interval < 1.0 || interval > 500.0) {
      lastTickTime = now;
      return false;
    }

    // Outlier rejection (after we have baseline)
    if (count >= 24) {
      double avgInterval = runningSum / std::min(count, HISTORY_SIZE);
      double stdDev = std::sqrt(variance);

      if (std::abs(interval - avgInterval) > stdDev * 2.5) {
        outlierCount++;
        if (outlierCount > 6) {
          reset();
          lastTickTime = now;
          return false;
        }
        lastTickTime = now;
        return false;
      }
      outlierCount = 0;
    }

    // Update circular buffer
    int idx = writeIndex;
    double oldInterval = history[idx];
    history[idx] = interval;
    writeIndex = (writeIndex + 1) % HISTORY_SIZE;

    if (count >= HISTORY_SIZE) {
      runningSum -= oldInterval;
    }
    runningSum += interval;

    // Update variance (Welford's online algorithm)
    if (count > 0) {
      double mean = runningSum / std::min(count + 1, HISTORY_SIZE);
      double delta = interval - mean;
      variance = variance * 0.95 + delta * delta * 0.05;
    }

    if (count < HISTORY_SIZE)
      count++;

    if (count >= 24) {
      calculateBpm();
    }
  }

  lastTickTime = now;
  return true;
}

double ClockSmoother::getConfidence() const {
  if (!isLocked)
    return 0.0;
  double jitter = latestJitter;
  return juce::jlimit(0.0, 1.0, 1.0 - (jitter / 10.0));
}

void ClockSmoother::calculateBpm() {
  int samples = std::min(count, HISTORY_SIZE);
  double avgInterval = runningSum / samples;

  double rawBpm = 60000.0 / (avgInterval * 24.0);
  rawBpm = juce::jlimit(MIN_BPM, MAX_BPM, rawBpm);

  latestJitter = std::sqrt(variance);
  isLocked = (count >= 24) && (latestJitter < 12.0);

  // Heavier smoothing: run with safest BPM, only accept MIDI clock when stable
  const double bpmHysteresis = 1.5;  // Ignore tiny changes (was 0.5)
  double delta = rawBpm - stableBpm;
  if (std::abs(delta) < bpmHysteresis)
    delta = 0.0;
  // Cap delta when jitter is high so single bad intervals don't move BPM much (EXT clock smoothing)
  if (latestJitter > 5.0)
    delta = juce::jlimit(-2.0, 2.0, delta);
  else if (latestJitter > 3.0)
    delta = juce::jlimit(-3.0, 3.0, delta);
  // Alpha: slower when jittery - prefer app timer/Link until EXT is stable
  double alpha = (latestJitter > 5.0) ? 0.02 : (latestJitter > 3.0) ? 0.03 : 0.04;
  stableBpm = stableBpm + delta * alpha;
}
