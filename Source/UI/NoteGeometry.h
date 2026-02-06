/*
  ==============================================================================
    Source/UI/NoteGeometry.h
    Role: Shared note rectangle calculation (roadmap 10.4 - deduplication)
  ==============================================================================
*/
#pragma once

#include "../Audio/EditableNote.h"
#include <juce_graphics/juce_graphics.h>

namespace NoteGeometry {

/** Calculate note rectangle from beat/pitch coordinates. */
inline juce::Rectangle<float> getNoteRect(
    double startBeat, double durationBeats, int noteNumber,
    float pixelsPerBeat, float noteHeight,
    float scrollX, float scrollY, float pianoKeysWidth = 0.0f) {
  float x = pianoKeysWidth + (float)(startBeat * pixelsPerBeat) - scrollX;
  float y = (float)((127 - noteNumber) * noteHeight) - scrollY;
  float w = (float)(durationBeats * pixelsPerBeat);
  return {x, y, w, noteHeight};
}

/** Calculate note rectangle from EditableNote. */
inline juce::Rectangle<float> getNoteRect(
    const EditableNote &note, float pixelsPerBeat, float noteHeight,
    float scrollX, float scrollY, float pianoKeysWidth = 0.0f) {
  return getNoteRect(note.startBeat, note.durationBeats, note.noteNumber,
                     pixelsPerBeat, noteHeight, scrollX, scrollY, pianoKeysWidth);
}

/** Convert X coordinate to beat. */
inline double xToBeat(float x, float scrollX, float pixelsPerBeat, float pianoKeysWidth = 0.0f) {
  return (x - pianoKeysWidth + scrollX) / pixelsPerBeat;
}

/** Convert Y coordinate to MIDI note number. */
inline int yToNote(float y, float scrollY, float noteHeight) {
  return 127 - (int)((y + scrollY) / noteHeight);
}

/** Convert beat to X coordinate. */
inline float beatToX(double beat, float scrollX, float pixelsPerBeat, float pianoKeysWidth = 0.0f) {
  return pianoKeysWidth + (float)(beat * pixelsPerBeat) - scrollX;
}

/** Convert MIDI note number to Y coordinate. */
inline float noteToY(int noteNumber, float scrollY, float noteHeight) {
  return (float)((127 - noteNumber) * noteHeight) - scrollY;
}

/** Quantize beat to grid. */
inline double quantizeBeat(double beat, double grid) {
  if (grid <= 0.0) return beat;
  return std::round(beat / grid) * grid;
}

/** Smart quantize: only quantize if deviation is significant (10-90% of grid). */
inline double smartQuantizeBeat(double beat, double grid, double threshold = 0.1) {
  if (grid <= 0.0) return beat;
  double quantized = std::round(beat / grid) * grid;
  double deviation = std::abs(beat - quantized) / grid;
  if (deviation > threshold && deviation < (1.0 - threshold)) {
    return quantized;
  }
  return beat; // Keep original if very close or very far
}

} // namespace NoteGeometry
