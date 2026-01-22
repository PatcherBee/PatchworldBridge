/*
  ==============================================================================
    Source/Components/Sequencer.h
    Status: VERIFIED COMPLETE (All helpers & buttons present)
  ==============================================================================
*/
#pragma once
#include "Common.h"
#include <JuceHeader.h>
#include <functional>
#include <vector>

struct StepData {
  int note = 60;
  float velocity = 0.78f;
};

class StepSequencer : public juce::Component {
public:
  struct Track {
    int channel;
    int program;
    juce::String name;
  };
  std::vector<Track> activeTracks;
  std::vector<StepData> stepData;

  // --- ROLL BUTTONS ---
  juce::TextButton btnRoll4{"1/4"}, btnRoll8{"1/8"}, btnRoll16{"1/16"},
      btnRoll32{"1/32"};
  int activeRollDiv = 0;

  // --- CONFIG ---
  juce::Slider noteSlider;
  juce::ComboBox cmbSteps;
  juce::ComboBox cmbTimeSig;
  juce::ComboBox cmbSeqOutCh;
  juce::ComboBox cmbMode;
  int outputChannel = 1;

  // --- BOTTOM ROW CONTROLS ---
  juce::TextButton btnPage{"Page 1"};

  juce::TextButton btnSwing{"Swing"}; // Button (if needed)
  juce::Slider swingSlider;           // Slider (Main Swing control)

  juce::TextButton btnCountIn{"Cnt"}; // Changed from ToggleButton to TextButton
  juce::TextButton btnRec{"Rec"};
  juce::TextButton btnExport{"Export"};

  juce::TextButton btnForceGrid{
      "Grid"}; // Renamed from btnQuantize, TextButton for color

  juce::TextButton btnResetCH{"Reset CH"};
  juce::TextButton btnClearAll{"Clear All"};
  juce::TextButton btnClear{"Clear"}; // Header clear button

  // --- GRID ---
  juce::ToggleButton btnLinkRoot{"Link Root"};
  juce::OwnedArray<juce::ToggleButton> stepButtons;

  // --- STATE ---
  std::function<void(int, int)> onTimeSigChange;
  std::function<void()> onExportRequest;

  int numSteps = 16, currentStep = -1;
  enum Mode { Time, Loop, Roll };
  Mode currentMode = Mode::Time;

  // Roll/Loop State
  double rollCaptureBeat = 0.0;
  bool isRollActive = false;
  int lastRollFiredStep = -1;

  // 32-Step Paging & Rec
  int currentPage = 0;
  bool isRecording = false;

  // ==============================================================================
  // HELPERS (Defined before use)
  // ==============================================================================
  void clearSteps() {
    for (auto *b : stepButtons)
      b->setToggleState(false, juce::dontSendNotification);
    repaint();
  }

  void clearAllSequencerData() {
    int defaultNote = (int)noteSlider.getValue();
    for (auto &s : stepData) {
      s.note = defaultNote;
      s.velocity = 0.78f;
    }
    clearSteps();
  }

