/*
  ==============================================================================
    Source/Components/Sequencer.h
    Status: FIXED (Added missing isStepActive method)
  ==============================================================================
*/
#pragma once
#include "Common.h"
#include <JuceHeader.h>
#include <functional>
#include <vector>

class StepSequencer : public juce::Component {
public:
  struct Track {
    int channel;
    int program;
    juce::String name;
  };
  std::vector<Track> activeTracks;

  juce::TextButton btnRoll4{"1/4"}, btnRoll8{"1/8"}, btnRoll16{"1/16"},
      btnRoll32{"1/32"};
  int activeRollDiv = 0;
  juce::Slider noteSlider;
  juce::ComboBox cmbSteps;
  juce::ComboBox cmbTimeSig;
  juce::ComboBox cmbSeqOutCh; // New Output Channel selection
  int outputChannel = 1;
  juce::TextButton btnResetCH{"Reset CH"};

  juce::Label lblTitle{{}, "Sequencer"};
  juce::OwnedArray<juce::ToggleButton> stepButtons;
  std::function<void(int, int)> onTimeSigChange;

  int numSteps = 16, currentStep = -1;
  juce::TextButton btnClear{"Clear"};

  enum Mode { Time, Loop, Roll };
  Mode currentMode = Mode::Time;
  juce::ComboBox cmbMode;

  // Roll/Loop State
  double rollCaptureBeat = 0.0;
  bool isRollActive = false;
  int lastRollFiredStep = -1;

  // 32-Step Paging & Rec
  int currentPage = 0;
  juce::TextButton btnPage{"Page 1"};
  juce::TextButton btnRec{"Rec"};
  juce::TextButton btnExport{"Export"};
  bool isRecording = false;

  StepSequencer() {
    addAndMakeVisible(lblTitle);
    lblTitle.setFont(juce::FontOptions(12.0f).withStyle("Bold"));

    // Time Sig
    addAndMakeVisible(cmbTimeSig);
    cmbTimeSig.addItem("4/4", 1);
    cmbTimeSig.addItem("3/4", 2);
    cmbTimeSig.addItem("5/4", 3);
    cmbTimeSig.setSelectedId(1, juce::dontSendNotification);
    cmbTimeSig.onChange = [this] {
      int id = cmbTimeSig.getSelectedId();
      int num = 4;
      if (id == 2)
        num = 3; // 3/4
      if (id == 3)
        num = 5; // 5/4
      if (onTimeSigChange)
        onTimeSigChange(num, 4);
    };

    addAndMakeVisible(cmbSteps);
    // Added "64" to the list
    cmbSteps.addItemList({"4", "8", "12", "16", "32", "64"}, 1);
    cmbSteps.setSelectedId(4, juce::dontSendNotification);

    // Page Button
    addAndMakeVisible(btnPage);
    btnPage.onClick = [this] {
      int maxPages = (numSteps + 15) / 16;
      currentPage = (currentPage + 1) % maxPages;
      updatePageButton();
      resized();
      repaint(); // For dynamic text
    };
    btnPage.setVisible(false);

    // Rec Button
    addAndMakeVisible(btnRec);
    addAndMakeVisible(btnExport); // Added
    btnRec.setClickingTogglesState(true);
    btnRec.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
    btnRec.onClick = [this] { isRecording = btnRec.getToggleState(); };

    cmbSteps.onChange = [this] {
      rebuildSteps(cmbSteps.getText().getIntValue());
    };

    addAndMakeVisible(cmbMode);
    cmbMode.addItemList({"Time", "Loop", "Roll"}, 1);
    // User requested Loop as default. Index 1=Time, 2=Loop, 3=Roll
    cmbMode.setSelectedId(2, juce::dontSendNotification);
    currentMode = Mode::Loop;
    cmbMode.onChange = [this] {
      currentMode = (Mode)(cmbMode.getSelectedId() - 1);
    };

    // Output Channel Dropdown
    addAndMakeVisible(cmbSeqOutCh);
    for (int i = 1; i <= 16; ++i)
      cmbSeqOutCh.addItem(juce::String(i), i);
    cmbSeqOutCh.setSelectedId(1, juce::dontSendNotification);
    cmbSeqOutCh.onChange = [this] {
      outputChannel = cmbSeqOutCh.getSelectedId();
    };

    noteSlider.setSliderStyle(juce::Slider::LinearBar);
    noteSlider.setRange(0, 127, 1);
    noteSlider.setValue(60);
    addAndMakeVisible(noteSlider);

    auto setupRoll = [&](juce::TextButton &b, int div) {
      b.setClickingTogglesState(false); // Momentary
      // Drag-to-select logic (Basic implementation: check mouse state in timer
      // or MainComponent? Actually, Juce buttons handle drag if we override
      // functionality, but simplistic way: The user asked for "drag to select"
      // to prevent clipping. For now, ensuring they don't lock is key. We will
      // enhance the checking in MainComponent or here if needed.
      b.setColour(juce::TextButton::buttonOnColourId, Theme::accent);
      b.onStateChange = [this, &b, div] {
        // Fix: Ensure we don't get stuck if mouse drag exits
        if (b.isDown())
          activeRollDiv = div;
        else if (activeRollDiv == div) // Only reset if WE were the active one
          activeRollDiv = 0;
      };
      addAndMakeVisible(b);
    };

    setupRoll(btnRoll4, 4);
    setupRoll(btnRoll8, 8);
    setupRoll(btnRoll16, 16);
    setupRoll(btnRoll32, 32);

    rebuildSteps(16);

    btnClear.onClick = [this] { clearSteps(); };
    addAndMakeVisible(btnClear);

    btnResetCH.setTooltip("Reset Mixer Channel Mapping");
    btnResetCH.setColour(juce::TextButton::buttonColourId,
                         juce::Colours::darkred.withAlpha(0.5f));
    addAndMakeVisible(btnResetCH);
  }

