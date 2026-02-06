#pragma once
#include "../Core/TimerHub.h"
#include "../Services/MidiMappingService.h"
#include "GamepadMapping.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <string>

class GamepadService {
public:
  GamepadService() = default;
  ~GamepadService() { stopPolling(); }

  void setMappingManager(MidiMappingService *ptr) { mappingManager = ptr; }

  void setOnOscSend(std::function<void(juce::String address, float value)> cb) {
    onOscSend = std::move(cb);
  }
  void setOnMidiSend(std::function<void(int ch, int noteOrCC, float value, bool isCC)> cb) {
    onMidiSend = std::move(cb);
  }

  void startPolling(int rate = 60) {
    if (!hubId.empty())
      return;
    hubId = "GamepadService_" + juce::Uuid().toDashedString().toStdString();
    auto p = (rate >= 60) ? TimerHub::High60Hz
           : (rate >= 30) ? TimerHub::Medium30Hz
           : TimerHub::Low15Hz;
    TimerHub::instance().subscribe(hubId, [this] { update(); }, p);
  }

  void stopPolling() {
    if (!hubId.empty()) {
      TimerHub::instance().unsubscribe(hubId);
      hubId.clear();
    }
  }

  /** Called at polling rate. Stub: wire gainput/platform API to poll axes/buttons. */
  void update();

  float applyDeadzoneAndSensitivity(float raw) const;

  int getControllerType() const { return (int)controllerType; }
  void setControllerType(int t) {
    controllerType = (GamepadMapping::ControllerType)juce::jlimit(0, 2, t);
  }

  float deadzone = 0.15f;
  float sensitivity = 1.0f;

  int getDefaultMidiChannel() const { return defaultMidiChannel; }
  void setDefaultMidiChannel(int ch) {
    defaultMidiChannel = juce::jlimit(1, 16, ch);
  }

private:
  std::string hubId;
  MidiMappingService *mappingManager = nullptr;
  GamepadMapping::ControllerType controllerType = GamepadMapping::Xbox;
  int defaultMidiChannel = 1;
  std::function<void(juce::String, float)> onOscSend;
  std::function<void(int, int, float, bool)> onMidiSend;
};