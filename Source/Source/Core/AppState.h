//////////////////////////////////////////////////////////////////////
// FILE: AppState.h
//////////////////////////////////////////////////////////////////////

#pragma once
// --- 2. JUCE Framework ---
#include <algorithm>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../Audio/OscTypes.h"
#include "../Core/BridgeSettings.h"
#include "../Core/TimerHub.h"

class AppState : public juce::ValueTree::Listener {
public:
  AppState() : state("PATCHWORLD_BRIDGE") {
    // 0. Initialize Atomics from current state
    syncSettings();

    juce::PropertiesFile::Options options;
    options.applicationName = "PatchworldBridge";
    options.filenameSuffix = ".settings";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.commonToAllUsers = false;

    // Load File
    props = std::make_unique<juce::PropertiesFile>(options);

    // Set Defaults if empty
    if (state.getNumProperties() == 0) {
      state.setProperty("ip", "127.0.0.1", nullptr);
      state.setProperty("portOut", 3330, nullptr);
      state.setProperty("portIn", 5550, nullptr);
      state.setProperty("useGL", true, nullptr);
      state.setProperty("proPerformance", true, nullptr);
      state.setProperty("winW", 1000, nullptr);
      state.setProperty("winH", 900, nullptr);
      state.setProperty("snapshotSync", false, nullptr);
      state.setProperty("uiScale", 0.9, nullptr);
      state.setProperty("clockSourceId", "", nullptr);
      state.setProperty("themeId", 1, nullptr);
      state.setProperty("multicast", false, nullptr);
      state.setProperty("zeroconf", true, nullptr);
    state.setProperty("savedLayout", "", nullptr);
    state.setProperty("defaultBpm", 120.0, nullptr);
    }

    loadFromXml();
    syncSettings(); // Initial sync
    // Ensure ConfigManager keys exist (migration from props or old XML)
    if (!state.hasProperty("uiScale")) state.setProperty("uiScale", 0.9, nullptr);
    if (!state.hasProperty("clockSourceId")) state.setProperty("clockSourceId", "", nullptr);
    if (!state.hasProperty("themeId")) state.setProperty("themeId", 1, nullptr);
    if (!state.hasProperty("multicast")) state.setProperty("multicast", false, nullptr);
    if (!state.hasProperty("zeroconf")) state.setProperty("zeroconf", true, nullptr);
    if (!state.hasProperty("savedLayout")) state.setProperty("savedLayout", "", nullptr);
    if (!state.hasProperty("defaultBpm")) state.setProperty("defaultBpm", 120.0, nullptr);
    if (!state.hasProperty("renderMode")) state.setProperty("renderMode", 3, nullptr);  // 3 = Software
    if (!state.hasProperty("gpuBackend")) state.setProperty("gpuBackend", "Software", nullptr);
    state.addListener(this);
  }
  std::unique_ptr<juce::PropertiesFile> props;
  BridgeSettings settings;
  EngineShadowState engineState;

  /** ValueTree used by ConfigManager for central get/set. */
  juce::ValueTree &getState() { return state; }
  const juce::ValueTree &getState() const { return state; }

  ~AppState() {
    if (!hubId.empty())
      TimerHub::instance().unsubscribe(hubId);
    if (savePending)
      saveToXml();
  }

  // --- Listener implementation ---
  void valueTreePropertyChanged(juce::ValueTree &tree,
                                const juce::Identifier &id) override {
    juce::ignoreUnused(tree);
    if (isLoading)
      return; // Don't trigger saves during load
    savePending = true;

    // Pro-Active Sync: Update atomics immediately
    if (id == juce::Identifier("lookaheadMs"))
      settings.networkLookaheadMs.store((float)state.getProperty(id));
    else if (id == juce::Identifier("lookaheadBypass"))
      settings.lookaheadBypass.store((bool)state.getProperty(id));
    else if (id == juce::Identifier("proPerformance"))
      settings.jitterFilterActive.store((bool)state.getProperty(id));

    lastChangeTimeMs = juce::Time::getMillisecondCounterHiRes();
    if (hubId.empty()) {
      hubId = "AppState_saveCoalesce_" + juce::Uuid().toDashedString().toStdString();
      TimerHub::instance().subscribe(hubId, [this] { tickSave(); },
                                     TimerHub::Rate2Hz);
    }
  }

