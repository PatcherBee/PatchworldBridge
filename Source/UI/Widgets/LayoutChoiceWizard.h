/*
  ==============================================================================
    Source/UI/Widgets/LayoutChoiceWizard.h
    Role: First-run layout picker – 2 boxes (Minimal, Full). Shown once until Reset to defaults.
  ==============================================================================
*/
#pragma once
#include "../Theme.h"
#include "../Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>

class LayoutChoiceWizard : public juce::Component {
public:
  std::function<void(const juce::String&)> onLayoutChosen;

  LayoutChoiceWizard() {
    setAlwaysOnTop(true);
    setOpaque(true);
    for (int i = 0; i < 2; ++i) {
      boxes[i].setButtonText(names[i]);
      boxes[i].setTooltip(i == 0 ? "Editor, OSC Log, Playlist only." : "3×3 grid: all 9 modules (Log, Playlist, Mixer | Editor, Sequencer, LFO | Arp, Chords, Macros).");
      boxes[i].setClickingTogglesState(false);
      boxes[i].setColour(juce::TextButton::buttonColourId, Theme::bgPanel.brighter(0.1f));
      boxes[i].setColour(juce::TextButton::buttonOnColourId, Theme::accent.darker(0.2f));
      boxes[i].onClick = [this, i] {
        if (onLayoutChosen)
          onLayoutChosen(names[i]);
      };
      addAndMakeVisible(boxes[i]);
    }
  }

  void paint(juce::Graphics& g) override {
    g.fillAll(juce::Colours::black.withAlpha(0.88f));
    g.setColour(Theme::text);
    g.setFont(Fonts::headerLarge().withHeight(18.0f));
    g.drawText("Choose your layout", getLocalBounds().removeFromTop(80).reduced(20),
               juce::Justification::centred, true);
    g.setFont(Fonts::body().withHeight(13.0f));
    g.drawText("Minimal: Editor + Log + Playlist. Full: 3×3 grid (Log|Editor|Arp, Playlist|Sequencer|Chords, Mixer|LFO|Macros). Change later via Connections → Layout.",
               getLocalBounds().withTrimmedTop(78).withHeight(44).reduced(24, 0),
               juce::Justification::centred, true);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(40).withTrimmedTop(120);
    int boxW = (r.getWidth() - 24) / 2;
    for (int i = 0; i < 2; ++i) {
      boxes[i].setBounds(r.removeFromLeft(boxW + (i == 1 ? r.getWidth() : 16)).reduced(8));
    }
  }

private:
  static constexpr const char* names[2] = { "Minimal", "Full" };
  juce::TextButton boxes[2];
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LayoutChoiceWizard)
};
