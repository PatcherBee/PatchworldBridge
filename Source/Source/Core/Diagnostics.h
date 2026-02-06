#pragma once
#include <atomic>
#include <juce_core/juce_core.h>

struct DiagnosticData {
  std::atomic<float> audioLoad{0.0f};
  std::atomic<float> fps{0.0f};
  std::atomic<double> cpuUsage{0.0};
  std::atomic<int> oscPacketsPerSec{0};
  std::atomic<double> midiJitterMs{0.0};
  std::atomic<int> numVoices{0};
  std::atomic<int> activeVoices{0};
  juce::String state = "Running";
};
