/*
  ==============================================================================
    Source/Tests/ClockSmootherTest.h
    Role: Unit tests for ClockSmoother (MIDI clock → BPM, lock, jitter).
  ==============================================================================
*/
#pragma once
#include "../Audio/ClockSmoother.h"
#include <cmath>
#include <cstring>

struct ClockSmootherTest {
  /** Feed 48 clock ticks at 120 BPM (500ms per 24 clocks => ~20.83ms per tick). Returns true if BPM and lock are sane. */
  static bool run() {
    ClockSmoother smoother;
    smoother.reset();

    // 120 BPM => 24 pulses per quarter => 1 pulse every (60/120)/24 = 0.5/24 ≈ 20.833 ms
    const double msPerTick = (60.0 * 1000.0) / (120.0 * 24.0); // ~20.833 ms
    double t = 1000.0;
    for (int i = 0; i < 52; ++i) {
      smoother.onMidiClockByte(t);
      t += msPerTick;
    }

    double bpm = smoother.getBpm();
    bool locked = smoother.getIsLocked();
    // Allow some tolerance (smoothing may not land exactly 120)
    bool bpmOk = bpm >= 115.0 && bpm <= 125.0;
    return locked && bpmOk;
  }

  /** Reset and sanity: after reset, not locked. */
  static bool runReset() {
    ClockSmoother smoother;
    smoother.reset();
    if (smoother.getIsLocked()) return false;
    smoother.onMidiClockByte(1000.0);
    smoother.onMidiClockByte(1020.0);
    smoother.reset();
    return !smoother.getIsLocked();
  }
};
