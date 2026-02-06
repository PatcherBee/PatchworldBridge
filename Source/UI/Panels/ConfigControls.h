#pragma once
#ifndef CONTROLS_H
#define CONTROLS_H
// --- 1. C++ Standard Library ---
#include <functional>
#include <string>
#include <vector>

// --- 2. JUCE Framework ---
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "../Audio/OscTypes.h"
#include "../Services/MidiMappingService.h"
#include "../Theme.h"
#include "../Fonts.h"
#include "../Widgets/MorphSlider.h"
#include "../Widgets/PerformanceXYPad.h"
#include "../Widgets/ProKnob.h"

// Helper to keep track of all editors
struct AddrEditor {
  juce::TextEditor *editor;
  juce::Label *label;
};

class OscAddressConfig : public juce::Component {
public:
  bool addressesVisible = false;
  juce::Label lblTitle{{}, "OSC Addresses"};
  juce::TextEditor ePlay, eStop, eRew, eLoop, eTap, eOctUp, eOctDn, ePanic;
  juce::Label lblGui{{}, "GUI Control"};

  // OUT
  juce::Label lOUTn{{}, "OUT Note:"}, lOUTv{{}, "OUT Vel:"},
      lOUToff{{}, "OUT Off:"}, lOUTcc{{}, "OUT CC:"},
      lOUTccv{{}, "OUT CC Val:"}, lOUTp{{}, "OUT Pitch:"},
      lOUTpr{{}, "OUT Press:"}, lOUTpoly{{}, "OUT PolyAT:"};
  juce::TextEditor eOUTn, eOUTv, eOUToff, eOUTcc, eOUTccv, eOUTp, eOUTpr,
      eOUTpoly;

  // IN
  juce::Label lINn{{}, "IN Note:"}, lINnv{{}, "IN Vel:"},
      lINnoff{{}, "IN Off:"};
  juce::TextEditor eINn, eINnv, eINnoff;

  juce::TextEditor eVol1, eVol2;

  juce::Label lINc{{}, "IN CC #:"}, lINcv{{}, "IN CC Val:"},
      lINwheel{{}, "IN Wheel:"}, lINpress{{}, "IN Press:"},
      lINpoly{{}, "IN PolyAT:"};
  juce::TextEditor eINc, eINcv, eINwheel, eINpress, eINpoly;

  juce::Label lPlay{{}, "Play:"}, lStop{{}, "Stop:"}, lRew{{}, "Rew:"},
      lLoop{{}, "Loop:"}, lTap{{}, "Tap:"}, lOctUp{{}, "Oct+:"},
      lOctDn{{}, "Oct-:"}, lPanic{{}, "Panic:"};

  juce::Label lMixVol{{}, "Mixer Vol:"}, lMixMute{{}, "Mixer Mute:"},
      lArpS{{}, "Arp Spd:"}, lArpV{{}, "Arp Vel:"};
  juce::TextEditor eMixVol, eMixMute, eArpS, eArpV;

  std::vector<AddrEditor> allEditors;

  OscAddressConfig();
  void setup(juce::Label &l, juce::TextEditor &e, juce::String def);
  void paint(juce::Graphics &g) override;
  void resized() override;

  // Callback when OSC addresses are changed
  std::function<void()> onSchemaChanged;
  std::function<void(const OscNamingSchema &)> onSchemaApplied;
  std::function<void()> onTestNoteRequested;

  juce::Label lblPreset{{}, "Preset:"};
  juce::ComboBox cmbPreset;
  juce::TextButton btnApply, btnReset, btnTest;
  juce::Label lblPreview{{}, "Live Preview:"};
  juce::Label lblExample;

  void loadPreset(int presetId);
  void updatePreview();
  void mouseDown(const juce::MouseEvent &event) override;