  // ==============================================================================
  // CONSTRUCTOR
  // ==============================================================================
  StepSequencer() {
    addAndMakeVisible(btnLinkRoot);
    btnLinkRoot.setTooltip("Link Root Note Slider to Recorded Steps");
    btnLinkRoot.setToggleState(true, juce::dontSendNotification);

    // Time Sig
    addAndMakeVisible(cmbTimeSig);
    cmbTimeSig.addItem("4/4", 1);
    cmbTimeSig.addItem("3/4", 2);
    cmbTimeSig.addItem("5/4", 3);
    cmbTimeSig.setSelectedId(1, juce::dontSendNotification);
    cmbTimeSig.onChange = [this] {
      int id = cmbTimeSig.getSelectedId();
      int num = (id == 2) ? 3 : (id == 3 ? 5 : 4);
      if (onTimeSigChange)
        onTimeSigChange(num, 4);
    };

    // Steps
    addAndMakeVisible(cmbSteps);
    cmbSteps.addItemList({"4", "8", "12", "16", "32", "64"}, 1);
    cmbSteps.setSelectedId(4, juce::dontSendNotification);
    cmbSteps.onChange = [this] {
      rebuildSteps(cmbSteps.getText().getIntValue());
    };

    // Page Button
    addAndMakeVisible(btnPage);
    btnPage.onClick = [this] {
      int maxPages = (numSteps + 15) / 16;
      currentPage = (currentPage + 1) % maxPages;
      updatePageButton();
      resized();
      repaint();
    };
    btnPage.setVisible(false);

    // --- BUTTON CONFIGURATION (Color Toggles) ---
    auto configBtn = [this](juce::TextButton &b, juce::Colour onCol,
                            juce::String tip) {
      addAndMakeVisible(b);
      b.setClickingTogglesState(true);
      b.setColour(juce::TextButton::buttonOnColourId, onCol);
      b.setTooltip(tip);
    };

    configBtn(btnRec, juce::Colours::red, "Enable Recording");
    configBtn(btnCountIn, juce::Colours::orange, "1 Bar Count-In");
    configBtn(btnForceGrid, juce::Colours::cyan,
              "Force Strict Grid (No Swing)");
    configBtn(btnSwing, juce::Colours::cyan.darker(0.3f), "Swing Toggle");

    addAndMakeVisible(btnExport);
    btnRec.onClick = [this] { isRecording = btnRec.getToggleState(); };
    btnExport.onClick = [this] {
      if (onExportRequest)
        onExportRequest();
    };

    // Output Channel
    addAndMakeVisible(cmbSeqOutCh);
    for (int i = 1; i <= 16; ++i)
      cmbSeqOutCh.addItem(juce::String(i), i);
    cmbSeqOutCh.setSelectedId(1, juce::dontSendNotification);
    cmbSeqOutCh.onChange = [this] {
      outputChannel = cmbSeqOutCh.getSelectedId();
    };

    // Mode
    addAndMakeVisible(cmbMode);
    cmbMode.addItemList({"Time", "Loop", "Roll"}, 1);
    cmbMode.setSelectedId(2, juce::dontSendNotification);
    currentMode = Mode::Loop;
    cmbMode.onChange = [this] {
      currentMode = (Mode)(cmbMode.getSelectedId() - 1);
    };

    // Slider
    noteSlider.setSliderStyle(juce::Slider::LinearBar);
    noteSlider.setRange(0, 127, 1);
    noteSlider.setValue(60);
    addAndMakeVisible(noteSlider);

    // Roll Buttons
    auto setupRoll = [&](juce::TextButton &b, int div) {
      addAndMakeVisible(b);
      b.setClickingTogglesState(false);
      b.setColour(juce::TextButton::buttonOnColourId, Theme::accent);
      b.onStateChange = [this, &b, div] {
        if (b.isDown())
          activeRollDiv = div;
        else if (activeRollDiv == div)
          activeRollDiv = 0;
      };
    };
    setupRoll(btnRoll4, 4);
    setupRoll(btnRoll8, 8);
    setupRoll(btnRoll16, 16);
    setupRoll(btnRoll32, 32);

    // Reset & Clear
    btnClear.onClick = [this] { clearSteps(); };
    addAndMakeVisible(btnClear);

    btnResetCH.setTooltip("Reset Mixer Channel Mapping");
    btnResetCH.setColour(juce::TextButton::buttonColourId,
                         juce::Colours::darkred.withAlpha(0.5f));
    addAndMakeVisible(btnResetCH);

    addAndMakeVisible(btnClearAll);
    btnClearAll.setTooltip("Reset ALL pitch and velocity data");
    btnClearAll.setColour(juce::TextButton::buttonColourId,
                          juce::Colours::darkred);
    btnClearAll.onClick = [this] { clearAllSequencerData(); };

    // Swing Slider
    addAndMakeVisible(swingSlider);
    swingSlider.setRange(0.0, 100.0, 1.0);
    swingSlider.setValue(0.0);
    swingSlider.setSliderStyle(juce::Slider::LinearBar);
    swingSlider.setTextValueSuffix("% Swing");

    rebuildSteps(16);
  }

