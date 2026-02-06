#pragma once

#include <array>
#include <set>
#include <vector>

// --- 2. JUCE Framework ---
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

// --- 3. Project Headers ---
#include "../ControlHelpers.h"
#include "../Fonts.h"
#include "../PopupMenuOptions.h"
#include "../Theme.h"
#include <functional>

// Forward declarations
class ClockSmoother;

// Chord preset for chord pads mode
struct ChordPreset {
  juce::String name;
  std::vector<int> intervals; // semitone offsets from root
};

// Legacy shim for UI compatibility
struct StepData {
  int note = 60;
  float velocity = 0.0f;
  float probability = 1.0f; // 1.0 = Always play, 0.0 = Never play
  // Explicit constructor to satisfy std::vector::resize
  StepData() = default;
  StepData(int n, float v, float p = 1.0f)
      : note(n), velocity(v), probability(p) {}
};

class PerfButton : public juce::Button {
public:
  PerfButton(const juce::String &name, int division)
      : Button(name), rollDivision(division) {
    setButtonText(name);
  }

  std::function<void(int)> onEngage;
  std::function<void()> onRelease;

  void paintButton(juce::Graphics &g, bool isMouseOver,
                   bool isButtonDown) override {
    auto r = getLocalBounds().toFloat();
    g.setColour(isButtonDown ? Theme::accent : Theme::bgPanel.brighter(0.1f));
    g.fillRoundedRectangle(r, 4.0f);

    g.setColour(isButtonDown ? juce::Colours::black : juce::Colours::white);
    g.setFont(Fonts::body());
    g.drawText(getButtonText(), r, juce::Justification::centred);

    if (isMouseOver) {
      g.setColour(juce::Colours::white.withAlpha(0.2f));
      g.drawRoundedRectangle(r, 4.0f, 1.0f);
    }
  }

  void mouseDown(const juce::MouseEvent &) override {
    if (onEngage)
      onEngage(rollDivision);
    setState(ButtonState::buttonDown);
  }

  void mouseUp(const juce::MouseEvent &) override {
    if (onRelease)
      onRelease();
    setState(ButtonState::buttonNormal);
  }

  void mouseEnter(const juce::MouseEvent &e) override {
    if (e.mods.isLeftButtonDown()) {
      mouseDown(e);
    } else {
      setState(ButtonState::buttonOver);
    }
  }

  void mouseExit(const juce::MouseEvent &e) override {
    if (e.mods.isLeftButtonDown()) {
      mouseUp(e);
    } else {
      setState(ButtonState::buttonNormal);
    }
  }

private:
  int rollDivision;
};

class SequencerPanel : public juce::Component {
public:
  struct Track {
    int channel;
    int program;
    juce::String name;
  };
  std::vector<Track> activeTracks;
  std::vector<StepData> stepData;

  // ADD THIS FUNCTION:
  void visualizeStep(int newStep) {
    if (currentStep != newStep) {
      int oldStep = currentStep;
      currentStep = newStep;
      juce::Component::SafePointer<SequencerPanel> safe(this);
      juce::MessageManager::callAsync([safe, oldStep, newStep] {
        if (!safe)
          return;
        int maxPages = (safe->numSteps + 15) / 16;
        if (maxPages <= 0)
          return;
        int page = juce::jlimit(0, maxPages - 1, safe->currentPage);
        auto r = safe->stepGrid.getLocalBounds();
        float visibleCount = (float)juce::jmin(safe->numSteps, 16);
        if (visibleCount < 1.0f)
          return;
        float stepWidth = r.getWidth() / visibleCount;

        if (oldStep >= 0 && oldStep / 16 == page) {
          int visualIndex = oldStep % 16;
          safe->stepGrid.repaint((int)(visualIndex * stepWidth), 0,
                                (int)stepWidth + 2, r.getHeight());
        }
        if (newStep >= 0 && newStep / 16 == page) {
          int visualIndex = newStep % 16;
          safe->stepGrid.repaint((int)(visualIndex * stepWidth), 0,
                                (int)stepWidth + 2, r.getHeight());
        }
      });
    }
  }

  // Call this whenever UI changes steps
  void flushToEngine() {
    if (onStepChanged)
      onStepChanged();
  }

  /** Major scale semitones (C D E F G A B) for harmonized RND/Euclid. */
  static int randomScaleNote(int root, juce::Random& rng) {
    static const int majorScale[] = {0, 2, 4, 5, 7, 9, 11};
    int degree = majorScale[rng.nextInt(7)];
    return juce::jlimit(0, 127, root + degree);
  }
  static int scaleNoteAtStep(int root, int stepIndex) {
    static const int majorScale[] = {0, 2, 4, 5, 7, 9, 11};
    int degree = majorScale[stepIndex % 7];
    return juce::jlimit(0, 127, root + degree);
  }

  /** Randomize current page (30% notes, random velocity, harmonized pitches). */
  void randomizeCurrentPage() {
    int start = currentPage * 16;
    int end = juce::jmin(start + 16, (int)stepData.size());
    auto &rand = juce::Random::getSystemRandom();

    for (int i = start; i < end; ++i) {
      if (rand.nextFloat() > 0.7f) {
        stepData[i].velocity = 0.5f + (rand.nextFloat() * 0.5f);
        stepData[i].note = randomScaleNote(defaultNote, rand);
        stepData[i].probability = 0.8f + (rand.nextFloat() * 0.2f);
      } else {
        stepData[i].velocity = 0.0f;
      }
    }
    repaint();
    flushToEngine();
  }

  // --- ROLL BUTTONS ---
  juce::OwnedArray<PerfButton> rollButtons;
  int activeRollDiv = 0;
  std::function<void(int)> onRollChange;

  // --- CONFIG ---
  ControlHelpers::ResponsiveSlider probSlider;
  std::function<void(float)> onProbabilityChange;
  std::function<void(int)> onSequencerChannelChange;
  juce::ComboBox cmbSteps;
  juce::ComboBox cmbTimeSig;
  juce::ComboBox cmbSeqOutCh;
  juce::ComboBox cmbMode;
  juce::ComboBox cmbPattern;  // A-H pattern bank selector (replaces row of letter buttons)
  int outputChannel = 1;