  // Validate for conflicts
  void validateConflicts() {
    // Reset all to default color first
    for (auto &ae : allEditors) {
      ae.editor->setColour(juce::TextEditor::backgroundColourId,
                           juce::Colours::black);
      ae.editor->setColour(juce::TextEditor::textColourId,
                           juce::Colours::white);
    }

    // Check for duplicates
    for (size_t i = 0; i < allEditors.size(); ++i) {
      for (size_t j = i + 1; j < allEditors.size(); ++j) {
        juce::String t1 = allEditors[i].editor->getText();
        juce::String t2 = allEditors[j].editor->getText();

        if (t1.isNotEmpty() && t1 == t2) {
          // HIGHLIGHT CONFLICT
          allEditors[i].editor->setColour(juce::TextEditor::backgroundColourId,
                                          juce::Colours::darkred);
          allEditors[j].editor->setColour(juce::TextEditor::backgroundColourId,
                                          juce::Colours::darkred);
        }
      }
    }
  }

  // Helper to get current schema values
  juce::String getNotePrefix() const {
    return eOUTn.getText().upToFirstOccurrenceOf("{X}", false, true);
  }
  juce::String getNoteSuffix() const {
    return eOUTn.getText().fromFirstOccurrenceOf("{X}", false, true);
  }
  juce::String getCcPrefix() const {
    return eOUTcc.getText().upToFirstOccurrenceOf("{X}", false, true);
  }
  juce::String getCcSuffix() const {
    return eOUTcc.getText().fromFirstOccurrenceOf("{X}", false, true);
  }
  juce::String getPitchPrefix() const {
    return eOUTp.getText().upToFirstOccurrenceOf("{X}", false, true);
  }
  juce::String getPitchSuffix() const {
    return eOUTp.getText().fromFirstOccurrenceOf("{X}", false, true);
  }

  // Full schema for sync (includes IN/OUT, BPM, etc.)
  OscNamingSchema getSchema() const {
    OscNamingSchema s;
    s.outNotePrefix = eOUTn.getText().upToFirstOccurrenceOf("{X}", false, true);
    s.outNoteSuffix = eOUTn.getText().fromFirstOccurrenceOf("{X}", false, true);
    s.outNoteOff = eOUToff.getText().fromFirstOccurrenceOf("{X}", false, true);
    s.outCC = eOUTcc.getText().fromFirstOccurrenceOf("{X}", false, true);
    s.outPitch = eOUTp.getText().fromFirstOccurrenceOf("{X}", false, true);
    s.outPressure = eOUTpr.getText().fromFirstOccurrenceOf("{X}", false, true);
    s.inNotePrefix = eINn.getText().upToFirstOccurrenceOf("{X}", false, true);
    s.inNoteSuffix = eINn.getText().fromFirstOccurrenceOf("{X}", false, true);
    s.inNoteOff = eINnoff.getText().fromFirstOccurrenceOf("{X}", false, true);
    s.inCC = eINc.getText().fromFirstOccurrenceOf("{X}", false, true);
    s.inWheel = eINwheel.getText().fromFirstOccurrenceOf("{X}", false, true);
    s.inPress = eINpress.getText().fromFirstOccurrenceOf("{X}", false, true);
    s.playAddr = ePlay.getText();
    s.stopAddr = eStop.getText();
    s.bpmAddr = "/clock/bpm";
    s.inProgramChange = "pc";
    s.outProgramChange = "pc";
    s.inPolyAftertouch = "pat";
    s.outPolyAftertouch = "pat";
    s.notePrefix = s.outNotePrefix;
    s.noteSuffix = s.outNoteSuffix;
    s.noteOffSuffix = s.outNoteOff;
    s.ccPrefix = s.outNotePrefix;
    s.ccSuffix = s.outCC;
    s.pitchPrefix = s.outNotePrefix;
    s.pitchSuffix = s.outPitch;
    s.aftertouchSuffix = s.outPressure;
    return s;
  }

