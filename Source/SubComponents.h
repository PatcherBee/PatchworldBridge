/*
  ==============================================================================
    Source/SubComponents.h
    Status: REPAIRED (Defines ComplexPianoRoll and CustomKeyboard)
  ==============================================================================
*/
#pragma once
#include "Components/Common.h"
#include "Components/Controls.h"
#include "Components/Mixer.h"
#include "Components/Sequencer.h"
#include "Components/Tools.h"
#include <JuceHeader.h>

// --- PIANO ROLL RATIOS ---
static const float NoteWidthRatios[12] = {
    1.0f, // C
    0.6f, // C#
    1.0f, // D
    0.6f, // D#
    1.0f, // E
    1.0f, // F
    0.6f, // F#
    1.0f, // G
    0.6f, // G#
    1.0f, // A
    0.6f, // A#
    1.0f  // B
};

// --- MIDI INDICATOR LIGHT ---
class MidiIndicator : public juce::Component, public juce::Timer {
public:
  MidiIndicator() { startTimerHz(30); } // 30Hz refresh for smoothness

  // Thread-safe activation - Just sets a flag
  void activate() { triggered.store(true, std::memory_order_relaxed); }

  void paint(juce::Graphics &g) override {
    auto r = getLocalBounds().reduced(1).toFloat();
    // Background (dimmed orange or dark)
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(r, 2.0f);

    if (level > 0.01f) {
      auto color = juce::Colours::orange.withAlpha(level);
      g.setColour(color);
      g.fillRoundedRectangle(r, 2.0f);

      // Glow
      g.setColour(color.withAlpha(level * 0.4f));
      g.drawRoundedRectangle(r, 2.0f, 1.5f);
    }
  }

  void timerCallback() override {
    if (triggered.exchange(false, std::memory_order_relaxed)) {
      level = 1.0f;
    }

    if (level > 0.001f) {
      level *= 0.8f; // Smoother decay
      if (level < 0.01f)
        level = 0.0f;
      repaint();
    }
  }

private:
  float level = 0.0f;
  std::atomic<bool> triggered{false};
};

// --- KEYBOARD WRAPPER (Renamed to avoid conflict) ---
class CustomKeyboard : public juce::MidiKeyboardComponent {
public:
  using juce::MidiKeyboardComponent::MidiKeyboardComponent;
};

// --- PIANO ROLL COMPONENT ---
class ComplexPianoRoll : public juce::Component, public juce::Timer {
public:
  juce::MidiKeyboardState &keyboardState;
  juce::MidiMessageSequence *sequence = nullptr;
  juce::MidiKeyboardComponent *keyboardComp = nullptr; // Reference to keyboard
  float zoomX = 10.0f;
  float noteHeight = 12.0f;
  float playbackCursor = 0.0f;
  double ticksPerQuarter = 960.0;
  int octaveShift = 0;
  int wheelStripWidth = 0;

  ComplexPianoRoll(juce::MidiKeyboardState &state) : keyboardState(state) {
    startTimer(16); // ~60fps for smoother visuals
  }

  void setKeyboardComponent(juce::MidiKeyboardComponent *kc) {
    keyboardComp = kc;
  }

  void loadSequence(juce::MidiMessageSequence &seq) {
    sequence = &seq;
    repaint();
  }

  void setTicksPerQuarter(double tpq) {
    if (tpq > 0)
      ticksPerQuarter = tpq;
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgDark);

    auto r = getLocalBounds();
    int w = getWidth();
    int h = getHeight();

    // Fill Wheel Strip
    if (wheelStripWidth > 0) {
      g.setColour(juce::Colours::black.withAlpha(0.4f));
      g.fillRect(0, 0, wheelStripWidth, h);
      g.setColour(Theme::grid.withAlpha(0.2f));
      g.drawVerticalLine(wheelStripWidth, 0, (float)h);
    }

