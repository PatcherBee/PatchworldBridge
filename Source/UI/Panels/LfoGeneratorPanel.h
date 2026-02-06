/*
  ==============================================================================
    Source/UI/Panels/LfoGeneratorPanel.h
    Role: Serum-style LFO module – tabs (LFO 1–4), waveform display, MODE/sync,
          RATE + Attack/Decay/Sustain/Release (ADSR) knobs to shape LFO curve.
  ==============================================================================
*/
#pragma once
#include "../Theme.h"
#include "../Widgets/ProKnob.h"
#include <algorithm>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>

/** Envelope point: phase 0..1, value 0..1. */
struct LfoEnvelopePoint {
  float phase = 0.0f;
  float value = 0.5f;
};

/** Draws one LFO cycle with grid, curve (preset or envelope points), phase bar, and draggable points. */
class LfoWaveformDisplay : public juce::Component {
public:
  LfoWaveformDisplay() { setOpaque(true); }

  void setShape(int shapeIndex) {
    shape_ = juce::jlimit(0, 4, shapeIndex);  // 4 = custom (envelope)
    if (shapeIndex >= 0 && shapeIndex <= 3)
      useAdsr_ = false;  // Preset shapes (sine/tri/saw/square) don't use ADSR
    repaint();
  }
  int getShape() const { return shape_; }

  /** Set envelope points for custom shape (phase and value 0..1). Sorted by phase. */
  void setEnvelopePoints(const std::vector<LfoEnvelopePoint> &points) {
    envelopePoints_ = points;
    std::sort(envelopePoints_.begin(), envelopePoints_.end(),
              [](const LfoEnvelopePoint &a, const LfoEnvelopePoint &b) {
                return a.phase < b.phase;
              });
    repaint();
  }
  const std::vector<LfoEnvelopePoint> &getEnvelopePoints() const { return envelopePoints_; }

  /** Real-time LFO phase 0..1 for position bar. */
  void setPhaseBar(float phase01) {
    float next = (phase01 < 0.0f) ? -1.0f : juce::jlimit(0.0f, 1.0f, phase01);
    if (std::abs(next - phaseBar_) > 0.001f) {
      phaseBar_ = next;
      repaint();
    }
  }

  /** Drive the displayed curve from ADSR knobs (0..1). When set, the waveform
   *  is drawn as one cycle: (0,0) -> (attack, 1) -> (attack+decay, sustain) -> (1-release, sustain) -> (1,0). */
  void setAdsr(float attack, float decay, float sustain, float release) {
    useAdsr_ = true;
    adsr_[0] = juce::jlimit(0.0f, 1.0f, attack);
    adsr_[1] = juce::jlimit(0.0f, 1.0f, decay);
    adsr_[2] = juce::jlimit(0.0f, 1.0f, sustain);
    adsr_[3] = juce::jlimit(0.0f, 1.0f, release);
    repaint();
  }

  /** Stop drawing from ADSR; use shape and envelope points again. */
  void setAdsrOff() {
    useAdsr_ = false;
    repaint();
  }