  void syncSettings() {
    settings.networkLookaheadMs.store(
        (float)state.getProperty("lookaheadMs", 30.0f));
    settings.lookaheadBypass.store(
        (bool)state.getProperty("lookaheadBypass", false));
    settings.jitterFilterActive.store(
        (bool)state.getProperty("proPerformance", true));
  }

  void tickSave() {
    double now = juce::Time::getMillisecondCounterHiRes();
    if (savePending && (now - lastChangeTimeMs) >= 500.0) {
      savePending = false;
      if (!hubId.empty()) {
        TimerHub::instance().unsubscribe(hubId);
        hubId.clear();
      }
      saveToXml();
    }
  }

  // --- Getters/Setters ---
  juce::String getIp() { return state.getProperty("ip").toString(); }
  void setIp(const juce::String &s) { state.setProperty("ip", s, nullptr); }

  int getPortOut() { return (int)state.getProperty("portOut"); }
  void setPortOut(int p) { state.setProperty("portOut", p, nullptr); }

  int getPortIn() { return (int)state.getProperty("portIn"); }
  void setPortIn(int p) { state.setProperty("portIn", p, nullptr); }

  bool getUseIPv6() { return (bool)state.getProperty("useIPv6"); }
  void setUseIPv6(bool b) { state.setProperty("useIPv6", b, nullptr); }

  bool getMidiScaling() { return (bool)state.getProperty("midiScaling127"); }
  void setMidiScaling(bool b) {
    state.setProperty("midiScaling127", b, nullptr);
  }

  int getWindowWidth() { return (int)state.getProperty("winW"); }
  int getWindowHeight() { return (int)state.getProperty("winH"); }
  void setWindowSize(int w, int h) {
    state.setProperty("winW", w, nullptr);
    state.setProperty("winH", h, nullptr);
  }

  juce::String getLastMidiInId() {
    return state.getProperty("midiInId").toString();
  }
  void setLastMidiInId(const juce::String &s) {
    state.setProperty("midiInId", s, nullptr);
  }

  juce::StringArray getActiveMidiIds(bool isInput) {
    juce::StringArray ids;
    juce::String key = isInput ? "midiInIds" : "midiOutIds";
    ids.addTokens(state.getProperty(key).toString(), ";", "");
    return ids;
  }

  void updateActiveMidiIds(const juce::StringArray &ids, bool isInput) {
    juce::String key = isInput ? "midiInIds" : "midiOutIds";
    state.setProperty(key, ids.joinIntoString(";"), nullptr);
  }

  /** Per-device options (Ableton-style): Track, Sync, Remote, MPE. Stored as "id:tsrm;id2:tsrm" (t=track,s=sync,r=remote,m=mpe, 0/1). */
  struct MidiDeviceOptions {
    bool track = true;
    bool sync = true;
    bool remote = true;
    bool mpe = false;
  };
  MidiDeviceOptions getMidiDeviceOptions(bool isInput,
                                         const juce::String &deviceId) const {
    juce::String key = isInput ? "midiInOpts" : "midiOutOpts";
    juce::String raw = state.getProperty(key).toString();
    if (raw.isEmpty() || deviceId.isEmpty())
      return {};
    juce::StringArray pairs;
    pairs.addTokens(raw, ";", "");
    for (auto &p : pairs) {
      int colon = p.indexOfChar(':');
      if (colon <= 0)
        continue;
      if (p.substring(0, colon).trim() != deviceId)
        continue;
      juce::String flags = p.substring(colon + 1).trim();
      MidiDeviceOptions o;
      if (flags.length() >= 1)
        o.track = (flags[0] == '1');
      if (flags.length() >= 2)
        o.sync = (flags[1] == '1');
      if (flags.length() >= 3)
        o.remote = (flags[2] == '1');
      if (flags.length() >= 4)
        o.mpe = (flags[3] == '1');
      return o;
    }
    return {};
  }
  void setMidiDeviceOptions(bool isInput, const juce::String &deviceId,
                            const MidiDeviceOptions &opts) {
    juce::String key = isInput ? "midiInOpts" : "midiOutOpts";
    juce::String raw = state.getProperty(key).toString();
    juce::StringArray pairs;
    pairs.addTokens(raw, ";", "");
    juce::String flagStr;
    flagStr << (opts.track ? '1' : '0') << (opts.sync ? '1' : '0')
            << (opts.remote ? '1' : '0') << (opts.mpe ? '1' : '0');
    juce::String newPair = deviceId + ":" + flagStr;
    bool found = false;
    for (int i = 0; i < pairs.size(); ++i) {
      int colon = pairs[i].indexOfChar(':');
      if (colon > 0 && pairs[i].substring(0, colon).trim() == deviceId) {
        pairs.set(i, newPair);
        found = true;
        break;
      }
    }
    if (!found)
      pairs.add(newPair);
    state.setProperty(key, pairs.joinIntoString(";"), nullptr);
  }

