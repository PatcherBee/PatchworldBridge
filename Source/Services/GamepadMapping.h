/*
  ==============================================================================
    Source/Services/GamepadMapping.h
    Logical button/axis IDs and defaults for Xbox, PlayStation, Wii.
    Used by GamepadService for OSC/MIDI mapping (sensitivity, deadzone).
  ==============================================================================
*/
#pragma once
#include <juce_core/juce_core.h>

struct GamepadMapping {
  enum ControllerType {
    Xbox = 0,
    PlayStation = 1,
    Wii = 2
  };

  static constexpr int kMaxButtons = 16;
  static constexpr int kMaxAxes = 4;

  // Axis indices: 0=LX, 1=LY, 2=RX, 3=RY (Xbox/PS); Wii: 0=Nunchuk X, 1=Nunchuk Y, 2=Stick X, 3=Stick Y
  static juce::String getAxisName(ControllerType type, int index) {
    if (index < 0 || index >= kMaxAxes) return "Axis " + juce::String(index);
    if (type == Xbox)
      return juce::StringArray("LX", "LY", "RX", "RY")[index];
    if (type == PlayStation)
      return juce::StringArray("L-Stick X", "L-Stick Y", "R-Stick X", "R-Stick Y")[index];
    return juce::StringArray("Nunchuk X", "Nunchuk Y", "Stick X", "Stick Y")[index];
  }

  static juce::String getButtonName(ControllerType type, int index) {
    if (index < 0 || index >= kMaxButtons) return "Btn " + juce::String(index);
    static const char* xbox[] = { "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "L3", "R3", "D-Up", "D-Down", "D-Left", "D-Right", "Guide", "Extra" };
    static const char* ps[] = { "Cross", "Circle", "Square", "Triangle", "L1", "R1", "Share", "Options", "L3", "R3", "D-Up", "D-Down", "D-Left", "D-Right", "PS", "Touch" };
    static const char* wii[] = { "A", "B", "1", "2", "+", "-", "Home", "D-Up", "D-Down", "D-Left", "D-Right", "Z", "C", "", "", "" };
    if (type == Xbox) return xbox[index];
    if (type == PlayStation) return ps[index];
    return wii[index];
  }

  static int getDefaultMidiCCForAxis(int axisIndex) {
    return juce::jlimit(1, 127, axisIndex + 1);
  }
  static int getDefaultMidiNoteForButton(int buttonIndex) {
    return juce::jlimit(0, 127, 60 + buttonIndex);
  }
};