  std::function<void(std::vector<LfoEnvelopePoint>)> onEnvelopePointsChanged;

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgDark.darker(0.2f));
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    const float margin = 4.0f;
    r = r.reduced(margin);
    drawArea_ = r;

    // Grid
    g.setColour(Theme::grid.withAlpha(0.4f));
    for (int i = 1; i < 4; ++i) {
      float x = r.getX() + r.getWidth() * (float)i / 4.0f;
      g.drawVerticalLine((int)x, r.getY(), r.getBottom());
    }
    for (int i = 1; i < 4; ++i) {
      float y = r.getY() + r.getHeight() * (float)i / 4.0f;
      g.drawHorizontalLine((int)y, r.getX(), r.getRight());
    }

    juce::Path path;
    if (useAdsr_) {
      float a = adsr_[0], d = adsr_[1], s = adsr_[2], rel = adsr_[3];
      float total = a + d + rel;
      if (total > 0.95f) {
        float scale = 0.95f / total;
        a *= scale; d *= scale; rel *= scale;
      }
      float p1 = a;
      float p2 = a + d;
      float p3 = 1.0f - rel;
      auto pt = [&](float ph, float val) {
        float x = r.getX() + r.getWidth() * ph;
        float yy = r.getBottom() - r.getHeight() * val;
        return juce::Point<float>(x, yy);
      };
      path.startNewSubPath(pt(0.0f, 0.0f));
      path.lineTo(pt(p1, 1.0f));
      path.lineTo(pt(p2, s));
      path.lineTo(pt(p3, s));
      path.lineTo(pt(1.0f, 0.0f));
    } else if (shape_ == 4 && !envelopePoints_.empty()) {
      for (size_t i = 0; i < envelopePoints_.size(); ++i) {
        float x = r.getX() + r.getWidth() * envelopePoints_[i].phase;
        float yy = r.getBottom() - r.getHeight() * envelopePoints_[i].value;
        if (i == 0) path.startNewSubPath(x, yy);
        else path.lineTo(x, yy);
      }
      if (envelopePoints_.back().phase < 1.0f)
        path.lineTo(r.getRight(), r.getBottom() - r.getHeight() * envelopePoints_.back().value);
    } else {
      const int steps = 64;
      for (int i = 0; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        float y = 0.5f;
        const float twoPi = 6.28318530718f;
        if (shape_ == 0)
          y = 0.5f + 0.5f * std::sin(t * twoPi);
        else if (shape_ == 1) {
          y = 2.0f * std::abs(2.0f * t - 1.0f) - 0.5f;
          y = (y + 0.5f) * 0.5f;
        } else if (shape_ == 2)
          y = 1.0f - t;
        else if (shape_ == 3)
          y = t < 0.5f ? 1.0f : 0.0f;
        float x = r.getX() + r.getWidth() * t;
        float yy = r.getBottom() - r.getHeight() * y;
        if (i == 0) path.startNewSubPath(x, yy);
        else path.lineTo(x, yy);
      }
    }
    g.setColour(Theme::accent);
    g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Filled area under curve
    path.lineTo(r.getRight(), r.getBottom());
    path.lineTo(r.getX(), r.getBottom());
    path.closeSubPath();
    g.setColour(Theme::accent.withAlpha(0.25f));
    g.fillPath(path);

    // Phase bar (real-time position)
    if (phaseBar_ >= 0.0f && phaseBar_ <= 1.0f) {
      float px = r.getX() + r.getWidth() * phaseBar_;
      g.setColour(juce::Colours::yellow.withAlpha(0.8f));
      g.drawVerticalLine((int)px, r.getY(), r.getBottom());
      g.fillRect(px - 1.0f, r.getY(), 2.0f, r.getHeight());
    }

    // Draggable points (custom shape only; hide when ADSR-driven)
    if (shape_ == 4 && !useAdsr_) {
      for (size_t i = 0; i < envelopePoints_.size(); ++i) {
        float x = r.getX() + r.getWidth() * envelopePoints_[i].phase;
        float y = r.getBottom() - r.getHeight() * envelopePoints_[i].value;
        bool hi = (int)i == highlightedPoint_;
        g.setColour(hi ? Theme::accent.brighter(0.5f) : Theme::accent);
        g.fillEllipse(x - pointRadius_, y - pointRadius_, pointRadius_ * 2.0f, pointRadius_ * 2.0f);
        g.setColour(Theme::text);
        g.drawEllipse(x - pointRadius_, y - pointRadius_, pointRadius_ * 2.0f, pointRadius_ * 2.0f, 1.0f);
      }
    }
  }

  void mouseDown(const juce::MouseEvent &e) override {
    if (shape_ != 4 || envelopePoints_.empty()) return;
    int idx = hitTestPoint(e.position);
    if (idx >= 0) {
      draggingPoint_ = idx;
      repaint();
    }
  }

  void mouseDrag(const juce::MouseEvent &e) override {
    if (draggingPoint_ < 0 || shape_ != 4 || drawArea_.isEmpty()) return;
    float px = (e.position.x - drawArea_.getX()) / drawArea_.getWidth();
    float py = 1.0f - (e.position.y - drawArea_.getY()) / drawArea_.getHeight();
    px = juce::jlimit(0.0f, 1.0f, px);
    py = juce::jlimit(0.0f, 1.0f, py);
    size_t i = (size_t)draggingPoint_;
    if (i < envelopePoints_.size()) {
      envelopePoints_[i].phase = px;
      envelopePoints_[i].value = py;
      if (i > 0 && envelopePoints_[i].phase < envelopePoints_[i - 1].phase)
        std::swap(envelopePoints_[i].phase, envelopePoints_[i - 1].phase);
      if (i + 1 < envelopePoints_.size() && envelopePoints_[i].phase > envelopePoints_[i + 1].phase)
        std::swap(envelopePoints_[i].phase, envelopePoints_[i + 1].phase);
      repaint();
    }
  }

  void mouseUp(const juce::MouseEvent &) override {
    if (draggingPoint_ >= 0 && onEnvelopePointsChanged)
      onEnvelopePointsChanged(envelopePoints_);
    draggingPoint_ = -1;
    repaint();
  }

  void mouseMove(const juce::MouseEvent &e) override {
    if (shape_ != 4) return;
    int idx = hitTestPoint(e.position);
    if (idx != highlightedPoint_) {
      highlightedPoint_ = idx;
      repaint();
    }
  }

  void mouseExit(const juce::MouseEvent &) override {
    if (highlightedPoint_ >= 0) {
      highlightedPoint_ = -1;
      repaint();
    }
  }

