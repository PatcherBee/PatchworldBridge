/*
  ==============================================================================
    Source/UI/Panels/SpliceEditorMouse.cpp
    Mouse and keyboard interaction for SpliceEditor (extracted for file splitting).
  ==============================================================================
*/
#include "SpliceEditor.h"
#include "../NoteEditUndoAction.h"
#include "../PopupMenuOptions.h"

// ==============================================================================
// MOUSE MOVE & GHOST NOTE
// ==============================================================================
void SpliceEditor::mouseMove(const juce::MouseEvent &e) {
  if (e.x < pianoKeysWidth)
    return;

  double beat = xToBeat((float)e.x);
  int pitch = yToPitch((float)e.y);

  // Check modifiers
  bool isAlt = e.mods.isAltDown(); // Splice Mode

  // Hit test for existing notes (lock protects notes read vs paint)
  int hoveredIndex = -1;
  double snappedBeat;
  int tooltipNote = -1;
  double tooltipStart = 0, tooltipDur = 0;
  float tooltipVel = 0;
  {
    const juce::ScopedLock sl(noteLock);
    for (int i = 0; i < (int)notes.size(); ++i) {
      if (notes[i].noteNumber == pitch && beat >= notes[i].startBeat &&
          beat < notes[i].getEndBeat()) {
        hoveredIndex = i;
        tooltipNote = notes[i].noteNumber;
        tooltipStart = notes[i].startBeat;
        tooltipDur = notes[i].durationBeats;
        tooltipVel = notes[i].velocity;
        break;
      }
    }
    double grid = snapGrid > 0.0 ? snapGrid : 0.25;
    snappedBeat = std::round(beat / grid) * grid;
  }

  hoveredNoteIndex = hoveredIndex;
  if (hoveredNoteIndex >= 0 && tooltipNote >= 0) {
    static const char *noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int nn = tooltipNote % 12;
    int oct = tooltipNote / 12;
    int vel127 = juce::jlimit(0, 127, (int)(tooltipVel * 127.0f));
    juce::String tt = juce::String(noteNames[nn]) + juce::String(oct) + " (" + juce::String(tooltipNote)
        + ") · Vel " + juce::String(vel127) + " · " + juce::String(tooltipDur, 2) + " beats · Start " + juce::String(tooltipStart, 2);
    setTooltip(tt);
  } else {
    setTooltip({});
  }

  if (isAlt && hoveredIndex != -1) {
    isSpliceHover = true;
    showGhost = true;
    ghostNote.startBeat = snappedBeat;
    ghostNote.noteNumber = pitch;
  } else if (hoveredIndex == -1) {
    isSpliceHover = false;
    showGhost = true;
    ghostNote.noteNumber = pitch;
    ghostNote.startBeat = snappedBeat;
    ghostNote.durationBeats = lastNoteLength;
    ghostNote.velocity = 1.0f;

    // Play note preview on hover only in Play mode (no sounds when editing grid)
    if (notePreviewEnabled && currentViewMode == ViewMode::Play && context && context->engine) {
      auto now = static_cast<uint64_t>(juce::Time::getMillisecondCounter());
      if (pitch != lastPreviewedPitch &&
          (now - lastPreviewTime) >= NOTE_PREVIEW_DEBOUNCE_MS) {
        lastPreviewedPitch = pitch;
        lastPreviewTime = now;
        // Send short preview note (100ms duration, velocity 0.6)
        juce::MidiMessage noteOn = juce::MidiMessage::noteOn(1, pitch, (juce::uint8)76);
        juce::MidiMessage noteOff = juce::MidiMessage::noteOff(1, pitch);
        context->engine->onMidiEvent(noteOn);
        juce::Component::SafePointer<SpliceEditor> safeThis(this);
        auto ctx = context;
        juce::Timer::callAfterDelay(100, [safeThis, noteOff, ctx]() {
          if (safeThis && ctx && ctx->engine)
            ctx->engine->onMidiEvent(noteOff);
        });
      }
    }
  } else {
    showGhost = false;
    lastPreviewedPitch = -1; // Reset when hovering over existing note
  }

  pushRenderState();
}

