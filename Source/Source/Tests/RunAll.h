/*
  ==============================================================================
    Source/Tests/RunAll.h
    Role: Run all unit/stress tests. Invoke from app with --run-tests.
  ==============================================================================
*/
#pragma once
#include "AirlockTest.h"
#include "ClockSmootherTest.h"
#include "MidiMappingCurveTest.h"
#include <juce_core/juce_core.h>

struct RunAllTests {
  /** Run all tests. Returns true if all pass. Logs to juce::Logger. */
  static bool run() {
    juce::Logger::writeToLog("Running unit tests...");
    int failed = 0;

    juce::Logger::writeToLog("  AirlockTest::runStressTest");
    try {
      AirlockTest::runStressTest();
      juce::Logger::writeToLog("    OK");
    } catch (const std::exception &e) {
      juce::Logger::writeToLog("    FAIL: " + juce::String(e.what()));
      failed++;
    } catch (...) {
      juce::Logger::writeToLog("    FAIL: unknown exception");
      failed++;
    }

    juce::Logger::writeToLog("  ClockSmootherTest::run");
    try {
      if (!ClockSmootherTest::run()) {
        juce::Logger::writeToLog("    FAIL: BPM/lock assertion");
        failed++;
      } else
        juce::Logger::writeToLog("    OK");
    } catch (const std::exception &e) {
      juce::Logger::writeToLog("    FAIL: " + juce::String(e.what()));
      failed++;
    } catch (...) {
      juce::Logger::writeToLog("    FAIL: unknown exception");
      failed++;
    }

    juce::Logger::writeToLog("  ClockSmootherTest::runReset");
    try {
      if (!ClockSmootherTest::runReset()) {
        juce::Logger::writeToLog("    FAIL: reset assertion");
        failed++;
      } else
        juce::Logger::writeToLog("    OK");
    } catch (const std::exception &e) {
      juce::Logger::writeToLog("    FAIL: " + juce::String(e.what()));
      failed++;
    } catch (...) {
      juce::Logger::writeToLog("    FAIL: unknown exception");
      failed++;
    }

    juce::Logger::writeToLog("  MidiMappingCurveTest::run");
    try {
      if (!MidiMappingCurveTest::run()) {
        juce::Logger::writeToLog("    FAIL: curve assertion");
        failed++;
      } else
        juce::Logger::writeToLog("    OK");
    } catch (const std::exception &e) {
      juce::Logger::writeToLog("    FAIL: " + juce::String(e.what()));
      failed++;
    } catch (...) {
      juce::Logger::writeToLog("    FAIL: unknown exception");
      failed++;
    }

    if (failed == 0)
      juce::Logger::writeToLog("All tests passed.");
    else
      juce::Logger::writeToLog("FAILED: " + juce::String(failed) + " test(s).");
    return failed == 0;
  }
};