private:
  static constexpr float pointRadius_ = 5.0f;
  int shape_ = 0;
  float phaseBar_ = -1.0f;
  std::vector<LfoEnvelopePoint> envelopePoints_;
  juce::Rectangle<float> drawArea_;
  int draggingPoint_ = -1;
  int highlightedPoint_ = -1;
  bool useAdsr_ = false;
  float adsr_[4] = {0.0f, 0.3f, 1.0f, 0.3f}; // attack, decay, sustain, release

  int hitTestPoint(juce::Point<float> pos) const {
    if (drawArea_.isEmpty() || shape_ != 4) return -1;
    for (size_t i = 0; i < envelopePoints_.size(); ++i) {
      float x = drawArea_.getX() + drawArea_.getWidth() * envelopePoints_[i].phase;
      float y = drawArea_.getBottom() - drawArea_.getHeight() * envelopePoints_[i].value;
      if (pos.getDistanceFrom(juce::Point<float>(x, y)) <= pointRadius_ * 2.0f)
        return (int)i;
    }
    return -1;
  }
};

class LfoGeneratorPanel : public juce::Component {
public:
  struct LfoSlot {
    float rate = 1.0f;
    float depth = 0.5f;
    int shape = 0;   // 0=Sine, 1=Tri, 2=Saw, 3=Square, 4=Custom (envelope)
    float attack = 0.0f;
    float decay = 0.3f;
    float sustain = 1.0f;
    float release = 0.3f;
    bool modeTrig = false;
    bool modeEnv = false;
    bool modeOff = false;
    bool syncBpm = true;
    bool syncAnch = true;
    bool syncTrip = false;
    int grid = 8;
    std::vector<LfoEnvelopePoint> envelopePoints;  // Custom shape; use default if empty
  };