  void applySchema(const OscNamingSchema &s) {
    eOUTn.setText(s.notePrefix + "{X}" + s.noteSuffix);
    eOUToff.setText(s.notePrefix + "{X}" + s.noteOffSuffix);
    eOUTcc.setText(s.ccPrefix + "{X}" + s.ccSuffix);
    eOUTp.setText(s.pitchPrefix + "{X}" + s.pitchSuffix);
    eOUTpr.setText(s.notePrefix + "{X}" + s.aftertouchSuffix);
    ePlay.setText(s.playAddr);
    eStop.setText(s.stopAddr);
    eINn.setText(s.inNotePrefix + "{X}" + s.inNoteSuffix);
    eINnoff.setText(s.inNotePrefix + "{X}" + s.inNoteOff);
    eINc.setText(s.inNotePrefix + "{X}" + s.inCC);
    eINwheel.setText(s.inNotePrefix + "{X}" + s.inWheel);
    eINpress.setText(s.inNotePrefix + "{X}" + s.inPress);
  }
};

class ControlPage : public juce::Component {
public:
  struct GenericControl : public juce::Component {
    juce::Slider slider;
    juce::TextButton button;
    juce::TextEditor addrBox;
    bool isSlider = true;
    std::function<void(juce::String, float)> onAction;

    GenericControl(bool sliderMode, juce::String defaultAddr);
    void resized() override;
  };

  juce::OwnedArray<GenericControl> controls;

  void setMidiMappingManager(MidiMappingService *m) {
    this->mappingManager = m;
  }

  PerformanceXYPad xyPad;
  MorphSlider morphSlider;
  std::function<void(float, float)> onXYPadChanged;
  std::function<void(float)> onMorphChanged;

  ControlPage();
  void resized() override;

private:
  MidiMappingService *mappingManager = nullptr;
};

class MacroControls : public juce::Component {
public:
  struct MacroFader : public juce::Component {
    ProKnob knob;
    juce::Label label;
    juce::String paramId;
    std::function<void(float)> onSlide;
    MacroFader(juce::String name, juce::String pid);
    void resized() override;
  };

  struct MacroButton : public juce::Component {
    juce::TextButton btn;
    juce::String paramId;
    std::function<void(bool)> onTrigger;
    MacroButton(juce::String name, juce::String pid);
    void resized() override { btn.setBounds(getLocalBounds()); }
  };

  juce::OwnedArray<MacroFader> faders;
  juce::OwnedArray<MacroButton> buttons;

  MacroControls();
  void resized() override;
};

// --- IMPLEMENTATIONS (Inline for simplicity) ---

