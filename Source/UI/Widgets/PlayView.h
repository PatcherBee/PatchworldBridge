/*
  ==============================================================================
    Source/UI/Widgets/PlayView.h
    Role: Falling notes visualization for learning mode (roadmap 8.1)
  ==============================================================================
*/
#pragma once

#include "../../Audio/ChordDetector.h"
#include "../../Audio/EditableNote.h"
#include "../Theme.h"
#include "../Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include <vector>

/**
 * PlayView: Falling notes visualization (like Synthesia/Piano Hero).
 * Notes fall from top towards a hit line near the bottom.
 */
class PlayView : public juce::Component,
                 public juce::SettableTooltipClient {
public:
  PlayView() {
    setOpaque(true);
    addAndMakeVisible(chordLabel);
    chordLabel.setJustificationType(juce::Justification::centredLeft);
    chordLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    chordLabel.setFont(Fonts::headerLarge().withHeight(18.0f));
    chordLabel.setTooltip(
        "Detected chord from currently held notes (Performance view).");
  }

  void setNotes(const std::vector<EditableNote> &newNotes) {
    notes = newNotes;
    repaint();
  }

  void setCurrentBeat(double beat) {
    currentBeat = beat;
    repaint();
  }

  void setActiveNotes(const std::set<int> &active) {
    activeNotes = active;
    auto chord = chordDetector.detect(active);
    chordLabel.setText(chord.name.isEmpty() ? "" : chord.name,
                       juce::dontSendNotification);
    repaint();
  }

  /** Sets BPM and ties scroll speed so note fall rate matches tempo (e.g. 120 BPM = base speed). */
  void setBpm(double bpm) {
    beatsPerSecond = bpm / 60.0;
    pixelsPerSecond = basePixelsPerSecondAt120 * (float)(bpm / 120.0) * scrollSpeedScale;
  }
  /** Multiplier for scroll speed (1.0 = tempo-synced; >1 = faster fall). */
  void setScrollSpeedScale(float scale) {
    scrollSpeedScale = juce::jlimit(0.25f, 4.0f, scale);
    double bpm = beatsPerSecond * 60.0;
    pixelsPerSecond = basePixelsPerSecondAt120 * (float)(bpm / 120.0) * scrollSpeedScale;
  }
  float getScrollSpeedScale() const { return scrollSpeedScale; }

  void setKeyboardComponent(juce::MidiKeyboardComponent *k) {
    keyboardComp = k;
  }
  juce::MidiKeyboardComponent *keyboardComp = nullptr;

  void paint(juce::Graphics &g) override {
    g.fillAll(juce::Colours::black);

    auto bounds = getLocalBounds().toFloat();
    // Hit line at bottom of view = top of keyboard (notes "hit" when they reach
    // the keys)
    float hitLineY =
        bounds.getHeight() * juce::jlimit(0.5f, 1.0f, hitLinePosition);

    // Draw hit line aligned with very top of keyboard (bottom of this
    // component)
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.fillRect(0.0f, hitLineY - 2.0f, bounds.getWidth(), 4.0f);

    // Calculate visible beat range (notes fall from above toward hitLineY)
    double visibleBeats = (hitLineY / pixelsPerSecond) * beatsPerSecond;

    // Default linear width (fallback)
    float keyWidth = bounds.getWidth() / (float)visibleKeyCount;

    for (const auto &note : notes) {
      // Skip notes outside visible range (beat-wise)
      if (note.getEndBeat() < currentBeat)
        continue;
      if (note.startBeat > currentBeat + visibleBeats)
        continue;

      // Calculate Y position
      float startY = (float)(hitLineY - (note.startBeat - currentBeat) /
                                            beatsPerSecond * pixelsPerSecond);
      float endY = (float)(hitLineY - (note.getEndBeat() - currentBeat) /
                                          beatsPerSecond * pixelsPerSecond);

      // Note X position (map MIDI note to screen)
      float x = 0.0f;
      float w = 0.0f;

      if (keyboardComp) {
        auto r = keyboardComp->getRectangleForKey(note.noteNumber);
        if (r.isEmpty())
          continue; // Key not visible
        x = (float)r.getX();
        w = (float)r.getWidth();
      } else {
        int keyInRange = note.noteNumber - lowestKey;
        if (keyInRange < 0 || keyInRange >= visibleKeyCount)
          continue;
        x = (float)keyInRange * keyWidth;
        w = keyWidth;
      }

      // Per-channel colours so user can tell channels apart (1-16)
      juce::Colour noteColour = channelColour(note.channel);

      // Highlight if currently playing
      if (activeNotes.count(note.noteNumber)) {
        noteColour = noteColour.brighter(0.3f);
      }

      // Draw note bar
      g.setColour(noteColour);
      g.fillRoundedRectangle(x + 1.0f, endY, w - 2.0f, startY - endY, 4.0f);

      // Glow for active notes
      if (activeNotes.count(note.noteNumber)) {
        g.setColour(noteColour.withAlpha(0.3f));
        g.fillRoundedRectangle(x - 2.0f, endY - 2.0f, w + 4.0f,
                               startY - endY + 4.0f, 6.0f);
      }
    }
  }

  /** Sync key range with the bottom keyboard so notes align vertically. */
  void setKeyRange(int lowest, int count) {
    lowestKey = juce::jlimit(0, 127, lowest);
    visibleKeyCount = juce::jlimit(1, 88, count);
  }

  void resized() override {
    auto r = getLocalBounds();
    chordLabel.setBounds(r.removeFromTop(28).reduced(6, 2));
  }

  // Configuration: 1.0 = hit line at bottom (aligned with top of keyboard)
  float hitLinePosition = 1.0f;
  float pixelsPerSecond = 200.0f; // Updated by setBpm/setScrollSpeedScale
  int lowestKey = 36;             // C2
  int visibleKeyCount = 49;       // 4 octaves

private:
  std::vector<EditableNote> notes;
  std::set<int> activeNotes;
  double currentBeat = 0.0;
  double beatsPerSecond = 2.0;   // 120 BPM
  float scrollSpeedScale = 1.0f;
  static constexpr float basePixelsPerSecondAt120 = 200.0f;
  ChordDetector chordDetector;
  juce::Label chordLabel;

  bool isBlackKey(int pitchClass) const {
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 ||
           pitchClass == 8 || pitchClass == 10;
  }

  static juce::Colour channelColour(int ch) {
    int c = juce::jlimit(1, 16, ch);
    float hue = ((c - 1) / 15.0f) * 0.85f; // 0..0.85 to avoid wrapping to red
    return juce::Colour::fromHSV(hue, 0.75f, 0.95f, 1.0f);
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlayView)
};
