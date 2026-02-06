#include "../Services/ProfileService.h"
#include "../Core/AppState.h"
#include "../Services/MidiMappingService.h"
#include "../UI/Panels/MixerPanel.h"
#include <map>


bool ProfileService::saveProfile(const juce::File &file) {
  JUCE_ASSERT_MESSAGE_THREAD;

  auto *root = new juce::DynamicObject();
  root->setProperty("version", "1.1.0");
  root->setProperty("timestamp",
                    (juce::int64)juce::Time::getCurrentTime().toMilliseconds());

  // 1. Mappings
  if (mappingService) {
    mappingService->saveMappingsToJSON(root);
  }

  // 1b. Control message overrides (right-click "Change message")
  if (appState) {
    root->setProperty("control_message_overrides",
                      appState->getControlMessageOverridesAsVar());
  }

  // 2. Mixer State
  if (mixer) {
    root->setProperty("mixer_state", getMixerState());
  }

  // 3. ATOMIC WRITE (temp then move so existing file is not truncated on failure)
  juce::File parent = file.getParentDirectory();
  if (!parent.exists() && !parent.createDirectory()) {
    if (onLog)
      onLog("Could not create folder for profile. Check path or permissions.", true);
    return false;
  }
  juce::File tempFile = file.withFileExtension(".tmp");
  juce::String jsonStr = juce::JSON::toString(juce::var(root));
  bool ok = tempFile.replaceWithText(jsonStr) && tempFile.moveFileTo(file);
  if (onLog)
    onLog(ok ? "Profile saved: " + file.getFileName() : "Profile save failed. Check disk space or permissions.", !ok);
  return ok;
}

bool ProfileService::loadProfile(const juce::File &file) {
  JUCE_ASSERT_MESSAGE_THREAD;

  if (!file.existsAsFile()) {
    if (onLog)
      onLog("Profile not found. Choose an existing file or save a new one.", true);
    return false;
  }

  juce::var data;
  {
    juce::String jsonStr = file.loadFileAsString();
    if (jsonStr.isEmpty()) {
      if (onLog)
        onLog("Could not read profile file. It may be in use or locked.", true);
      return false;
    }
    try {
      data = juce::JSON::parse(jsonStr);
    } catch (...) {
      if (onLog)
        onLog("Profile file is invalid or corrupted. Try another file or save a new one.", true);
      return false;
    }
  }
  if (data.isVoid() || data.isUndefined()) {
    if (onLog)
      onLog("Profile file is invalid or corrupted. Try another file or reset.", true);
    return false;
  }
  auto *obj = data.getDynamicObject();
  if (!obj) {
    if (onLog)
      onLog("Profile format is invalid. Try another file or reset.", true);
    return false;
  }
  bool ok = true;
  if (mappingService && obj->hasProperty("mappings")) {
    try {
      mappingService->loadMappingsFromJSON(obj->getProperty("mappings"));
    } catch (...) {
      if (onLog) onLog("Profile mappings could not be loaded; keeping current mappings.", true);
      ok = false;
    }
  }
  if (appState && obj->hasProperty("control_message_overrides")) {
    try {
      appState->setControlMessageOverridesFromVar(
          obj->getProperty("control_message_overrides"));
    } catch (...) {
      if (onLog) onLog("Profile control overrides could not be applied.", true);
    }
  }
  if (mixer && obj->hasProperty("mixer_state")) {
    try {
      setMixerState(obj->getProperty("mixer_state"));
    } catch (...) {
      if (onLog) onLog("Profile mixer state could not be applied.", true);
    }
  }

  if (onLog)
    onLog("Profile loaded: " + file.getFileName(), false);
  if (onProfileLoaded)
    onProfileLoaded();
  return ok;
}

juce::var ProfileService::getMixerState() {
  juce::Array<juce::var> strips;
  if (!mixer)
    return strips;
  for (auto *s : mixer->strips) {
    auto *stripObj = new juce::DynamicObject();
    stripObj->setProperty("name", s->nameLabel.getText());
    stripObj->setProperty("vol", s->volSlider.getValue());
    stripObj->setProperty("active", s->btnActive.getToggleState());
    stripObj->setProperty("oscAddr", s->customOscIn);
    strips.add(juce::var(stripObj));
  }
  return strips;
}

void ProfileService::setMixerState(const juce::var &data) {
  if (!mixer)
    return;
  auto *arr = data.getArray();
  if (!arr)
    return;
  const int numStrips = static_cast<int>(mixer->strips.size());
  for (int i = 0; i < arr->size() && i < numStrips; ++i) {
    const auto sObj = (*arr)[i];
    auto *o = sObj.getDynamicObject();
    if (!o)
      continue;
    if (i >= numStrips)
      break;
    auto *s = mixer->strips[static_cast<size_t>(i)];
    if (o->hasProperty("name"))
      s->setTrackName(sObj["name"].toString());
    if (o->hasProperty("vol"))
      s->volSlider.setValue(sObj["vol"], juce::dontSendNotification);
    if (o->hasProperty("active"))
      s->btnActive.setToggleState(sObj["active"], juce::dontSendNotification);
    if (o->hasProperty("oscAddr"))
      s->setCustomOscAddress(sObj["oscAddr"].toString());
  }
}

juce::StringArray ProfileService::getProfileNames() const {
  juce::StringArray names;
  auto files =
      rootFolder.findChildFiles(juce::File::findFiles, false, "*.json");
  for (auto &f : files)
    names.add(f.getFileNameWithoutExtension());
  return names;
}

bool ProfileService::deleteProfile(const juce::String &name) {
  auto targetFile = rootFolder.getChildFile(name + ".json");
  if (targetFile.existsAsFile()) {
    return targetFile.deleteFile();
  }
  return false;
}

bool ProfileService::profileExists(const juce::String &name) const {
  auto targetFile = rootFolder.getChildFile(name + ".json");
  return targetFile.existsAsFile();
}

void ProfileService::saveProfileAsync(const juce::String &name,
                                      const juce::var &data,
                                      std::function<void(bool)> onComplete) {
  auto targetFile = rootFolder.getChildFile(name + ".json");
  juce::Thread::launch([targetFile, data, onComplete] {
    juce::String jsonStr = juce::JSON::toString(data, true);
    bool success = targetFile.replaceWithText(jsonStr);
    if (onComplete)
      juce::MessageManager::callAsync(
          [onComplete, success] { onComplete(success); });
  });
}

void ProfileService::loadProfileAsync(
    const juce::String &name, std::function<void(juce::var)> onComplete) {
  auto targetFile = rootFolder.getChildFile(name + ".json");
  juce::Thread::launch([targetFile, onComplete] {
    if (!targetFile.existsAsFile()) {
      if (onComplete)
        juce::MessageManager::callAsync(
            [onComplete] { onComplete(juce::var()); });
      return;
    }
    juce::String jsonStr = targetFile.loadFileAsString();
    if (jsonStr.isEmpty()) {
      if (onComplete)
        juce::MessageManager::callAsync(
            [onComplete] { onComplete(juce::var()); });
      return;
    }
    juce::var data;
    try {
      data = juce::JSON::parse(jsonStr);
    } catch (...) {
      data = juce::var();
    }
    if (data.isVoid() || data.isUndefined())
      data = juce::var();
    juce::var toPass = data;
    if (onComplete)
      juce::MessageManager::callAsync([onComplete, toPass] { onComplete(toPass); });
  });
}