inline OscAddressConfig::OscAddressConfig() {
  allEditors.reserve(40); // Pre-allocate

  addAndMakeVisible(lblTitle);
  lblTitle.setFont(juce::FontOptions(16.0f).withStyle("Bold"));

  addAndMakeVisible(lblPreset);
  lblPreset.setText("Preset:", juce::dontSendNotification);
  addAndMakeVisible(cmbPreset);
  cmbPreset.addItem("Patchworld Default", 1);
  cmbPreset.addItem("TouchOSC", 2);
  cmbPreset.addItem("Lemur", 3);
  cmbPreset.addItem("Pure Data", 4);
  cmbPreset.addItem("Max/MSP", 5);
  cmbPreset.addItem("OSC-MIDI Bridge", 6);
  cmbPreset.addItem("Custom", 99);
  cmbPreset.setSelectedId(1, juce::dontSendNotification);
  cmbPreset.onChange = [this] { loadPreset(cmbPreset.getSelectedId()); };

  addAndMakeVisible(btnApply);
  btnApply.setButtonText("Apply Changes");
  btnApply.onClick = [this] {
    if (onSchemaApplied)
      onSchemaApplied(getSchema());
  };
  addAndMakeVisible(btnReset);
  btnReset.setButtonText("Reset");
  btnReset.onClick = [this] { loadPreset(1); };
  addAndMakeVisible(btnTest);
  btnTest.setButtonText("Test Note");
  btnTest.onClick = [this] {
    if (onTestNoteRequested)
      onTestNoteRequested();
  };

  addAndMakeVisible(lblPreview);
  addAndMakeVisible(lblExample);
  lblExample.setFont(Fonts::small().withHeight(11.0f));
  updatePreview();

  setup(lOUTn, eOUTn, "/ch{X}note");
  setup(lOUTv, eOUTv, "/ch{X}nvalue");
  setup(lOUToff, eOUToff, "/ch{X}noteoff");
  setup(lOUTcc, eOUTcc, "/ch{X}cc");
  setup(lOUTccv, eOUTccv, "/ch{X}ccvalue");
  setup(lOUTp, eOUTp, "/ch{X}pitch");
  setup(lOUTpr, eOUTpr, "/ch{X}pressure");
  setup(lOUTpoly, eOUTpoly, "/ch{X}pressure");
  setup(lINn, eINn, "/ch{X}n");
  setup(lINnv, eINnv, "/ch{X}nv");
  setup(lINnoff, eINnoff, "/ch{X}noff");
  setup(lINc, eINc, "/ch{X}c");
  setup(lINcv, eINcv, "/ch{X}cv");
  setup(lINwheel, eINwheel, "/ch{X}wheel");
  setup(lINpress, eINpress, "/ch{X}press");
  setup(lINpoly, eINpoly, "/ch{X}press");
  addAndMakeVisible(lblGui);
  lblGui.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
  setup(lPlay, ePlay, "/play");
  setup(lStop, eStop, "/stop");
  setup(lRew, eRew, "/rewind");
  setup(lLoop, eLoop, "/loop");
  setup(lTap, eTap, "/tap");
  setup(lOctUp, eOctUp, "/octup");
  setup(lOctDn, eOctDn, "/octdown");
  setup(lPanic, ePanic, "/panic");
  setup(lMixVol, eMixVol, "/mix/{X}vol");
  setup(lMixMute, eMixMute, "/mix/{X}mute");
  setup(lArpS, eArpS, "/arpspeed");
  setup(lArpV, eArpV, "/arpvel");

  // Custom manual generic setup for these two as they aren't using the setup()
  // helper in original code
  addAndMakeVisible(eVol1);
  eVol1.setText("/ch1vol");
  eVol1.onTextChange = [this] {
    if (onSchemaChanged)
      onSchemaChanged();
  };
  allEditors.push_back({&eVol1, nullptr});

  addAndMakeVisible(eVol2);
  eVol2.setText("/ch2vol");
  eVol2.onTextChange = [this] {
    if (onSchemaChanged)
      onSchemaChanged();
  };
  allEditors.push_back({&eVol2, nullptr});

  addMouseListener(this, true);
  setSize(450, 950);
}

inline void OscAddressConfig::mouseDown(const juce::MouseEvent &event) {
  if (!event.mods.isRightButtonDown())
    return;
  auto *ed = dynamic_cast<juce::TextEditor *>(event.originalComponent);
  if (!ed)
    return;
  for (const auto &ae : allEditors) {
    if (ae.editor == ed) {
      juce::PopupMenu m;
      m.addItem(1, "Copy address");
      m.showMenuAsync(juce::PopupMenu::Options()
                          .withTargetComponent(ed)
                          .withParentComponent(nullptr)
                          .withStandardItemHeight(24),
          [ed](int result) {
            if (result == 1)
              juce::SystemClipboard::copyTextToClipboard(ed->getText());
          });
      return;
    }
  }
}

inline void OscAddressConfig::setup(juce::Label &l, juce::TextEditor &e,
                                    juce::String def) {
  addAndMakeVisible(l);
  addAndMakeVisible(e);
  e.setText(def);
  e.onTextChange = [this] {
    cmbPreset.setSelectedId(99, juce::dontSendNotification);
    updatePreview();
    if (onSchemaChanged)
      onSchemaChanged();
  };

  allEditors.push_back({&e, &l});
}