  LfoGeneratorPanel() {
    setOpaque(true);
    for (int i = 0; i < 4; ++i)
      slots_[i] = LfoSlot();

    addAndMakeVisible(btnLfoOnOff);
    btnLfoOnOff.setButtonText("On");
    btnLfoOnOff.setClickingTogglesState(true);
    btnLfoOnOff.setToggleState(true, juce::dontSendNotification);
    btnLfoOnOff.setAlwaysOnTop(false);
    btnLfoOnOff.onClick = [this] {
      lfoRunning_ = btnLfoOnOff.getToggleState();
      btnLfoOnOff.setButtonText(lfoRunning_ ? "On" : "Off");
      waveformDisplay.setPhaseBar(lfoRunning_ ? 0.0f : -1.0f);
      notifyEngineIfSelected();  // Push depth=0 when off so engine stops LFO output
      repaint();
    };
    addAndMakeVisible(btnPatchingMode);
    btnPatchingMode.setButtonText("Patching mode");
    btnPatchingMode.setClickingTogglesState(true);
    btnPatchingMode.onClick = [this] {
      patchingModeActive = btnPatchingMode.getToggleState();
      if (onPatchingModeChanged)
        onPatchingModeChanged(patchingModeActive);
      repaint();
    };
    addAndMakeVisible(lblPatchingHint);
    lblPatchingHint.setJustificationType(juce::Justification::centredLeft);
    lblPatchingHint.setColour(juce::Label::textColourId, Theme::text.withAlpha(0.8f));

    // Tabs
    for (int i = 0; i < 4; ++i) {
      tabButtons[i].setButtonText("LFO " + juce::String(i + 1));
      tabButtons[i].onClick = [this, i] { setSelectedSlot(i); };
      addAndMakeVisible(tabButtons[i]);
    }

    addAndMakeVisible(waveformDisplay);

    // Presets (coming soon)
    btnFolder.setButtonText("...");
    btnFolder.setTooltip("Presets (coming soon)");
    addAndMakeVisible(btnFolder);
    addAndMakeVisible(lblGrid);
    lblGrid.setText("GRID", juce::dontSendNotification);
    gridSpinner.setRange(1, 32, 1);
    gridSpinner.setValue(8);
    gridSpinner.onValueChange = [this] {
      if (selectedSlot_ >= 0 && selectedSlot_ < 4)
        slots_[selectedSlot_].grid = (int)gridSpinner.getValue();
    };
    addAndMakeVisible(gridSpinner);

    // MODE (buttons that toggle; one of TRIG/ENV/OFF)
    modeTrig.setClickingTogglesState(true);
    modeTrig.setButtonText("TRIG");
    modeEnv.setClickingTogglesState(true);
    modeEnv.setButtonText("ENV");
    modeOff.setClickingTogglesState(true);
    modeOff.setButtonText("OFF");
    addAndMakeVisible(modeTrig);
    addAndMakeVisible(modeEnv);
    addAndMakeVisible(modeOff);

    // Sync (buttons that toggle)
    syncBpm.setClickingTogglesState(true);
    syncBpm.setButtonText("BPM");
    syncBpm.setToggleState(true, juce::dontSendNotification);
    syncAnch.setClickingTogglesState(true);
    syncAnch.setButtonText("ANCH");
    syncAnch.setToggleState(true, juce::dontSendNotification);
    syncTrip.setClickingTogglesState(true);
    syncTrip.setButtonText("TRIP");
    addAndMakeVisible(syncBpm);
    addAndMakeVisible(syncAnch);
    addAndMakeVisible(syncTrip);

    // Knobs: RATE + Attack, Decay, Sustain, Release (ADSR envelope per cycle)
    rateKnob.setRange(0.01, 20.0, 0.01);
    rateKnob.setValue(1.0);
    rateKnob.setDoubleClickReturnValue(true, 1.0);
    rateKnob.onValueChange = [this] { syncSlotFromControls(); notifyEngineIfSelected(); };
    depthKnob.setRange(0.0, 1.0, 0.01);
    depthKnob.setValue(0.5);
    depthKnob.setDoubleClickReturnValue(true, 0.5);
    depthKnob.onValueChange = [this] { syncSlotFromControls(); notifyEngineIfSelected(); };
    attackKnob.setRange(0.0, 1.0, 0.01);
    attackKnob.setValue(0.0);
    attackKnob.setDoubleClickReturnValue(true, 0.0);
    attackKnob.onValueChange = [this] { syncSlotFromControls(); notifyEngineIfSelected(); };
    decayKnob.setRange(0.0, 1.0, 0.01);
    decayKnob.setValue(0.3);
    decayKnob.setDoubleClickReturnValue(true, 0.3);
    decayKnob.onValueChange = [this] { syncSlotFromControls(); notifyEngineIfSelected(); };
    sustainKnob.setRange(0.0, 1.0, 0.01);
    sustainKnob.setValue(1.0);
    sustainKnob.setDoubleClickReturnValue(true, 1.0);
    sustainKnob.onValueChange = [this] { syncSlotFromControls(); notifyEngineIfSelected(); };
    releaseKnob.setRange(0.0, 1.0, 0.01);
    releaseKnob.setValue(0.3);
    releaseKnob.setDoubleClickReturnValue(true, 0.3);
    releaseKnob.onValueChange = [this] { syncSlotFromControls(); notifyEngineIfSelected(); };

    rateKnob.setLabel("RATE");
    depthKnob.setLabel("Depth");
    attackKnob.setLabel("Attack");
    decayKnob.setLabel("Decay");
    sustainKnob.setLabel("Sustain");
    releaseKnob.setLabel("Release");
    addAndMakeVisible(rateKnob);
    addAndMakeVisible(depthKnob);
    addAndMakeVisible(attackKnob);
    addAndMakeVisible(decayKnob);
    addAndMakeVisible(sustainKnob);
    addAndMakeVisible(releaseKnob);
    rateKnob.getProperties().set("paramID", "LFO_Rate");
    depthKnob.getProperties().set("paramID", "LFO_Depth");
    attackKnob.getProperties().set("paramID", "LFO_Attack");
    decayKnob.getProperties().set("paramID", "LFO_Decay");
    sustainKnob.getProperties().set("paramID", "LFO_Sustain");
    releaseKnob.getProperties().set("paramID", "LFO_Release");

    // Waveform type: fixed to sine (dropdown removed per UX)
    waveformDisplay.onEnvelopePointsChanged = [this](std::vector<LfoEnvelopePoint> points) {
      if (selectedSlot_ >= 0 && selectedSlot_ < 4) {
        slots_[selectedSlot_].envelopePoints = std::move(points);
        slots_[selectedSlot_].shape = 4;
        waveformDisplay.setShape(4);
        notifyEngineIfSelected();
      }
    };

    btnPatch.setButtonText("+");
    btnPatch.onClick = [this] {
      if (onRequestPatchLfo && selectedSlot_ >= 0)
        onRequestPatchLfo(selectedSlot_);
    };
    addAndMakeVisible(btnPatch);

    setSelectedSlot(0);
  }

