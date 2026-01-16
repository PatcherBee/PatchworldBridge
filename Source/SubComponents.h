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

  ComplexPianoRoll(juce::MidiKeyboardState &state) : keyboardState(state) {
    startTimer(30);
  }

  void loadSequence(juce::MidiMessageSequence &seq) {
    sequence = &seq;
    repaint();
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgDark);
    g.setColour(Theme::grid);
    // Draw 128 note lines
    for (int i = 0; i < 128; ++i) {
      float y = getHeight() - (i + 1) * noteHeight;
      g.drawHorizontalLine((int)y, 0.0f, (float)getWidth());
    }

    if (sequence) {
      g.setColour(Theme::accent);
      for (auto *ev : *sequence) {
        if (ev->message.isNoteOn()) {
          auto note = ev->message.getNoteNumber();
          auto time = ev->message.getTimeStamp();
          // Simple logic to draw note start points
          float x = (float)(time / 960.0 * zoomX);
          float y = getHeight() - (note + 1) * noteHeight;
          float w = 20.0f;
          g.fillRect(x, y, w, noteHeight - 1);
        }
      }
    }
  }

  void timerCallback() override {
    // Reserved for playhead animation
  }
};