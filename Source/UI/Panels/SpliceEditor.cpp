/*
  ==============================================================================
    Source/UI/Panels/SpliceEditor.cpp
  ==============================================================================
*/
#include "SpliceEditor.h"
#include "SpliceEditorRendering.h"
#include "../NoteEditUndoAction.h"
#include "../NoteGeometry.h"
#include "../PopupMenuOptions.h"

#include <cmath>

SpliceEditor::SpliceEditor() {
  notes.reserve(8192);
  setInterceptsMouseClicks(true, true);
  setWantsKeyboardFocus(true);

  addAndMakeVisible(btnSnap);
  btnSnap.setButtonText("Snap");
  btnSnap.setTooltip("Snap to grid (toggle). Grid size: dropdown to the right. Shift+drag when moving or resizing for finer grid.");
  btnSnap.setClickingTogglesState(true);
  btnSnap.setToggleState(true, juce::dontSendNotification);
  btnSnap.setColour(juce::TextButton::buttonOnColourId,
                    juce::Colours::orange.darker(0.2f));

  addAndMakeVisible(btnFollow);
  btnFollow.setButtonText("Follow");
  btnFollow.setTooltip("Follow playhead: auto-scroll while playing.");
  btnFollow.setClickingTogglesState(true);
  btnFollow.setToggleState(true, juce::dontSendNotification);
  btnFollow.setColour(juce::TextButton::buttonOnColourId,
                     juce::Colours::orange.darker(0.2f));
  btnFollow.onClick = [this] {
    setFollowPlayhead(btnFollow.getToggleState());
  };

  addAndMakeVisible(btnQuantize);
  btnQuantize.setButtonText("Quantize");
  btnQuantize.setTooltip("Quantize selected notes (Q). Right-click for mode: Soft, Hard, Groove; Humanize timing.");
  btnQuantize.onClick = [this] { quantizeSelected(); };

  addAndMakeVisible(btnVelCurve);
  btnVelCurve.setButtonText("Vel Curve");
  btnVelCurve.setTooltip("Apply velocity curve to selected notes");
  btnVelCurve.onClick = [this] {
    juce::PopupMenu m;
    m.addItem(1, "Linear", true, false);
    m.addItem(2, "Soft (gentle)", true, false);
    m.addItem(3, "Hard (punchy)", true, false);
    m.addItem(4, "S-Curve", true, false);
    m.showMenuAsync(PopupMenuOptions::forComponent(this),
        [this](int id) { if (id > 0) applyVelocityCurveToSelected(id); });
  };

  // Tool buttons (V=Select, D=Draw, P=Paint, E=Erase, S=Stretch)
  struct ToolDef { EditMode m; juce::String txt; juce::String tip; };
  for (auto &d : std::vector<ToolDef>{{EditMode::Selecting, "Sel", "Select (V)"},
                                      {EditMode::Drawing, "Draw", "Draw (D)"},
                                      {EditMode::Paint, "Paint", "Paint (P)"},
                                      {EditMode::Erase, "Erase", "Erase (E)"},
                                      {EditMode::Stretching, "Str", "Stretch (S)"}}) {
    auto *b = new juce::TextButton(d.txt);
    b->setClickingTogglesState(true);
    b->setRadioGroupId(8001);
    b->setTooltip(d.tip);
    b->onClick = [this, m = d.m] { setTool(m); };
    addAndMakeVisible(toolButtons.add(b));
  }
  toolButtons[1]->setToggleState(true, juce::dontSendNotification);  // Draw default

  addAndMakeVisible(cmbGrid);
  cmbGrid.addItemList({"1/4", "1/8", "1/16", "1/32", "1/64"}, 1);
  cmbGrid.setSelectedId(4, juce::dontSendNotification);  // Default 1/32 for finer precision
  cmbGrid.setTooltip("Grid size: 1/4, 1/8, 1/16, 1/32, 1/64. Ctrl+wheel = zoom (grid, notes, and keyboard scale together).");
  cmbGrid.onChange = [this] {
    int id = cmbGrid.getSelectedId();
    if (id >= 1 && id <= 5)
      snapGrid = 1.0 / std::pow(2.0, (double)(id + 1));  // 1/4, 1/8, 1/16, 1/32, 1/64
    pushRenderState();
  };

  addAndMakeVisible(scrollBarH);
  addAndMakeVisible(scrollBarV);
  addAndMakeVisible(velocityLane);
  scrollBarH.addListener(this);
  scrollBarV.addListener(this);

  velocityLane.setNotes(&notes);
  velocityLane.onVelocityChanged = [this] {
    updateEngine();
    pushRenderState();
  };

  // Default grid/keyboard position: C3–C4 (MIDI 48–60) visible; C4 at top
  scrollY = (127.0f - 60.0f) * noteHeight;
}

SpliceEditor::~SpliceEditor() = default;

