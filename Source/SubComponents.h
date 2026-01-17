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
  float zoomX = 10.0f;
  float noteHeight = 12.0f;
  float playbackCursor = 0.0f;
  double ticksPerQuarter = 960.0;
  int octaveShift = 0;
  int wheelStripWidth = 0;

  ComplexPianoRoll(juce::MidiKeyboardState &state) : keyboardState(state) {
    startTimer(16); // ~60fps for smoother visuals
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

    float availableW = (float)w; // Fallback

    // Fill Wheel Strip Background
    if (wheelStripWidth > 0) {
      g.setColour(juce::Colours::black.withAlpha(0.4f));
      g.fillRect(0, 0, wheelStripWidth, h);
      g.setColour(Theme::grid.withAlpha(0.2f));
      g.drawVerticalLine(wheelStripWidth, 0, (float)h);
      availableW -= wheelStripWidth;
    }
    float totalRatio = 0;
    for (int i = 0; i < 128; ++i)
      totalRatio += NoteWidthRatios[i % 12];
    float unitW = availableW / totalRatio;

    auto getNoteX = [&](int note) {
      float x = (float)wheelStripWidth;
      for (int i = 0; i < note; ++i)
        x += NoteWidthRatios[i % 12] * unitW;
      return x;
    };

    auto getNoteW = [&](int note) {
      return NoteWidthRatios[note % 12] * unitW;
    };

    for (int i = 0; i <= 128; ++i) {
      float x = getNoteX(i);
      if (i % 12 == 0)
        g.setColour(Theme::grid.withAlpha(0.5f)); // C notes stronger
      else
        g.setColour(Theme::grid.withAlpha(0.2f));
      g.drawVerticalLine((int)x, 0.0f, (float)h);
    }

    float speedScale = (zoomX > 0.1f ? zoomX : 10.0f) / 480.0f;

    if (sequence && sequence->getEndTime() > 0) {
      // Draw individual notes
      g.setColour(Theme::accent);
      float currentTick = playbackCursor;

      // -- FIX: Clip notes so they don't draw over the timeline header --
      juce::Graphics::ScopedSaveState ss(g);
      g.reduceClipRegion(0, 20, w, h - 20);

      for (int i = 0; i < sequence->getNumEvents(); ++i) {
        auto *ev = sequence->getEventPointer(i);
        if (ev->message.isNoteOn()) {
          auto note = ev->message.getNoteNumber();
          int displayNote = note + (octaveShift * 12);
          if (displayNote < 0 || displayNote > 127)
            continue;

          auto startTime = ev->message.getTimeStamp();
          double endTime = startTime + 240.0;
          if (ev->noteOffObject)
            endTime = ev->noteOffObject->message.getTimeStamp();

          float yEnd = h - (float)(startTime - currentTick) * speedScale;
          float yStart = h - (float)(endTime - currentTick) * speedScale;

          if (yEnd < 0)
            continue;
          if (yStart > h)
            continue;

          float rectX = getNoteX(displayNote) + 1.0f;
          float rectW = juce::jmax(2.0f, getNoteW(displayNote) - 1.0f);
          float rectH = yEnd - yStart;

          g.setColour(Theme::getChannelColor(ev->message.getChannel()));
          g.fillRect(rectX, yStart, rectW, rectH);
        }
      }
    }

    // Timeline Header (Draw LAST to stay on top)
    g.setColour(Theme::bgPanel.brighter(0.1f));
    g.fillRect(0, 0, w, 20);
    g.setColour(juce::Colours::white);
    g.drawText("Timeline", 5, 0, 100, 20, juce::Justification::centredLeft);

    // Progress marker on timeline
    if (sequence && sequence->getEndTime() > 0) {
      double progress = playbackCursor / sequence->getEndTime();
      float markerX = (float)progress * w;
      g.setColour(juce::Colours::red);
      g.fillRect(markerX - 2, 0.0f, 4.0f, 20.0f);
    }
  }

  void timerCallback() override { repaint(); }
};