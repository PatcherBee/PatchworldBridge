/*
  ==============================================================================
    Source/UI/Panels/MidiDevicePickerPanel.h
    Role: Multi-select MIDI devices - stays open until user clicks outside.
  ==============================================================================
*/
#pragma once
#include "../Theme.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

class MidiDevicePickerPanel : public juce::Component {
public:
  bool isInput = true;

  MidiDevicePickerPanel(bool forInput)
      : isInput(forInput) {
    addAndMakeVisible(list);
    list.setModel(&model);
    list.setRowHeight(22);
    list.getViewport()->setScrollBarsShown(true, false);
  }

  void setDevices(const juce::Array<juce::MidiDeviceInfo> &devices,
                  const juce::StringArray &activeIds) {
    deviceInfos = devices;
    activeIdentifiers = activeIds;
    list.updateContent();
    repaint();
  }

  juce::StringArray getSelectedIds() const { return activeIdentifiers; }

  void resized() override {
    list.setBounds(getLocalBounds().reduced(4).withTrimmedTop(22));
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgPanel);
    g.setColour(Theme::text.withAlpha(0.8f));
    g.setFont(13.0f);
    g.drawText(isInput ? "MIDI Inputs (click to toggle)" : "MIDI Outputs (click to toggle)",
               4, 2, getWidth() - 8, 20, juce::Justification::centredLeft);
    g.setColour(Theme::accent.withAlpha(0.4f));
    g.drawRect(getLocalBounds(), 1);
  }

  juce::ListBox list;

  class Model : public juce::ListBoxModel {
  public:
    MidiDevicePickerPanel &owner;
    Model(MidiDevicePickerPanel &o) : owner(o) {}

    int getNumRows() override { return owner.deviceInfos.size(); }

    void paintListBoxItem(int row, juce::Graphics &g, int w, int h,
                         bool selected) override {
      if (row < 0 || row >= owner.deviceInfos.size())
        return;
      g.fillAll(selected ? Theme::accent.withAlpha(0.3f)
                         : juce::Colours::transparentBlack);
      g.setColour(Theme::text);
      g.setFont(12.0f);
      g.drawText(owner.deviceInfos[row].name, 8, 0, w - 16, h,
                 juce::Justification::centredLeft);
      bool isOn = owner.activeIdentifiers.contains(owner.deviceInfos[row].identifier);
      g.setColour(isOn ? Theme::accent : Theme::text.withAlpha(0.4f));
      g.drawText(isOn ? "[ON]" : "[--]", w - 50, 0, 45, h,
                 juce::Justification::centredRight);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent &) override {
      if (row < 0 || row >= owner.deviceInfos.size())
        return;
      juce::String id = owner.deviceInfos[row].identifier;
      int idx = owner.activeIdentifiers.indexOf(id);
      bool enable;
      if (idx >= 0) {
        owner.activeIdentifiers.remove(idx);
        enable = false;
      } else {
        owner.activeIdentifiers.add(id);
        enable = true;
      }
      owner.list.repaintRow(row);
      if (owner.onDeviceToggled)
        owner.onDeviceToggled(id, enable);
    }
  };

  std::function<void(const juce::String &, bool)> onDeviceToggled;

private:
  juce::Array<juce::MidiDeviceInfo> deviceInfos;
  juce::StringArray activeIdentifiers;
  Model model{*this};
};