void SpliceEditor::mouseExit(const juce::MouseEvent &) {
  hoveredNoteIndex = -1;
  setTooltip({});
  showGhost = false;
  lastPreviewedPitch = -1; // Reset preview state
  pushRenderState();
}

// ==============================================================================
// INTERACTION
// ==============================================================================
void SpliceEditor::mouseDown(const juce::MouseEvent &e) {
  if (context == nullptr || context->engine == nullptr)
    return;

  lastMousePos = e.getPosition();
  bool isShift = e.mods.isShiftDown();
  bool isAlt = e.mods.isAltDown();
  bool isRight = e.mods.isRightButtonDown();

  if (e.x < pianoKeysWidth)
    return;

  const juce::ScopedLock sl(noteLock);

  // Right-click: context menu
  if (isRight) {
    int hitIdx = -1;
    double beatAtCursor = xToBeat((float)e.x);
    for (int i = 0; i < (int)notes.size(); ++i) {
      auto r = getNoteRect(notes[i]);
      if (r.contains(e.position)) {
        hitIdx = i;
        break;
      }
    }
    juce::PopupMenu m;
    if (hitIdx >= 0) {
      m.addItem("Split Note at Cursor", [this, hitIdx, beatAtCursor] {
        splitNoteAtPosition(hitIdx, beatAtCursor);
      });
      m.addItem("Delete Note", [this, hitIdx] {
        const juce::ScopedLock sl2(noteLock);
        if (hitIdx >= 0 && hitIdx < (int)notes.size()) {
          auto beforeNotes = notes;
          auto beforeSel = selectedIndices;
          notes.erase(notes.begin() + hitIdx);
          std::set<int> newSel;
          for (int i : selectedIndices) {
            if (i < hitIdx) newSel.insert(i);
            else if (i > hitIdx) newSel.insert(i - 1);
          }
          selectedIndices = newSel;
          updateEngine();
          if (context)
            context->undoManager.perform(new NoteEditUndoAction(
                this, context, beforeNotes, beforeSel, notes, selectedIndices, "Delete Note"));
          pushRenderState();
        }
      });
      m.addSeparator();
    }
    if (!selectedIndices.empty()) {
      m.addItem("Copy (Ctrl+C)", [this] { copySelected(); });
      m.addItem("Duplicate (Ctrl+D)", [this] { duplicateSelected(); });
      m.addItem("Delete (Del)", [this] { deleteSelected(); });
      m.addSeparator();
      m.addItem("Quantize Selected (Q)", [this] { quantizeSelected(); });
      juce::PopupMenu quantizeSub;
      quantizeSub.addItem("Soft", true, getQuantizeMode() == QuantizeMode::Soft, [this] { quantizeSelectedWithMode(QuantizeMode::Soft); });
      quantizeSub.addItem("Hard", true, getQuantizeMode() == QuantizeMode::Hard, [this] { quantizeSelectedWithMode(QuantizeMode::Hard); });
      quantizeSub.addItem("Groove", true, getQuantizeMode() == QuantizeMode::Groove, [this] { quantizeSelectedWithMode(QuantizeMode::Groove); });
      m.addSubMenu("Quantize mode", quantizeSub);
      m.addItem("Humanize timing", [this] { humanizeTiming(0.02f); });
      if (selectedIndices.size() >= 2)
        m.addItem("Merge Selected (G)", [this] { mergeSelectedNotes(); });
      m.addSeparator();
    }
    m.addItem("Paste (Ctrl+V)", [this] { pasteFromClipboard(); });
    m.addSeparator();
    m.addItem("Select All", [this] { selectAll(); });
    m.addItem("Quantize All", [this] {
      selectAll();
      quantizeSelected();
    });
    m.showMenuAsync(PopupMenuOptions::forComponent(this));
    return;
  }

  // 1. Hit Test
  int hitIndex = -1;
  for (int i = 0; i < (int)notes.size(); ++i) {
    auto r = getNoteRect(notes[i]);
    if (r.contains(e.position)) {
      hitIndex = i;
      break;
    }
  }

  // Erase mode
  if (currentMode == EditMode::Erase) {
    if (hitIndex >= 0) {
      notes.erase(notes.begin() + hitIndex);
      updateEngine();
    }
    pushRenderState();
    return;
  }
  if (currentMode == EditMode::Paint) {
    paintedThisDrag.clear();
  }

  // Stretch mode: initialize stretch operation on selected notes
  if (currentMode == EditMode::Stretching && !selectedIndices.empty()) {
    double beat = xToBeat((float)e.x);
    stretchAnchorBeat = beat;
    stretchInitialOffsets.clear();
    double minBeat = 1e30, maxBeat = -1e30;
    for (int idx : selectedIndices) {
      if (idx >= 0 && idx < (int)notes.size()) {
        minBeat = std::min(minBeat, notes[idx].startBeat);
        maxBeat = std::max(maxBeat, notes[idx].getEndBeat());
      }
    }
    stretchInitialSpan = maxBeat - minBeat;
    for (int idx : selectedIndices) {
      if (idx >= 0 && idx < (int)notes.size()) {
        stretchInitialOffsets.push_back({idx, notes[idx].startBeat - minBeat});
      }
    }
    return;
  }

  // 2. Logic
  if (isAlt) {
    // DELETE / SPLICE
    if (hitIndex != -1) {
      notes.erase(notes.begin() + hitIndex);
      updateEngine();
      pushRenderState();
    }
    return;
  }

  if (hitIndex != -1) {
    auto &n = notes[hitIndex];
    float noteStartX = beatToX(n.startBeat);
    float noteEndX = beatToX(n.getEndBeat());

    if (std::abs((float)e.x - noteEndX) < 10.0f) {
      currentMode = EditMode::ResizingEnd;
    } else if (std::abs((float)e.x - noteStartX) < 10.0f) {
      currentMode = EditMode::ResizingStart;
    } else {
      currentMode = EditMode::Moving;
    }

    hoveredNoteIndex = hitIndex;

    if (isShift) {
      if (selectedIndices.count(hitIndex))
        selectedIndices.erase(hitIndex);
      else
        selectedIndices.insert(hitIndex);
    } else {
      if (selectedIndices.find(hitIndex) == selectedIndices.end()) {
        deselectAll();
        selectedIndices.insert(hitIndex);
      }
    }
    pushRenderState();
  } else if (currentMode == EditMode::Paint) {
    // Paint mode: drag will add notes; nothing on down
    pushRenderState();
    return;
  } else {
    // EMPTY SPACE
    bool createNote = e.mods.isCtrlDown() || e.mods.isCommandDown() ||
                      (currentMode == EditMode::Drawing);
    if (createNote) {
      // Create Note
      double beat = xToBeat((float)e.x);
      double grid = snapGrid > 0.0 ? snapGrid : 0.25;
      beat = std::round(beat / grid) * grid;

      int pitch = yToPitch((float)e.y);

      EditableNote n;
      n.startBeat = beat;
      n.durationBeats = lastNoteLength; // Use remembered length
      n.noteNumber = pitch;
      n.velocity = 0.8f;
      n.channel = 1;

      notes.push_back(n);

      deselectAll();
      selectedIndices.insert((int)notes.size() - 1);
      hoveredNoteIndex = (int)notes.size() - 1;
      // Draw mode: one note per click; drag right to set length. Paint mode: drag adds more notes.
      currentMode = (currentMode == EditMode::Drawing)
          ? EditMode::ResizingEnd   // Place one note, then drag to lengthen tail
          : EditMode::Moving;       // Ctrl/Cmd click: place and allow move

      updateEngine();
      pushRenderState();
    } else {
      // Marquee
      currentMode = EditMode::Selecting;
      selectionRect.setBounds(e.x, e.y, 0, 0);
      isSelectionRectActive = true;
      if (!isShift)
        deselectAll();
      pushRenderState();
    }
  }
}

