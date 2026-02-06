/*
  ==============================================================================
    Source/UI/Panels/SpliceEditorRendering.cpp
    Role: SpliceEditor paint helpers (waterfall + edit-mode).
  ==============================================================================
*/
#include "SpliceEditorRendering.h"
#include "../Theme.h"
#include "../Fonts.h"

#include <cmath>

static const int kRulerHeight = 18;

namespace SpliceEditorRendering {

static bool isBlackKey(int pitch) {
  int p = pitch % 12;
  return (p == 1 || p == 3 || p == 6 || p == 8 || p == 10);
}

static float noteToXWaterfall(int noteNumber, float width) {
  return (noteNumber / 127.0f) * width;
}

void paintWaterfall(
    juce::Graphics &g,
    const std::vector<EditableNote> &notes,
    double playheadBeat,
    float waterfallVisibleBeats,
    juce::Rectangle<float> bounds,
    bool highlightActiveNotes) {
  g.fillAll(juce::Colours::black);

  float hitLineY = bounds.getHeight() * 0.85f;
  g.setColour(juce::Colours::white.withAlpha(0.8f));
  g.fillRect(0.0f, hitLineY - 2.0f, bounds.getWidth(), 4.0f);

  float pxlPerBeat = bounds.getHeight() / waterfallVisibleBeats;

  for (const auto &note : notes) {
    if (note.getEndBeat() < playheadBeat ||
        note.startBeat > playheadBeat + (double)waterfallVisibleBeats)
      continue;
    float relativeStart = (float)(note.startBeat - playheadBeat);
    float relativeEnd = (float)(note.getEndBeat() - playheadBeat);
    float y1 = bounds.getHeight() - relativeEnd * pxlPerBeat;
    float y2 = bounds.getHeight() - relativeStart * pxlPerBeat;
    float x = noteToXWaterfall(note.noteNumber, bounds.getWidth());
    float noteWidth = bounds.getWidth() / 128.0f * 2.0f;
    juce::Colour noteColor = Theme::getChannelColor(note.channel);
    bool isPlaying =
        (note.startBeat <= playheadBeat && note.getEndBeat() > playheadBeat);
    if (highlightActiveNotes && isPlaying)
      noteColor = noteColor.brighter(0.5f);
    g.setColour(noteColor);
    g.fillRoundedRectangle(x, y1, noteWidth, y2 - y1, 3.0f);
  }
}

void paintEditMode(
    juce::Graphics &g,
    const SpliceEditor::RenderState &state,
    int width,
    int height,
    bool drawNotesOnCpu) {
  // Guard against invalid dimensions (avoids black/invalid content when layout not yet applied)
  if (width <= 0 || height <= 0) return;
  g.fillAll(juce::Colour(0xff131313));
  if (state.noteHeight <= 0.0f || state.pianoKeysWidth < 0.0f ||
      state.pixelsPerBeat <= 0.0f)
    return;

  const int h = height - kRulerHeight;
  const int w = width;
  auto beatToX = [&](double beat) {
    return state.pianoKeysWidth +
           (float)((beat - state.scrollX) * state.pixelsPerBeat);
  };
  auto pitchToY = [&](int note) {
    return (127 - note) * state.noteHeight - state.scrollY;
  };

  // Horizontal ruler: beat/bar labels and optional measure numbers
  {
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect((int)state.pianoKeysWidth, 0, w - (int)state.pianoKeysWidth, kRulerHeight);
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::FontOptions(10.0f));
    double beatStart = state.scrollX;
    double beatEnd = state.scrollX + (double)(w - state.pianoKeysWidth) / state.pixelsPerBeat;
    for (double b = std::floor(beatStart); b <= beatEnd + 0.001; b += 1.0) {
      float x = beatToX(b);
      if (x >= state.pianoKeysWidth && x < w) {
        bool isBar = (std::abs(std::fmod(b, 4.0)) < 0.001);
        g.setColour(isBar ? juce::Colours::white.withAlpha(0.9f) : juce::Colours::white.withAlpha(0.5f));
        g.drawText(juce::String(juce::roundToInt(b)), (int)x - 12, 0, 24, kRulerHeight, juce::Justification::centred);
      }
    }
    // Optional measure numbers at bar boundaries (every 4 beats)
    for (double b = 4.0 * std::floor(beatStart / 4.0); b <= beatEnd + 0.001; b += 4.0) {
      if (b < 0) continue;
      float x = beatToX(b);
      if (x >= state.pianoKeysWidth && x < w - 30) {
        int measure = 1 + (int)(b / 4.0);
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.setFont(juce::FontOptions(8.0f));
        g.drawText("M" + juce::String(measure), (int)x - 14, 0, 28, kRulerHeight, juce::Justification::centred);
      }
    }
    if (state.snapGrid > 0.0) {
      int snapDenom = juce::jmax(1, (int)(0.5 + 1.0 / state.snapGrid));
      g.setColour(juce::Colours::white.withAlpha(0.5f));
      g.setFont(juce::FontOptions(9.0f));
      g.drawText("Snap 1/" + juce::String(snapDenom), w - 52, 0, 50, kRulerHeight, juce::Justification::centredRight);
    }
  }

