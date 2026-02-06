/*
  ==============================================================================
    DebugLog.h
    Writes startup/crash debug lines to Desktop/PatchworldBridge_debug.log
    (for terminal-only debugging when the app crashes before the UI is ready)
  ==============================================================================
*/

#pragma once

#include <fstream>
#include <mutex>
#include <sstream>
#include <juce_core/juce_core.h>

namespace DebugLog {

inline std::mutex& logMutex() {
  static std::mutex m;
  return m;
}

inline std::ostream* logStream() {
  static std::ofstream* stream = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    juce::File desktop = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
    juce::File logFile = desktop.getChildFile("PatchworldBridge_debug.log");
    stream = new std::ofstream(logFile.getFullPathName().toStdString(), std::ios::app);
    if (stream->is_open()) {
      *stream << "\n--- session " << juce::Time::getMillisecondCounter() << " ---\n";
      stream->flush();
    } else {
      delete stream;
      stream = nullptr;
    }
  }
  return stream;
}

/** Write a line to Desktop/PatchworldBridge_debug.log (timestamp + msg), then flush. Safe to call from any thread. */
inline void debugLog(const char* msg) {
  std::lock_guard<std::mutex> lock(logMutex());
  std::ostream* s = logStream();
  if (s && s->good()) {
    *s << juce::Time::getCurrentTime().formatted("%H:%M:%S.").toStdString()
       << juce::String(juce::Time::getMillisecondCounter() % 1000).paddedLeft('0', 3).toStdString()
       << " " << msg << "\n";
    s->flush();
  }
}

inline void debugLog(const juce::String& msg) {
  debugLog(msg.toRawUTF8());
}

} // namespace DebugLog
