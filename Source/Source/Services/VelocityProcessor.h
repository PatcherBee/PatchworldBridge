/*
  ==============================================================================
    Source/Services/VelocityProcessor.h
    Velocity curve processing for MIDI/OSC.
  ==============================================================================
*/
#pragma once
#include <array>
#include <cmath>
#include <juce_core/juce_core.h>

class VelocityProcessor {
public:
  enum class Curve { Linear, Soft, Hard, SCurve, Fixed, Custom };

  float process(float velocity) const {
    velocity = juce::jlimit(0.0f, 1.0f, velocity);

    switch (curve) {
    case Curve::Soft:
      return std::sqrt(velocity);
    case Curve::Hard:
      return velocity * velocity;
    case Curve::SCurve: {
      float x = velocity - 0.5f;
      return 0.5f + 0.5f * (float)std::tanh((double)(x * 4.0f));
    }
    case Curve::Fixed:
      return fixedVelocity;
    case Curve::Custom:
      return applyCustomCurve(velocity);
    default:
      return velocity;
    }
  }

  void setCurve(Curve c) { curve = c; }
  void setFixedVelocity(float v) { fixedVelocity = v; }
  void setCustomCurve(const std::array<float, 128> &table) {
    customCurve = table;
  }

private:
  Curve curve = Curve::Linear;
  float fixedVelocity = 0.8f;
  std::array<float, 128> customCurve{};

  float applyCustomCurve(float velocity) const {
    int idx = (int)(velocity * 127.0f);
    return customCurve[juce::jlimit(0, 127, idx)];
  }
};