  // ==============================================================================
  // LOGIC
  // ==============================================================================
  void updatePageButton() {
    btnPage.setButtonText(juce::String(currentPage + 1));
    btnPage.setTooltip("Sequencer Page " + juce::String(currentPage + 1));
  }

  void rebuildSteps(int count) {
    stepButtons.clear();
    numSteps = count;

    int defaultNote = (int)noteSlider.getValue();
    if (stepData.size() < (size_t)numSteps)
      stepData.resize(numSteps, {defaultNote, 0.78f});
    else if (stepData.size() > (size_t)numSteps)
      stepData.resize(numSteps);

    if (numSteps <= 16) {
      currentPage = 0;
      btnPage.setVisible(false);
    } else {
      btnPage.setVisible(true);
      updatePageButton();
    }

    for (int i = 0; i < numSteps; ++i) {
      auto *b = stepButtons.add(new juce::ToggleButton());
      b->setColour(juce::ToggleButton::tickColourId, Theme::accent);
      b->setButtonText(juce::String(i + 1));
      b->onClick = [this, i] {
        if (stepButtons[i]->getToggleState()) {
          if (!isRecording) {
            stepData[i].note = (int)noteSlider.getValue();
          }
        }
      };
      addAndMakeVisible(b);
    }
    resized();
  }

  void setActiveStep(int step) {
    if (step != currentStep) {
      currentStep = step;
      juce::MessageManager::callAsync([this] { repaint(); });
    }
  }

  void recordNoteOnStep(int step, int note, float velocity) {
    if (step >= 0 && step < stepButtons.size()) {
      stepButtons[step]->setToggleState(true, juce::dontSendNotification);
      // Ensure stepData is large enough
      if (step < (int)stepData.size()) {
        stepData[step].note = note;
        stepData[step].velocity = velocity;
      }
      if (btnLinkRoot.getToggleState()) {
        juce::MessageManager::callAsync([this, note] {
          noteSlider.setValue(note, juce::dontSendNotification);
        });
      }
    }
  }

  int getStepNote(int step) {
    if (step >= 0 && step < (int)stepData.size())
      return stepData[step].note;
    return 0;
  }

  bool isStepActive(int step) {
    if (step >= 0 && step < stepButtons.size())
      return stepButtons[step]->getToggleState();
    return false;
  }

  // ==============================================================================
  // LAYOUT & PAINT
  // ==============================================================================
  void resized() override {
    auto r = getLocalBounds().reduced(2);

    // 1. HEADER ROW (Link, Steps, TimeSig, Mode, Rolls, Ch, Note, Clear)
    auto header = r.removeFromTop(28);

    // Left Group
    btnLinkRoot.setBounds(header.removeFromLeft(75).reduced(2));
    cmbSteps.setBounds(header.removeFromLeft(45).reduced(1));
    cmbTimeSig.setBounds(header.removeFromLeft(45).reduced(1));
    cmbMode.setBounds(header.removeFromLeft(50).reduced(1));

    // Mid Group (Rolls)
    auto midHeader = header.removeFromLeft(140).reduced(5, 0);
    int rw = midHeader.getWidth() / 4;
    btnRoll4.setBounds(midHeader.removeFromLeft(rw).reduced(1));
    btnRoll8.setBounds(midHeader.removeFromLeft(rw).reduced(1));
    btnRoll16.setBounds(midHeader.removeFromLeft(rw).reduced(1));
    btnRoll32.setBounds(midHeader.removeFromLeft(rw).reduced(1));

    // Right Group
    btnClear.setBounds(header.removeFromRight(55).reduced(2));
    noteSlider.setBounds(header.removeFromRight(45).reduced(1));
    cmbSeqOutCh.setBounds(header.removeFromRight(45).reduced(1));

    r.removeFromTop(5); // Spacer

    // 3. BOTTOM FOOTER ROW
    auto bottomRow = r.removeFromBottom(28);

    // Left to Right: Swing -> CountIn -> Rec -> Export -> Grid -> ClearAll ->
    // ResetCH
    swingSlider.setBounds(bottomRow.removeFromLeft(85).reduced(2));
    btnCountIn.setBounds(bottomRow.removeFromLeft(40).reduced(2));
    btnRec.setBounds(bottomRow.removeFromLeft(40).reduced(2));
    btnExport.setBounds(bottomRow.removeFromLeft(55).reduced(2));
    btnForceGrid.setBounds(bottomRow.removeFromLeft(50).reduced(2));
    btnClearAll.setBounds(bottomRow.removeFromLeft(70).reduced(2));

    if (btnPage.isVisible()) {
      btnPage.setBounds(bottomRow.removeFromLeft(35).reduced(2));
    }

    btnResetCH.setBounds(bottomRow.removeFromRight(75).reduced(2));

    // 4. BEAT STEP GRID
    int visibleCount = std::min(numSteps, 16);
    int startIdx = currentPage * 16;
    for (auto *b : stepButtons)
      b->setVisible(false);

    if (visibleCount > 0) {
      int sw = r.getWidth() / visibleCount;
      for (int i = 0; i < visibleCount; ++i) {
        int realIdx = startIdx + i;
        if (realIdx < stepButtons.size()) {
          auto *b = stepButtons[realIdx];
          b->setVisible(true);
          b->setBounds(r.getX() + (i * sw), r.getY(), sw, r.getHeight() - 5);
          b->setButtonText(juce::String(realIdx + 1));
        }
      }
    }
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgDark);