  // --- BOTTOM ROW CONTROLS ---
  juce::TextButton btnPage{"Page 1"};
  juce::TextButton btnSwing{"Swing"};
  ControlHelpers::ResponsiveSlider swingSlider;

  juce::TextButton btnRec{"Rec"};
  juce::TextButton btnExport{"Export"};

  juce::TextButton btnForceGrid{"Grid"};

  juce::TextButton btnClearAll{"Clear All"};
  juce::TextButton btnRandom{"RND"}, btnEuclid{"Euclid"}, btnCopy{"Copy"},
      btnPaste{"Paste"};
  std::vector<StepData> clipboardBuffer;

  // Default note for new steps
  int defaultNote = 60;

  // Pattern banks A-H
  static constexpr int NUM_PATTERNS = 8;
  int currentPattern = 0;
  std::array<std::vector<StepData>, NUM_PATTERNS> patternBanks;
  juce::OwnedArray<juce::TextButton> patternButtons;

  // --- GRID --- (btnLinkRoot removed - deprecated)

  /** Euclidean rhythm dialog (avoids runModalLoop for JUCE 8 compatibility). */
  class EuclideanPopup : public juce::Component {
  public:
    juce::Label lblPulses, lblRot, lblSteps;
    juce::TextEditor edPulses, edRot, edSteps;
    juce::ComboBox cmbAlgorithm;
    juce::TextButton btnApply{"Apply"};
    juce::TextButton btnCancel{"Cancel"};
    std::function<void(int pulses, int steps, int rotation, int algo)> onApply;

    EuclideanPopup(int maxSteps) {
      setSize(260, 160);
      lblPulses.setText("Pulses:", juce::dontSendNotification);
      lblRot.setText("Rotation:", juce::dontSendNotification);
      lblSteps.setText("Steps:", juce::dontSendNotification);
      addAndMakeVisible(lblPulses);
      addAndMakeVisible(lblRot);
      addAndMakeVisible(lblSteps);
      addAndMakeVisible(edPulses);
      addAndMakeVisible(edRot);
      addAndMakeVisible(edSteps);
      addAndMakeVisible(cmbAlgorithm);
      addAndMakeVisible(btnApply);
      addAndMakeVisible(btnCancel);
      edPulses.setText(juce::String(juce::jmin(7, maxSteps)));
      edRot.setText("0");
      edSteps.setText(juce::String(maxSteps));
      cmbAlgorithm.addItem("Euclidean", 1);
      cmbAlgorithm.addItem("Golden", 2);
      cmbAlgorithm.addItem("Random", 3);
      cmbAlgorithm.setSelectedId(1);
      btnApply.onClick = [this] {
        if (onApply) {
          int p = edPulses.getText().getIntValue();
          int s = edSteps.getText().getIntValue();
          int r = edRot.getText().getIntValue();
          onApply(p, juce::jmax(2, s), r, cmbAlgorithm.getSelectedId());
        }
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
          box->dismiss();
      };
      btnCancel.onClick = [this] {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
          box->dismiss();
      };
    }
    void paint(juce::Graphics& g) override {
      g.fillAll(Theme::bgPanel);
      g.setColour(Theme::grid);
      g.drawRect(getLocalBounds());
    }
    void resized() override {
      auto r = getLocalBounds().reduced(10);
      auto row1 = r.removeFromTop(22);
      lblPulses.setBounds(row1.removeFromLeft(55));
      edPulses.setBounds(row1.removeFromLeft(45));
      row1.removeFromLeft(8);
      lblSteps.setBounds(row1.removeFromLeft(45));
      edSteps.setBounds(row1);
      r.removeFromTop(4);
      auto row2 = r.removeFromTop(22);
      lblRot.setBounds(row2.removeFromLeft(55));
      edRot.setBounds(row2.removeFromLeft(45));
      row2.removeFromLeft(8);
      cmbAlgorithm.setBounds(row2);
      r.removeFromTop(8);
      auto row3 = r.removeFromTop(28);
      btnCancel.setBounds(row3.removeFromRight(70).reduced(2));
      btnApply.setBounds(row3.removeFromRight(70).reduced(2));
    }
  };

  void showEuclideanDialog() {
    auto* popup = new EuclideanPopup(numSteps);
    popup->onApply = [this](int pulses, int steps, int rotation, int algo) {
      int s = juce::jlimit(2, (int)stepData.size(), steps);
      int p = juce::jlimit(1, s, pulses);
      if (algo == 2)
        generateGoldenRhythm(p, s, rotation);
      else if (algo == 3)
        generateRandomRhythm(p, s);
      else
        generateEuclideanRhythm(p, s, rotation);
    };
    juce::CallOutBox::launchAsynchronously(
        std::unique_ptr<juce::Component>(popup),
        btnEuclid.getScreenBounds(), getTopLevelComponent());
  }

  /** Per-step note picker popup (Shift+Click on step). */
  class NotePickerPopup : public juce::Component {
  public:
    std::function<void(int)> onNoteSelected;
    int currentNote = 60;

    NotePickerPopup() { setSize(180, 100); }

