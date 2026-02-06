/*
  ==============================================================================
    Source/Core/ConfigManager.h
    Role: Central config access over ValueTree (single place for get/set/listener).
    Wraps BridgeContext::appState / ValueTree for consistent API.
  ==============================================================================
*/
#pragma once

#include <functional>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

class ConfigManager : public juce::ValueTree::Listener {
public:
  explicit ConfigManager(juce::ValueTree& tree) : configTree(tree) {
    configTree.addListener(this);
  }

  ~ConfigManager() override {
    if (configTree.isValid())
      configTree.removeListener(this);
  }

  void valueTreePropertyChanged(juce::ValueTree& tree,
                               const juce::Identifier& id) override {
    juce::ignoreUnused(tree);
    auto it = listeners.find(id);
    if (it != listeners.end() && it->second)
      it->second(configTree.getProperty(id));
  }

  template<typename T>
  T get(const juce::Identifier& key, T defaultVal) const {
    if (!configTree.isValid())
      return defaultVal; // Safe fallback if tree was cleared/corrupt
    auto v = configTree.getProperty(key);
    if (v.isVoid()) return defaultVal;
    if constexpr (std::is_same_v<T, int>)
      return static_cast<int>(v);
    else if constexpr (std::is_same_v<T, double>)
      return static_cast<double>(v);
    else if constexpr (std::is_same_v<T, bool>)
      return static_cast<bool>(v);
    else if constexpr (std::is_same_v<T, juce::String>)
      return v.toString();
    return defaultVal;
  }

  template<typename T>
  void set(const juce::Identifier& key, T value) {
    configTree.setProperty(key, value, nullptr);
  }

  void addListener(const juce::Identifier& key,
                  std::function<void(const juce::var&)> onChange) {
    listeners[key] = std::move(onChange);
  }

  void removeListener(const juce::Identifier& key) {
    listeners.erase(key);
  }

  juce::ValueTree& getTree() { return configTree; }
  const juce::ValueTree& getTree() const { return configTree; }

private:
  juce::ValueTree& configTree;
  std::map<juce::Identifier, std::function<void(const juce::var&)>> listeners;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConfigManager)
};