  g.saveState();
  g.addTransform(juce::AffineTransform::translation(0, kRulerHeight));

  int minNote = juce::jlimit(
      0, 127, 127 - (int)((h + state.scrollY) / state.noteHeight));
  int maxNote =
      juce::jlimit(0, 127, 127 - (int)(-state.scrollY / state.noteHeight));

  for (int n = maxNote; n >= minNote; --n) {
    float y = pitchToY(n);
    if (n % 12 == 0) {
      g.setColour(juce::Colours::white.withAlpha(0.05f));
      g.fillRect(state.pianoKeysWidth, y, (float)w, state.noteHeight);
    } else if (isBlackKey(n)) {
      g.setColour(juce::Colour(0xff1e1e1e));
      g.fillRect(state.pianoKeysWidth, y, (float)w, state.noteHeight);
    }
    g.setColour(juce::Colour(0xff333333));
    g.drawHorizontalLine((int)y, state.pianoKeysWidth, (float)w);
  }

  double beatStart = state.scrollX;
  double beatEnd =
      state.scrollX +
      (double)(w - state.pianoKeysWidth) / state.pixelsPerBeat;
  double gridStep = state.snapGrid > 0.0 ? state.snapGrid : 0.25;
  bool snapOn = state.snapGrid > 0.0;
  for (double b = std::floor(beatStart); b <= beatEnd; b += gridStep) {
    float x = beatToX(b);
    if (x < state.pianoKeysWidth)
      continue;
    bool isBar = (std::abs(std::fmod(b, 4.0)) < 0.001);
    bool isBeat = (std::abs(std::fmod(b, 1.0)) < 0.001);
    if (isBar)
      g.setColour(juce::Colour(0xff444444));
    else if (isBeat)
      g.setColour(snapOn ? juce::Colour(0xff383838) : juce::Colour(0xff333333));
    else
      g.setColour(snapOn ? juce::Colour(0xff282828) : juce::Colour(0xff222222));
    g.drawVerticalLine((int)x, 0.0f, (float)h);
  }

  if (drawNotesOnCpu) {
    for (size_t i = 0; i < state.notes.size(); ++i) {
      const auto &n = state.notes[i];
      if (n.channel < 0)
        continue;
      float y = pitchToY(n.noteNumber);
      if (y > h || y + state.noteHeight < 0)
        continue;
      float x = beatToX(n.startBeat);
      float endX = beatToX(n.getEndBeat());
      if (x > w || endX < state.pianoKeysWidth)
        continue;

      float rw = (float)(n.durationBeats * state.pixelsPerBeat);
      auto r = juce::Rectangle<float>(x, y + 1, rw - 1, state.noteHeight - 2);

      juce::Colour baseC;
      switch (n.channel % 4) {
      case 0: baseC = juce::Colour(0xff00f0ff); break;
      case 1: baseC = juce::Colour(0xffbd00ff); break;
      case 2: baseC = juce::Colour(0xff00ff9d); break;
      case 3: baseC = juce::Colour(0xff00a2ff); break;
      default: baseC = juce::Colours::white; break;
      }
      if (state.selectedIndices.count((int)i))
        baseC = baseC.brighter(0.5f);

      juce::ColourGradient grad(baseC.brighter(0.2f), r.getX(), r.getY(),
                                baseC.darker(0.2f), r.getX(), r.getBottom(),
                                false);
      g.setGradientFill(grad);
      g.fillRoundedRectangle(r, 4.0f);
      g.setColour(baseC.brighter(0.8f));
      g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
    }
  }