    void paint(juce::Graphics &g) override {
      g.fillAll(Theme::bgPanel);
      g.setColour(Theme::grid);
      g.drawRect(getLocalBounds());

      auto area = getLocalBounds().reduced(5);
      float keyW = area.getWidth() / 7.0f;

      static const int whitePc[] = {0, 2, 4, 5, 7, 9, 11};
      for (int i = 0; i < 7; ++i) {
        auto rect = juce::Rectangle<float>(area.getX() + i * keyW + 1,
                                           (float)area.getY(), keyW - 2,
                                           (float)area.getHeight() - 20);
        bool active = (currentNote % 12 == whitePc[i]);
        g.setColour(active ? Theme::accent : juce::Colours::white);
        g.fillRoundedRectangle(rect, 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.drawRoundedRectangle(rect, 2.0f, 1.0f);
      }

      static const int blackPc[] = {1, 3, -1, 6, 8, 10};
      for (int i = 0; i < 6; ++i) {
        if (blackPc[i] < 0) continue;
        float x = area.getX() + (i + 0.7f) * keyW;
        auto rect = juce::Rectangle<float>(
            x, (float)area.getY(), keyW * 0.6f, (area.getHeight() - 20) * 0.6f);
        bool active = (currentNote % 12 == blackPc[i]);
        g.setColour(active ? Theme::accent.darker() : juce::Colours::black);
        g.fillRoundedRectangle(rect, 2.0f);
      }

      g.setColour(Theme::text);
      g.setFont(Fonts::small().withHeight(11.0f));
      g.drawText("Oct: " + juce::String(currentNote / 12 - 1) + "  [< >]",
                 area.removeFromBottom(18), juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent &e) override {
      auto area = getLocalBounds().reduced(5);
      if (e.y > getHeight() - 25) {
        int oct = currentNote / 12;
        int pc = currentNote % 12;
        if (e.x < getWidth() / 2) oct = juce::jmax(0, oct - 1);
        else oct = juce::jmin(10, oct + 1);
        currentNote = oct * 12 + pc;
        if (onNoteSelected) onNoteSelected(currentNote);
        repaint();
        return;
      }
      float keyW = area.getWidth() / 7.0f;
      float blackH = (area.getHeight() - 20) * 0.6f;
      static const int blackPc[] = {1, 3, -1, 6, 8, 10};
      for (int i = 0; i < 6; ++i) {
        if (blackPc[i] < 0) continue;
        float x = area.getX() + (i + 0.7f) * keyW;
        juce::Rectangle<float> rect(x, (float)area.getY(), keyW * 0.6f, blackH);
        if (rect.contains((float)e.x, (float)e.y)) {
          currentNote = (currentNote / 12) * 12 + blackPc[i];
          if (onNoteSelected) onNoteSelected(currentNote);
          repaint();
          return;
        }
      }
      int whiteIdx = (int)((e.x - area.getX()) / keyW);
      static const int whitePc[] = {0, 2, 4, 5, 7, 9, 11};
      if (whiteIdx >= 0 && whiteIdx < 7) {
        currentNote = (currentNote / 12) * 12 + whitePc[whiteIdx];
        if (onNoteSelected) onNoteSelected(currentNote);
        repaint();
      }
    }
  };

  class StepGrid : public juce::Component, public juce::SettableTooltipClient {
  public:
    SequencerPanel &owner;
    int draggingStep = -1;
    float dragStartVelocity = 0.0f;
    /** Right-click: quick click = clear step, hold = show menu. */
    int rightClickStepIndex = -1;
    bool rightClickMenuShown = false;
    static constexpr int RIGHT_CLICK_HOLD_MS = 400;

    StepGrid(SequencerPanel &o) : owner(o) {}

    void paint(juce::Graphics &g) override {
      auto r = getLocalBounds();
      int visibleCount = juce::jmin(owner.numSteps, 16);
      if (visibleCount <= 0)
        return;

      int sw = r.getWidth() / visibleCount;
      int startIdx = owner.currentPage * 16;

      for (int i = 0; i < visibleCount; ++i) {
        int realIdx = startIdx + i;
        if (realIdx >= (int)owner.stepData.size())
          break;

        auto cell = juce::Rectangle<float>((float)(i * sw), 0.0f, (float)sw,
                                           (float)r.getHeight())
                        .reduced(2.0f);
        bool isActive = owner.stepData[realIdx].velocity > 0;
        bool isCurrent = (realIdx == owner.currentStep);
        bool isBeatFourth = (realIdx > 0 && realIdx % 4 == 0);

        Theme::drawControlShadow(g, cell, 4.0f, 1.5f);

        juce::Colour baseCol = isActive ? Theme::accent : Theme::bgPanel;
        if (isActive && owner.stepData[realIdx].probability < 0.99f) {
          // Fade color based on probability
          baseCol = baseCol.interpolatedWith(
              Theme::bgPanel, 1.0f - owner.stepData[realIdx].probability);
        }
        juce::ColourGradient grad(baseCol.brighter(0.15f), cell.getX(),
                                  cell.getY(), baseCol.darker(0.1f),
                                  cell.getX(), cell.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(cell, 4.0f);

        if (isActive) {
          float vel = owner.stepData[realIdx].velocity;

          // Draw Velocity Bar at bottom of step
          float barHeight = cell.getHeight() * vel;
          g.setColour(Theme::accent.withAlpha(0.6f));
          g.fillRoundedRectangle(
              juce::Rectangle<float>(cell.getX(), cell.getBottom() - barHeight,
                                     cell.getWidth(), barHeight),
              2.0f);

          g.setColour(Theme::accent.withAlpha(0.25f));
          g.fillRoundedRectangle(cell.expanded(2.0f), 5.0f);

          // Note name at top
          g.setColour(Theme::text.withAlpha(0.9f));
          g.setFont(Fonts::smallBold().withHeight(9.0f));
          juce::String noteName = juce::MidiMessage::getMidiNoteName(
              owner.stepData[realIdx].note, true, true, 4);
          if (sw < 35) noteName = noteName.dropLastCharacters(1);
          g.drawText(noteName, cell.removeFromTop(12),
                     juce::Justification::centred);
          if (owner.stepData[realIdx].probability < 0.99f) {
            g.setColour(Theme::text.withAlpha(0.5f));
            g.setFont(Fonts::monoSmall().withHeight(8.0f));
            g.drawText(juce::String((int)(owner.stepData[realIdx].probability *
                                          100)) +
                           "%",
                       cell.removeFromBottom(10), juce::Justification::centred);
          }

          // Value text if dragging
          if (realIdx == draggingStep) {
            g.setColour(juce::Colours::white);
            g.setFont(Fonts::small());
            juce::String txt =
                juce::ModifierKeys::currentModifiers.isAltDown()
                    ? ("P:" +
                       juce::String((int)(owner.stepData[realIdx].probability *
                                          100.0f)) +
                       "%")
                    : ("V:" + juce::String((int)(vel * 127.0f)));
            g.drawText(txt, cell, juce::Justification::centred);
          }
        }

        if (isCurrent) {
          g.setColour(juce::Colours::white.withAlpha(0.3f));
          g.drawRoundedRectangle(cell.expanded(1.0f), 5.0f, 2.0f);
          g.setColour(juce::Colours::white.withAlpha(0.15f));
          g.fillRoundedRectangle(cell, 4.0f);
        }

        if (isBeatFourth) {
          // Center the 2px line in the gap between steps (at boundary i*sw), not overlapping the step
          const float lineW = 2.0f;
          float lineX = (float)(i * sw) - lineW * 0.5f;
          g.setColour(Theme::accent.withAlpha(0.4f));
          g.fillRect(lineX, 0.0f, lineW, (float)r.getHeight());
        }

        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.fillRoundedRectangle(cell.withHeight(cell.getHeight() * 0.28f).reduced(1.0f), 3.0f);

        g.setColour(juce::Colours::white.withAlpha(isActive ? 0.9f : 0.5f));
        g.setFont(Fonts::smallBold());
        g.drawText(juce::String(realIdx + 1), cell.toNearestInt(),
                   juce::Justification::centred);

        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawRoundedRectangle(cell, 4.0f, 1.0f);
      }
    }

    void mouseDown(const juce::MouseEvent &e) override {
      int visibleCount = juce::jmin(owner.numSteps, 16);
      if (visibleCount <= 0)
        return;

      int sw = getWidth() / visibleCount;
      int i = e.x / sw;
      int realIdx = owner.currentPage * 16 + i;

      if (realIdx >= 0 && realIdx < (int)owner.stepData.size()) {
        if (e.mods.isShiftDown()) {
          showNotePicker(realIdx, e.getScreenPosition());
          return;
        }
        if (e.mods.isRightButtonDown()) {
          rightClickStepIndex = realIdx;
          rightClickMenuShown = false;
          juce::Component::SafePointer<StepGrid> safe(this);
          juce::Timer::callAfterDelay(RIGHT_CLICK_HOLD_MS, [safe, realIdx]() {
            if (!safe)
              return;
            if (safe->rightClickStepIndex == realIdx) {
              safe->showStepMenu(realIdx);
              safe->rightClickMenuShown = true;
            }
          });
          return;
        }

        rightClickStepIndex = -1;
        rightClickMenuShown = false;
        draggingStep = realIdx;
        if (owner.stepData[realIdx].velocity <= 0.05f) {
          owner.stepData[realIdx].velocity = 100.0f / 127.0f;
          owner.stepData[realIdx].note = owner.defaultNote;
        }
        dragStartVelocity = owner.stepData[realIdx].velocity;
        if (owner.onStepChanged)
          owner.onStepChanged();
        repaint();
      }
    }

    void showNotePicker(int stepIdx, juce::Point<int> screenPos) {
      auto *picker = new NotePickerPopup();
      picker->currentNote = owner.stepData[stepIdx].note;
      picker->onNoteSelected = [this, stepIdx](int note) {
        owner.stepData[stepIdx].note = note;
        if (owner.stepData[stepIdx].velocity < 0.01f)
          owner.stepData[stepIdx].velocity = 0.8f;
        owner.flushToEngine();
        repaint();
      };
      juce::CallOutBox::launchAsynchronously(
          std::unique_ptr<juce::Component>(picker),
          juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1),
          getTopLevelComponent());
    }

    void showStepMenu(int stepIdx) {
      juce::PopupMenu m;
      m.addSectionHeader("Step " + juce::String(stepIdx + 1));

      m.addItem("Set Note... (current: " +
                    juce::MidiMessage::getMidiNoteName(
                        owner.stepData[stepIdx].note, true, true, 4) + ")",
                [this, stepIdx] {
                  showNotePicker(stepIdx, getScreenPosition());
                });

      juce::PopupMenu pMenu;
      auto setProb = [this, stepIdx](float p) {
        owner.stepData[stepIdx].probability = p;
        if (owner.onStepChanged) owner.onStepChanged();
        repaint();
      };
      pMenu.addItem("100%", [=] { setProb(1.0f); });
      pMenu.addItem("75%", [=] { setProb(0.75f); });
      pMenu.addItem("50%", [=] { setProb(0.5f); });
      pMenu.addItem("25%", [=] { setProb(0.25f); });
      m.addSubMenu("Probability", pMenu);

      juce::PopupMenu vMenu;
      auto setVel = [this, stepIdx](float v) {
        owner.stepData[stepIdx].velocity = v;
        if (owner.onStepChanged) owner.onStepChanged();
        repaint();
      };
      vMenu.addItem("Max (127)", [=] { setVel(1.0f); });
      vMenu.addItem("High (100)", [=] { setVel(100.0f / 127.0f); });
      vMenu.addItem("Mid (64)", [=] { setVel(0.5f); });
      vMenu.addItem("Ghost (30)", [=] { setVel(30.0f / 127.0f); });
      m.addSubMenu("Velocity", vMenu);

      m.addSeparator();
      m.addItem("Clear Step", [this, stepIdx] {
        owner.stepData[stepIdx].velocity = 0.0f;
        owner.flushToEngine();
        repaint();
      });
      m.showMenuAsync(PopupMenuOptions::forComponent(this));
    }

    void mouseDrag(const juce::MouseEvent &e) override {
      if (draggingStep >= 0) {
        float delta = (e.getDistanceFromDragStartY() / -150.0f);
        if (e.mods.isAltDown()) {
          owner.stepData[draggingStep].probability = juce::jlimit(
              0.0f, 1.0f,
              dragStartVelocity + delta); // dragStartProb logic simplified
        } else {
          float newVel = juce::jlimit(0.0f, 1.0f, dragStartVelocity + delta);
          if (newVel < 0.05f)
            newVel = 0.0f;
          owner.stepData[draggingStep].velocity = newVel;
          if (newVel <= 0.0f)
            owner.flushToEngine();
        }
        repaint();
      }
    }

    void mouseUp(const juce::MouseEvent &e) override {
      if (draggingStep >= 0) {
        owner.flushToEngine();
        draggingStep = -1;
      }
      if (rightClickStepIndex >= 0 && !e.mods.isRightButtonDown()) {
        if (!rightClickMenuShown) {
          int idx = rightClickStepIndex;
          if (idx >= 0 && idx < (int)owner.stepData.size()) {
            owner.stepData[idx].velocity = 0.0f;
            owner.flushToEngine();
            repaint();
          }
        }
        rightClickStepIndex = -1;
        rightClickMenuShown = false;
      }
    }
  };

  StepGrid stepGrid{*this};

  // --- STATE ---
  std::function<void(int, int)> onTimeSigChange;
  std::function<void()> onTimeSigRestore;
  std::function<void(int)> onLoopChange;
  std::function<void()> onExportRequest;
  std::function<void()> onStepChanged;
  /** Called before clearing step data so router can send all-notes-off on sequencer channel (stops sustained notes). */
  std::function<void()> onClearRequested;

  int numSteps = 16, currentStep = -1;
  enum Mode { Time, Loop, Roll, Chord };
  Mode currentMode = Mode::Time;

  // Chord Pads
  juce::ComboBox cmbChordType;
  std::vector<ChordPreset> chordPresets = {
      {"Maj", {0, 4, 7}},      {"Min", {0, 3, 7}},
      {"7th", {0, 4, 7, 10}},  {"m7", {0, 3, 7, 10}},
      {"Maj7", {0, 4, 7, 11}}, {"dim", {0, 3, 6}},
      {"aug", {0, 4, 8}},      {"sus2", {0, 2, 7}},
      {"sus4", {0, 5, 7}},     {"9th", {0, 4, 7, 10, 14}},
  };
  std::function<void(int root, const std::vector<int> &intervals, float vel)>
      onChordTriggered;

  double rollCaptureBeat = 0.0;
  bool isRollActive = false;
  int lastRollFiredStep = -1;

  int currentPage = 0;
  bool isRecording = false;

  double lastProcessedBeat = 0.0;
  bool extSyncActive = false;
  // OscAirlock *airlockRef = nullptr; // Removed
  // MidiClockSmoother *clockSmootherRef = nullptr; // Removed
  // void setAirlock(OscAirlock *a) { airlockRef = a; } // Removed
  // void setClockSmoother(MidiClockSmoother *s) { clockSmootherRef = s; } //
  // Removed
  void setExtSyncActive(bool active) { extSyncActive = active; }

  // Timing State
  std::atomic<bool> isPlaying{false};

  void clearSteps() {
    for (auto &s : stepData)
      s.velocity = 0.0f;
    repaint();
  }

  struct SequencerStateData {
    std::array<std::array<float, 8>, 128> velocities;
    std::array<std::array<int, 8>, 128> notes;
    std::array<std::array<float, 8>, 128> probabilities;
    std::array<uint64_t, 2> activeStepMask;
  };

  struct EngineData {
    bool isLinkRoot = true;
    SequencerStateData sequencerData; // <--- This was missing!
  };

  EngineData getEngineSnapshot() const {
    EngineData data;
    data.isLinkRoot = false; // btnLinkRoot removed - deprecated
    data.sequencerData = getSafeSnapshot();
    return data;
  }

  // 1. Decouple UI state from Audio state (Fix A)
  SequencerStateData getSafeSnapshot() const {
    SequencerStateData data;
    for (int i = 0; i < 128; ++i) {
      if (i < (int)stepData.size()) {
        // Simple mapping: 1 voice (index 0) per step
        data.velocities[i][0] = stepData[i].velocity;
        data.notes[i][0] = stepData[i].note;
        data.probabilities[i][0] = stepData[i].probability;

        // Update bitmask for fast skipping
        int maskIdx = i / 64;
        int bitIdx = i % 64;
        if (stepData[i].velocity > 0.001f)
          data.activeStepMask[maskIdx] |= (1ULL << bitIdx);
        else
          data.activeStepMask[maskIdx] &= ~(1ULL << bitIdx);
      } else {
        // Clear remaining slots
        data.velocities[i].fill(0.0f);
        data.notes[i].fill(60);
      }
    }
    return data;
  }

  void clearAllSequencerData() {
    for (auto &s : stepData) {
      s.note = defaultNote;
      s.velocity = 0.0f;
    }
    repaint();
  }

  void clearAllSteps() {
    if (onClearRequested)
      onClearRequested();  // Cut any sounding sequencer notes immediately
    clearAllSequencerData();
    flushToEngine();  // Push empty state to engine so steps stop
  }

  void generateEuclideanRhythm(int pulses, int steps, int rotation = 0) {
    if (steps <= 0)
      return;

    // 1. Clear current steps in range
    for (int i = 0; i < steps && i < (int)stepData.size(); ++i)
      stepData[i].velocity = 0.0f;

    // 2. Bresenham-like Euclidean distribution; each step gets a scale degree for harmony
    float bucket = 0.0f;
    int baseNote = defaultNote;

    for (int i = 0; i < steps; ++i) {
      bucket += (float)pulses;
      if (bucket >= (float)steps) {
        bucket -= (float)steps;

        int targetStep = (i + rotation) % steps;
        if (targetStep < 0)
          targetStep += steps;

        if (targetStep < (int)stepData.size()) {
          stepData[targetStep].velocity = 1.0f;
          stepData[targetStep].note = scaleNoteAtStep(baseNote, targetStep);
          stepData[targetStep].probability = 1.0f;
        }
      }
    }
    repaint();
    flushToEngine();
  }

  void generateGoldenRhythm(int pulses, int steps, int rotation = 0) {
    if (steps <= 0 || pulses <= 0) return;
    static const double phi = 1.618033988749895;
    for (int i = 0; i < steps && i < (int)stepData.size(); ++i)
      stepData[i].velocity = 0.0f;
    int baseNote = defaultNote;
    std::set<int> used;
    for (int i = 0; i < pulses; ++i) {
      int raw = (int)std::floor(i * phi * (double)steps) % steps;
      int targetStep = ((raw + rotation) % steps + steps) % steps;
      while (used.count(targetStep) && (int)used.size() < steps)
        targetStep = (targetStep + 1) % steps;
      used.insert(targetStep);
      if (targetStep < (int)stepData.size()) {
        stepData[targetStep].velocity = 1.0f;
        stepData[targetStep].note = scaleNoteAtStep(baseNote, targetStep);
        stepData[targetStep].probability = 1.0f;
      }
    }
    repaint();
    flushToEngine();
  }

  void generateRandomRhythm(int pulses, int steps) {
    if (steps <= 0 || pulses <= 0) return;
    for (int i = 0; i < steps && i < (int)stepData.size(); ++i)
      stepData[i].velocity = 0.0f;
    std::vector<int> indices(steps);
    for (int i = 0; i < steps; ++i) indices[i] = i;
    auto &rng = juce::Random::getSystemRandom();
    for (int i = 0; i < juce::jmin(pulses, steps); ++i) {
      int j = i + rng.nextInt(steps - i);
      std::swap(indices[i], indices[j]);
    }
    int baseNote = defaultNote;
    for (int i = 0; i < juce::jmin(pulses, steps); ++i) {
      int idx = indices[i];
      if (idx < (int)stepData.size()) {
        stepData[idx].velocity = 1.0f;
        stepData[idx].note = randomScaleNote(baseNote, rng);
        stepData[idx].probability = 1.0f;
      }
    }
    repaint();
    flushToEngine();
  }

  SequencerPanel() {
    addAndMakeVisible(stepGrid);

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

    auto configBtn = [this](juce::TextButton &b, juce::Colour onCol,
                            juce::String tip) {
      addAndMakeVisible(b);
      b.setClickingTogglesState(true);
      b.setColour(juce::TextButton::buttonOnColourId, onCol);
      b.setTooltip(tip);
    };

    configBtn(btnRec, juce::Colours::red, "Enable Recording");
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
      if (onSequencerChannelChange)
        onSequencerChannelChange(outputChannel);
    };

    // Probability Slider
    addAndMakeVisible(probSlider);
    probSlider.setSliderStyle(juce::Slider::LinearBar);
    probSlider.setRange(0.0, 1.0, 0.01);
    probSlider.setValue(1.0);
    probSlider.setDefaultValue(1.0);
    probSlider.setDoubleClickReturnValue(true, 1.0);
    probSlider.setTextValueSuffix(" Prob");
    probSlider.onValueChange = [this] {
      if (onProbabilityChange)
        onProbabilityChange((float)probSlider.getValue());
    };

    // Mode
    addAndMakeVisible(cmbMode);
    cmbMode.addItemList({"Time", "Loop", "Roll", "Chord"}, 1);
    cmbMode.setSelectedId(3, juce::dontSendNotification);
    currentMode = Mode::Roll;
    cmbMode.onChange = [this] {
      currentMode = (Mode)(cmbMode.getSelectedId() - 1);
      cmbChordType.setVisible(currentMode == Mode::Chord);
      resized();
    };

    // Chord Type Selector
    addAndMakeVisible(cmbChordType);
    for (int i = 0; i < (int)chordPresets.size(); ++i)
      cmbChordType.addItem(chordPresets[i].name, i + 1);
    cmbChordType.setSelectedId(1, juce::dontSendNotification);
    cmbChordType.setVisible(false); // Only visible in Chord mode

    // Step note default = 60 (no slider; use NotePickerPopup or edit in piano roll)

    // Div Buttons (1/4, 1/8, 1/16, 1/32) - Mode-aware: Roll, Time, or Loop
    int divs[] = {4, 8, 16, 32};
    for (int d : divs) {
      auto *b = rollButtons.add(new PerfButton("1/" + juce::String(d), d));
      addAndMakeVisible(b);
      b->onEngage = [this](int div) {
        if (currentMode == Mode::Roll && onRollChange)
          onRollChange(div);
        else if (currentMode == Mode::Time && onTimeSigChange)
          onTimeSigChange(4, div);
        else if (currentMode == Mode::Loop && onLoopChange)
          onLoopChange(div);
      };
      b->onRelease = [this] {
        if (currentMode == Mode::Roll && onRollChange)
          onRollChange(0);
        else if (currentMode == Mode::Time && onTimeSigRestore)
          onTimeSigRestore();
        else if (currentMode == Mode::Loop && onLoopChange)
          onLoopChange(0);
      };
    }

    // Reset & Clear (Clear All only; per-step clear via context menu)
    addAndMakeVisible(btnClearAll);
    btnClearAll.setButtonText("Clear");
    btnClearAll.setTooltip("Reset ALL pitch and velocity data");
    btnClearAll.setColour(juce::TextButton::buttonColourId,
                          juce::Colours::darkred);
    btnClearAll.onClick = [this] {
      juce::Component::SafePointer<SequencerPanel> safe(this);
      juce::NativeMessageBox::showOkCancelBox(
          juce::MessageBoxIconType::WarningIcon, "Clear all steps",
          "Clear all pitch and velocity data in the sequencer? This cannot be undone.",
          getTopLevelComponent(),
          juce::ModalCallbackFunction::create([safe](int result) {
            if (result == 1 && safe != nullptr) {
              if (safe->onClearRequested) safe->onClearRequested();
              safe->clearAllSteps();
            }
          }));
    };

    // Swing Slider
    addAndMakeVisible(swingSlider);
    swingSlider.setRange(0.0, 100.0, 1.0);
    swingSlider.setValue(0.0);
    swingSlider.setDefaultValue(0.0);
    swingSlider.setDoubleClickReturnValue(true, 0.0);
    swingSlider.setSliderStyle(juce::Slider::LinearBar);
    swingSlider.setTextValueSuffix("% Swing");

    addAndMakeVisible(btnRandom);
    btnRandom.onClick = [this] { randomizeCurrentPage(); };

    addAndMakeVisible(btnEuclid);
    btnEuclid.onClick = [this] {
      showEuclideanDialog();
    };

    addAndMakeVisible(btnCopy);
    btnCopy.onClick = [this] {
      int start = currentPage * 16;
      int count = juce::jmin(16, (int)stepData.size() - start);
      clipboardBuffer.clear();
      for (int i = 0; i < count; ++i)
        clipboardBuffer.push_back(stepData[start + i]);
    };

    addAndMakeVisible(btnPaste);
    btnPaste.onClick = [this] {
      if (clipboardBuffer.empty())
        return;
      int start = currentPage * 16;
      for (size_t i = 0; i < clipboardBuffer.size(); ++i) {
        if (start + (int)i < (int)stepData.size()) {
          stepData[start + i] = clipboardBuffer[i];
        }
      }
      repaint();
    };

    rebuildSteps(16);
    initPatternBanks();
  }