    // Draw Beat Group Lines (Every 4)
    int visibleCount = std::min(numSteps, 16);
    if (visibleCount > 0) {
      int sw = getWidth() / visibleCount;
      g.setColour(juce::Colours::white.withAlpha(0.1f));
      // Approx heights based on resized()
      int topY = 30 + 25 + 10;
      int bottomY = getHeight() - 25;
      for (int i = 1; i < visibleCount; ++i) {
        if (i % 4 == 0)
          g.drawVerticalLine(i * sw, (float)topY, (float)bottomY);
      }
    }

    // Highlight Active Step
    int startIdx = currentPage * 16;
    int endIdx = startIdx + 16;
    if (stepButtons.size() > 0 && currentStep >= startIdx &&
        currentStep < endIdx && currentStep < stepButtons.size()) {
      auto r = stepButtons[currentStep]->getBounds();
      g.setColour(juce::Colours::white.withAlpha(0.2f));
      g.fillRect(r);
      g.setColour(Theme::accent);
      g.drawRect(r, 2.0f);
    }
  }

  void exportToMidi(juce::File file) {
    if (stepButtons.isEmpty())
      return;
    juce::MidiMessageSequence seq;
    int ppq = 960;
    double ticksPerStep = ppq / 4.0;

    int num = 4;
    int id = cmbTimeSig.getSelectedId();
    if (id == 2)
      num = 3;
    else if (id == 3)
      num = 5;
    seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(num, 4), 0);

    // Use btnForceGrid state instead of old btnQuantize
    double swingAmount =
        btnForceGrid.getToggleState() ? 0.0 : (swingSlider.getValue() / 100.0);
    double swingOffset = (ticksPerStep * 0.5) * swingAmount;

    for (int i = 0; i < numSteps; ++i) {
      if (i < stepButtons.size() && stepButtons[i]->getToggleState()) {
        int n_val = stepData[i].note;
        float v_val = stepData[i].velocity;

        double start = (double)i * ticksPerStep;
        if (i % 2 != 0)
          start += swingOffset;

        seq.addEvent(juce::MidiMessage::noteOn(outputChannel, n_val, v_val),
                     start);
        seq.addEvent(juce::MidiMessage::noteOff(outputChannel, n_val),
                     start + (ticksPerStep * 0.9));
      }
    }
    double totalLoopTicks = numSteps * ticksPerStep;
    seq.addEvent(juce::MidiMessage::endOfTrack(), totalLoopTicks);
    seq.updateMatchedPairs();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ppq);
    midiFile.addTrack(seq);

    if (file.existsAsFile())
      file.deleteFile();
    juce::FileOutputStream stream(file);
    if (stream.openedOk())
      midiFile.writeTo(stream);
  }
};