  void updatePageButton() {
    btnPage.setButtonText("Page " + juce::String(currentPage + 1));
  }

  std::vector<int> stepNotes;

  void addTrack(int ch, int prog, juce::String name) {
    activeTracks.push_back({ch, prog, name});
    repaint();
  }

  void rebuildSteps(int count) {
    stepButtons.clear();
    numSteps = count;

    // Resize notes vector and preserve existing if possible, or fill with
    // default
    int defaultNote = (int)noteSlider.getValue();
    if (stepNotes.size() < numSteps) {
      stepNotes.resize(numSteps, defaultNote);
    } else if (stepNotes.size() > numSteps) {
      stepNotes.resize(numSteps);
    }

    // Auto-reset page if we shrink
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
      // Capture Note Logic:
      // When clicked ON: Capture current noteSlider value.
      // When clicked OFF: No change (logic handles clearing elsewhere if
      // needed) BUT: ToggleButton onClick happens AFTER state change usually.
      b->onClick = [this, i] {
        if (stepButtons[i]->getToggleState()) {
          // If turning ON, snap to current slider value
          int note = (int)noteSlider.getValue();
          if (i < stepNotes.size())
            stepNotes[i] = note;
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

  void recordNoteOnStep(int step, int note) {
    if (step >= 0 && step < stepButtons.size()) {
      stepButtons[step]->setToggleState(true, juce::dontSendNotification);
      if (step < stepNotes.size()) {
        stepNotes[step] = note;
      }
      // Update Root Slider to match incoming note for feedback
      juce::MessageManager::callAsync([this, note] {
        noteSlider.setValue(note, juce::dontSendNotification);
      });
    }
  }

  int getStepNote(int step) {
    if (step >= 0 && step < stepNotes.size())
      return stepNotes[step];
    return (int)noteSlider.getValue();
  }

  bool isStepActive(int step) {
    if (step >= 0 && step < stepButtons.size())
      return stepButtons[step]->getToggleState();
    return false;
  }

  Mode getMode() const { return currentMode; }

  void resized() override {
    auto r = getLocalBounds().reduced(2);
    auto header = r.removeFromTop(30);
    lblTitle.setBounds(header.removeFromLeft(70));
    cmbSteps.setBounds(header.removeFromLeft(50));
    cmbTimeSig.setBounds(header.removeFromLeft(50).reduced(2, 0));

    cmbMode.setBounds(
        header.removeFromLeft(70).reduced(5, 0)); // Added Mode Menu

    // Page Button logic
    if (numSteps > 16) {
      btnPage.setBounds(header.removeFromLeft(60).reduced(5, 0));
    }

    btnClear.setBounds(header.removeFromRight(50).reduced(2));
    noteSlider.setBounds(header.removeFromRight(50).reduced(2, 0));
    cmbSeqOutCh.setBounds(
        header.removeFromRight(50).reduced(2, 0)); // Left of Note Slider

    // Export Button (Wait, user wants swap: Swap Rec/Export)
    // Previous: Rec = 40, Export = 50.
    // New: Export = 60, Rec = 55. Widen and move right.
    btnRec.setBounds(header.removeFromRight(55).reduced(2));
    btnExport.setBounds(header.removeFromRight(60).reduced(2));

    auto rollRow = r.removeFromTop(25);
    int rw = rollRow.getWidth() / 4;
    btnRoll4.setBounds(rollRow.removeFromLeft(rw).reduced(1));
    btnRoll8.setBounds(rollRow.removeFromLeft(rw).reduced(1));
    btnRoll16.setBounds(rollRow.removeFromLeft(rw).reduced(1));
    btnRoll32.setBounds(rollRow.removeFromLeft(rw).reduced(1));

    r.removeFromTop(10); // Gap

    // Beat steps - Logic for Paging
    int visibleCount = std::min(numSteps, 16);
    int startIdx = currentPage * 16;

    // Hide all first
    for (auto *b : stepButtons)
      b->setVisible(false);

    if (visibleCount > 0) {
      int sw = r.getWidth() / visibleCount;
      for (int i = 0; i < visibleCount; ++i) {
        int realIdx = startIdx + i;
        if (realIdx < stepButtons.size()) {
          auto *b = stepButtons[realIdx];
          b->setVisible(true);
          b->setBounds(i * sw, r.getY(), sw, r.getHeight() - 10);
          b->setButtonText(juce::String(realIdx + 1));
        }
      }
    }

    // Reset CH button - Very small, bottom right
    btnResetCH.setBounds(
        getLocalBounds().removeFromRight(50).removeFromBottom(15).reduced(2));
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgDark);

    if (currentPage >= 0 && numSteps > 16) {
      g.setColour(juce::Colours::white.withAlpha(0.6f));
      g.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
      g.drawText("PAGE " + juce::String(currentPage + 1), 5, getHeight() - 15,
                 50, 15, juce::Justification::bottomLeft);
    }

    // Visual Markers every 4 steps (1 beat)
    if (stepButtons.size() > 0) {
      // Based on VISIBLE buttons
      int visibleCount = std::min(numSteps, 16);
      auto r = getLocalBounds().reduced(2);
      int topY = r.getY() + 35 + 25 + 10; // Header + Roll + Gap approx
      int h = r.getHeight() - (35 + 25 + 10) - 10;

      if (h > 0 && visibleCount > 0) {
        int sw = r.getWidth() / visibleCount;

        for (int i = 0; i < visibleCount; ++i) {
          // We want markers AFTER step 4, 8, 12 (0-indexed: 3, 7, 11)
          // But "user changes via drop down menu... we need every 4th step"
          // So i=3, i=7, i=11.
          // "Do not put a visual step indicator at the end of the step
          // sequence" -> not at i=15

          if ((i + 1) % 4 == 0 && (i + 1) < visibleCount) {
            g.setColour(juce::Colours::white.withAlpha(0.15f));
            int x = (i + 1) * sw;
            g.fillRect(x - 1, topY, 2, h); // Draw a subtle vertical bar
          }
        }
      }
    }

    // Playhead Highlight
    // Needs to handle Paging: only draw if currentStep is in visible range
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

  void paintOverChildren(juce::Graphics &g) override {
    int startIdx = currentPage * 16;
    int endIdx = startIdx + 16;

    if (stepButtons.size() > 0 && currentStep >= startIdx &&
        currentStep < endIdx && currentStep < stepButtons.size()) {
      auto r = stepButtons[currentStep]->getBounds();
      g.setColour(juce::Colours::white.withAlpha(0.3f));
      g.fillRect(r.reduced(1));
    }
  }

  void clearSteps() {
    for (auto *b : stepButtons)
      b->setToggleState(false, juce::dontSendNotification);
  }
};