  void initPatternBanks() {
    for (int i = 0; i < NUM_PATTERNS; ++i) {
      if ((int)patternBanks[i].size() != numSteps)
        patternBanks[i].resize((size_t)numSteps, StepData(defaultNote, 0.0f, 1.0f));
    }
    patternBanks[0] = stepData;
    if (patternButtons.isEmpty()) {
      for (int i = 0; i < NUM_PATTERNS; ++i) {
        auto *btn = new juce::TextButton(juce::String::charToString((juce::juce_wchar)('A' + i)));
        btn->setClickingTogglesState(true);
        btn->setRadioGroupId(9001);
        btn->setColour(juce::TextButton::buttonOnColourId, Theme::accent);
        btn->onClick = [this, i] { switchToPattern(i); };
        addChildComponent(btn);
        patternButtons.add(btn);
      }
      patternButtons[0]->setToggleState(true, juce::dontSendNotification);
    }
    if (cmbPattern.getNumItems() == 0) {
      addAndMakeVisible(cmbPattern);
      for (int i = 0; i < NUM_PATTERNS; ++i)
        cmbPattern.addItem(juce::String::charToString((juce::juce_wchar)('A' + i)), i + 1);
      cmbPattern.setSelectedId(1, juce::dontSendNotification);
      cmbPattern.onChange = [this] {
        int id = cmbPattern.getSelectedId();
        if (id >= 1 && id <= NUM_PATTERNS) switchToPattern(id - 1);
      };
    }
  }