  /** Per-control OSC/MIDI message override (right-click "Change message").
   *  type: 0=use default, 1=CC, 2=Note, 3=PitchBend. Stored as "id:type:ch:noc;...". */
  struct ControlMessageOverride {
    int type = 0;       // 0=default, 1=CC, 2=Note, 3=PitchBend
    int channel = 1;
    int noteOrCC = 0;   // CC number or note number
  };
  ControlMessageOverride getControlMessageOverride(const juce::String &paramID) const {
    juce::String raw = state.getProperty("controlMsgOverrides").toString();
    if (raw.isEmpty() || paramID.isEmpty()) return {};
    juce::StringArray pairs;
    pairs.addTokens(raw, ";", "");
    for (auto &p : pairs) {
      juce::StringArray parts;
      parts.addTokens(p, ":", "");
      if (parts.size() >= 4 && parts[0].trim() == paramID) {
        ControlMessageOverride o;
        o.type = parts[1].trim().getIntValue();
        o.channel = juce::jlimit(1, 16, parts[2].trim().getIntValue());
        o.noteOrCC = juce::jlimit(0, 127, parts[3].trim().getIntValue());
        return o;
      }
    }
    return {};
  }
  void setControlMessageOverride(const juce::String &paramID,
                                  const ControlMessageOverride &o) {
    juce::String key = "controlMsgOverrides";
    juce::String raw = state.getProperty(key).toString();
    juce::StringArray pairs;
    pairs.addTokens(raw, ";", "");
    juce::String newEntry = paramID + ":" + juce::String(o.type) + ":" +
                            juce::String(o.channel) + ":" + juce::String(o.noteOrCC);
    bool found = false;
    for (int i = 0; i < pairs.size(); ++i) {
      int colon = pairs[i].indexOfChar(':');
      if (colon > 0 && pairs[i].substring(0, colon).trim() == paramID) {
        if (o.type == 0) {
          pairs.remove(i);
        } else {
          pairs.set(i, newEntry);
        }
        found = true;
        break;
      }
    }
    if (!found && o.type != 0)
      pairs.add(newEntry);
    state.setProperty(key, pairs.joinIntoString(";"), nullptr);
  }
  void clearControlMessageOverride(const juce::String &paramID) {
    setControlMessageOverride(paramID, {});
  }
  /** For JSON profile export: serialize all overrides as an object (paramID -> {type,ch,noteOrCC}). */
  juce::var getControlMessageOverridesAsVar() const {
    juce::String raw = state.getProperty("controlMsgOverrides").toString();
    if (raw.isEmpty())
      return juce::var(new juce::DynamicObject());
    auto *root = new juce::DynamicObject();
    juce::StringArray pairs;
    pairs.addTokens(raw, ";", "");
    for (auto &p : pairs) {
      juce::StringArray parts;
      parts.addTokens(p, ":", "");
      if (parts.size() >= 4) {
        juce::String id = parts[0].trim();
        auto *obj = new juce::DynamicObject();
        obj->setProperty("type", parts[1].trim().getIntValue());
        obj->setProperty("ch", parts[2].trim().getIntValue());
        obj->setProperty("noteOrCC", parts[3].trim().getIntValue());
        root->setProperty(id, juce::var(obj));
      }
    }
    return juce::var(root);
  }
  /** Restore overrides from JSON profile (paramID -> {type,ch,noteOrCC}). */
  void setControlMessageOverridesFromVar(const juce::var &v) {
    if (v.isVoid() || v.isUndefined())
      return;
    juce::String key = "controlMsgOverrides";
    juce::StringArray pairs;
    if (auto *obj = v.getDynamicObject()) {
      for (auto &prop : obj->getProperties()) {
        juce::String id = prop.name.toString();
        juce::var val = prop.value;
        if (auto *o = val.getDynamicObject()) {
          int t = o->getProperty("type");
          int ch = o->getProperty("ch");
          int noc = o->getProperty("noteOrCC");
          pairs.add(id + ":" + juce::String(t) + ":" + juce::String(ch) + ":" +
                   juce::String(noc));
        }
      }
    }
    state.setProperty(key, pairs.joinIntoString(";"), nullptr);
  }

