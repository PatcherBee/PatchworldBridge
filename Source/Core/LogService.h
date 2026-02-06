/*
  ==============================================================================
    Source/Core/LogService.h
    Role: Unified logging (consolidates writeDebugLog, onLog callbacks) (10.2).
  ==============================================================================
*/
#pragma once

#include <functional>
#include <iostream>
#include <juce_core/juce_core.h>

class LogService {
public:
  enum class Level { Debug, Info, Warning, Error };

  static LogService &instance() {
    static LogService svc;
    return svc;
  }

  void log(const juce::String &msg, Level level = Level::Info) {
    // Console output
    juce::Logger::outputDebugString(msg);
    std::cout << levelPrefix(level) << msg << std::endl;

    // Callback for UI
    if (onLogEntry)
      onLogEntry(msg, level == Level::Error || level == Level::Warning);
  }

  void debug(const juce::String &msg) { log(msg, Level::Debug); }
  void info(const juce::String &msg) { log(msg, Level::Info); }
  void warning(const juce::String &msg) { log(msg, Level::Warning); }
  void error(const juce::String &msg) { log(msg, Level::Error); }

  // Callback for UI log panels
  std::function<void(const juce::String &, bool isError)> onLogEntry;

private:
  LogService() = default;

  juce::String levelPrefix(Level level) const {
    switch (level) {
    case Level::Debug:
      return "[DEBUG] ";
    case Level::Info:
      return "[INFO] ";
    case Level::Warning:
      return "[WARN] ";
    case Level::Error:
      return "[ERROR] ";
    default:
      return "";
    }
  }
};

/** Convenience: write to LogService for debug/user feedback. */
inline void writeDebugLog(const juce::String &msg) {
  LogService::instance().info(msg);
}