  if (state.showGhost) {
    float x = beatToX(state.ghostNote.startBeat);
    float y = pitchToY(state.ghostNote.noteNumber);
    if (state.isSpliceHover) {
      g.setColour(juce::Colours::white);
      g.drawLine(x, y, x, y + state.noteHeight, 2.0f);
      g.setColour(juce::Colours::white.withAlpha(0.4f));
      g.fillRect(x - 1, y, 3.0f, state.noteHeight);
    } else {
      float gw = (float)(state.ghostNote.durationBeats * state.pixelsPerBeat);
      auto ghostRect = juce::Rectangle<float>(x + 1, y + 1, gw - 2,
                                              state.noteHeight - 2);
      g.setColour(juce::Colours::white.withAlpha(0.2f));
      g.fillRoundedRectangle(ghostRect, 4.0f);
      g.setColour(juce::Colours::white.withAlpha(0.5f));
      g.drawRoundedRectangle(ghostRect, 4.0f, 1.0f);
    }
  }

  if (state.isSelectionRectActive) {
    auto selRect = state.selectionRect.toFloat();
    // selectionRect is in component coords; we're drawn after translation(0, kRulerHeight)
    selRect.setY(selRect.getY() - kRulerHeight);
    g.setColour(Theme::accent.withAlpha(0.15f));
    g.fillRect(selRect);
    g.setColour(Theme::accent.withAlpha(0.8f));
    g.drawRect(selRect, 1.0f);
  }

  g.setColour(juce::Colour(0xff181818));
  g.fillRect(0.0f, 0.0f, state.pianoKeysWidth, (float)h);

  float pianoKeysWidth = state.pianoKeysWidth;
  float noteHeight = state.noteHeight;
  for (int n = maxNote; n >= minNote; --n) {
    float y = pitchToY(n);
    auto keyRect =
        juce::Rectangle<float>(0, y, pianoKeysWidth, noteHeight);
    float keyBottom = keyRect.getBottom();

    if (isBlackKey(n)) {
      g.setColour(juce::Colours::black);
      g.fillRect(keyRect);
      g.setColour(juce::Colour(0xff222222));
      g.fillRect(keyRect.reduced(0, 1));
    } else {
      g.setColour(juce::Colours::white);
      g.fillRect(keyRect);
      g.setColour(juce::Colour(0xffdddddd));
      g.fillRect(keyRect.reduced(0, 1));
      if (n % 12 == 0) {
        g.setColour(juce::Colours::black);
        g.setFont(Fonts::bodyBold());
        g.drawText("C" + juce::String(n / 12 - 2),
                   keyRect.removeFromRight(pianoKeysWidth - 5),
                   juce::Justification::centredRight);
      }
    }
    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.drawHorizontalLine((int)keyBottom, 0.0f, pianoKeysWidth);
  }

  if (state.playheadBeat >= state.scrollX && state.playheadBeat <= beatEnd) {
    float phX = beatToX(state.playheadBeat);
    // Restrict playhead to note area (right of piano strip); use content height h (we are after translation by kRulerHeight)
    float noteAreaX = state.pianoKeysWidth;
    float noteAreaW = (float)w - state.pianoKeysWidth;
    if (phX >= noteAreaX && phX < (float)w && noteAreaW > 0 && h > 0) {
      g.saveState();
      g.reduceClipRegion(juce::roundToInt(noteAreaX), 0, juce::roundToInt(noteAreaW), h);
      g.setColour(juce::Colour(0xff00a2ff).withAlpha(0.4f));
      g.fillRect(phX - 1.0f, 0.0f, 3.0f, (float)h);
      g.setColour(juce::Colour(0xff00a2ff));
      g.drawVerticalLine((int)phX, 0.0f, (float)h);
      juce::Path cap;
      cap.addTriangle(phX - 6.0f, 0.0f, phX + 6.0f, 0.0f, phX, 12.0f);
      g.setColour(juce::Colour(0xff00a2ff));
      g.fillPath(cap);
      g.restoreState();
    }
  }
}

} // namespace SpliceEditorRendering
