/*
  ==============================================================================
    Source/Core/FlightRecorder.h
    Ring buffer for crash diagnostics. Logs kept in RAM, written on exit.
  ==============================================================================
*/
#pragma once
#include <array>
#include <cstdint>
#include <juce_core/juce_core.h>
#include <string>

struct FlightRecorder {
  static constexpr int Capacity = 100;

  void log(const char *msg) {
    int idx = writeIndex.fetch_add(1, std::memory_order_relaxed) % Capacity;
    auto &entry = entries[idx];
    entry.sequence = writeIndex.load(std::memory_order_relaxed);
    juce::zeromem(entry.text, sizeof(entry.text));
    juce::String(msg).copyToUTF8(entry.text, sizeof(entry.text) - 1);
    entry.text[sizeof(entry.text) - 1] = '\0';
  }

  void log(const juce::String &msg) { log(msg.toRawUTF8()); }

  void flushToFile() {
    juce::File file = juce::File::getSpecialLocation(
                          juce::File::userApplicationDataDirectory)
                          .getChildFile("PatchworldBridge")
                          .getChildFile("_flight_recorder.txt");
    if (!file.getParentDirectory().exists())
      file.getParentDirectory().createDirectory();

    juce::FileOutputStream stream(file);
    if (stream.openedOk()) {
      int start = writeIndex.load(std::memory_order_relaxed) - Capacity;
      if (start < 0)
        start = 0;
      for (int i = 0; i < Capacity; ++i) {
        int idx = (start + i) % Capacity;
        if (entries[idx].text[0] != '\0') {
          stream.writeText(juce::String(entries[idx].sequence) + ": " +
                               juce::String(entries[idx].text) + "\n",
                           false, false, nullptr);
        }
      }
    }
  }

  struct Entry {
    std::int64_t sequence = 0;
    char text[256] = {};
  };
  std::array<Entry, Capacity> entries;
  std::atomic<int> writeIndex{0};
};
