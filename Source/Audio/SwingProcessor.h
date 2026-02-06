#pragma once
#include <juce_core/juce_core.h>

class SwingProcessor {
public:
  void setSwingAmount(float amount0to1) {
    swingFactor = juce::jlimit(0.0f, 1.0f, amount0to1);
  }

  // Apply swing to a Beat position (e.g. 1.0, 1.25, 1.50, 1.75)
  // stepDuration = 0.25 for 16th notes
  double applySwing(int stepIndex, double straightBeat, double stepDuration) {
    if (swingFactor < 0.01f)
      return straightBeat;

    // If we are on an off-beat (1.25, 1.75, etc aka odd 16th steps)
    if (stepIndex % 2 != 0) {
      // Push it forward by percentage of the step duration
      // Max swing (1.0) pushes it 2/3rds of the way (triplet feel) or 50% (hard
      // swing) Let's use standard MPC style: 0.5 swing = 50% (straight), 0.66 =
      // triplet But your UI uses 0.0 to 1.0 add-on.

      double offset =
          stepDuration * (swingFactor * 0.33f); // Max swing = triplet feel
      return straightBeat + offset;
    }
    return straightBeat;
  }

  float getSwingAmount() const { return swingFactor; }

private:
  float swingFactor = 0.0f;
};
