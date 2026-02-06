#pragma once
#include <algorithm>
#include <cmath>
#include <juce_core/juce_core.h>

/**
 * PRO: Smooth parameter transitions
 */
class ParameterSmoother {
public:
  float process(float target) {
    currentValue = currentValue + 0.1f * (target - currentValue);
    return (std::abs(currentValue - target) < 0.0001f) ? target : currentValue;
  }

private:
  float currentValue = 0.0f;
};

/**
 * Common.h - Compatibility Bridge
 *
 * This file has been restored to provide a central anchor for project-wide
 * types. Core definitions have been moved to Engine-level headers to solve
 * circular dependencies, but this header ensures existing code remains
 * functional.
 */

#include "../Core/AppState.h"
// #include "Diagnostics.h" // Removed: File missing and unused
#include "../Audio/NoteTracker.h"
#include "../Audio/OscTypes.h"
#include "../UI/Theme.h"