  void setSelectedSlot(int index) {
    index = juce::jlimit(0, 3, index);
    if (selectedSlot_ == index) return;
    selectedSlot_ = index;
    refreshControlsFromSlot();
    waveformDisplay.setShape(slots_[selectedSlot_].shape);
    waveformDisplay.setEnvelopePoints(slots_[selectedSlot_].envelopePoints);
    notifyEngineIfSelected();
    repaint();
  }

  /** Call from main loop to show real-time LFO phase (0..1). When LFO is Off, phase bar is hidden. */
  void setLfoPhase(float phase01) {
    if (lfoRunning_)
      waveformDisplay.setPhaseBar(phase01);
    else
      waveformDisplay.setPhaseBar(-1.0f);  // Hide phase bar when off
  }

  int getSelectedSlot() const { return selectedSlot_; }

  /** Push current knob/combo values into the selected slot (e.g. after MIDI mapping sets knobs). */
  void flushControlsToSelectedSlot() { syncSlotFromControls(); }

  float getRate(int slotIndex) const {
    if (slotIndex >= 0 && slotIndex < 4) return slots_[slotIndex].rate;
    return 1.0f;
  }
  float getDepth(int slotIndex) const {
    if (slotIndex >= 0 && slotIndex < 4) return slots_[slotIndex].depth;
    return 0.5f;
  }
  int getShape(int slotIndex) const {
    if (slotIndex >= 0 && slotIndex < 4) return slots_[slotIndex].shape + 1;
    return 1;
  }
  float getAttack(int slotIndex) const {
    if (slotIndex >= 0 && slotIndex < 4) return slots_[slotIndex].attack;
    return 0.0f;
  }
  float getDecay(int slotIndex) const {
    if (slotIndex >= 0 && slotIndex < 4) return slots_[slotIndex].decay;
    return 0.3f;
  }
  float getSustain(int slotIndex) const {
    if (slotIndex >= 0 && slotIndex < 4) return slots_[slotIndex].sustain;
    return 1.0f;
  }
  float getRelease(int slotIndex) const {
    if (slotIndex >= 0 && slotIndex < 4) return slots_[slotIndex].release;
    return 0.3f;
  }
  /** Envelope multiplier at phase 0..1 (ADSR for preset shapes, or custom curve for shape 4). */
  float getEnvelopeAtPhase(int slotIndex, float phase01) const {
    if (slotIndex < 0 || slotIndex >= 4) return 1.0f;
    const auto &slot = slots_[slotIndex];
    if (slot.shape == 4 && !slot.envelopePoints.empty()) {
      phase01 = juce::jlimit(0.0f, 1.0f, phase01);
      const auto &pts = slot.envelopePoints;
      if (pts.size() == 1) return pts[0].value;
      for (size_t i = 0; i + 1 < pts.size(); ++i) {
        if (phase01 >= pts[i].phase && phase01 <= pts[i + 1].phase) {
          float denom = pts[i + 1].phase - pts[i].phase;
          float t = (denom > 1e-6f) ? (phase01 - pts[i].phase) / denom : 0.0f;
          return pts[i].value + t * (pts[i + 1].value - pts[i].value);
        }
      }
      return pts.back().value;
    }
    float a = slot.attack, d = slot.decay, r = slot.release, sus = slot.sustain;
    if (a + d + r < 0.0001f) return 1.0f;
    float total = a + d + r;
    if (total > 1.0f) { float scale = 1.0f / total; a *= scale; d *= scale; r *= scale; }
    float holdStart = a + d;
    float releaseStart = 1.0f - r;
    if (phase01 <= a) return (a > 0.0f) ? (phase01 / a) : 1.0f;
    if (phase01 <= holdStart) return (d > 0.0f) ? 1.0f + (sus - 1.0f) * (phase01 - a) / d : sus;
    if (phase01 <= releaseStart) return sus;
    return (r > 0.0f) ? sus * (1.0f - (phase01 - releaseStart) / r) : 0.0f;
  }

