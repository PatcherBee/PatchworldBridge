#pragma once
#include <juce_core/juce_core.h>

class BridgeListener {
public:
  virtual ~BridgeListener() = default;
  virtual void onLogMessage(const juce::String &msg, bool isError) {
    juce::ignoreUnused(msg, isError);
  }
  virtual void onCpuOverload() {}
  virtual void onConnectionLost() {}
};
