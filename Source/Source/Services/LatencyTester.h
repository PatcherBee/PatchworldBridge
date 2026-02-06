/*
  ==============================================================================
    Source/Engine/LatencyTester.h
    Role: Simple RTT (Round Trip Time) measurement tool
  ==============================================================================
*/
#pragma once
#include <juce_core/juce_core.h>

class LatencyTester {
public:
  void sendPing() {
    start = juce::Time::getMillisecondCounterHiRes();
    // Send a ping message (caller handles actual send)
  }

  double receivePong() {
    double end = juce::Time::getMillisecondCounterHiRes();
    return end - start;
  }

private:
  double start = 0.0;
};