    // Helper to get X position and Width
    auto getKeyRect = [&](int note) -> juce::Rectangle<float> {
      if (keyboardComp) {
        // Keyboard is offset by wheelStripWidth relative to this component
        // (trackGrid) trackGrid = (X=0, W=800), Keyboard = (X=50, W=750)
        // keyboardComp->getRectangleForKey returns X relative to Keyboard
        // (0..750) We need to draw at X + wheelStripWidth to align (because we
        // start at 0)
        auto r = keyboardComp->getRectangleForKey(note);
        return r.withX(r.getX() + (float)wheelStripWidth);
      }
      // Fallback
      float availableW = (float)(w - wheelStripWidth);
      float totalRatio = 75.0f * 1.0f; // Approx 75 white keys equivalent?
      // Fallback simple:
      float noteW = availableW / 128.0f;
      return juce::Rectangle<float>((float)wheelStripWidth + (note * noteW), 0,
                                    noteW, (float)h);
    };

    // 0. Calculate Scales
    float speedScale =
        (zoomX > 0.1f ? zoomX : 50.0f) / 480.0f; // Faster default
    float timelineH = 22.0f;

    // 1. Draw Background Grid (Beats) & Keys Grid (Only if showNotes)
    if (showNotes) {
      // Draw Grid (aligned to keys)
      for (int i = 0; i < 128; ++i) {
        auto rect = getKeyRect(i);
        float x = rect.getX();
        if (i % 12 == 0) // C
          g.setColour(Theme::grid.withAlpha(0.5f));
        else
          g.setColour(Theme::grid.withAlpha(0.2f));

        g.drawVerticalLine((int)x, 0.0f, (float)h);
        if (i == 127)
          g.drawVerticalLine((int)rect.getRight(), 0.0f, (float)h);
      }

      g.setColour(Theme::grid.withAlpha(0.1f));
      for (int beat = 0; beat < 100; ++beat) {
        float tick = (float)(beat * 480);
        float y = h - (tick - playbackCursor) * speedScale;
        if (y > timelineH && y < h)
          g.drawHorizontalLine((int)y, 0, (float)w);
      }

      if (sequence && sequence->getNumEvents() > 0) {
        // 2. Draw falling notes
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(0, (int)timelineH, w, (int)(h - timelineH));

        for (int i = 0; i < sequence->getNumEvents(); ++i) {
          auto *ev = sequence->getEventPointer(i);
          if (ev->message.isNoteOn()) {
            auto note = ev->message.getNoteNumber();
            int displayNote = note + (octaveShift * 12);
            if (displayNote < 0 || displayNote > 127)
              continue;

            auto startTime = ev->message.getTimeStamp();
            double endTime = startTime + 240.0; // Default duration
            if (ev->noteOffObject)
              endTime = ev->noteOffObject->message.getTimeStamp();

            float currentTick = playbackCursor;
            // Notes fall from top to keyboard at bottom
            float yEnd = h - (float)(startTime - currentTick) * speedScale;
            float yStart = h - (float)(endTime - currentTick) * speedScale;

            if (yEnd < timelineH || yStart > h)
              continue;

            auto kRect = getKeyRect(displayNote);
            float rectX = kRect.getX() + 1.0f;
            float rectW = juce::jmax(2.0f, kRect.getWidth() - 1.0f);
            float rectH = juce::jmax(2.0f, yEnd - yStart);

            g.setColour(Theme::getChannelColor(ev->message.getChannel())
                            .withAlpha(0.8f));
            g.fillRect(rectX, yStart, rectW, rectH);
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.drawRect(rectX, yStart, rectW, rectH, 0.5f);
          }
        }
      }
    }

    // 3. Timeline Header (Always drawn)
    g.setColour(Theme::bgPanel.brighter(0.05f));
    g.fillRect(0.0f, 0.0f, (float)w, timelineH);
    g.setColour(Theme::grid.withAlpha(0.3f));
    g.drawHorizontalLine((int)timelineH, 0, (float)w);

    // 4. Playback Head (Yellow Vertical Line)
    if (sequence && sequence->getEndTime() > 0) {
      double duration = sequence->getEndTime();
      float progress =
          juce::jlimit(0.0f, 1.0f, (float)(playbackCursor / duration));
      float markerX = progress * (float)w;

      g.setColour(juce::Colours::yellow);
      g.drawVerticalLine((int)markerX, 0.0f, timelineH);

      // Playhead Triangle
      juce::Path p;
      p.addTriangle(markerX - 6, 0, markerX + 6, 0, markerX, 8);
      g.fillPath(p);
    }
  }

  void timerCallback() override { repaint(); }

  bool showNotes = true;
  void setShowNotes(bool shouldShow) {
    showNotes = shouldShow;
    repaint();
  }
};