  void switchToPattern(int index) {
    if (index < 0 || index >= NUM_PATTERNS) return;
    patternBanks[currentPattern] = stepData;
    currentPattern = index;
    stepData = patternBanks[currentPattern];
    if (cmbPattern.getNumItems() > 0)
      cmbPattern.setSelectedId(currentPattern + 1, juce::dontSendNotification);
    if (!patternButtons.isEmpty() && index < patternButtons.size())
      patternButtons[index]->setToggleState(true, juce::dontSendNotification);
    flushToEngine();
    repaint();
  }

  void updatePageButton() {
    btnPage.setButtonText(juce::String(currentPage + 1));
    btnPage.setTooltip("Sequencer Page " + juce::String(currentPage + 1));
  }

  void rebuildSteps(int count) {
    numSteps = count;
    int defNote = defaultNote;

    if (stepData.size() < (size_t)numSteps)
      stepData.resize(numSteps, StepData(defNote, 0.0f, 1.0f));
    else if (stepData.size() > (size_t)numSteps)
      stepData.resize(numSteps);

    for (int i = 0; i < NUM_PATTERNS; ++i) {
      if ((int)patternBanks[i].size() != numSteps)
        patternBanks[i].resize((size_t)numSteps, StepData(defNote, 0.0f, 1.0f));
    }

    if (numSteps <= 16) {
      currentPage = 0;
      btnPage.setVisible(false);
    } else {
      btnPage.setVisible(true);
      updatePageButton();
    }

    resized();
    repaint();
  }

