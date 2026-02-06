#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

struct EditableNote {
  int channel = 1;
  int noteNumber = 60;
  float velocity = 0.8f;
  double startBeat = 0.0;
  double durationBeats = 1.0;
  bool isSelected = false;

  // Per-note expression (humanization, curves)
  float velocityCurve = 1.0f;     // 0.5=soft, 1.0=linear, 2.0=hard
  float timingOffsetMs = 0.0f;    // Humanize timing (-50 to +50ms)
  float pitchBend = 0.0f;         // Per-note pitch (-1 to +1 semitones)
  int articulationType = 0;       // 0=normal, 1=staccato, 2=legato, 3=accent

  // Helper for End Beat
  double getEndBeat() const { return startBeat + durationBeats; }

  // Overlap Check
  bool overlaps(double beat) const {
    return beat >= startBeat && beat < (startBeat + durationBeats);
  }

  // Apply velocity curve (0-1 in, returns 0-1 out)
  static float applyVelocityCurve(float velocity, float curve) {
    if (curve == 1.0f || velocity <= 0.0f)
      return velocity;
    if (velocity >= 1.0f)
      return 1.0f;
    return std::pow(velocity, 1.0f / curve);
  }
};