void SpliceEditor::setTool(EditMode t) {
  currentMode = t;
  int idx = (t == EditMode::Selecting) ? 0 : (t == EditMode::Drawing) ? 1
             : (t == EditMode::Paint) ? 2 : (t == EditMode::Erase) ? 3 
             : (t == EditMode::Stretching) ? 4 : 1;
  if (idx >= 0 && idx < toolButtons.size())
    toolButtons[idx]->setToggleState(true, juce::dontSendNotification);
}

void SpliceEditor::setContext(BridgeContext *ctx) { context = ctx; }

void SpliceEditor::beginEdit() {
  if (context)
    context->undoManager.beginNewTransaction();
}

void SpliceEditor::endEdit() {
  pushRenderState();
}

void SpliceEditor::setNoteHeightFromKeyboard(int keyboardHeight) {
  if (keyboardHeight > 0) {
    noteHeight = (float)keyboardHeight / 128.0f;
    noteHeight = juce::jmax(4.0f, noteHeight);
    hasInitializedScroll = false;  // Recalc scroll so piano strip is visible
  }
}

void SpliceEditor::setNotes(const std::vector<EditableNote> &newNotes) {
  const juce::ScopedLock sl(noteLock);
  notes = newNotes;
  pushRenderState();
  if (onNotesChanged)
    onNotesChanged();
}

void SpliceEditor::setFollowPlayhead(bool shouldFollow) {
  followPlayhead = shouldFollow;
}

void SpliceEditor::updateVisuals() {
  if (!context || !context->engine)
    return;

  // When scrubbing, show pending seek so piano roll doesn't freeze on old position
  double pending = context->engine->getPendingSeekTarget();
  double engineBeat = (pending >= 0.0) ? pending : context->engine->getCurrentBeat();
  bool isPlaying = context->engine->getIsPlaying();

  if (std::abs(engineBeat - playheadBeat) > 0.001 || isPlaying) {
    double oldBeat = playheadBeat;
    playheadBeat = engineBeat;

    // Auto-Scroll Logic (Page Flip Style like Ableton)
    float oldScrollX = scrollX;
    if (followPlayhead && isPlaying) {
      double viewWidthBeats = (getWidth() - pianoKeysWidth) / pixelsPerBeat;
      if (playheadBeat > scrollX + viewWidthBeats)
        scrollX = (float)playheadBeat;
      else if (playheadBeat < scrollX)
        scrollX = (float)playheadBeat;
    }

    // Scroll changed: full repaint. Otherwise: dirty-region playhead only
    if (std::abs(scrollX - oldScrollX) > 0.001f) {
      pushRenderState();
    } else {
      updatePlayheadOnly(engineBeat, oldBeat);
    }
  }
}

void SpliceEditor::pushRenderState() {
  RenderState newState;
  {
    const juce::ScopedLock sl(noteLock);
    newState.notes = notes;
    newState.selectedIndices = selectedIndices;
    newState.scrollX = scrollX;
    newState.scrollY = scrollY;
    newState.pixelsPerBeat = pixelsPerBeat;
    newState.noteHeight = noteHeight;
    newState.pianoKeysWidth = pianoKeysWidth;
    newState.playheadBeat = playheadBeat;
    newState.snapGrid = snapGrid;
    newState.showGhost = showGhost;
    newState.ghostNote = ghostNote;
    newState.isSpliceHover = isSpliceHover;
    newState.selectionRect = selectionRect;
    newState.isSelectionRectActive = isSelectionRectActive;
  }
  {
    const juce::ScopedLock sl(renderLock);
    activeRenderState = std::move(newState);
  }

  // Throttled repaint (~60fps cap) to reduce CPU during fast edits/drag
  auto now = static_cast<uint64_t>(juce::Time::getMillisecondCounter());
  if (now - lastRepaintTicks >= static_cast<uint64_t>(MIN_REPAINT_MS)) {
    lastRepaintTicks = now;
    repaint();
  } else if (!repaintScheduled) {
    repaintScheduled = true;
    juce::Component::SafePointer<SpliceEditor> safe(this);
    juce::Timer::callAfterDelay(MIN_REPAINT_MS, [safe]() {
      if (auto* se = safe.getComponent()) {
        se->repaintScheduled = false;
        se->lastRepaintTicks =
            static_cast<uint64_t>(juce::Time::getMillisecondCounter());
        se->repaint();
      }
    });
  }
}

void SpliceEditor::updatePlayheadOnly(double newBeat, double oldBeat) {
  {
    const juce::ScopedLock sl(renderLock);
    activeRenderState.playheadBeat = newBeat;
  }
  float oldX = beatToX(oldBeat);
  float newX = beatToX(newBeat);
  int left = juce::jmax(0, (int)std::min(oldX, newX) - 4);
  int right = juce::jmin(getWidth(), (int)std::max(oldX, newX) + 4);
  int w = right - left;
  if (w > 0)
    repaint(left, 0, w, getHeight());
}