void SpliceEditor::mouseDrag(const juce::MouseEvent &e) {
  {
    const juce::ScopedLock sl(noteLock);
    double currentBeat = xToBeat((float)e.x);
    double grid = snapGrid > 0 ? snapGrid : 0.25;
    double snappedBeat = std::round(currentBeat / grid) * grid;
    // Shift+drag: finer grid for move/resize (half grid, min 1/64)
    if (e.mods.isShiftDown() &&
        (currentMode == EditMode::Moving || currentMode == EditMode::ResizingEnd ||
         currentMode == EditMode::ResizingStart)) {
      double fineGrid = juce::jmax(1.0 / 64.0, grid / 2.0);
      snappedBeat = std::round(currentBeat / fineGrid) * fineGrid;
    }
    int currentPitch = yToPitch((float)e.y);

    if (currentMode == EditMode::Paint && e.x >= pianoKeysWidth) {
      int64_t key = (int64_t)currentPitch * 100000 + (int64_t)(snappedBeat * 1000);
      if (paintedThisDrag.find(key) == paintedThisDrag.end()) {
        EditableNote n;
        n.startBeat = snappedBeat;
        n.noteNumber = currentPitch;
        n.durationBeats = snapGrid > 0 ? snapGrid : 0.25;
        n.velocity = lastPaintVelocity;
        n.channel = 1;
        notes.push_back(n);
        paintedThisDrag.insert(key);
        updateEngine();
      }
    } else if (currentMode == EditMode::Erase) {
      for (int i = (int)notes.size() - 1; i >= 0; --i) {
        if (notes[i].noteNumber == currentPitch &&
            currentBeat >= notes[i].startBeat &&
            currentBeat < notes[i].getEndBeat()) {
          notes.erase(notes.begin() + i);
          updateEngine();
          break;
        }
      }
    } else if (currentMode == EditMode::Stretching && !stretchInitialOffsets.empty()) {
      // Time-stretch selected notes based on drag distance
      double dragBeat = xToBeat((float)e.x);
      double dragDelta = dragBeat - stretchAnchorBeat;
      double factor = stretchInitialSpan > 0.01 ?
                      (stretchInitialSpan + dragDelta) / stretchInitialSpan : 1.0;
      factor = juce::jlimit(0.1, 10.0, factor); // Clamp stretch factor

      // Find min beat of selection to use as pivot
      double minBeat = 1e30;
      for (auto& [idx, offset] : stretchInitialOffsets) {
        if (idx >= 0 && idx < (int)notes.size()) {
          minBeat = std::min(minBeat, notes[idx].startBeat);
        }
      }

      // Apply stretch: scale offsets from pivot
      for (auto& [idx, initialOffset] : stretchInitialOffsets) {
        if (idx >= 0 && idx < (int)notes.size()) {
          notes[idx].startBeat = minBeat + initialOffset * factor;
          notes[idx].durationBeats *= factor;
        }
      }
      updateEngine();
    } else if (currentMode == EditMode::Selecting) {
      selectionRect.setWidth(e.x - selectionRect.getX());
      selectionRect.setHeight(e.y - selectionRect.getY());
      auto r = selectionRect.toFloat();
      if (selectionRect.getWidth() < 0) {
        r.setX((float)e.x);
        r.setWidth((float)(selectionRect.getX() - e.x));
      }
      if (selectionRect.getHeight() < 0) {
        r.setY((float)e.y);
        r.setHeight((float)(selectionRect.getY() - e.y));
      }
      selectedIndices.clear();
      for (int i = 0; i < (int)notes.size(); ++i) {
        if (r.intersects(getNoteRect(notes[i])))
          selectedIndices.insert(i);
      }
    } else if (currentMode != EditMode::None && currentMode != EditMode::Paint && currentMode != EditMode::Erase && currentMode != EditMode::Stretching) {
      if (currentMode == EditMode::Moving) {
        if (hoveredNoteIndex != -1 && selectedIndices.count(hoveredNoteIndex)) {
          EditableNote &mainNote = notes[hoveredNoteIndex];
          double beatDelta = snappedBeat - mainNote.startBeat;
          int pitchDelta = currentPitch - mainNote.noteNumber;
          if (std::abs(beatDelta) > 0.001 || pitchDelta != 0) {
            for (int idx : selectedIndices) {
              notes[idx].startBeat += beatDelta;
              notes[idx].noteNumber += pitchDelta;
            }
            updateEngine();
          }
        }
      } else if (currentMode == EditMode::ResizingEnd) {
        if (hoveredNoteIndex != -1 && selectedIndices.count(hoveredNoteIndex)) {
          EditableNote &mainNote = notes[hoveredNoteIndex];
          double minGrid = snapGrid > 0.0 ? snapGrid : 0.25;
          double newEnd = juce::jmax(mainNote.startBeat + minGrid, snappedBeat);
          mainNote.durationBeats = newEnd - mainNote.startBeat;
          lastNoteLength = (float)mainNote.durationBeats;
          updateEngine();
        }
      } else if (currentMode == EditMode::ResizingStart) {
        if (hoveredNoteIndex != -1 && selectedIndices.count(hoveredNoteIndex)) {
          EditableNote &mainNote = notes[hoveredNoteIndex];
          double oldEnd = mainNote.getEndBeat();
          double minGrid = snapGrid > 0.0 ? snapGrid : 0.25;
          double newStart = juce::jmin(oldEnd - minGrid, snappedBeat);
          if (newStart < oldEnd) {
            mainNote.startBeat = newStart;
            mainNote.durationBeats = oldEnd - newStart;
            lastNoteLength = (float)mainNote.durationBeats;
            updateEngine();
          }
        }
      }
      lastMousePos = e.getPosition();
    }
  }
  pushRenderState();
}

