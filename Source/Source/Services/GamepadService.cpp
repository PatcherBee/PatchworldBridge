/*
  ==============================================================================
    Source/Services/GamepadService.cpp
    Gamepad polling stub and deadzone/sensitivity. Wire gainput or platform
    API to poll axes/buttons and call sendAxis/sendButton.
  ==============================================================================
*/
#include "GamepadService.h"
#include <cmath>

float GamepadService::applyDeadzoneAndSensitivity(float raw) const {
  float v = raw;
  if (std::abs(v) < deadzone)
    return 0.0f;
  v = (v > 0.0f) ? (v - deadzone) / (1.0f - deadzone)
                 : (v + deadzone) / (1.0f - deadzone);
  v *= sensitivity;
  return juce::jlimit(-1.0f, 1.0f, v);
}

void GamepadService::update() {
  // Stub: When gainput or platform API (XInput/DirectInput on Windows,
  // etc.) is integrated, poll device here and call:
  //   - For axes: applyDeadzoneAndSensitivity(axisValue), then
  //     onOscSend("/ch1cc", value) or onMidiSend(1, ccIndex, value, true)
  //     or mappingManager->setParameterValue("Gamepad_LX", value);
  //   - For buttons: onMidiSend(ch, note, 1.0f, false) on press,
  //     onMidiSend(ch, note, 0.0f, false) on release.
  (void)this;
}