void SpliceEditor::resized() {
  auto r = getLocalBounds();

  auto toolbar = r.removeFromTop(26);
  auto toolArea = toolbar.removeFromLeft(juce::jmin(160, r.getWidth() / 3));
  if (!toolButtons.isEmpty()) {
    int tw = juce::jmax(28, toolArea.getWidth() / toolButtons.size());
    for (auto *b : toolButtons)
      b->setBounds(toolArea.removeFromLeft(tw).reduced(1));
  }
  toolbar.removeFromLeft(6);
  int rightW = juce::jmax(60, (toolbar.getWidth() - 80) / 4);
  btnVelCurve.setBounds(toolbar.removeFromRight(rightW).reduced(2));
  btnQuantize.setBounds(toolbar.removeFromRight(rightW).reduced(2));
  btnFollow.setBounds(toolbar.removeFromRight(juce::jmax(50, rightW - 10)).reduced(2));
  btnSnap.setBounds(toolbar.removeFromRight(juce::jmax(44, rightW - 10)).reduced(2));
  cmbGrid.setBounds(toolbar.reduced(2));

  auto hBar = r.removeFromBottom(14);
  auto velLaneArea = r.removeFromBottom(24);
  auto vBar = r.removeFromRight(14);

  scrollBarH.setBounds(hBar);
  scrollBarV.setBounds(vBar);
  velocityLane.setBounds(velLaneArea);
  velocityLane.setCoordinateHelpers(
      [this](double b) { return beatToX(b); },
      [this](float x) { return xToBeat(x); });
  velocityLane.setScrollX(scrollX);
  velocityLane.setPixelsPerBeat(pixelsPerBeat);
  velocityLane.setPianoKeysWidth(pianoKeysWidth);

  // Update scrollbar limits based on zoom
  double totalBeats = 1000.0;
  double viewWidthBeats = (getWidth() - pianoKeysWidth) / pixelsPerBeat;
  scrollBarH.setRangeLimits(0.0, totalBeats);
  scrollBarH.setCurrentRange(scrollX, viewWidthBeats);

  float totalPitchPixels = 128.0f * noteHeight;
  scrollBarV.setRangeLimits(0.0, (double)totalPitchPixels);

  if (!hasInitializedScroll && getHeight() > 50) {
    float targetY = (127.0f - 60.0f) * noteHeight;
    float centerOffset = (float)getHeight() / 2.0f;
    scrollY = juce::jlimit(0.0f, totalPitchPixels - (float)getHeight(),
                          targetY - centerOffset);
    hasInitializedScroll = true;
  }

  scrollBarV.setCurrentRange((double)scrollY, (double)getHeight());
}

void SpliceEditor::scrollBarMoved(juce::ScrollBar *scrollBar,
                                  double newRangeStart) {
  if (scrollBar == &scrollBarH) {
    scrollX = (float)newRangeStart;
  } else if (scrollBar == &scrollBarV) {
    scrollY = (float)newRangeStart;
    if (onScrollChanged)
      onScrollChanged(scrollY);
  }
  pushRenderState();
}

void SpliceEditor::setScrollY(float y) {
  float totalPitchPixels = 128.0f * noteHeight;
  float maxScrollY = juce::jmax(0.0f, totalPitchPixels - (float)getHeight());
  scrollY = juce::jlimit(0.0f, maxScrollY, y);
  scrollBarV.setRangeLimits(0.0, (double)totalPitchPixels);
  scrollBarV.setCurrentRange((double)scrollY, (double)getHeight());
  if (onScrollChanged)
    onScrollChanged(scrollY);
  pushRenderState();
}

// ==============================================================================
// COORDINATE TRANSFORMS
// ==============================================================================
float SpliceEditor::beatToX(double beat) const {
  return pianoKeysWidth + (float)((beat - scrollX) * pixelsPerBeat);
}

double SpliceEditor::xToBeat(float x) const {
  return scrollX + (double)((x - pianoKeysWidth) / pixelsPerBeat);
}

float SpliceEditor::pitchToY(int note) const {
  float absY = (127 - note) * noteHeight;
  return absY - scrollY;
}

int SpliceEditor::yToPitch(float y) const {
  if (noteHeight <= 0.1f)
    return 60; // Helper Guard
  float absY = y + scrollY;
  int note = 127 - (int)(absY / noteHeight);
  return juce::jlimit(0, 127, note);
}

// Must match SpliceEditorRendering::paintEditMode ruler offset so hit-test matches drawn notes
static constexpr int kGridRulerHeight = 18;

juce::Rectangle<float> SpliceEditor::getNoteRect(const EditableNote &n) const {
  auto r = NoteGeometry::getNoteRect(n, pixelsPerBeat, noteHeight, scrollX, scrollY, pianoKeysWidth);
  return r.withY(r.getY() + kGridRulerHeight + 1)
      .withWidth(r.getWidth() - 1)
      .withHeight(noteHeight - 2);
}

