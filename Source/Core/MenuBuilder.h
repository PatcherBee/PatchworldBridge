/*
  ==============================================================================
    Source/Core/MenuBuilder.h
    Role: Centralized popup menu creation (MIDI device picker, modules toggle).
    Consolidates duplicate menu logic from ConfigPanel / SystemController (10.1).
  ==============================================================================
*/
#pragma once

#include "../UI/Panels/MidiDevicePickerPanel.h"
#include "../UI/Panels/ModulesTogglePanel.h"
#include "../UI/Widgets/ModuleWindow.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class BridgeContext;

struct MenuBuilder {
  /** Build MIDI Input picker panel; callbacks wired to context. Caller launches as CallOutBox. */
  static std::unique_ptr<MidiDevicePickerPanel> createMidiInputPanel(BridgeContext &context);

  /** Build MIDI Output picker panel; callbacks wired to context. Caller launches as CallOutBox. */
  static std::unique_ptr<MidiDevicePickerPanel> createMidiOutputPanel(BridgeContext &context);

  /** Build Modules toggle panel; caller launches as CallOutBox. */
  static std::unique_ptr<ModulesTogglePanel> createModulesTogglePanel(
      ModuleWindow *editor, ModuleWindow *mixer, ModuleWindow *sequencer,
      ModuleWindow *playlist, ModuleWindow *arp, ModuleWindow *macros,
      ModuleWindow *log, ModuleWindow *chords, ModuleWindow *control,
      ModuleWindow *lfoGen = nullptr);
};
