/*
  ==============================================================================
    Source/UI/Panels/MidiPortsTablePanel.h
    Role: Table of MIDI inputs/outputs with Track / Sync / Remote / MPE
          enable/disable per device (Ableton-style).
  ==============================================================================
*/
#pragma once
#include "../Theme.h"
#include "../Fonts.h"
#include "../../Core/AppState.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

class MidiPortsTablePanel : public juce::Component {
public:
  MidiPortsTablePanel() {
    setOpaque(true);
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&tableContainer, false);
    viewport.setScrollBarsShown(true, true);
  }

  struct Callbacks {
    std::function<bool(juce::String)> isInputEnabled;
    std::function<bool(juce::String)> isOutputEnabled;
    std::function<AppState::MidiDeviceOptions(bool, juce::String)> getMidiDeviceOptions;
    std::function<void(bool, juce::String, const AppState::MidiDeviceOptions &)> setMidiDeviceOptions;
    std::function<void(juce::String)> onInputToggle;
    std::function<void(juce::String)> onOutputToggle;
  };

  void setCallbacks(Callbacks cbs) { callbacks = std::move(cbs); }

  void refresh() {
    tableContainer.removeAllChildren();
    rows.clear();

    auto inputs = juce::MidiInput::getAvailableDevices();
    auto outputs = juce::MidiOutput::getAvailableDevices();

    const int rowH = 28;
    const int nameWidth = 200;
    const int optColW = 48;
    const int totalOptCols = 5;  // On, Track, Sync, Remote, MPE
    const int tableW = nameWidth + totalOptCols * optColW;

    // Header row
    struct HeaderComponent : juce::Component {
      int nw, ocw;
      HeaderComponent(int nameW, int optColWidth) : nw(nameW), ocw(optColWidth) {}
      void paint(juce::Graphics &g) override {
        g.fillAll(Theme::bgDark.darker(0.2f));
        g.setColour(Theme::text);
        g.setFont(Fonts::body().withHeight(11.0f));
        g.drawText("MIDI Ports", 4, 0, nw - 8, getHeight(), juce::Justification::centredLeft);
        g.drawText("On", nw + 2, 0, ocw - 4, getHeight(), juce::Justification::centred);
        g.drawText("Track", nw + ocw + 2, 0, ocw - 4, getHeight(), juce::Justification::centred);
        g.drawText("Sync", nw + 2 * ocw + 2, 0, ocw - 4, getHeight(), juce::Justification::centred);
        g.drawText("Remote", nw + 3 * ocw + 2, 0, ocw - 4, getHeight(), juce::Justification::centred);
        g.drawText("MPE", nw + 4 * ocw + 2, 0, ocw - 4, getHeight(), juce::Justification::centred);
      }
    };
    auto *header = new HeaderComponent(nameWidth, optColW);
    header->setSize(tableW, rowH);
    header->setBounds(0, 0, tableW, rowH);
    tableContainer.addAndMakeVisible(header);

    int y = rowH;

    // Virtual Keyboard (input only)
    {
      juce::String id = "VirtualKeyboard";
      juce::String name = "In: Virtual Keyboard";
      bool enabled = callbacks.isInputEnabled && callbacks.isInputEnabled(id);
      AppState::MidiDeviceOptions opts = callbacks.getMidiDeviceOptions ? callbacks.getMidiDeviceOptions(true, id) : AppState::MidiDeviceOptions{};
      addRow(y, true, id, name, enabled, opts, rowH, nameWidth, optColW);
      y += rowH;
    }
    for (const auto &d : inputs) {
      juce::String id = d.identifier;
      juce::String name = "In: " + d.name;
      bool enabled = callbacks.isInputEnabled && callbacks.isInputEnabled(id);
      AppState::MidiDeviceOptions opts = callbacks.getMidiDeviceOptions ? callbacks.getMidiDeviceOptions(true, id) : AppState::MidiDeviceOptions{};
      addRow(y, true, id, name, enabled, opts, rowH, nameWidth, optColW);
      y += rowH;
    }
    for (const auto &d : outputs) {
      juce::String id = d.identifier;
      juce::String name = "Out: " + d.name;
      bool enabled = callbacks.isOutputEnabled && callbacks.isOutputEnabled(id);
      AppState::MidiDeviceOptions opts = callbacks.getMidiDeviceOptions ? callbacks.getMidiDeviceOptions(false, id) : AppState::MidiDeviceOptions{};
      addRow(y, false, id, name, enabled, opts, rowH, nameWidth, optColW);
      y += rowH;
    }

    tableContainer.setSize(juce::jmax(viewport.getWidth(), tableW), juce::jmax(viewport.getHeight(), y));
  }

  void resized() override {
    viewport.setBounds(getLocalBounds());
    int tw = getWidth() - (viewport.getScrollBarThickness());
    tableContainer.setSize(juce::jmax(tw, 200 + 5 * 48), tableContainer.getHeight());
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgPanel);
  }

