/*
  ==============================================================================
    Source/Tests/MidiMappingCurveTest.h
    Role: Unit tests for MidiMappingService::applyCurve (Linear, Log, Exp, S_Curve).
  ==============================================================================
*/
#pragma once
#include "../Services/MidiMappingService.h"
#include <cmath>
#include <cstring>

struct MidiMappingCurveTest {
  static bool run() {
    using Curve = MappingEntry::Curve;

    // Linear: 0→0, 1→1, 0.5→0.5
    if (std::abs(MidiMappingService::applyCurve(0.0f, Curve::Linear)) > 1e-5f) return false;
    if (std::abs(MidiMappingService::applyCurve(1.0f, Curve::Linear) - 1.0f) > 1e-5f) return false;
    if (std::abs(MidiMappingService::applyCurve(0.5f, Curve::Linear) - 0.5f) > 1e-5f) return false;

    // Clamp: values outside [0,1] clamped
    if (MidiMappingService::applyCurve(-0.1f, Curve::Linear) != 0.0f) return false;
    if (MidiMappingService::applyCurve(1.5f, Curve::Linear) != 1.0f) return false;

    // Log: 0→0, 1→1 (log10(10)=1), monotonic
    float log0 = MidiMappingService::applyCurve(0.0f, Curve::Log);
    float log1 = MidiMappingService::applyCurve(1.0f, Curve::Log);
    if (std::abs(log0) > 1e-5f || std::abs(log1 - 1.0f) > 1e-5f) return false;
    float log25 = MidiMappingService::applyCurve(0.25f, Curve::Log);
    float log50 = MidiMappingService::applyCurve(0.5f, Curve::Log);
    float log75 = MidiMappingService::applyCurve(0.75f, Curve::Log);
    if (log25 <= 0.0f || log75 >= 1.0f) return false;
    if (log25 >= log50 || log50 >= log75) return false; // monotonic

    // Exp: 0→0, 1→1, 0.5→0.25
    if (std::abs(MidiMappingService::applyCurve(0.0f, Curve::Exp)) > 1e-5f) return false;
    if (std::abs(MidiMappingService::applyCurve(1.0f, Curve::Exp) - 1.0f) > 1e-5f) return false;
    if (std::abs(MidiMappingService::applyCurve(0.5f, Curve::Exp) - 0.25f) > 1e-5f) return false;

    // S_Curve: 0→0, 1→1, 0.5→0.5 (symmetric)
    if (std::abs(MidiMappingService::applyCurve(0.0f, Curve::S_Curve)) > 1e-5f) return false;
    if (std::abs(MidiMappingService::applyCurve(1.0f, Curve::S_Curve) - 1.0f) > 1e-5f) return false;
    if (std::abs(MidiMappingService::applyCurve(0.5f, Curve::S_Curve) - 0.5f) > 1e-4f) return false;

    return true;
  }
};