  juce::String getLastMidiOutId() {
    return state.getProperty("midiOutId").toString();
  }
  void setLastMidiOutId(const juce::String &s) {
    state.setProperty("midiOutId", s, nullptr);
  }

  bool hasCrashedLastSession() { return (bool)state.getProperty("crashed"); }
  void setCrashed(bool b) { state.setProperty("crashed", b, nullptr); }

  bool getUseOpenGL() { return (bool)state.getProperty("useGL"); }
  void setUseOpenGL(bool b) { state.setProperty("useGL", b, nullptr); }

  bool wasLastShutdownClean() { return (bool)state.getProperty("cleanExit"); }
  void setCleanExit(bool b) { state.setProperty("cleanExit", b, nullptr); }

  bool getShowDiagnostics() {
    return (bool)state.getProperty("showDiagnostics");
  }
  void setShowDiagnostics(bool b) {
    state.setProperty("showDiagnostics", b, nullptr);
  }

  int getRenderMode() { return (int)state.getProperty("renderMode", 3); }  // 3 = Software default
  void setRenderMode(int mode) {
    state.setProperty("renderMode", juce::jlimit(1, 4, mode), nullptr);
  }
  juce::String getGpuBackend() { return state.getProperty("gpuBackend", "Software").toString(); }
  void setGpuBackend(const juce::String &s) { state.setProperty("gpuBackend", s, nullptr); }

  int getMidiOutChannel() { return (int)state.getProperty("midiOutCh", 1); }
  void setMidiOutChannel(int ch) {
    state.setProperty("midiOutCh", ch, nullptr);
  }

  bool getMidiThru() { return (bool)state.getProperty("midiThru", false); }
  void setMidiThru(bool b) { state.setProperty("midiThru", b, nullptr); }

  void setChannelName(int index, const juce::String &name) {
    state.setProperty("chName_" + juce::String(index), name, nullptr);
  }
  juce::String getChannelName(int index) {
    return state.getProperty("chName_" + juce::String(index)).toString();
  }

  bool hasSeenTour() { return (bool)state.getProperty("hasSeenTour"); }
  void setSeenTour(bool b) { state.setProperty("hasSeenTour", b, nullptr); }

  /** Layout wizard: show 3 layout boxes once on first run; cleared by Reset to defaults. */
  bool hasSeenLayoutWizard() const {
    return state.getProperty("hasSeenLayoutWizard", false);
  }
  void setSeenLayoutWizard(bool b) {
    state.setProperty("hasSeenLayoutWizard", b, nullptr);
  }
  juce::String getLayoutPreset(const juce::String& name) const {
    juce::String key = "savedLayout_" + name;
    return state.getProperty(key, "").toString();
  }
  void setLayoutPreset(const juce::String& name, const juce::String& xmlStr) {
    state.setProperty("savedLayout_" + name, xmlStr, nullptr);
  }
  juce::String getCurrentLayoutName() const {
    return state.getProperty("currentLayoutName", "").toString();
  }
  void setCurrentLayoutName(const juce::String& name) {
    state.setProperty("currentLayoutName", name, nullptr);
  }

  double getNetworkLookahead() {
    return (double)state.getProperty("lookaheadMs");
  }
  void setNetworkLookahead(double ms) {
    state.setProperty("lookaheadMs", ms, nullptr);
  }
  double getClockOffset() {
    return (double)state.getProperty("clockOffsetMs", 0.0);
  }
  void setClockOffset(double ms) {
    state.setProperty("clockOffsetMs", ms, nullptr);
  }

