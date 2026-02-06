/*
  ==============================================================================
    Source/Core/MidiHardwareController.h
    Role: Single place for MIDI device enable/disable and config load.
    Isolates hardware reconciliation from layout/binding so changes to one
    don't break the other.
  ==============================================================================
*/
#pragma once

#include <juce_core/juce_core.h>

class MidiDeviceService;
class AppState;
class MidiRouter;

class MidiHardwareController {
public:
  MidiHardwareController() = default;

  void setDeviceService(MidiDeviceService* service) { deviceService_ = service; }
  void setAppState(AppState* state) { appState_ = state; }

  bool isInputEnabled(const juce::String& deviceId) const;
  bool isOutputEnabled(const juce::String& deviceId) const;

  /** Toggle input; callback receives router for loadConfig. Returns true if state changed. */
  bool setInputEnabled(const juce::String& deviceId, bool enabled, MidiRouter* callbackRouter);

  /** Toggle output. Returns true if state changed. */
  bool setOutputEnabled(const juce::String& deviceId, bool enabled);

  /** Load saved MIDI config into router (e.g. after profile load). */
  void loadConfig(MidiRouter* router);

private:
  MidiDeviceService* deviceService_ = nullptr;
  AppState* appState_ = nullptr;
};