inline void OscAddressConfig::updatePreview() {
  juce::String prefix = eOUTn.getText().upToFirstOccurrenceOf("{X}", false, true);
  juce::String suffix = eOUTn.getText().fromFirstOccurrenceOf("{X}", false, true);
  juce::String inPrefix = eINn.getText().upToFirstOccurrenceOf("{X}", false, true);
  juce::String inSuffix = eINn.getText().fromFirstOccurrenceOf("{X}", false, true);
  juce::String outNote = prefix + "1" + suffix;
  juce::String inNote = inPrefix + "1" + inSuffix;
  lblExample.setText("OUT Note C4 Ch1: " + outNote + " 60 100\n"
                     "IN Note: " + inNote + " [note] [vel]",
                     juce::dontSendNotification);
}

inline void OscAddressConfig::loadPreset(int presetId) {
  switch (presetId) {
  case 1: // Patchworld Default
    eOUTn.setText("/ch{X}note");
    eOUToff.setText("/ch{X}noteoff");
    eOUTcc.setText("/ch{X}cc");
    eOUTp.setText("/ch{X}pitch");
    eOUTpr.setText("/ch{X}pressure");
    eINn.setText("/ch{X}n");
    eINnoff.setText("/ch{X}noff");
    eINc.setText("/ch{X}c");
    eINwheel.setText("/ch{X}wheel");
    eINpress.setText("/ch{X}press");
    ePlay.setText("/play");
    eStop.setText("/stop");
    break;
  case 2: // TouchOSC
    eOUTn.setText("/1/{X}toggle");
    eOUToff.setText("/1/{X}toggle");
    eOUTcc.setText("/1/{X}fader");
    eINn.setText("/1/{X}toggle");
    eINc.setText("/1/{X}fader");
    break;
  case 3: // Lemur
    eOUTn.setText("/Keyboard/{X}x");
    eOUTcc.setText("/Faders/{X}x");
    eINn.setText("/Keyboard/{X}x");
    eINc.setText("/Faders/{X}x");
    break;
  case 4: // Pure Data
    eOUTn.setText("/pd/{X}note");
    eOUTcc.setText("/pd/{X}cc");
    eINn.setText("/pd/{X}note");
    eINc.setText("/pd/{X}cc");
    break;
  case 5: // Max/MSP
    eOUTn.setText("/max/midi/ch{X}/note");
    eOUTcc.setText("/max/midi/ch{X}/ctrl");
    eINn.setText("/max/midi/ch{X}/note");
    eINc.setText("/max/midi/ch{X}/ctrl");
    break;
  case 6: // OSC-MIDI Bridge
    eOUTn.setText("/midi/ch{X}/noteon");
    eOUToff.setText("/midi/ch{X}/noteoff");
    eOUTcc.setText("/midi/ch{X}/cc");
    eOUTp.setText("/midi/ch{X}/pitchbend");
    eINn.setText("/midi/ch{X}/noteon");
    eINnoff.setText("/midi/ch{X}/noteoff");
    eINc.setText("/midi/ch{X}/cc");
    break;
  default:
    break;
  }
  updatePreview();
  if (onSchemaChanged)
    onSchemaChanged();
}

