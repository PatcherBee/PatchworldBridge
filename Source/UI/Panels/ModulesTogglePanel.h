/*
  ==============================================================================
    Source/UI/Panels/ModulesTogglePanel.h
    Role: Toggle module visibility - stays open until user clicks away.
  ==============================================================================
*/
#pragma once
#include "../LayoutHelpers.h"
#include "../Theme.h"
#include "../Fonts.h"
#include "../Widgets/ModuleWindow.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ModulesTogglePanel : public juce::Component {
public:
  /** Optional: called when a module is shown/hidden so the host can mark repaint dirty and avoid module going black. */
  std::function<void(ModuleWindow *)> onModuleVisibilityChanged;

  ModulesTogglePanel() {
    addAndMakeVisible(lblTitle);
    lblTitle.setText("Toggle Modules (click to show/hide)", juce::dontSendNotification);
    lblTitle.setFont(Fonts::bodyBold());
    lblTitle.setColour(juce::Label::textColourId, Theme::text);
    btnShowAll.setButtonText("Show all");
    btnShowAll.onClick = [this] { showAll(); };
    addAndMakeVisible(btnShowAll);
    btnHideAll.setButtonText("Hide all");
    btnHideAll.onClick = [this] { hideAll(); };
    addAndMakeVisible(btnHideAll);
  }

  void setModules(
      ModuleWindow *editor, ModuleWindow *mixer, ModuleWindow *sequencer,
      ModuleWindow *playlist, ModuleWindow *arp, ModuleWindow *macros,
      ModuleWindow *log, ModuleWindow *chords, ModuleWindow *control,
      ModuleWindow *lfoGen = nullptr) {
    windows[0] = editor;
    windows[1] = mixer;
    windows[2] = sequencer;
    windows[3] = playlist;
    windows[4] = arp;
    windows[5] = macros;
    windows[6] = log;
    windows[7] = chords;
    windows[8] = control;
    windows[9] = lfoGen;
    names[0] = "Editor";
    names[1] = "Mixer";
    names[2] = "Sequencer";
    names[3] = "Playlist";
    names[4] = "Arpeggiator";
    names[5] = "Macros";
    names[6] = "Log";
    names[7] = "Chords";
    names[8] = "Control";
    names[9] = "LFO Generator";
    buildToggles();
  }

  void resized() override {
    auto r = getLocalBounds().reduced(6);
    lblTitle.setBounds(r.removeFromTop(24));
    r.removeFromTop(4);
    auto btnRow = r.removeFromBottom(32).reduced(2);
    LayoutHelpers::layoutHorizontally(btnRow, 4, {&btnShowAll, &btnHideAll});
    for (int i = 0; i < 10; ++i) {
      if (windows[i] != nullptr)
        toggles[i].setBounds(r.removeFromTop(28).reduced(0, 2));
    }
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgPanel);
    g.setColour(Theme::accent.withAlpha(0.4f));
    g.drawRect(getLocalBounds(), 1);
  }

private:
  juce::Label lblTitle;
  juce::TextButton btnShowAll;
  juce::TextButton btnHideAll;
  ModuleWindow *windows[10] = {nullptr};
  juce::String names[10];
  juce::ToggleButton toggles[10];

  void showAll() {
    for (int i = 0; i < 10; ++i) {
      if (windows[i]) {
        windows[i]->setVisible(true);
        windows[i]->toFront(true);
        toggles[i].setToggleState(true, juce::dontSendNotification);
        if (onModuleVisibilityChanged)
          onModuleVisibilityChanged(windows[i]);
      }
    }
  }

  void hideAll() {
    for (int i = 0; i < 10; ++i) {
      if (windows[i]) {
        windows[i]->setVisible(false);
        toggles[i].setToggleState(false, juce::dontSendNotification);
      }
    }
  }

  void buildToggles() {
    for (int i = 0; i < 10; ++i) {
      if (windows[i] == nullptr) continue;
      toggles[i].setButtonText(names[i]);
      toggles[i].setToggleState(windows[i]->isVisible(), juce::dontSendNotification);
      toggles[i].onClick = [this, i] {
        if (windows[i]) {
          windows[i]->setVisible(!windows[i]->isVisible());
          if (windows[i]->isVisible()) {
            windows[i]->toFront(true);
            if (onModuleVisibilityChanged)
              onModuleVisibilityChanged(windows[i]);
          }
          toggles[i].setToggleState(windows[i]->isVisible(), juce::dontSendNotification);
        }
      };
      addAndMakeVisible(toggles[i]);
    }
  }
};
