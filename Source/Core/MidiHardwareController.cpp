/*
  ==============================================================================
    Source/Core/MidiHardwareController.cpp
  ==============================================================================
*/
#include "../Core/MidiHardwareController.h"
#include "../Core/AppState.h"
#include "../Services/MidiDeviceService.h"
#include "../Audio/MidiRouter.h"

bool MidiHardwareController::isInputEnabled(const juce::String& deviceId) const {
  if (!appState_)
    return false;
  return appState_->getActiveMidiIds(true).contains(deviceId);
}

bool MidiHardwareController::isOutputEnabled(const juce::String& deviceId) const {
  if (!appState_)
    return false;
  return appState_->getActiveMidiIds(false).contains(deviceId);
}

bool MidiHardwareController::setInputEnabled(const juce::String& deviceId,
                                             bool enabled,
                                             MidiRouter* callbackRouter) {
  if (!deviceService_ || !appState_)
    return false;
  bool was = isInputEnabled(deviceId);
  if (was == enabled)
    return false;
  deviceService_->setInputEnabled(deviceId, enabled, callbackRouter);
  return true;
}

bool MidiHardwareController::setOutputEnabled(const juce::String& deviceId,
                                              bool enabled) {
  if (!deviceService_ || !appState_)
    return false;
  bool was = isOutputEnabled(deviceId);
  if (was == enabled)
    return false;
  deviceService_->setOutputEnabled(deviceId, enabled);
  return true;
}

void MidiHardwareController::loadConfig(MidiRouter* router) {
  if (deviceService_ && router)
    deviceService_->loadConfig(router);
}