void SpliceEditor::setViewMode(ViewMode mode) {
  if (currentViewMode == mode)
    return;
  currentViewMode = mode;
  if (mode == ViewMode::Play) {
    setInterceptsMouseClicks(false, false);
    velocityLane.setVisible(false);
    followPlayhead = true;
    highlightActiveNotes = true;
  } else {
    setInterceptsMouseClicks(true, true);
    velocityLane.setVisible(true);
    highlightActiveNotes = false;
  }
  repaint();
}

float SpliceEditor::noteToXWaterfall(int noteNumber, float width) const {
  return (noteNumber / 127.0f) * width;
}

void SpliceEditor::paintWaterfallMode(juce::Graphics &g) {
  juce::Rectangle<float> bounds = getLocalBounds().toFloat();
  std::vector<EditableNote> notesCopy;
  {
    const juce::ScopedLock sl(noteLock);
    notesCopy = notes;
  }
  SpliceEditorRendering::paintWaterfall(
      g, notesCopy, playheadBeat, waterfallVisibleBeats, bounds,
      highlightActiveNotes);
}

bool SpliceEditor::isBlackKey(int pitch) const {
  int p = pitch % 12;
  return (p == 1 || p == 3 || p == 6 || p == 8 || p == 10);
}

// Mouse/key handlers implemented in SpliceEditorMouse.cpp

// ==============================================================================
// PAINTING (Uses RenderState snapshot - safe for OpenGL/Message thread)
// ==============================================================================
void SpliceEditor::paint(juce::Graphics &g) {
  if (currentViewMode == ViewMode::Play) {
    paintWaterfallMode(g);
    return;
  }

  RenderState drawState;
  {
    const juce::ScopedLock sl(renderLock);
    drawState = activeRenderState;
  }

  const int w = getWidth();
  const int h = getHeight();
  const bool drawNotesOnCpu = !gpuNotesActive;

  if (gpuNotesActive && w > 0 && h > 0) {
    auto beatToX = [&](double beat) {
      return drawState.pianoKeysWidth +
             (float)((beat - drawState.scrollX) * drawState.pixelsPerBeat);
    };
    auto pitchToY = [&](int note) {
      return (127 - note) * drawState.noteHeight - drawState.scrollY;
    };
    std::vector<NoteInstance> instances;
    instances.reserve(drawState.notes.size());
    for (size_t i = 0; i < drawState.notes.size(); ++i) {
      const auto &n = drawState.notes[i];
      if (n.channel < 0)
        continue;
      float y = pitchToY(n.noteNumber);
      if (y > h || y + drawState.noteHeight < 0)
        continue;
      float x = beatToX(n.startBeat);
      float endX = beatToX(n.getEndBeat());
      if (x > w || endX < drawState.pianoKeysWidth)
        continue;
      float rw = (float)(n.durationBeats * drawState.pixelsPerBeat);
      float nx = x + 1;
      float ny = y + 1;
      float nw = rw - 1;
      float nh = drawState.noteHeight - 2;
      juce::Colour baseC;
      switch (n.channel % 4) {
      case 0: baseC = juce::Colour(0xff00f0ff); break;
      case 1: baseC = juce::Colour(0xffbd00ff); break;
      case 2: baseC = juce::Colour(0xff00ff9d); break;
      case 3: baseC = juce::Colour(0xff00a2ff); break;
      default: baseC = juce::Colours::white; break;
      }
      if (drawState.selectedIndices.count((int)i))
        baseC = baseC.brighter(0.5f);
      NoteInstance inst;
      inst.x = nx / (float)w;
      inst.y = ny / (float)h;
      inst.w = nw / (float)w;
      inst.h = nh / (float)h;
      inst.r = baseC.getFloatRed();
      inst.g = baseC.getFloatGreen();
      inst.b = baseC.getFloatBlue();
      inst.a = baseC.getFloatAlpha();
      instances.push_back(inst);
    }
    const juce::ScopedLock gl(glInstanceLock);
    glNoteInstances = std::move(instances);
  }

  SpliceEditorRendering::paintEditMode(g, drawState, w, h, drawNotesOnCpu);
}

void SpliceEditor::copySelected() {
  const juce::ScopedLock sl(noteLock);
  clipboardNotes.clear();
  double minBeat = 1e30;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      clipboardNotes.push_back(notes[idx]);
      minBeat = juce::jmin(minBeat, notes[idx].startBeat);
    }
  }
  for (auto &n : clipboardNotes)
    n.startBeat -= minBeat;
}