  bool isPatchingModeActive() const { return patchingModeActive; }
  /** Whether the LFO generator is running (sending modulation to patched targets). */
  bool isLfoRunning() const { return lfoRunning_; }
  void setPatchingHint(const juce::String &text) { lblPatchingHint.setText(text, juce::dontSendNotification); }

  void visibilityChanged() override {
    juce::Component::visibilityChanged();
    if (isVisible()) {
      btnLfoOnOff.setToggleState(lfoRunning_, juce::dontSendNotification);
      btnLfoOnOff.setButtonText(lfoRunning_ ? "On" : "Off");
    }
  }

  void setupTooltips() {
    btnLfoOnOff.setTooltip("Turn the LFO generator On or Off. Affects only this module (modulation to patched controls). Does not affect playback.");
    btnPatchingMode.setTooltip("When on, click any control (Mixer, Macros, Transport, etc.) to assign the selected LFO slot to modulate it.");
    lblPatchingHint.setTooltip("Shows the current patch (e.g. LFO 1 → Macro_Fader_1). Click a control in another module to patch.");
    for (int i = 0; i < 4; ++i)
      tabButtons[i].setTooltip("Select LFO slot " + juce::String(i + 1) + ". Rate and shape apply to this slot.");
    rateKnob.setTooltip("Rate (Hz). Speed of the LFO cycle. 0.01–20. Patched control (0–1) and ADSR envelope also modulate speed.");
    depthKnob.setTooltip("Depth (0–1). How much the LFO modulates the target. 0 = no effect, 1 = full range.");
    attackKnob.setTooltip("Attack (0–1). Start of each cycle: rise time from 0 to peak. Envelope also modulates LFO speed.");
    decayKnob.setTooltip("Decay (0–1). Time from peak down to sustain level within the cycle.");
    sustainKnob.setTooltip("Sustain (0–1). Level held in the middle of the cycle before release.");
    releaseKnob.setTooltip("Release (0–1). End of cycle: fall from sustain back to 0.");
    btnPatch.setTooltip("Quick-assign this LFO to a macro or transport (opens menu). Or use Patching mode and click any control.");
    gridSpinner.setTooltip("Grid (1–32). Division for BPM sync when sync is enabled.");
    modeTrig.setTooltip("TRIG: LFO cycle restarts on trigger.");
    modeEnv.setTooltip("ENV: Envelope-style (one-shot per trigger).");
    modeOff.setTooltip("OFF: This slot does not output modulation.");
    syncBpm.setTooltip("BPM: Sync LFO rate to project tempo.");
    syncAnch.setTooltip("ANCH: Anchor sync to bar.");
    syncTrip.setTooltip("TRIP: Triplet sync.");
  }

