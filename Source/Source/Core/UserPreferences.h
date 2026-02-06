/*
  ==============================================================================
    Source/Core/UserPreferences.h
    Role: User preferences system skeleton (roadmap 14.1)
  ==============================================================================
*/
#pragma once

#include <juce_data_structures/juce_data_structures.h>

class UserPreferences {
public:
  static UserPreferences &instance() {
    static UserPreferences prefs;
    return prefs;
  }

  // --- Audio ---
  int getBufferSize() const { return get("audio.bufferSize", 512); }
  void setBufferSize(int size) { set("audio.bufferSize", size); }

  int getSampleRate() const { return get("audio.sampleRate", 48000); }
  void setSampleRate(int rate) { set("audio.sampleRate", rate); }

  // --- UI ---
  bool getShowVelocityLane() const { return get("ui.showVelocityLane", true); }
  void setShowVelocityLane(bool show) { set("ui.showVelocityLane", show); }

  bool getSnapToGrid() const { return get("ui.snapToGrid", true); }
  void setSnapToGrid(bool snap) { set("ui.snapToGrid", snap); }

  double getDefaultNoteLength() const { return get("ui.defaultNoteLength", 0.25); }
  void setDefaultNoteLength(double len) { set("ui.defaultNoteLength", len); }

  int getDefaultVelocity() const { return get("ui.defaultVelocity", 100); }
  void setDefaultVelocity(int vel) { set("ui.defaultVelocity", vel); }

  int getThemeId() const { return get("ui.themeId", 1); }
  void setThemeId(int id) { set("ui.themeId", id); }

  float getUiScale() const { return (float)get("ui.scale", 1.0); }
  void setUiScale(float scale) { set("ui.scale", (double)scale); }

  // --- Performance ---
  bool getGpuAcceleration() const { return get("perf.gpuAcceleration", true); }
  void setGpuAcceleration(bool enable) { set("perf.gpuAcceleration", enable); }

  int getTargetFrameRate() const { return get("perf.targetFps", 60); }
  void setTargetFrameRate(int fps) { set("perf.targetFps", fps); }

  bool getLowLatencyMode() const { return get("perf.lowLatency", false); }
  void setLowLatencyMode(bool enable) { set("perf.lowLatency", enable); }

  // --- MIDI ---
  bool getMidiThru() const { return get("midi.thru", true); }
  void setMidiThru(bool enable) { set("midi.thru", enable); }

  int getTranspose() const { return get("midi.transpose", 0); }
  void setTranspose(int semitones) { set("midi.transpose", semitones); }

  // --- Network ---
  int getOscPortIn() const { return get("network.oscPortIn", 9000); }
  void setOscPortIn(int port) { set("network.oscPortIn", port); }

  int getOscPortOut() const { return get("network.oscPortOut", 8000); }
  void setOscPortOut(int port) { set("network.oscPortOut", port); }

  // --- Persistence ---
  void load(juce::PropertiesFile *props) {
    if (props) {
      propsFile = props;
    }
  }

  void save() {
    if (propsFile)
      propsFile->saveIfNeeded();
  }

private:
  UserPreferences() = default;

  juce::PropertiesFile *propsFile = nullptr;

  template <typename T>
  T get(const juce::String &key, T defaultValue) const {
    if (propsFile)
      return static_cast<T>(propsFile->getDoubleValue(key, static_cast<double>(defaultValue)));
    return defaultValue;
  }

  template <>
  bool get(const juce::String &key, bool defaultValue) const {
    if (propsFile)
      return propsFile->getBoolValue(key, defaultValue);
    return defaultValue;
  }

  template <typename T>
  void set(const juce::String &key, T value) {
    if (propsFile)
      propsFile->setValue(key, static_cast<double>(value));
  }

  template <>
  void set(const juce::String &key, bool value) {
    if (propsFile)
      propsFile->setValue(key, value);
  }
};
