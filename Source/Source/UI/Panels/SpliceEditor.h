/*
  ==============================================================================
    Source/UI/Panels/SpliceEditor.h
    Role: Horizontal Piano Roll Editor (FL Studio Style)
    Features: Draw, Move, Resize, Delete, Snap-to-Grid
  ==============================================================================
*/
#pragma once
#include "../../Audio/AudioEngine.h"
#include "../../Audio/EditableNote.h"
#include "../../Core/BridgeContext.h"
#include "../../Core/RepaintCoordinator.h"
#include "../Theme.h"
#include "../Widgets/VelocityLane.h"

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>
#include <set>
#include <vector>

class SpliceEditor : public juce::Component,
                     public juce::SettableTooltipClient,
                     public juce::ScrollBar::Listener {
public:
  SpliceEditor();
  ~SpliceEditor() override;

  void setContext(BridgeContext *ctx);
  void setNotes(const std::vector<EditableNote> &newNotes);

  void paint(juce::Graphics &g) override;
  void resized() override;

  // --- MOUSE HANDLERS ---
  void mouseDown(const juce::MouseEvent &e) override;
  void mouseDrag(const juce::MouseEvent &e) override;
  void mouseUp(const juce::MouseEvent &e) override;
  void mouseMove(const juce::MouseEvent &e) override;
  void mouseExit(const juce::MouseEvent &e) override;
  void mouseDoubleClick(const juce::MouseEvent &e) override;
  
  // Note preview settings
  void setNotePreviewEnabled(bool enabled) { notePreviewEnabled = enabled; }
  bool getNotePreviewEnabled() const { return notePreviewEnabled; }
  void mouseWheelMove(const juce::MouseEvent &e,
                      const juce::MouseWheelDetails &wheel) override;
  bool keyPressed(const juce::KeyPress &key) override;

  // Called from MainComponent master timer (no per-component timers)
  void updateVisuals();
  void setFollowPlayhead(bool shouldFollow);
  void setPlayheadBeat(double beat) {
    double oldBeat = playheadBeat;
    playheadBeat = beat;
    {
      const juce::ScopedLock sl(renderLock);
      activeRenderState.playheadBeat = beat;
    }
    // Immediate playhead repaint so timeline drag doesn't appear to freeze piano roll (TimerHub flush can be delayed during drag)
    updatePlayheadOnly(beat, oldBeat);
    if (context)
      context->repaintCoordinator.markDirty(RepaintCoordinator::Playhead);
  }
  /** Call from parent to sync noteHeight with keyboard (128 keys). */
  void setNoteHeightFromKeyboard(int keyboardHeight);
  /** Notify parent when vertical scroll changes (for keyboard sync). */
  std::function<void(float scrollY)> onScrollChanged;
  /** Notify when notes change (for PlayView sync). */
  std::function<void()> onNotesChanged;
  /** Notify when Ctrl+wheel zoom changes (for zoom feedback overlay). Pass display percent (e.g. 10–200). */
  std::function<void(float percent)> onZoomChanged;
  float getNoteHeight() const { return noteHeight; }
  float getScrollY() const { return scrollY; }
  /** Set vertical scroll (e.g. for Oct +/-); clamps and notifies onScrollChanged. */
  void setScrollY(float y);
  const std::vector<EditableNote> &getNotes() const { return notes; }
  VelocityLane &getVelocityLane() { return velocityLane; }
  const VelocityLane &getVelocityLane() const { return velocityLane; }
  /** Apply full state (for undo/redo). */
  void applyState(const std::vector<EditableNote> &newNotes,
                  const std::set<int> &newSelected);
  void scrollBarMoved(juce::ScrollBar *scrollBar,
                      double newRangeStart) override;

  // --- STATE ---
  std::vector<EditableNote> notes;
  int activeNoteIndex = -1;

  enum class ViewMode { Edit, Play };
  ViewMode currentViewMode = ViewMode::Edit;
  void setViewMode(ViewMode mode);
  ViewMode getViewMode() const { return currentViewMode; }

  enum class EditMode { 
    None,           // No active operation
    Drawing,        // Creating new notes
    Paint,          // FL Studio-style paint (drag to add notes)
    Erase,          // Erase notes under cursor
    Moving,         // Moving selected notes
    ResizingEnd,    // Dragging note end
    ResizingStart,  // Dragging note start
    Selecting,      // Marquee selection
    Stretching      // Time-stretch selected notes
  };
  EditMode currentMode = EditMode::None;

  void setTool(EditMode t);

  std::set<int> selectedIndices;
  void selectAll();
  void deselectAll();
  void deleteSelected();
  enum class QuantizeMode { Off, Hard, Soft, Groove };
  void quantizeSelected();
  void quantizeSelectedWithMode(QuantizeMode mode);
  void smartQuantizeSelected(); // Only quantize notes significantly off-grid
  void setQuantizeMode(QuantizeMode mode);
  QuantizeMode getQuantizeMode() const;
  void splitNoteAtPosition(int noteIndex, double splitBeat);
  void mergeSelectedNotes();
  void copySelected();
  void pasteFromClipboard();
  void duplicateSelected();

  // Transpose and velocity editing (roadmap 7.3)
  void transposeSelected(int semitones);
  void scaleVelocity(float factor);
  void humanizeVelocity(float amount); // Random variation
  void humanizeTiming(float amountBeats); // Random start-beat offset (e.g. 0.02)
  void applyVelocityCurveToSelected(int curve); // 0=Linear 1=Soft 2=Hard 3=SCurve
  void setVelocityAll(float velocity);
  void nudgeVelocity(int delta);

  /** Nudge selected notes in time (arrow keys). */
  void nudgeSelected(double beatOffset);
  void stretchSelected(double factor); // Time-stretch selected notes

  /** GPU note rendering: call from MainComponent OpenGL lifecycle. */
  void setGpuNotesActive(bool on) { gpuNotesActive = on; }
  void initGL(juce::OpenGLContext &openGLContext);
  void releaseGL(juce::OpenGLContext &openGLContext);
  bool hasGLContent() const;
  void renderGL(juce::OpenGLContext &openGLContext, int viewWidth, int viewHeight,
                int viewX, int viewY, int viewW, int viewH);

  /** Begin an undoable edit transaction (e.g. for multi-step ops). Use with ScopedNoteEdit for RAII. */
  void beginEdit();
  /** End current edit transaction (optional refresh). */
  void endEdit();

  /** Copy current note/scroll state into activeRenderState (and optionally trigger repaint). Call when becoming visible so GL has data. */
  void pushRenderState();

  /** Snapshot state for rendering (SpliceEditorRendering reads this). */
  struct RenderState {
    std::vector<EditableNote> notes;
    std::set<int> selectedIndices;
    float scrollX = 0.0f, scrollY = 0.0f;
    float pixelsPerBeat = 60.0f, noteHeight = 16.0f, pianoKeysWidth = 48.0f;
    double playheadBeat = 0.0, snapGrid = 0.25;
    bool showGhost = false;
    EditableNote ghostNote;
    bool isSpliceHover = false;
    juce::Rectangle<int> selectionRect;
    bool isSelectionRectActive = false;
  };

private:
  BridgeContext *context = nullptr;
  juce::CriticalSection noteLock;
  RenderState activeRenderState;
  juce::CriticalSection renderLock;
  /** Minimal repaint when only playhead moved (no scroll/notes change). */
  void updatePlayheadOnly(double newBeat, double oldBeat);

  // Repaint throttling (~60fps cap during fast edits)
  uint64_t lastRepaintTicks = 0;
  bool repaintScheduled = false;
  static constexpr int MIN_REPAINT_MS = 16;

  // --- COORDINATE HELPERS ---
  float beatToX(double beat) const;
  double xToBeat(float x) const;
  float pitchToY(int note) const;
  int yToPitch(float y) const;

  juce::Rectangle<float> getNoteRect(const EditableNote &n) const;
  bool isBlackKey(int pitch) const;
  void updateEngine();
  void paintWaterfallMode(juce::Graphics &g);
  float noteToXWaterfall(int noteNumber, float width) const;

  // View Settings
  float pixelsPerBeat = 60.0f; // Zoomed in a bit
  float scrollX = 0.0f;
  float scrollY = 0.0f;
  float pianoKeysWidth = 48.0f; // Key strip width (aligns with MidiKeyboardComponent)
  float noteHeight = 16.0f;     // Must match keyboard key height for alignment

  juce::ScrollBar scrollBarH{false};
  juce::ScrollBar scrollBarV{true};
  VelocityLane velocityLane;
  juce::TextButton btnSnap{"Snap"}, btnFollow{"Follow"}, btnQuantize{"Quantize"},
      btnVelCurve{"Vel Curve"};
  juce::OwnedArray<juce::TextButton> toolButtons;
  juce::ComboBox cmbGrid;
  double snapGrid = 0.03125;  // 1/32 default (finer, less blocky)

  // Advanced quantization
  struct QuantizeSettings {
    enum Mode { Off, Hard, Soft, Groove };
    Mode mode = Soft;
    float strength = 0.75f;
    std::array<float, 16> grooveTemplate = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.02f, 0.0f, 0.03f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.01f, 0.0f, 0.02f, 0.0f};
    double quantize(double beat, double grid) const;
  } quantizeSettings;

  // Using EditMode enum (defined in public section above)
  juce::Point<int> lastMousePos;
  juce::Rectangle<int> selectionRect;
  bool isSelectionRectActive = false;
  std::set<int64_t> paintedThisDrag;
  float lastPaintVelocity = 0.8f;
  bool showGhost = false;
  EditableNote ghostNote;
  bool isSpliceHover = false;
  float lastNoteLength = 1.0f;
  double playheadBeat = 0.0;
  bool followPlayhead = true;
  float waterfallVisibleBeats = 4.0f;
  bool highlightActiveNotes = false;
  int hoveredNoteIndex = -1;
  // Note: isSplicing and isDragging removed - use currentMode enum instead
  bool hasInitializedScroll = false;
  std::vector<EditableNote> clipboardNotes;
  
  // Note preview on hover
  bool notePreviewEnabled = true;
  int lastPreviewedPitch = -1;
  uint64_t lastPreviewTime = 0;
  static constexpr int NOTE_PREVIEW_DEBOUNCE_MS = 150;
  
  // Stretch tool state
  double stretchAnchorBeat = 0.0;
  double stretchInitialSpan = 1.0;
  std::vector<std::pair<int, double>> stretchInitialOffsets; // (index, offset from anchor)

  // --- GPU note rendering (instanced quads) ---
  struct NoteInstance {
    float x, y, w, h;
    float r, g, b, a;
  };
  std::vector<NoteInstance> glNoteInstances;           // UI thread writes
  std::vector<NoteInstance> glNoteInstancesForRender;  // GL thread reads after swap (no lock during draw)
  juce::CriticalSection glInstanceLock;               // Held only for swap
  std::unique_ptr<juce::OpenGLShaderProgram> glShader;
  juce::uint32 glQuadVbo = 0;
  juce::uint32 glInstanceVbo = 0;
  bool gpuNotesActive = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpliceEditor)
};