  std::function<void(bool)> onPatchingModeChanged;
  std::function<void(int lfoIndex)> onRequestPatchLfo;
  /** Called when rate/depth/shape/ADSR of the selected LFO changes (wire to engine). waveform 0-based; attack/decay/sustain/release 0..1. Only called when LFO is On. */
  std::function<void(float freq, float depth, int waveform, float attack, float decay, float sustain, float release)> onLfoParamsChanged;

  juce::OwnedArray<juce::Component> lfoBlocks; // Kept empty; getRate/getDepth/getShape used instead for compatibility

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgPanel);
    if (patchingModeActive) {
      g.setColour(Theme::accent.withAlpha(0.2f));
      g.fillRoundedRectangle(4.0f, (float)(getHeight() - 26), (float)getWidth() - 8.0f, 22.0f, 4.0f);
    }
  }

  void resized() override {
    auto r = getLocalBounds().reduced(6);
    auto top = r.removeFromTop(20);
    btnPatchingMode.setBounds(top.removeFromLeft(100).reduced(1));
    btnPatch.setBounds(top.removeFromLeft(28).reduced(1));  // + to the right of Patching Mode
    btnLfoOnOff.setBounds(top.removeFromRight(44).reduced(1));
    lblPatchingHint.setBounds(top.reduced(2));

    auto tabRow = r.removeFromTop(24);
    const int tabW = 44;
    const int btnW = 36;
    for (int i = 0; i < 4; ++i)
      tabButtons[i].setBounds(tabRow.removeFromLeft(tabW).reduced(1));
    tabRow.removeFromLeft(4);
    modeTrig.setBounds(tabRow.removeFromLeft(btnW).reduced(1));
    modeEnv.setBounds(tabRow.removeFromLeft(btnW).reduced(1));
    modeOff.setBounds(tabRow.removeFromLeft(btnW).reduced(1));
    tabRow.removeFromLeft(4);
    syncBpm.setBounds(tabRow.removeFromLeft(btnW).reduced(1));
    syncAnch.setBounds(tabRow.removeFromLeft(btnW).reduced(1));
    syncTrip.setBounds(tabRow.removeFromLeft(btnW).reduced(1));
    r.removeFromTop(2);

    auto waveRect = r.removeFromTop(juce::jmin(120, r.getHeight() / 2));
    waveformDisplay.setBounds(waveRect.reduced(2));
    r.removeFromTop(4);

    auto ctrlRow = r.removeFromTop(56);
    btnFolder.setBounds(ctrlRow.removeFromLeft(28).reduced(2));
    lblGrid.setBounds(ctrlRow.removeFromLeft(24).reduced(1));
    gridSpinner.setBounds(ctrlRow.removeFromLeft(36).reduced(1));
    ctrlRow.removeFromLeft(8);

    int kw = ctrlRow.getWidth() / 6;
    rateKnob.setBounds(ctrlRow.removeFromLeft(kw).reduced(2));
    depthKnob.setBounds(ctrlRow.removeFromLeft(kw).reduced(2));
    attackKnob.setBounds(ctrlRow.removeFromLeft(kw).reduced(2));
    decayKnob.setBounds(ctrlRow.removeFromLeft(kw).reduced(2));
    sustainKnob.setBounds(ctrlRow.removeFromLeft(kw).reduced(2));
    releaseKnob.setBounds(ctrlRow.removeFromLeft(kw).reduced(2));

    }

