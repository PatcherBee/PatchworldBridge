/*
  ==============================================================================
    Source/Core/MenuBuilder.cpp
  ==============================================================================
*/
#include "MenuBuilder.h"
#include "../Audio/MidiRouter.h"
#include "../Core/BridgeContext.h"
#include "../Services/MidiDeviceService.h"
#include <juce_audio_devices/juce_audio_devices.h>

std::unique_ptr<MidiDevicePickerPanel> MenuBuilder::createMidiInputPanel(BridgeContext &context) {
  auto panel = std::make_unique<MidiDevicePickerPanel>(true);
  panel->setSize(260, 320);
  auto devices = juce::MidiInput::getAvailableDevices();
  // Virtual Keyboard: on-screen keyboard as a selectable MIDI input
  juce::MidiDeviceInfo vk;
  vk.name = "Virtual Keyboard";
  vk.identifier = "VirtualKeyboard";
  devices.add(vk);
  panel->setDevices(devices, context.appState.getActiveMidiIds(true));
  panel->onDeviceToggled = [&context](const juce::String &id, bool enable) {
    if (context.deviceService)
      context.deviceService->setInputEnabled(id, enable, context.midiRouter.get());
  };
  return panel;
}

std::unique_ptr<MidiDevicePickerPanel> MenuBuilder::createMidiOutputPanel(BridgeContext &context) {
  auto panel = std::make_unique<MidiDevicePickerPanel>(false);
  panel->setSize(260, 320);
  panel->setDevices(juce::MidiOutput::getAvailableDevices(),
                    context.appState.getActiveMidiIds(false));
  panel->onDeviceToggled = [&context](const juce::String &id, bool enable) {
    if (context.deviceService)
      context.deviceService->setOutputEnabled(id, enable);
  };
  return panel;
}

std::unique_ptr<ModulesTogglePanel> MenuBuilder::createModulesTogglePanel(
    ModuleWindow *editor, ModuleWindow *mixer, ModuleWindow *sequencer,
    ModuleWindow *playlist, ModuleWindow *arp, ModuleWindow *macros,
    ModuleWindow *log, ModuleWindow *chords, ModuleWindow *control,
    ModuleWindow *lfoGen) {
  auto panel = std::make_unique<ModulesTogglePanel>();
  // setModules MUST run before setSize so resized() sees populated windows[] and lays out toggles
  panel->setModules(editor, mixer, sequencer, playlist, arp, macros, log, chords, control, lfoGen);
  panel->setSize(220, 402);
  return panel;
}