  bool getSnapshotSyncEnabled() {
    return (bool)state.getProperty("snapshotSync", false);
  }
  void setSnapshotSyncEnabled(bool b) {
    state.setProperty("snapshotSync", b, nullptr);
  }

  bool getLinkPref() { return (bool)state.getProperty("linkPref", true); }
  void setLinkPref(bool b) { state.setProperty("linkPref", b, nullptr); }

  bool getLookaheadBypass() {
    return (bool)state.getProperty("lookaheadBypass", false);
  }
  void setLookaheadBypass(bool b) {
    state.setProperty("lookaheadBypass", b, nullptr);
  }

  bool getPerformanceMode() {
    return (bool)state.getProperty("proPerformance");
  }
  void setPerformanceMode(bool b) {
    state.setProperty("proPerformance", b, nullptr);
  }

  double getDefaultBpm() const {
    return (double)state.getProperty("defaultBpm", 120.0);
  }
  void setDefaultBpm(double bpm) {
    state.setProperty("defaultBpm", juce::jlimit(20.0, 300.0, bpm), nullptr);
  }

  /** Recent .mid files (last 5, newest first). */
  juce::StringArray getRecentMidiFiles() const {
    juce::StringArray out;
    for (int i = 0; i < 5; ++i) {
      juce::String p = state.getProperty("recentMidi_" + juce::String(i), "").toString();
      if (p.isNotEmpty() && juce::File(p).existsAsFile())
        out.add(p);
    }
    return out;
  }
  void addRecentMidiFile(const juce::String &path) {
    if (path.isEmpty())
      return;
    juce::StringArray current;
    for (int i = 0; i < 5; ++i) {
      juce::String p = state.getProperty("recentMidi_" + juce::String(i), "").toString();
      if (p.isNotEmpty())
        current.add(p);
    }
    current.removeString(path, false);
    current.insert(0, path);
    while (current.size() > 5)
      current.remove(current.size() - 1);
    for (int i = 0; i < 5; ++i)
      state.setProperty("recentMidi_" + juce::String(i),
                        i < current.size() ? current[i] : juce::String(), nullptr);
  }

  // --- OSC SCHEMA PERSISTENCE ---
  void saveOscSchema(const OscNamingSchema &schema) {
    auto schemaTree = state.getOrCreateChildWithName("OscSchema", nullptr);
    schemaTree.setProperty("notePrefix", schema.notePrefix, nullptr);
    schemaTree.setProperty("noteSuffix", schema.noteSuffix, nullptr);
    schemaTree.setProperty("noteOffSuffix", schema.noteOffSuffix, nullptr);
    schemaTree.setProperty("ccPrefix", schema.ccPrefix, nullptr);
    schemaTree.setProperty("ccSuffix", schema.ccSuffix, nullptr);
    schemaTree.setProperty("pitchPrefix", schema.pitchPrefix, nullptr);
    schemaTree.setProperty("pitchSuffix", schema.pitchSuffix, nullptr);
    schemaTree.setProperty("playAddr", schema.playAddr, nullptr);
    schemaTree.setProperty("stopAddr", schema.stopAddr, nullptr);
    saveToXml();
  }

  OscNamingSchema loadOscSchema() {
    OscNamingSchema s;
    auto schemaTree = state.getChildWithName("OscSchema");

    if (schemaTree.isValid()) {
      s.notePrefix = schemaTree.getProperty("notePrefix", "/ch").toString();
      s.noteSuffix = schemaTree.getProperty("noteSuffix", "note").toString();
      s.noteOffSuffix =
          schemaTree.getProperty("noteOffSuffix", "noteoff").toString();
      s.ccPrefix = schemaTree.getProperty("ccPrefix", "/ch").toString();
      s.ccSuffix = schemaTree.getProperty("ccSuffix", "cc").toString();
      s.pitchPrefix = schemaTree.getProperty("pitchPrefix", "/ch").toString();
      s.pitchSuffix = schemaTree.getProperty("pitchSuffix", "pitch").toString();
      s.playAddr = schemaTree.getProperty("playAddr", "/play").toString();
      s.stopAddr = schemaTree.getProperty("stopAddr", "/stop").toString();
    }
    return s;
  }