void SpliceEditor::pasteFromClipboard() {
  if (clipboardNotes.empty())
    return;
  const juce::ScopedLock sl(noteLock);
  auto beforeNotes = notes;
  auto beforeSel = selectedIndices;
  double pasteBeat = playheadBeat;
  double grid = snapGrid > 0.0 ? snapGrid : 0.25;
  pasteBeat = std::round(pasteBeat / grid) * grid;

  selectedIndices.clear();
  int base = (int)notes.size();
  for (size_t i = 0; i < clipboardNotes.size(); ++i) {
    EditableNote n = clipboardNotes[i];
    n.startBeat += pasteBeat;
    notes.push_back(n);
    selectedIndices.insert(base + (int)i);
  }
  updateEngine();
  if (context)
    context->undoManager.perform(new NoteEditUndoAction(
        this, context, beforeNotes, beforeSel, notes, selectedIndices, "Paste"));
  pushRenderState();
}

void SpliceEditor::duplicateSelected() {
  if (selectedIndices.empty())
    return;
  const juce::ScopedLock sl(noteLock);
  auto beforeNotes = notes;
  auto beforeSel = selectedIndices;
  double maxEnd = 0.0;
  std::vector<EditableNote> toAdd;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      maxEnd = juce::jmax(maxEnd, notes[idx].getEndBeat());
      toAdd.push_back(notes[idx]);
    }
  }
  double offset = maxEnd > 0.0 ? maxEnd : 0.25;
  selectedIndices.clear();
  int base = (int)notes.size();
  for (size_t i = 0; i < toAdd.size(); ++i) {
    toAdd[i].startBeat += offset;
    notes.push_back(toAdd[i]);
    selectedIndices.insert(base + (int)i);
  }
  updateEngine();
  if (context)
    context->undoManager.perform(new NoteEditUndoAction(
        this, context, beforeNotes, beforeSel, notes, selectedIndices, "Duplicate"));
  pushRenderState();
}

void SpliceEditor::deleteSelected() {
  {
    const juce::ScopedLock sl(noteLock);
    if (selectedIndices.empty())
      return;
    auto beforeNotes = notes;
    auto beforeSel = selectedIndices;
    std::vector<EditableNote> newNotes;
    for (int i = 0; i < (int)notes.size(); ++i) {
      if (selectedIndices.find(i) == selectedIndices.end())
        newNotes.push_back(notes[i]);
    }
    notes = std::move(newNotes);
    selectedIndices.clear();
    updateEngine();
    if (context)
      context->undoManager.perform(new NoteEditUndoAction(
          this, context, beforeNotes, beforeSel, notes, selectedIndices, "Delete Notes"));
  }
  pushRenderState();
}

void SpliceEditor::deselectAll() {
  selectedIndices.clear();
  repaint();
}

void SpliceEditor::selectAll() {
  {
    const juce::ScopedLock sl(noteLock);
    selectedIndices.clear();
    for (int i = 0; i < (int)notes.size(); ++i)
      selectedIndices.insert(i);
  }
  pushRenderState();
}

void SpliceEditor::updateEngine() {
  if (context && context->engine) {
    context->engine->setNewSequence(notes);
  }
  if (onNotesChanged)
    onNotesChanged();
}

void SpliceEditor::applyState(const std::vector<EditableNote> &newNotes,
                              const std::set<int> &newSelected) {
  const juce::ScopedLock sl(noteLock);
  notes = newNotes;
  // Preserve selection, clamping indices to valid range after note changes
  const int n = static_cast<int>(notes.size());
  selectedIndices.clear();
  for (int idx : newSelected)
    if (idx >= 0 && idx < n)
      selectedIndices.insert(idx);
  updateEngine();
  pushRenderState();
}

// ==============================================================================
// QuantizeSettings
// ==============================================================================
double SpliceEditor::QuantizeSettings::quantize(double beat, double grid) const {
  if (mode == Off || grid <= 0.0)
    return beat;
  double gridBeat = std::round(beat / grid) * grid;
  if (mode == Hard)
    return gridBeat;
  double quantized = beat + (gridBeat - beat) * strength;
  if (mode == Groove && grid > 0.0) {
    int stepIndex = (int)(quantized / grid) % 16;
    if (stepIndex >= 0 && stepIndex < 16)
      quantized += grooveTemplate[(size_t)stepIndex];
  }
  return quantized;
}

void SpliceEditor::quantizeSelected() {
  const juce::ScopedLock sl(noteLock);
  if (selectedIndices.empty())
    return;
  auto beforeNotes = notes;
  double grid = snapGrid > 0.0 ? snapGrid : 0.25;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      notes[idx].startBeat =
          quantizeSettings.quantize(notes[idx].startBeat, grid);
    }
  }
  updateEngine();
  if (context)
    context->undoManager.perform(new NoteEditUndoAction(
        this, context, beforeNotes, selectedIndices, notes, selectedIndices, "Quantize"));
  pushRenderState();
}

