#pragma once

#include "../Audio/OscTypes.h"
#include "Panels/ConfigControls.h"
#include <juce_gui_basics/juce_gui_basics.h>

/** Content for the OSC Addresses dialog: list of OSC addresses to type/change (in/out). */
class OscAddressDialogContent : public juce::Component {
public:
  OscAddressDialogContent() {
    addAndMakeVisible(viewport);
    config.addressesVisible = true;
    viewport.setViewedComponent(&config, false);
    config.setSize(460, 960);

    addAndMakeVisible(btnClose);
    btnClose.setButtonText("Close");
    btnClose.onClick = [this] {
      if (onRequestClose)
        onRequestClose(0);
    };

    config.onSchemaApplied = [this](const OscNamingSchema &schema) {
      if (onApplySchema)
        onApplySchema(schema);
      if (onRequestClose)
        onRequestClose(1);
    };

    // Default: close dialog by finding parent (JUCE 8 launchAsync; no runModalLoop)
    onRequestClose = [this](int code) {
      if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(code);
    };
  }

  void refresh() {
    if (onLoadSchema) {
      OscNamingSchema s = onLoadSchema();
      config.applySchema(s);
    }
    config.updatePreview();
  }

  std::function<OscNamingSchema()> onLoadSchema;
  std::function<void(const OscNamingSchema &)> onApplySchema;
  std::function<void(int)> onRequestClose;

  void resized() override {
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom(36);
    btnClose.setBounds(bottom.removeFromRight(100).reduced(4));
    viewport.setBounds(r);
  }

private:
  juce::Viewport viewport;
  OscAddressConfig config;
  juce::TextButton btnClose;
};
