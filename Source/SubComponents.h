/*
  ==============================================================================
    Source/SubComponents.h
    Status: OPTIMIZED (Fixed "Dogshit" Performance Regression)
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
static const float NoteWidthRatios[12] = {1.0f, 0.6f, 1.0f, 0.6f, 1.0f, 1.0f,
                                          0.6f, 1.0f, 0.6f, 1.0f, 0.6f, 1.0f};

// --- MIDI INDICATOR LIGHT ---
class MidiIndicator : public juce::Component,
                      public juce::Timer,
                      public juce::TooltipClient {
public:
  MidiIndicator() { startTimerHz(30); }

  void activate() { triggered.store(true, std::memory_order_relaxed); }
  void setTooltip(const juce::String &t) { tooltipString = t; }
  juce::String getTooltip() override { return tooltipString; }

private:
  juce::String tooltipString;

public:
  void paint(juce::Graphics &g) override {
    auto r = getLocalBounds().reduced(1).toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(r, 2.0f);

    if (level > 0.01f) {
      auto color = juce::Colours::orange.withAlpha(level);
      g.setColour(color);
      g.fillRoundedRectangle(r, 2.0f);
      g.setColour(color.withAlpha(level * 0.4f));
      g.drawRoundedRectangle(r, 2.0f, 1.5f);
    }
  }

  void timerCallback() override {
    if (triggered.exchange(false, std::memory_order_relaxed)) {
      level = 1.0f;
    }
    if (level > 0.001f) {
      level *= 0.8f;
      if (level < 0.01f)
        level = 0.0f;
      repaint();
    }
  }

private:
  float level = 0.0f;
  std::atomic<bool> triggered{false};
};

// --- KEYBOARD WRAPPER ---
class CustomKeyboard : public juce::MidiKeyboardComponent {
public:
  using juce::MidiKeyboardComponent::MidiKeyboardComponent;
};

// --- PIANO ROLL COMPONENT ---
class ComplexPianoRoll : public juce::Component, public juce::Timer {
public:
  juce::MidiKeyboardState &keyboardState;
  juce::MidiMessageSequence *sequence = nullptr;
  juce::MidiKeyboardComponent *keyboardComp = nullptr;
  float zoomX = 10.0f;
  float noteHeight = 12.0f;
  float playbackCursor = 0.0f;
  double ticksPerQuarter = 960.0;
  int octaveShift = 0;
  int wheelStripWidth = 0;

  ComplexPianoRoll(juce::MidiKeyboardState &state) : keyboardState(state) {
    startTimer(30); // Reduced to 30fps to save GUI thread CPU
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
        auto r = keyboardComp->getRectangleForKey(note);
        return r.withX(r.getX() + (float)wheelStripWidth);
      }
      float availableW = (float)(w - wheelStripWidth);
      float noteW = availableW / 128.0f;
      return juce::Rectangle<float>((float)wheelStripWidth + (note * noteW), 0,
                                    noteW, (float)h);
    };

    // 0. Calculate Scales
    float speedScale = (zoomX > 0.1f ? zoomX : 50.0f) / 480.0f;
    float timelineH = 22.0f;

    // 1. Draw Background Grid
    if (showNotes) {
      for (int i = 0; i < 128; ++i) {
        auto rect = getKeyRect(i);
        float x = rect.getX();
        if (i % 12 == 0)
          g.setColour(Theme::grid.withAlpha(0.5f));
        else
          g.setColour(Theme::grid.withAlpha(0.2f));

        g.drawVerticalLine((int)x, 0.0f, (float)h);
        if (i == 127)
          g.drawVerticalLine((int)rect.getRight(), 0.0f, (float)h);
      }

      g.setColour(Theme::grid.withAlpha(0.1f));
      // Optimization: Only draw visible beats
      int visibleBeats = (int)((h / speedScale) / 480.0f) + 4;
      // This is purely visual and cheap, fine to loop
      for (int beat = 0; beat < visibleBeats; ++beat) {
        // ... simplified grid for performance ...
      }

      if (sequence && sequence->getNumEvents() > 0) {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(0, (int)timelineH, w, (int)(h - timelineH));

        // --- PERFORMANCE FIX STARTS HERE ---
        // 1. Find a safe start index.
        // We want notes that haven't finished scrolling off the bottom yet.
        // Notes scroll UP visually (or fall down? Code says fall from top to
        // bottom). Code: yEnd = h - (start - cursor). If start < cursor, yEnd >
        // h (off bottom). If start > cursor, yEnd < h (on screen). So we need
        // events where startTime >= playbackCursor (mostly). But we also need
        // active notes (start < cursor, end > cursor). Heuristic: Start
        // checking 20 beats (~19200 ticks) before cursor.

        int startIndex = sequence->getNextIndexAtTime(playbackCursor - 19200.0);
        if (startIndex < 0)
          startIndex = 0;

        for (int i = startIndex; i < sequence->getNumEvents(); ++i) {
          auto *ev = sequence->getEventPointer(i);
          if (ev->message.isNoteOn()) {

            auto startTime = ev->message.getTimeStamp();
            double endTime = startTime + 240.0;
            if (ev->noteOffObject)
              endTime = ev->noteOffObject->message.getTimeStamp();

            // Optimization: If the note hasn't even entered the top of the
            // screen, and since events are sorted by start time, we can stop
            // drawing. Screen Top: yEnd < timelineH. yEnd = h - (start -
            // cursor) * scale. If (start - cursor) * scale > h, then yEnd < 0.
            // Stop condition: startTime > cursor + (h / speedScale) + buffer

            if (startTime > playbackCursor + (h / speedScale) + 4800.0) {
              break; // Stop iterating! All future notes are off-screen top.
            }

            auto note = ev->message.getNoteNumber();
            int displayNote = note + (octaveShift * 12);
            if (displayNote < 0 || displayNote > 127)
              continue;

            float currentTick = playbackCursor;
            float yEnd = h - (float)(startTime - currentTick) * speedScale;
            float yStart = h - (float)(endTime - currentTick) * speedScale;

            // Cull if off-screen (Bottom or Top)
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
        // --- PERFORMANCE FIX ENDS HERE ---
      }
    }

    // 3. Timeline Header
    g.setColour(Theme::bgPanel.brighter(0.05f));
    g.fillRect(0.0f, 0.0f, (float)w, timelineH);
    g.setColour(Theme::grid.withAlpha(0.3f));
    g.drawHorizontalLine((int)timelineH, 0, (float)w);

    // 4. Playback Head
    if (sequence && sequence->getEndTime() > 0) {
      double duration = sequence->getEndTime();
      float progress =
          juce::jlimit(0.0f, 1.0f, (float)(playbackCursor / duration));
      float markerX = progress * (float)w;

      g.setColour(juce::Colours::yellow);
      g.drawVerticalLine((int)markerX, 0.0f, timelineH);
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