void SpliceEditor::splitNoteAtPosition(int noteIndex, double splitBeat) {
  const juce::ScopedLock sl(noteLock);
  if (noteIndex < 0 || noteIndex >= (int)notes.size())
    return;
  auto &original = notes[noteIndex];
  if (splitBeat <= original.startBeat || splitBeat >= original.getEndBeat())
    return;
  EditableNote second = original;
  second.startBeat = splitBeat;
  second.durationBeats = original.getEndBeat() - splitBeat;
  original.durationBeats = splitBeat - original.startBeat;
  notes.push_back(second);
  updateEngine();
  pushRenderState();
}

void SpliceEditor::mergeSelectedNotes() {
  const juce::ScopedLock sl(noteLock);
  if (selectedIndices.size() < 2)
    return;
  auto beforeNotes = notes;
  auto beforeSel = selectedIndices;
  double minStart = 1e30, maxEnd = 0.0;
  int targetPitch = -1;
  float avgVel = 0.0f;
  for (int idx : selectedIndices) {
    if (idx < 0 || idx >= (int)notes.size())
      continue;
    auto &n = notes[idx];
    minStart = std::min(minStart, n.startBeat);
    maxEnd = std::max(maxEnd, n.getEndBeat());
    avgVel += n.velocity;
    if (targetPitch == -1)
      targetPitch = n.noteNumber;
  }
  if (targetPitch < 0)
    return;
  avgVel /= selectedIndices.size();

  // Remove selected notes (inline to avoid lock re-entry)
  std::vector<EditableNote> newNotes;
  for (int i = 0; i < (int)notes.size(); ++i) {
    if (selectedIndices.find(i) == selectedIndices.end())
      newNotes.push_back(notes[i]);
  }
  notes = std::move(newNotes);
  selectedIndices.clear();

  EditableNote merged;
  merged.startBeat = minStart;
  merged.durationBeats = maxEnd - minStart;
  merged.noteNumber = targetPitch;
  merged.velocity = avgVel;
  merged.channel = 1;
  notes.push_back(merged);
  updateEngine();
  if (context)
    context->undoManager.perform(new NoteEditUndoAction(
        this, context, beforeNotes, beforeSel, notes, selectedIndices, "Merge Notes"));
  pushRenderState();
}

void SpliceEditor::smartQuantizeSelected() {
  const juce::ScopedLock sl(noteLock);
  if (selectedIndices.empty())
    return;
  auto beforeNotes = notes;
  double grid = snapGrid > 0.0 ? snapGrid : 0.25;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      double original = notes[idx].startBeat;
      double quantized = std::round(original / grid) * grid;
      double deviation = std::abs(original - quantized) / grid;
      // Only quantize if 10-90% off grid (significant timing error)
      if (deviation > 0.1 && deviation < 0.9) {
        notes[idx].startBeat = quantized;
      }
    }
  }
  updateEngine();
  if (context)
    context->undoManager.perform(new NoteEditUndoAction(
        this, context, beforeNotes, selectedIndices, notes, selectedIndices, "Smart Quantize"));
  pushRenderState();
}

void SpliceEditor::transposeSelected(int semitones) {
  const juce::ScopedLock sl(noteLock);
  if (selectedIndices.empty())
    return;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      int newPitch = notes[idx].noteNumber + semitones;
      notes[idx].noteNumber = juce::jlimit(0, 127, newPitch);
    }
  }
  updateEngine();
  pushRenderState();
}

void SpliceEditor::nudgeSelected(double beatOffset) {
  const juce::ScopedLock sl(noteLock);
  if (selectedIndices.empty())
    return;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      notes[idx].startBeat += beatOffset;
      if (notes[idx].startBeat < 0.0)
        notes[idx].startBeat = 0.0;
    }
  }
  updateEngine();
  pushRenderState();
  if (onNotesChanged)
    onNotesChanged();
}

void SpliceEditor::scaleVelocity(float factor) {
  const juce::ScopedLock sl(noteLock);
  if (selectedIndices.empty())
    return;
  auto beforeNotes = notes;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      notes[idx].velocity = juce::jlimit(0.0f, 1.0f, notes[idx].velocity * factor);
    }
  }
  updateEngine();
  if (context)
    context->undoManager.perform(new NoteEditUndoAction(
        this, context, beforeNotes, selectedIndices, notes, selectedIndices, "Scale Velocity"));
  pushRenderState();
}

void SpliceEditor::humanizeVelocity(float amount) {
  const juce::ScopedLock sl(noteLock);
  if (selectedIndices.empty())
    return;
  auto beforeNotes = notes;
  juce::Random rng;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      float variation = (rng.nextFloat() - 0.5f) * 2.0f * amount;
      notes[idx].velocity = juce::jlimit(0.0f, 1.0f, notes[idx].velocity + variation);
    }
  }
  updateEngine();
  if (context)
    context->undoManager.perform(new NoteEditUndoAction(
        this, context, beforeNotes, selectedIndices, notes, selectedIndices, "Humanize Velocity"));
  pushRenderState();
}

