/*
  ==============================================================================
    Source/UI/Widgets/ModuleHeader.h
    Role: Consistent module header with title, preset selector, menu button.
  ==============================================================================
*/
#pragma once
#include "../Theme.h"
#include "../Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class ModuleHeader : public juce::Component {
public:
  juce::String title;
  juce::ComboBox cmbPreset;
  juce::TextButton btnSave{"S"};
  juce::TextButton btnMenu{"≡"};
  std::function<void()> onMenuClicked;
  std::function<void(int)> onPresetChanged;

  explicit ModuleHeader(const juce::String& titleText) : title(titleText) {
    addAndMakeVisible(cmbPreset);
    cmbPreset.setTooltip("Select preset");
    cmbPreset.onChange = [this] {
      if (onPresetChanged) onPresetChanged(cmbPreset.getSelectedId());
    };
    addAndMakeVisible(btnSave);
    btnSave.setTooltip("Save preset");
    addAndMakeVisible(btnMenu);
    btnMenu.setTooltip("Module options");
    btnMenu.onClick = [this] {
      if (onMenuClicked) onMenuClicked();
    };
  }

  void paint(juce::Graphics& g) override {
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient grad(
        Theme::bgPanel.brighter(0.12f), 0.0f, 0.0f,
        Theme::bgPanel, 0.0f, r.getHeight(), false);
    g.setGradientFill(grad);
    g.fillRect(r);
    g.setColour(Theme::text);
    g.setFont(Fonts::bodyBold());
    g.drawText(title, r.reduced(8.0f, 0.0f), juce::Justification::centredLeft);
    g.setColour(Theme::accent.withAlpha(0.4f));
    g.fillRect(0.0f, r.getBottom() - 1.0f, r.getWidth(), 1.0f);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(4, 2);
    btnMenu.setBounds(r.removeFromRight(24).reduced(2));
    btnSave.setBounds(r.removeFromRight(24).reduced(2));
    r.removeFromRight(5);
    cmbPreset.setBounds(r.removeFromRight(100).reduced(2));
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleHeader)
};
