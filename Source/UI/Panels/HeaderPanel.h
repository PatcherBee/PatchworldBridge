/*
  ==============================================================================
    Source/UI/Panels/HeaderPanel.h
    Role: Compact top bar with Modules button and OSC status.
    Network/MIDI config moved to Menu dropdown.
  ==============================================================================
*/
#pragma once
#include "../UI/Theme.h"
#include "../Widgets/Indicators.h"
#include <juce_gui_basics/juce_gui_basics.h>

class HeaderPanel : public juce::Component {
public:
  HeaderPanel() {
    addChildComponent(btnModules);
    btnModules.setColour(juce::TextButton::buttonColourId,
                         Theme::bgPanel.brighter(0.2f));
  }

  void resized() override {
    btnModules.setBounds(0, 0, 0, 0);
  }

  void paint(juce::Graphics &g) override {
    g.setColour(Theme::bgPanel.withAlpha(0.8f));
    g.fillAll();
    g.setColour(Theme::accent.withAlpha(0.2f));
    g.drawHorizontalLine(getHeight() - 1, 0.0f, (float)getWidth());
  }

  juce::TextButton btnModules{"Modules"};
  ConnectionLight ledOsc;
  juce::Label lblOsc;

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeaderPanel)
};