void SpliceEditor::humanizeTiming(float amountBeats) {
  const juce::ScopedLock sl(noteLock);
  if (selectedIndices.empty() || amountBeats <= 0.0f)
    return;
  auto beforeNotes = notes;
  juce::Random rng;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      double offset = (rng.nextDouble() - 0.5) * 2.0 * (double)amountBeats;
      notes[idx].startBeat = std::max(0.0, notes[idx].startBeat + offset);
    }
  }
  updateEngine();
  if (context)
    context->undoManager.perform(new NoteEditUndoAction(
        this, context, beforeNotes, selectedIndices, notes, selectedIndices, "Humanize Timing"));
  pushRenderState();
}

void SpliceEditor::setQuantizeMode(QuantizeMode mode) {
  quantizeSettings.mode = static_cast<QuantizeSettings::Mode>(static_cast<int>(mode));
}

SpliceEditor::QuantizeMode SpliceEditor::getQuantizeMode() const {
  return static_cast<QuantizeMode>(static_cast<int>(quantizeSettings.mode));
}

void SpliceEditor::quantizeSelectedWithMode(QuantizeMode mode) {
  setQuantizeMode(mode);
  quantizeSelected();
}

static float applyCurve(float v, int curve) {
  v = juce::jlimit(0.0f, 1.0f, v);
  switch (curve) {
    case 1: return std::sqrt(v);                                    // Soft
    case 2: return v * v;                                           // Hard
    case 3: return 0.5f + 0.5f * (float)std::tanh((double)((v - 0.5f) * 4.0f));  // S-Curve
    default: return v;  // Linear
  }
}

void SpliceEditor::applyVelocityCurveToSelected(int curve) {
  const juce::ScopedLock sl(noteLock);
  if (selectedIndices.empty())
    return;
  auto beforeNotes = notes;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      notes[idx].velocity = applyCurve(notes[idx].velocity, curve);
    }
  }
  updateEngine();
  if (context)
    context->undoManager.perform(new NoteEditUndoAction(
        this, context, beforeNotes, selectedIndices, notes, selectedIndices, "Velocity Curve"));
  pushRenderState();
  if (onNotesChanged)
    onNotesChanged();
}

void SpliceEditor::setVelocityAll(float velocity) {
  const juce::ScopedLock sl(noteLock);
  if (selectedIndices.empty())
    return;
  auto beforeNotes = notes;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      notes[idx].velocity = juce::jlimit(0.0f, 1.0f, velocity);
    }
  }
  updateEngine();
  if (context)
    context->undoManager.perform(new NoteEditUndoAction(
        this, context, beforeNotes, selectedIndices, notes, selectedIndices, "Set Velocity"));
  pushRenderState();
}

void SpliceEditor::nudgeVelocity(int delta) {
  const juce::ScopedLock sl(noteLock);
  if (selectedIndices.empty())
    return;
  auto beforeNotes = notes;
  float change = (float)delta / 127.0f;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      notes[idx].velocity = juce::jlimit(0.0f, 1.0f, notes[idx].velocity + change);
    }
  }
  updateEngine();
  if (context)
    context->undoManager.perform(new NoteEditUndoAction(
        this, context, beforeNotes, selectedIndices, notes, selectedIndices, "Nudge Velocity"));
  pushRenderState();
}

void SpliceEditor::stretchSelected(double factor) {
  const juce::ScopedLock sl(noteLock);
  if (selectedIndices.empty() || factor <= 0.0)
    return;
  auto beforeNotes = notes;
  
  // Find pivot (earliest start beat)
  double minBeat = 1e30;
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      minBeat = std::min(minBeat, notes[idx].startBeat);
    }
  }
  
  // Apply stretch from pivot
  for (int idx : selectedIndices) {
    if (idx >= 0 && idx < (int)notes.size()) {
      double offset = notes[idx].startBeat - minBeat;
      notes[idx].startBeat = minBeat + offset * factor;
      notes[idx].durationBeats *= factor;
    }
  }
  
  updateEngine();
  if (context)
    context->undoManager.perform(new NoteEditUndoAction(
        this, context, beforeNotes, selectedIndices, notes, selectedIndices, "Stretch Notes"));
  pushRenderState();
}