private:
  std::vector<std::unique_ptr<juce::Component>> rows;

  juce::Viewport viewport;
  juce::Component tableContainer;
  Callbacks callbacks;

  void addRow(int y, bool isInput, const juce::String &deviceId, const juce::String &displayName,
              bool enabled, AppState::MidiDeviceOptions opts, int rowH, int nameWidth, int colWidth) {
    auto *row = new juce::Component();
    row->setSize(nameWidth + 5 * colWidth, rowH);
    row->setBounds(0, y, row->getWidth(), rowH);
    tableContainer.addAndMakeVisible(row);

    juce::Label *lbl = new juce::Label();
    lbl->setText(displayName, juce::dontSendNotification);
    lbl->setColour(juce::Label::textColourId, enabled ? Theme::text : Theme::text.withAlpha(0.5f));
    lbl->setFont(Fonts::body().withHeight(11.0f));
    lbl->setBounds(4, 0, nameWidth - 8, rowH);
    row->addAndMakeVisible(lbl);

    auto addCheck = [row, rowH, nameWidth, colWidth, this](int colIndex, bool value, const char *tooltip,
                                                          std::function<void(bool)> onToggle) {
      juce::ToggleButton *t = new juce::ToggleButton();
      t->setToggleState(value, juce::dontSendNotification);
      t->setTooltip(tooltip);
      t->setBounds(nameWidth + colIndex * colWidth + 4, 2, colWidth - 8, rowH - 4);
      t->onClick = [this, onToggle, t]() {
        if (onToggle) onToggle(t->getToggleState());
      };
      row->addAndMakeVisible(t);
      return t;
    };

    // Column 0: On (enable/disable device)
    juce::ToggleButton *btnOn = new juce::ToggleButton();
    btnOn->setToggleState(enabled, juce::dontSendNotification);
    btnOn->setTooltip("Enable this MIDI port (open device).");
    btnOn->setBounds(nameWidth + 4, 2, colWidth - 8, rowH - 4);
    btnOn->onClick = [this, isInput, deviceId, btnOn, lbl]() {
      if (isInput && callbacks.onInputToggle) callbacks.onInputToggle(deviceId);
      if (!isInput && callbacks.onOutputToggle) callbacks.onOutputToggle(deviceId);
      juce::Component::SafePointer<MidiPortsTablePanel> safe(this);
      juce::Timer::callAfterDelay(120, [safe]() {
        if (safe != nullptr) safe->refresh();
      });
    };
    row->addAndMakeVisible(btnOn);

    addCheck(1, opts.track, "Track: notes/CC for this port", [this, isInput, deviceId](bool v) {
      AppState::MidiDeviceOptions o = callbacks.getMidiDeviceOptions ? callbacks.getMidiDeviceOptions(isInput, deviceId) : AppState::MidiDeviceOptions{};
      o.track = v;
      if (callbacks.setMidiDeviceOptions) callbacks.setMidiDeviceOptions(isInput, deviceId, o);
    });
    addCheck(2, opts.sync, "Sync: clock from this port", [this, isInput, deviceId](bool v) {
      AppState::MidiDeviceOptions o = callbacks.getMidiDeviceOptions ? callbacks.getMidiDeviceOptions(isInput, deviceId) : AppState::MidiDeviceOptions{};
      o.sync = v;
      if (callbacks.setMidiDeviceOptions) callbacks.setMidiDeviceOptions(isInput, deviceId, o);
    });
    addCheck(3, opts.remote, "Remote: transport control", [this, isInput, deviceId](bool v) {
      AppState::MidiDeviceOptions o = callbacks.getMidiDeviceOptions ? callbacks.getMidiDeviceOptions(isInput, deviceId) : AppState::MidiDeviceOptions{};
      o.remote = v;
      if (callbacks.setMidiDeviceOptions) callbacks.setMidiDeviceOptions(isInput, deviceId, o);
    });
    addCheck(4, opts.mpe, "MPE", [this, isInput, deviceId](bool v) {
      AppState::MidiDeviceOptions o = callbacks.getMidiDeviceOptions ? callbacks.getMidiDeviceOptions(isInput, deviceId) : AppState::MidiDeviceOptions{};
      o.mpe = v;
      if (callbacks.setMidiDeviceOptions) callbacks.setMidiDeviceOptions(isInput, deviceId, o);
    });

    rows.push_back(std::unique_ptr<juce::Component>(row));
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPortsTablePanel)
};
