/*
  ==============================================================================
    Source/Core/CrashRecovery.h
    Role: Saves a recovery sentinel periodically; cleared on clean shutdown.
          Use with hasCrashedLastSession() to offer restore after crash.
  ==============================================================================
*/
#pragma once

#include <juce_core/juce_core.h>

class CrashRecovery {
public:
  static juce::File getRecoveryFile() {
    return juce::File::getSpecialLocation(
               juce::File::tempDirectory)
        .getChildFile("PatchworldBridge_recovery");
  }

  /** Call periodically (e.g. every 60s) to mark "last known good" point. */
  static void saveRecoveryPoint() {
    getRecoveryFile().replaceWithText(
        juce::String(juce::Time::getMillisecondCounter()));
  }

  /** Call on normal application shutdown so next startup knows we exited cleanly. */
  static void clearRecoveryPoint() {
    getRecoveryFile().deleteFile();
  }

  /** True if recovery file exists (e.g. app did not shut down cleanly). */
  static bool hasRecoveryData() { return getRecoveryFile().existsAsFile(); }
};