// ==============================================================================
// GPU note rendering (instanced quads)
// ==============================================================================
void SpliceEditor::initGL(juce::OpenGLContext &openGLContext) {
  if (glShader)
    return;
  const char *vshader =
      "attribute vec2 position;\n"
      "attribute vec4 instanceData1;\n"
      "attribute vec4 instanceData2;\n"
      "varying vec4 vColor;\n"
      "void main() {\n"
      "    vColor = instanceData2;\n"
      "    vec2 pos = position * instanceData1.zw + instanceData1.xy;\n"
      "    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
      "}";
  const char *fshader =
      "varying vec4 vColor;\n"
      "void main() {\n"
      "    gl_FragColor = vColor;\n"
      "}";
  glShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
  if (glShader->addVertexShader(vshader) && glShader->addFragmentShader(fshader))
    glShader->link();
  if (glQuadVbo == 0) {
    GLfloat quad[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                      0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f};
    openGLContext.extensions.glGenBuffers(1, &glQuadVbo);
    openGLContext.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, glQuadVbo);
    openGLContext.extensions.glBufferData(
        juce::gl::GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(quad), quad,
        juce::gl::GL_STATIC_DRAW);
    openGLContext.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, 0);
  }
  openGLContext.extensions.glGenBuffers(1, &glInstanceVbo);
}

void SpliceEditor::releaseGL(juce::OpenGLContext &openGLContext) {
  glShader.reset();
  if (glQuadVbo != 0) {
    openGLContext.extensions.glDeleteBuffers(1, &glQuadVbo);
    glQuadVbo = 0;
  }
  if (glInstanceVbo != 0) {
    openGLContext.extensions.glDeleteBuffers(1, &glInstanceVbo);
    glInstanceVbo = 0;
  }
}

bool SpliceEditor::hasGLContent() const {
  return glShader != nullptr && glShader->getProgramID() != 0 && glQuadVbo != 0 &&
         glInstanceVbo != 0;
}

void SpliceEditor::renderGL(juce::OpenGLContext &openGLContext, int /* viewWidth */,
                            int viewHeight, int viewX, int viewY, int viewW,
                            int viewH) {
  using namespace juce::gl;
  if (!glShader || glShader->getProgramID() == 0 || glQuadVbo == 0 || glInstanceVbo == 0)
    return;
  std::vector<NoteInstance> *drawList = nullptr;
  {
    const juce::ScopedLock sl(glInstanceLock);
    glNoteInstancesForRender.swap(glNoteInstances);
    if (glNoteInstancesForRender.empty())
      return;
    drawList = &glNoteInstancesForRender;
  }
  const size_t drawCount = drawList->size();
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  GLint vp[4];
  glGetIntegerv(GL_VIEWPORT, vp);
  glViewport((GLint)viewX, (GLint)(viewHeight - viewY - viewH),
             (GLsizei)viewW, (GLsizei)viewH);
  glShader->use();
  GLuint prog = (GLuint)glShader->getProgramID();
  GLint posLoc =
      (GLint)openGLContext.extensions.glGetAttribLocation(prog, "position");
  GLint i1Loc = (GLint)openGLContext.extensions.glGetAttribLocation(
      prog, "instanceData1");
  GLint i2Loc = (GLint)openGLContext.extensions.glGetAttribLocation(
      prog, "instanceData2");
  openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, glInstanceVbo);
  openGLContext.extensions.glBufferData(
      GL_ARRAY_BUFFER,
      (GLsizeiptr)(drawCount * sizeof(NoteInstance)),
      drawList->data(), GL_STREAM_DRAW);
  openGLContext.extensions.glVertexAttribPointer(
      (GLuint)i1Loc, 4, GL_FLOAT, GL_FALSE, sizeof(NoteInstance), (void *)0);
  openGLContext.extensions.glVertexAttribPointer(
      (GLuint)i2Loc, 4, GL_FLOAT, GL_FALSE, sizeof(NoteInstance),
      (void *)(4 * sizeof(float)));
  openGLContext.extensions.glEnableVertexAttribArray((GLuint)i1Loc);
  openGLContext.extensions.glEnableVertexAttribArray((GLuint)i2Loc);
  juce::gl::glVertexAttribDivisor((GLuint)i1Loc, 1);
  juce::gl::glVertexAttribDivisor((GLuint)i2Loc, 1);
  openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, glQuadVbo);
  openGLContext.extensions.glVertexAttribPointer(
      (GLuint)posLoc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  openGLContext.extensions.glEnableVertexAttribArray((GLuint)posLoc);
  juce::gl::glVertexAttribDivisor((GLuint)posLoc, 0);
  glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)drawCount);
  juce::gl::glVertexAttribDivisor((GLuint)posLoc, 0);
  juce::gl::glVertexAttribDivisor((GLuint)i1Loc, 0);
  juce::gl::glVertexAttribDivisor((GLuint)i2Loc, 0);
  openGLContext.extensions.glDisableVertexAttribArray((GLuint)posLoc);
  openGLContext.extensions.glDisableVertexAttribArray((GLuint)i1Loc);
  openGLContext.extensions.glDisableVertexAttribArray((GLuint)i2Loc);
  openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
  glViewport((GLint)vp[0], (GLint)vp[1], (GLsizei)vp[2], (GLsizei)vp[3]);
  glDisable(GL_BLEND);
}