  void save() { saveToXml(); }
  void forceSave() {
    if (!hubId.empty()) {
      TimerHub::instance().unsubscribe(hubId);
      hubId.clear();
    }
    savePending = false;
    saveToXml();
  }

  void resetToDefaults() {
    state.setProperty("hasSeenLayoutWizard", false, nullptr);
    state.setProperty("savedLayout", "", nullptr);
    state.setProperty("currentLayoutName", "Full", nullptr);
    state.setProperty("savedLayout_Minimal", "", nullptr);
    state.setProperty("savedLayout_Full", "", nullptr);
    state.setProperty("ip", "127.0.0.1", nullptr);
    state.setProperty("portOut", 3330, nullptr);
    state.setProperty("portIn", 5550, nullptr);
    state.setProperty("useGL", false, nullptr);
    state.setProperty("proPerformance", false, nullptr);
    state.setProperty("renderMode", 3, nullptr);   // 3 = Software (reset to safe default)
    state.setProperty("gpuBackend", "Software", nullptr);
    state.setProperty("winW", 1000, nullptr);
    state.setProperty("winH", 900, nullptr);
    state.setProperty("snapshotSync", false, nullptr);
    state.setProperty("uiScale", 0.9, nullptr);
    state.setProperty("clockSourceId", "", nullptr);
    state.setProperty("themeId", 1, nullptr);
    state.setProperty("multicast", false, nullptr);
    state.setProperty("zeroconf", true, nullptr);
    state.setProperty("savedLayout", "", nullptr);
    state.setProperty("defaultBpm", 120.0, nullptr);
    state.setProperty("midiInIds", "", nullptr);
    state.setProperty("midiOutIds", "", nullptr);
    state.setProperty("controlMsgOverrides", "", nullptr);
    syncSettings();
    savePending = false;
    if (!hubId.empty()) {
      TimerHub::instance().unsubscribe(hubId);
      hubId.clear();
    }
    saveToXml();
  }

private:
  juce::ValueTree state;
  bool savePending = false;
  bool isLoading = false;
  double lastChangeTimeMs = 0.0;
  std::string hubId;

  juce::File getSettingsFile() {
    return juce::File::getSpecialLocation(
               juce::File::userApplicationDataDirectory)
        .getChildFile("Patchworld")
        .getChildFile("PatchworldBridge.xml");
  }

  void saveToXml() {
    try {
      juce::File f = getSettingsFile();
      if (!f.getParentDirectory().exists())
        f.getParentDirectory().createDirectory();
      if (auto xml = state.createXml())
        xml->writeTo(f);
    } catch (...) {
      juce::Logger::writeToLog("PatchworldBridge: Settings save failed (e.g. read-only or disk full); changes not persisted.");
    }
  }

  void loadFromXml() {
    isLoading = true; // Suppress listener saves during load
    try {
      auto file = getSettingsFile();
      if (file.existsAsFile()) {
        auto xml = juce::XmlDocument::parse(file);
        if (xml != nullptr) {
          auto loadedState = juce::ValueTree::fromXml(*xml);
          if (loadedState.isValid())
            state.copyPropertiesFrom(loadedState, nullptr);
        } else {
          juce::Logger::writeToLog("PatchworldBridge: Settings file missing or invalid XML; using defaults.");
        }
      }
    } catch (...) {
      juce::Logger::writeToLog("PatchworldBridge: Settings load failed (corrupt or inaccessible); using defaults.");
    }

    // FIX: Cast var to int explicitly to prevent ambiguous operator error
    int pOut = state.getProperty("portOut");
    if (pOut < 1024)
      state.setProperty("portOut", 3330, nullptr);

    int pIn = state.getProperty("portIn");
    if (pIn < 1024)
      state.setProperty("portIn", 5550, nullptr);

    // IP Validation
    juce::String ip = state.getProperty("ip").toString();
    if (!isValidIP(ip))
      state.setProperty("ip", "127.0.0.1", nullptr);

    isLoading = false;
  }

  static bool isValidIP(const juce::String &ip) {
    juce::StringArray parts;
    parts.addTokens(ip, ".", "");
    if (parts.size() != 4)
      return false;
    for (auto &p : parts) {
      int val = p.getIntValue();
      if (val < 0 || val > 255)
        return false;
    }
    return true;
  }
};