inline void OscAddressConfig::paint(juce::Graphics &g) {
  g.fillAll(juce::Colours::black.withAlpha(0.95f));
  if (addressesVisible) {
    g.setColour(juce::Colours::cyan);
    g.drawRect(getLocalBounds(), 2);
  }
}
inline void OscAddressConfig::resized() {
  auto r = getLocalBounds().reduced(20);
  lblTitle.setBounds(r.removeFromTop(30));
  auto presetRow = r.removeFromTop(28);
  lblPreset.setBounds(presetRow.removeFromLeft(50));
  cmbPreset.setBounds(presetRow.removeFromLeft(140));
  presetRow.removeFromLeft(10);
  btnApply.setBounds(presetRow.removeFromLeft(90).reduced(2));
  btnReset.setBounds(presetRow.removeFromLeft(60).reduced(2));
  btnTest.setBounds(presetRow.removeFromLeft(70).reduced(2));
  r.removeFromTop(8);
  lblPreview.setBounds(r.removeFromTop(18));
  lblExample.setBounds(r.removeFromTop(36));
  r.removeFromTop(8);
  auto addRow = [&](juce::Label &l, juce::TextEditor &e) {
    if (!addressesVisible) {
      l.setVisible(false);
      e.setVisible(false);
      return;
    }
    l.setVisible(true);
    e.setVisible(true);
    auto row = r.removeFromTop(25);
    l.setBounds(row.removeFromLeft(70));
    e.setBounds(row);
    r.removeFromTop(5);
  };
  addRow(lOUTn, eOUTn);
  addRow(lOUTv, eOUTv);
  addRow(lOUToff, eOUToff);
  addRow(lOUTcc, eOUTcc);
  addRow(lOUTccv, eOUTccv);
  addRow(lOUTp, eOUTp);
  addRow(lOUTpr, eOUTpr);
  addRow(lOUTpoly, eOUTpoly);
  r.removeFromTop(10);
  addRow(lINn, eINn);
  addRow(lINnv, eINnv);
  addRow(lINnoff, eINnoff);
  addRow(lINc, eINc);
  addRow(lINcv, eINcv);
  addRow(lINwheel, eINwheel);
  addRow(lINpress, eINpress);
  addRow(lINpoly, eINpoly);
  r.removeFromTop(15);
  lblGui.setBounds(r.removeFromTop(25));
  addRow(lPlay, ePlay);
  addRow(lStop, eStop);
  addRow(lRew, eRew);
  addRow(lLoop, eLoop);
  addRow(lTap, eTap);
  addRow(lOctUp, eOctUp);
  addRow(lOctDn, eOctDn);
  addRow(lPanic, ePanic);
  r.removeFromTop(10);
  addRow(lMixVol, eMixVol);
  addRow(lMixMute, eMixMute);
  addRow(lArpS, eArpS);
  addRow(lArpV, eArpV);
}

inline ControlPage::GenericControl::GenericControl(bool sliderMode,
                                                   juce::String defaultAddr)
    : isSlider(sliderMode) {
  addrBox.setText(defaultAddr);
  addrBox.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
  addrBox.setColour(juce::TextEditor::outlineColourId, juce::Colours::grey);
  addrBox.setTooltip("OSC address (e.g. /ctrls/1) or MIDI CC: cc:ch:num (e.g. cc:1:74)");
  addAndMakeVisible(addrBox);
  if (isSlider) {
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 15);
    slider.setRange(0.0, 1.0, 0.01);
    slider.onValueChange = [this] {
      if (onAction)
        onAction(addrBox.getText(), (float)slider.getValue());
    };
    addAndMakeVisible(slider);
  } else {
    button.setButtonText("Trig");
    button.onClick = [this] {
      if (onAction)
        onAction(addrBox.getText(), 1.0f);
    };
    addAndMakeVisible(button);
  }
}
inline void ControlPage::GenericControl::resized() {
  auto r = getLocalBounds().reduced(2);
  addrBox.setBounds(r.removeFromBottom(20));
  if (isSlider)
    slider.setBounds(r);
  else
    button.setBounds(r.reduced(5));
}
inline ControlPage::ControlPage() {
  addAndMakeVisible(xyPad);
  addAndMakeVisible(morphSlider);
  morphSlider.onValueChange = [this] {
    if (onMorphChanged)
      onMorphChanged((float)morphSlider.getValue());
  };
  xyPad.onPositionChanged = [this](float x, float y) {
    if (onXYPadChanged)
      onXYPadChanged(x, y);
  };
  for (int i = 0; i < 4; ++i)
    controls.add(new GenericControl(true, "/ctrls/" + juce::String(i + 1)));
  for (int i = 0; i < 8; ++i)
    controls.add(new GenericControl(false, "/ctrlb/" + juce::String(i + 1)));
  for (auto *c : controls)
    addAndMakeVisible(c);
}
inline void ControlPage::resized() {
  auto r = getLocalBounds().reduced(15, 20);
  auto xyArea = r.removeFromTop(120);
  morphSlider.setBounds(xyArea.removeFromBottom(24).reduced(0, 4));
  xyPad.setBounds(xyArea.reduced(0, 4));
  r.removeFromTop(8);
  auto sliderRow = r.removeFromTop(r.getHeight() / 3);
  auto buttonRow1 = r.removeFromTop(r.getHeight() / 2);
  int w = r.getWidth() / 4;
  for (int i = 0; i < 4; ++i)
    controls[i]->setBounds(sliderRow.removeFromLeft(w).reduced(4, 2));
  for (int i = 4; i < 8; ++i)
    controls[i]->setBounds(buttonRow1.removeFromLeft(w).reduced(4, 2));
  for (int i = 8; i < 12; ++i)
    controls[i]->setBounds(r.removeFromLeft(w).reduced(4, 2));
}