  void setActiveStep(int step) {
    if (step != currentStep) {
      currentStep = step;

      // --- AUTO-FOLLOW PAGE LOGIC ---
      // Only follow if we are actually playing and user isn't holding a
      // specific page
      if (isPlaying.load() && step >= 0) {
        int stepPage = step / 16; // 0-15 = Page 0, 16-31 = Page 1

        // If the step moves to a new page, switch the view
        if (stepPage != currentPage) {
          currentPage = stepPage;

          // CRITICAL: Must be on Message Thread to update GUI
          juce::Component::SafePointer<SequencerPanel> safe(this);
          juce::MessageManager::callAsync([safe] {
            if (safe != nullptr) {
              safe->updatePageButton();
              safe->resized();
              safe->repaint();
            }
          });
        }
      }

      // Visual Redraw (Edge Triggered)
      juce::Component::SafePointer<SequencerPanel> safeRepaint(this);
      juce::MessageManager::callAsync([safeRepaint] {
        if (safeRepaint != nullptr)
          safeRepaint->stepGrid.repaint();
      });
    }
  }

  void setForceGridRecord(bool en) {
    btnForceGrid.setToggleState(en, juce::dontSendNotification);
    isRecording = btnRec.getToggleState();
  }

  void recordNoteOnStep(int step, int note, float velocity) {
    if (!isRecording)
      return;

    if (step >= 0 && step < (int)stepData.size()) {
      stepData[step].note = note;
      stepData[step].velocity = velocity;
      if (onStepChanged)
        onStepChanged();
      repaint();
    }
  }