void SpliceEditor::mouseUp(const juce::MouseEvent &) {
  if (currentMode == EditMode::Paint)
    paintedThisDrag.clear();
  if (currentMode == EditMode::Stretching)
    stretchInitialOffsets.clear();
  if (currentMode != EditMode::Paint && currentMode != EditMode::Erase && currentMode != EditMode::Stretching)
    currentMode = EditMode::None;
  isSelectionRectActive = false;
  pushRenderState();
}

void SpliceEditor::mouseDoubleClick(const juce::MouseEvent &e) {
  bool didErase = false;
  {
    const juce::ScopedLock sl(noteLock);
    for (auto it = notes.begin(); it != notes.end(); ++it) {
      auto r = getNoteRect(*it);
      if (r.contains(e.position)) {
        notes.erase(it);
        updateEngine();
        didErase = true;
        break;
      }
    }
  }
  if (didErase)
    pushRenderState();
}

void SpliceEditor::mouseWheelMove(const juce::MouseEvent &e,
                                  const juce::MouseWheelDetails &wheel) {
  bool isCtrl = e.mods.isCtrlDown() || e.mods.isCommandDown();
  bool isShift = e.mods.isShiftDown();

  if (isCtrl) {
    float factor = (wheel.deltaY > 0) ? 1.1f : 0.9f;
    pixelsPerBeat *= factor;
    pixelsPerBeat = juce::jlimit(10.0f, 200.0f, pixelsPerBeat);
    noteHeight *= factor;
    noteHeight = juce::jlimit(4.0f, 64.0f, noteHeight);
    pianoKeysWidth *= factor;
    pianoKeysWidth = juce::jlimit(24.0f, 120.0f, pianoKeysWidth);
    float totalPitchPixels = 128.0f * noteHeight;
    float maxScrollY = juce::jmax(0.0f, totalPitchPixels - (float)getHeight());
    scrollY = juce::jlimit(0.0f, maxScrollY, scrollY);
    scrollBarV.setRangeLimits(0.0, (double)totalPitchPixels);
    scrollBarV.setCurrentRange((double)scrollY, (double)getHeight());
    velocityLane.setPixelsPerBeat(pixelsPerBeat);
    velocityLane.setPianoKeysWidth(pianoKeysWidth);
    double viewWidthBeats = (getWidth() - pianoKeysWidth) / pixelsPerBeat;
    scrollBarH.setCurrentRange(scrollX, viewWidthBeats);
    if (onScrollChanged)
      onScrollChanged(scrollY);
    if (onZoomChanged)
      onZoomChanged((float)juce::jlimit(10, 200, juce::roundToInt(pixelsPerBeat)));
  } else if (isShift) {
    // SCROLL X
    scrollX -= wheel.deltaY * 4.0f;
    if (scrollX < 0)
      scrollX = 0;
  } else {
    // SCROLL Y (vertical) - sync with keyboard
    float totalPitchPixels = 128.0f * noteHeight;
    float maxScrollY = juce::jmax(0.0f, totalPitchPixels - (float)getHeight());
    scrollY -= wheel.deltaY * 50.0f;
    scrollY = juce::jlimit(0.0f, maxScrollY, scrollY);
    scrollBarV.setCurrentRange((double)scrollY, (double)getHeight());
    if (onScrollChanged)
      onScrollChanged(scrollY);
  }
  pushRenderState();
}