inline MacroControls::MacroFader::MacroFader(juce::String name,
                                             juce::String pid)
    : knob(name), paramId(pid) {
  knob.setRange(0.0, 1.0);
  knob.onValueChange = [this] {
    if (onSlide)
      onSlide((float)knob.getValue());
  };
  knob.getProperties().set("paramID", pid);
  knob.setTooltip(name + " (0–1). MIDI Learn: " + pid);
  addAndMakeVisible(knob);
  label.setText(name, juce::dontSendNotification);
  label.setJustificationType(juce::Justification::centred);
  label.setFont(Fonts::small());
  addAndMakeVisible(label);
}
inline void MacroControls::MacroFader::resized() {
  label.setBounds(0, getHeight() - 15, getWidth(), 15);
  knob.setBounds(0, 0, getWidth(), getHeight() - 15);
}
inline MacroControls::MacroButton::MacroButton(juce::String name,
                                               juce::String pid)
    : paramId(pid) {
  btn.setButtonText(name);
  btn.setClickingTogglesState(true);
  btn.onClick = [this] {
    if (onTrigger)
      onTrigger(btn.getToggleState());
  };
  btn.getProperties().set("paramID", pid);
  btn.setTooltip(name + " (toggle). MIDI Learn: " + pid);
  addAndMakeVisible(btn);
}
inline MacroControls::MacroControls() {
  for (int i = 0; i < 3; ++i) {
    faders.add(new MacroFader("M" + juce::String(i + 1),
                              "Macro_Fader_" + juce::String(i + 1)));
    buttons.add(new MacroButton("B" + juce::String(i + 1),
                                "Macro_Btn_" + juce::String(i + 1)));
  }
  for (auto *f : faders)
    addAndMakeVisible(f);
  for (auto *b : buttons)
    addAndMakeVisible(b);
}
inline void MacroControls::resized() {
  auto r = getLocalBounds().reduced(4);
  // Big chunky knobs: give ~75% height to faders, at least 52px tall for chunky style
  int faderH = juce::jmax(52, static_cast<int>(r.getHeight() * 0.75f));
  auto faderArea = r.removeFromTop(faderH);
  int w = r.getWidth() / 3;
  for (int i = 0; i < 3; ++i)
    faders[i]->setBounds(faderArea.removeFromLeft(w).reduced(2));
  r.removeFromTop(4);
  for (int i = 0; i < 3; ++i)
    buttons[i]->setBounds(r.removeFromLeft(w).reduced(2));
}
#endif