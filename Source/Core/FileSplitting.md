# File Splitting Plan (Roadmap 18)

## Recommended Splits

| File | Lines | Suggested split |
|------|-------|-----------------|
| **SpliceEditor.cpp** | ~1500 | `SpliceEditor.cpp` (core + paint), `SpliceEditorMouse.cpp` (mouse/key handlers), `SpliceEditorRendering.cpp` (waterfall, note rects) |
| **MainComponent.cpp** | ~1200+ | `MainComponentCore.cpp` (ctor, layout, audio), `MainComponentGL.cpp` (OpenGL, render mode), `MainComponentEvents.cpp` (key, mouse, file drop) |
| **MixerPanel.h** | ~500 | Keep single header; consider `MixerStrip.cpp` if MixerStrip implementation grows |

## Migration steps
1. Create new .cpp with extracted functions; add `#include "Original.h"`.
2. Move function bodies to new file; keep declarations in original header (or shared internal header).
3. Update CMakeLists.txt / project to add new source file.
4. Build and fix any missing includes or linkage.

## Done
- **SpliceEditor**: `SpliceEditorRendering.cpp` + `SpliceEditorRendering.h` added. `paintWaterfall()` and `paintEditMode()` moved into namespace `SpliceEditorRendering`; `SpliceEditor::paint()` delegates to them. `RenderState` moved to public in `SpliceEditor.h` for use by the rendering module. `SpliceEditorMouse.cpp` added: `mouseMove`, `mouseExit`, `mouseDown`, `mouseDrag`, `mouseUp`, `mouseDoubleClick`, `mouseWheelMove`, `keyPressed` implemented there; declarations remain in `SpliceEditor.h`.
- **MainComponent**: `MainComponentGL.cpp` (OpenGL), `MainComponentEvents.cpp` (key, mouse, file drop), `MainComponentCore.cpp` (resized, applyLayout, prepareToPlay, getNextAudioBlock, releaseResources, isPlaying). MainComponent.cpp keeps ctor, destructor, paint, setupComponentCaching, flushPendingResize, repaintDirtyRegions, setView, toggleMidiLearnOverlay, handleNoteOn/Off, onLogMessage, applyThemeToAllLookAndFeels, changeListenerCallback.

## Notes
- Timer migration: ModuleWindow and TrafficMonitor already use TimerHub (no `juce::Timer` inheritance found). Remaining `Timer::callAfterDelay` usages are one-shot delays, not periodic timers.