  void triggerChordAtStep(int step, float velocity) {
    if (currentMode != Mode::Chord || !onChordTriggered)
      return;
    int chordIdx = cmbChordType.getSelectedId() - 1;
    if (chordIdx < 0 || chordIdx >= (int)chordPresets.size())
      return;
    int root = (step >= 0 && step < (int)stepData.size())
                   ? stepData[step].note
                   : 60;
    onChordTriggered(root, chordPresets[chordIdx].intervals, velocity);
  }

  int getStepNote(int step) {
    if (step >= 0 && step < (int)stepData.size())
      return stepData[step].note;
    return 0;
  }

  bool isStepActive(int step) {
    if (step >= 0 && step < (int)stepData.size())
      return stepData[step].velocity > 0;
    return false;
  }

  void resized() override {
    auto r = getLocalBounds().reduced(2);
    auto header = r.removeFromTop(28);

    // Left: Steps, TimeSig, Mode (wider dropdowns)
    cmbSteps.setBounds(header.removeFromLeft(52).reduced(1));
    cmbTimeSig.setBounds(header.removeFromLeft(52).reduced(1));
    cmbMode.setBounds(header.removeFromLeft(56).reduced(1));

    // Beat div buttons 1/4, 1/8, 1/16, 1/32
    auto midHeader = header.removeFromLeft(118).reduced(4, 0);
    int rw = midHeader.getWidth() / 4;
    for (auto *b : rollButtons)
      b->setBounds(midHeader.removeFromLeft(rw).reduced(1));

    // Pattern A-H dropdown (fills empty space in middle)
    cmbPattern.setBounds(header.removeFromLeft(58).reduced(1));

    if (cmbChordType.isVisible())
      cmbChordType.setBounds(header.removeFromLeft(60).reduced(1));

    probSlider.setBounds(header.removeFromRight(50).reduced(1));
    cmbSeqOutCh.setBounds(header.removeFromRight(52).reduced(1));

    r.removeFromTop(5);

    auto bottomRow = r.removeFromBottom(28);
    const int btnGap = 6;

    // Left to right: Swing, Rec, Export, Grid, Page — gap — Copy, Paste, Euclid, Random, Clear
    swingSlider.setBounds(bottomRow.removeFromLeft(88).reduced(2));
    bottomRow.removeFromLeft(btnGap);
    btnRec.setBounds(bottomRow.removeFromLeft(44).reduced(2));
    bottomRow.removeFromLeft(btnGap);
    btnExport.setBounds(bottomRow.removeFromLeft(58).reduced(2));
    bottomRow.removeFromLeft(btnGap);
    btnForceGrid.setBounds(bottomRow.removeFromLeft(52).reduced(2));
    bottomRow.removeFromLeft(btnGap);
    auto pageArea = bottomRow.removeFromLeft(42);
    if (btnPage.isVisible())
      btnPage.setBounds(pageArea.reduced(2));

    bottomRow.removeFromRight(btnGap);
    btnClearAll.setBounds(bottomRow.removeFromRight(52).reduced(2));
    bottomRow.removeFromRight(btnGap);
    btnRandom.setBounds(bottomRow.removeFromRight(44).reduced(2));
    bottomRow.removeFromRight(btnGap);
    btnEuclid.setBounds(bottomRow.removeFromRight(54).reduced(2));
    bottomRow.removeFromRight(btnGap);
    btnPaste.setBounds(bottomRow.removeFromRight(46).reduced(2));
    bottomRow.removeFromRight(btnGap);
    btnCopy.setBounds(bottomRow.removeFromRight(46).reduced(2));

    stepGrid.setBounds(r.reduced(0, 5));
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();

    // 1. DYNAMIC PAGE BACKGROUND (Theme-aware)
    float pageShift = currentPage * 0.04f;
    juce::Colour pageCol =
        Theme::bgPanel.withMultipliedBrightness(1.0f + pageShift);
    juce::ColourGradient grad(pageCol.brighter(0.1f), bounds.getX(),
                              bounds.getY(), pageCol.darker(0.2f),
                              bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, 6.0f);

    // 2. PAGE WATERMARK (Big Number in Background)
    g.setColour(juce::Colours::white.withAlpha(0.04f)); // Very subtle
    g.setFont(Fonts::headerLarge().withHeight(80.0f));
    g.drawText(juce::String(currentPage + 1), bounds,
               juce::Justification::centred);

    // 3. Header Background
    auto headerBounds = bounds.withHeight(30.0f);
    g.setColour(Theme::bgPanel.withAlpha(0.8f));
    g.fillRoundedRectangle(headerBounds.withTrimmedBottom(-6.0f), 6.0f);

    // 4. Borders & Accents
    g.setColour(Theme::accent.withAlpha(0.15f));
    g.drawHorizontalLine(30, bounds.getX() + 4.0f, bounds.getRight() - 4.0f);

    // Draw 16th note grid lines
    int visibleCount = juce::jmin(numSteps, 16);
    if (visibleCount > 0) {
      float sw = bounds.getWidth() / visibleCount;
      g.setColour(Theme::grid.withAlpha(0.1f));
      for (int i = 1; i < visibleCount; ++i) {
        float x = (float)i * sw;
        if (i % 4 == 0) {
          // Beat line (stronger)
          g.setColour(Theme::grid.withAlpha(0.3f));
          g.drawVerticalLine((int)x, 30.0f, bounds.getBottom() - 30.0f);
        } else {
          // Step line (weaker)
          g.setColour(Theme::grid.withAlpha(0.1f));
          g.drawVerticalLine((int)x, 35.0f, bounds.getBottom() - 35.0f);
        }
      }
    }

    // Dice Icon for Probability
    if (probSlider.isVisible()) {
      auto pr =
          probSlider.getBounds().translated(-12, 0).withWidth(10).toFloat();
      g.setColour(Theme::accent.withAlpha(0.6f));
      float s = 2.0f;
      float cx = pr.getCentreX(), cy = pr.getCentreY();
      g.fillEllipse(cx - s, cy - s, s, s);
      g.fillEllipse(cx + s, cy + s, s, s);
      g.fillEllipse(cx + s, cy - s, s, s);
      g.fillEllipse(cx - s, cy + s, s, s);
    }

    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
  }

  double exportBpm = 120.0; // Set by MainComponent before export
  void setExportBpm(double bpm) { exportBpm = bpm; }

  void exportToMidi(juce::File file) {
    if (stepData.empty())
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

    // Add tempo meta event so exported file plays at correct BPM
    int microsecondsPerQuarter = (int)(60000000.0 / exportBpm);
    seq.addEvent(juce::MidiMessage::tempoMetaEvent(microsecondsPerQuarter), 0);

    double swingAmount =
        btnForceGrid.getToggleState() ? 0.0 : (swingSlider.getValue() / 100.0);
    double swingOffset = (ticksPerStep * 0.5) * swingAmount;

    for (int i = 0; i < numSteps; ++i) {
      if (i < (int)stepData.size() && stepData[i].velocity > 0) {
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