private:
  void refreshControlsFromSlot() {
    if (selectedSlot_ < 0 || selectedSlot_ >= 4) return;
    const auto &s = slots_[selectedSlot_];
    rateKnob.setValue(s.rate, juce::dontSendNotification);
    depthKnob.setValue(s.depth, juce::dontSendNotification);
    attackKnob.setValue(s.attack, juce::dontSendNotification);
    decayKnob.setValue(s.decay, juce::dontSendNotification);
    sustainKnob.setValue(s.sustain, juce::dontSendNotification);
    releaseKnob.setValue(s.release, juce::dontSendNotification);
    waveformDisplay.setShape(s.shape);
    waveformDisplay.setEnvelopePoints(s.envelopePoints);
    updateWaveformFromSlot();
    gridSpinner.setValue(s.grid, juce::dontSendNotification);
    modeTrig.setToggleState(s.modeTrig, juce::dontSendNotification);
    modeEnv.setToggleState(s.modeEnv, juce::dontSendNotification);
    modeOff.setToggleState(s.modeOff, juce::dontSendNotification);
    syncBpm.setToggleState(s.syncBpm, juce::dontSendNotification);
    syncAnch.setToggleState(s.syncAnch, juce::dontSendNotification);
    syncTrip.setToggleState(s.syncTrip, juce::dontSendNotification);
  }

  void syncSlotFromControls() {
    if (selectedSlot_ < 0 || selectedSlot_ >= 4) return;
    auto &s = slots_[selectedSlot_];
    s.rate = (float)rateKnob.getValue();
    s.depth = (float)depthKnob.getValue();
    s.attack = (float)attackKnob.getValue();
    s.decay = (float)decayKnob.getValue();
    s.sustain = (float)sustainKnob.getValue();
    s.release = (float)releaseKnob.getValue();
    s.shape = 0;  // Fixed sine (wave dropdown removed)
    s.grid = (int)gridSpinner.getValue();
    s.modeTrig = modeTrig.getToggleState();
    s.modeEnv = modeEnv.getToggleState();
    s.modeOff = modeOff.getToggleState();
    s.syncBpm = syncBpm.getToggleState();
    s.syncAnch = syncAnch.getToggleState();
    s.syncTrip = syncTrip.getToggleState();
    waveformDisplay.setShape(s.shape);
    if (s.shape == 4)
      waveformDisplay.setEnvelopePoints(s.envelopePoints);
    updateWaveformFromSlot();
  }

  /** Always drive the on-screen curve from ADSR knobs so they visibly shape the envelope. */
  void updateWaveformFromSlot() {
    if (selectedSlot_ < 0 || selectedSlot_ >= 4) return;
    const auto &s = slots_[selectedSlot_];
    waveformDisplay.setAdsr(s.attack, s.decay, s.sustain, s.release);
  }

  void notifyEngineIfSelected() {
    if (selectedSlot_ < 0 || selectedSlot_ >= 4) return;
    const auto &s = slots_[selectedSlot_];
    if (onLfoParamsChanged) {
      if (lfoRunning_)
        onLfoParamsChanged(s.rate, s.depth, std::min(3, s.shape), s.attack, s.decay, s.sustain, s.release);
      else
        onLfoParamsChanged(0.0f, 0.0f, 0, 0.0f, 0.3f, 1.0f, 0.3f);  // Zero depth when off so modulation stops
    }
  }

  void ensureEnvelopePoints(int slot) {
    if (slot < 0 || slot >= 4) return;
    auto &pts = slots_[slot].envelopePoints;
    if (pts.size() < 4) {
      pts = {{0.0f, 0.5f}, {0.25f, 1.0f}, {0.5f, 0.5f}, {0.75f, 0.25f}, {1.0f, 0.0f}};
      waveformDisplay.setEnvelopePoints(pts);
    }
  }

  LfoSlot slots_[4];
  int selectedSlot_ = 0;
  bool patchingModeActive = false;
  bool lfoRunning_ = true;

  juce::TextButton btnLfoOnOff;
  juce::TextButton tabButtons[4];
  LfoWaveformDisplay waveformDisplay;
  juce::TextButton btnFolder;
  juce::Label lblGrid;
  juce::Slider gridSpinner{juce::Slider::LinearBar, juce::Slider::TextBoxRight};
  juce::TextButton modeTrig, modeEnv, modeOff;
  juce::TextButton syncBpm, syncAnch, syncTrip;
public:
  /** Exposed for MIDI mapping (setParameterValueCallback / getParameterValue). */
  ProKnob rateKnob{"RATE"}, depthKnob{"Depth"}, attackKnob{"Attack"}, decayKnob{"Decay"}, sustainKnob{"Sustain"}, releaseKnob{"Release"};
private:
  juce::TextButton btnPatch{"+"};
  juce::TextButton btnPatchingMode;
  juce::Label lblPatchingHint{{}, "Click + or enable Patching mode, then click a fader/slider/knob to assign."};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LfoGeneratorPanel)
};
