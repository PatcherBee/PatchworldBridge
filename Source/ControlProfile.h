#include <JuceHeader.h>

struct ControlProfile {
  juce::String name = "Custom Profile"; // Added name field
  int ccCutoff = 74;
  int ccResonance = 71;
  int ccAttack = 73;
  int ccRelease = 72;
  int ccLevel = 7;
  int ccPan = 10;

  // Transport & Options
  bool isTransportLink = false; // If true, respects Start/Stop/Continue
  int ccPlay = -1;              // -1 if relying on Realtime System Messages
  int ccStop = -1;
  int ccRecord = -1;

  // Custom Mappings: ID -> CC
  std::map<juce::String, int> customMappings;

  static ControlProfile getDefault() { return ControlProfile(); }

  static ControlProfile getRolandJDXi() {
    ControlProfile p;
    p.name = "Roland JD-Xi";
    p.ccCutoff = 74;
    p.ccResonance = 71;
    p.ccAttack = 73;
    p.ccRelease = 72;
    p.ccLevel = 7;
    p.ccPan = 10;
    p.isTransportLink = true; // JD-Xi sends realtime
    return p;
  }

  static ControlProfile getGenericKeyboard() {
    ControlProfile p;
    p.name = "Generic Keyboard";
    p.ccLevel = 7;
    p.ccPan = 10;
    p.isTransportLink = true; // Expect Realtime Start/Stop
    return p;
  }

  static ControlProfile getFLStudio() {
    ControlProfile p;
    p.name = "FL Studio";
    p.ccLevel = 7;
    p.ccPan = 10;
    p.isTransportLink = true;
    return p;
  }

  static ControlProfile getAbletonLive() {
    ControlProfile p;
    p.name = "Ableton Live";
    p.ccLevel = 7;
    p.ccPan = 10;
    p.isTransportLink = true;
    return p;
  }

  // JSON Parsing
  static ControlProfile fromJson(const juce::String &jsonString);
  static ControlProfile fromFile(const juce::File &f);
};
