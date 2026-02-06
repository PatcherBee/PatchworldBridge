/*
  ==============================================================================
    Source/UI/Panels/SpliceEditorRendering.h
    Role: SpliceEditor paint helpers (waterfall + edit-mode grid/notes).
  ==============================================================================
*/
#pragma once

#include <juce_graphics/juce_graphics.h>
#include <vector>

#include "../../Audio/EditableNote.h"
#include "SpliceEditor.h"

namespace SpliceEditorRendering {

/** Paint waterfall (play) view: falling note bars. */
void paintWaterfall(
    juce::Graphics &g,
    const std::vector<EditableNote> &notes,
    double playheadBeat,
    float waterfallVisibleBeats,
    juce::Rectangle<float> bounds,
    bool highlightActiveNotes);

/** Paint edit-mode view: grid, notes, ghost, selection rect, piano strip, playhead.
 *  When drawNotesOnCpu is false, notes are drawn on GPU (SpliceEditor::renderGL). */
void paintEditMode(
    juce::Graphics &g,
    const SpliceEditor::RenderState &state,
    int width,
    int height,
    bool drawNotesOnCpu = true);

} // namespace SpliceEditorRendering
