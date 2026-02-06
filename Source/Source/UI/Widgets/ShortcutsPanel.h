/*
  ==============================================================================
    Source/UI/Widgets/ShortcutsPanel.h
    Role: Keyboard shortcuts cheat sheet (for Menu > Keyboard shortcuts).
  ==============================================================================
*/
#pragma once
#include "../../Core/ShortcutManager.h"
#include "../Theme.h"
#include "../Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ShortcutsPanel : public juce::Component {
public:
  ShortcutsPanel() {
    setSize(380, 420);
    addAndMakeVisible(title);
    title.setText("Keyboard shortcuts", juce::dontSendNotification);
    title.setFont(Fonts::headerLarge().withHeight(16.0f));
    title.setColour(juce::Label::textColourId, Theme::text);
    addAndMakeVisible(list);
    list.setReadOnly(true);
    list.setMultiLine(true);
    list.setScrollbarsShown(true);
    list.setColour(juce::TextEditor::backgroundColourId, Theme::bgDark);
    list.setColour(juce::TextEditor::textColourId, Theme::text);
    list.setFont(Fonts::body());
    refresh();
  }

  void refresh() {
    juce::String text;
    auto& sm = ShortcutManager::instance();
    auto actions = sm.getAllActions();
    for (const auto& [id, desc] : actions) {
      juce::String key = sm.getShortcut(id).getTextDescription();
      if (desc.isNotEmpty() && key.isNotEmpty())
        text << desc << "\t" << key << "\n";
    }
    list.setText(text.trimEnd());
  }

  void resized() override {
    auto r = getLocalBounds().reduced(10);
    title.setBounds(r.removeFromTop(28));
    r.removeFromTop(4);
    list.setBounds(r);
  }

  void paint(juce::Graphics& g) override {
    g.fillAll(Theme::bgPanel);
    g.setColour(Theme::accent.withAlpha(0.3f));
    g.drawRect(getLocalBounds(), 1);
  }

private:
  juce::Label title;
  juce::TextEditor list;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShortcutsPanel)
};
