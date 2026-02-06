/*
  ==============================================================================
    Source/Services/AutoSaver.h
    Crash recovery: saves to _autosave.json every 120 seconds when dirty.
  ==============================================================================
*/
#pragma once
#include "../Core/TimerHub.h"
#include "ProfileService.h"
#include <juce_core/juce_core.h>

class AutoSaver {
public:
  AutoSaver(ProfileService &profiles) : profiles(profiles) {
    hubId = "AutoSaver_" + juce::Uuid().toDashedString().toStdString();
    TimerHub::instance().subscribe(hubId, [this] { tick(); },
                                   TimerHub::Rate0_008Hz);
  }

  ~AutoSaver() { TimerHub::instance().unsubscribe(hubId); }

private:
  void tick() {
    juce::File recoveryFile =
        profiles.getRootFolder().getChildFile("_autosave.json");
    profiles.saveProfile(recoveryFile);
  }

  std::string hubId;
  ProfileService &profiles;
};
