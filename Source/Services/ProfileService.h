/*
  ==============================================================================
    Source/Services/ProfileService.h
    Role: Handles thread-safe JSON persistence and directory management.
    Renamed/Merged from ProfileManager.
  ==============================================================================
*/
#pragma once
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

// Forward Declarations
class AppState;
class MixerPanel;
class MidiMappingService;

class ProfileService {
public:
  ProfileService() {
    rootFolder =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("PatchworldBridge")
            .getChildFile("Profiles");

    if (!rootFolder.exists() && !rootFolder.createDirectory()) {
      rootFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("PatchworldBridge_Profiles");
      if (!rootFolder.exists())
        rootFolder.createDirectory();
    }
  }

  // --- Orchestrator Setters ---
  void setMappingService(MidiMappingService *m) { mappingService = m; }
  void setMixer(MixerPanel *m) { mixer = m; }
  void setAppState(AppState *a) { appState = a; }
  void setParameters(juce::ValueTree *p) { parameters = p; }

  // --- Core Actions ---
  /** Returns true if save succeeded. */
  bool saveProfile(const juce::File &file);
  /** Returns true if load succeeded (file existed and parsed). */
  bool loadProfile(const juce::File &file);

  // Async Helpers (Legacy/Internal)
  void saveProfileAsync(const juce::String &name, const juce::var &data,
                        std::function<void(bool)> onComplete);
  void loadProfileAsync(const juce::String &name,
                        std::function<void(juce::var)> onComplete);

  // Directory Access
  juce::File getRootFolder() const { return rootFolder; }
  juce::StringArray getProfileNames() const;
  bool deleteProfile(const juce::String &name);
  bool profileExists(const juce::String &name) const;

  // Callbacks
  std::function<void(juce::String, bool)> onLog;
  std::function<void()> onProfileLoaded;

private:
  juce::File rootFolder;
  AppState *appState = nullptr;
  MidiMappingService *mappingService = nullptr;
  MixerPanel *mixer = nullptr;
  juce::ValueTree *parameters = nullptr;

  juce::var getMixerState();
  void setMixerState(const juce::var &data);

  // Helper to avoid recursive locking (Issue #8)
  void publishChangesInternal(double currentBpm);
};