bool SpliceEditor::keyPressed(const juce::KeyPress &key) {
  bool ctrl = key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown();

  // Tool shortcuts (no modifier)
  if (!ctrl && !key.getModifiers().isShiftDown()) {
    if (key.getKeyCode() == 'V') { setTool(EditMode::Selecting); return true; }
    if (key.getKeyCode() == 'D') { setTool(EditMode::Drawing); return true; }
    if (key.getKeyCode() == 'P') { setTool(EditMode::Paint); return true; }
    if (key.getKeyCode() == 'E') { setTool(EditMode::Erase); return true; }
    if (key.getKeyCode() == 'S') { setTool(EditMode::Stretching); return true; }
  }

  if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
    deleteSelected();
    return true;
  }
  // Nudge Left/Right
  if (key == juce::KeyPress::leftKey && !ctrl) {
    nudgeSelected(-(snapGrid > 0 ? snapGrid : 0.25));
    return true;
  }
  if (key == juce::KeyPress::rightKey && !ctrl) {
    nudgeSelected(snapGrid > 0 ? snapGrid : 0.25);
    return true;
  }
  // Ctrl+Q quantize
  if (ctrl && key.getKeyCode() == 'Q') {
    quantizeSelected();
    return true;
  }
  if (key.getKeyCode() == 'Q' && !ctrl &&
      !key.getModifiers().isShiftDown()) {
    quantizeSelected();
    return true;
  }
  if (key.getKeyCode() == 'G' && !key.getModifiers().isCommandDown() &&
      !key.getModifiers().isShiftDown() && selectedIndices.size() >= 2) {
    mergeSelectedNotes();
    return true;
  }
  // Transpose: Up/Down arrows
  if (key == juce::KeyPress::upKey && !key.getModifiers().isCommandDown()) {
    int semitones = key.getModifiers().isShiftDown() ? 12 : 1;
    transposeSelected(semitones);
    return true;
  }
  if (key == juce::KeyPress::downKey && !key.getModifiers().isCommandDown()) {
    int semitones = key.getModifiers().isShiftDown() ? -12 : -1;
    transposeSelected(semitones);
    return true;
  }
  // Velocity: +/- keys
  if (key.getKeyCode() == '+' || key.getKeyCode() == '=') {
    nudgeVelocity(10);
    return true;
  }
  if (key.getKeyCode() == '-' || key.getKeyCode() == '_') {
    nudgeVelocity(-10);
    return true;
  }
  // Humanize: H key
  if (key.getKeyCode() == 'H' && !key.getModifiers().isCommandDown()) {
    humanizeVelocity(0.1f);
    return true;
  }
  if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown()) {
    if (key.getKeyCode() == 'A') {
      selectAll();
      return true;
    }
    if (key.getKeyCode() == 'C') {
      copySelected();
      return true;
    }
    if (key.getKeyCode() == 'V') {
      pasteFromClipboard();
      return true;
    }
    if (key.getKeyCode() == 'D') {
      duplicateSelected();
      return true;
    }
    if (key.getKeyCode() == 'Z') {
      if (context && key.getModifiers().isShiftDown()) {
        if (context->undoManager.canRedo()) {
          context->undoManager.redo();
          return true;
        }
      } else if (context && context->undoManager.canUndo()) {
        context->undoManager.undo();
        return true;
      }
    } else if (key.getKeyCode() == 'Y' && context &&
               context->undoManager.canRedo()) {
      context->undoManager.redo();
      return true;
    }
  }
  return false;
}
