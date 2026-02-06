# OpenGL and Window Ghosting – Research Summary

## What is window ghosting?

Ghosting is when previous frames or moved UI elements leave visible “trails” or semi-transparent duplicates: overlapping module outlines, stale “X” buttons, or old content in regions that should have been cleared (e.g. after moving a window).

---

## Common causes (from research)

### 1. **Missing or incomplete buffer clear**

- **Cause:** The color (and optionally depth/stencil) buffer is not cleared at the start of each frame.
- **Effect:** Uninitialized or stale buffer contents show through → artifacts / ghosting.
- **Fix:** Always call `glClear(GL_COLOR_BUFFER_BIT)` at the **start** of every `renderOpenGL()` (and clear depth/stencil if you use them). OpenGL does not clear buffers automatically.

### 2. **Multiple swap-buffer calls per frame**

- **Cause:** Calling the buffer-swap (e.g. present) more than once per frame.
- **Effect:** The second swap can show an unrendered or half-rendered buffer → flicker / ghosting.
- **Fix:** Ensure exactly one swap per frame; swap only after all rendering for that frame is done. (JUCE’s `OpenGLContext::triggerRepaint()` / internal swap should already enforce this.)

### 3. **Rendering order: OpenGL vs CPU paint**

- In JUCE, **`renderOpenGL()` runs before the component `paint()`** for the same frame. So the order is: GL clear + GL draws → then CPU 2D paint for the rest of the tree.
- If you rely on a specific order (e.g. “paint then GL”), you can get wrong stacking and what looks like ghosting or missing updates.
- **Fix:** Rely on “GL first, then paint”; avoid assuming paint runs before GL. Ensure anything that must appear on top is drawn in the paint pass (or in a later GL pass if you add one).

### 4. **Partial repaint leaving regions stale**

- When only **part** of the window is repainted (dirty regions), areas that are **not** in the dirty set keep their previous composited pixels.
- If a **module window is moved**, the **vacated** region must be marked dirty so the parent (and compositor) redraw it; otherwise the old module image can stay visible (ghost).
- **Fix:** When a window moves or resizes, always repaint both old and new bounds (e.g. `parent->repaint(oldBounds.getUnion(newBounds))`) and, for OpenGL, consider marking a “full dashboard” dirty so the next frame does a full redraw (clear + full composite). This is especially important when mixing GL and many movable windows.

### 5. **Cached component images (setBufferedToImage)**

- With `setBufferedToImage(true)`, a component is rendered to an image and that image is reused. If the component moves or is hidden, the **old** position may not be repainted, so the cached image can appear to “stick” (ghost).
- **Fix:** Use `setBufferedToImage(false)` for movable/resizable module windows so they are always repainted. Use caching only for relatively static panels; when layout or visibility changes, ensure the vacated areas are repainted (e.g. by marking Dashboard dirty).

### 6. **Skipping repaints when “nothing is dirty”**

- If, to save CPU, you **skip** calling `triggerRepaint()` when no region is dirty, the same buffer is shown again. That alone does not create new ghosting, but:
  - After a move/resize, if the “vacated” region was never marked dirty, it will keep showing old content until something triggers a full repaint.
- **Fix:** When any window is moved or resized, force a full dashboard repaint (e.g. mark Dashboard dirty) so the next frame does a full GL clear + full paint. Do not rely only on the moved window’s own repaint.

### 7. **Windows DWM / compositor**

- On Windows, with Aero/DWM, the compositor may not have correct OpenGL content in its backing store during resize or when updates are partial, leading to flicker or damaged content.
- **Fix:** Ensure one full redraw (clear + render + single swap) per logical frame; avoid relying on partial updates for the very next frame after a big layout change.

---

## What we already do in this codebase

- **Clear every frame:** `MainComponent::renderOpenGL()` calls `glClear(GL_COLOR_BUFFER_BIT)` at the start (and in exception paths). No depth/stencil buffers are used.
- **Module windows not cached:** `setupComponentCaching()` sets `setBufferedToImage(false)` for all module windows (winEditor, winMixer, winSequencer, etc.) so they are always repainted.
- **Repaint on move:** `ModuleWindow` calls `parent->repaint(oldBounds.getUnion(newBounds))` (and expanded) when dragging, and `onMoveOrResize` (which marks Dashboard dirty) so the main component does a full repaint.
- **Drag vs idle:** In the vblank path, when `mouseActive` (e.g. dragging), we force `RepaintCoordinator::Dashboard` dirty so we don’t skip the next frame and the compositor gets a full redraw.

---

## Recommended mitigations if ghosting persists

1. **Force full repaint after drag ends**  
   In `ModuleWindow::mouseUp()` after a drag, ensure Dashboard is marked dirty (e.g. call `onMoveOrResize()` if not already) and that the next vblank runs a full repaint (no early return). The existing delayed `parent->repaint()` at 16 ms is good; having Dashboard dirty guarantees that when the repaint runs, it’s a full repaint.

2. **Always clear at the very start of renderOpenGL()**  
   Keep the current pattern: no early returns before `glClear`. If you add early exits later, do them only after the clear.

3. **Avoid multiple triggerRepaint() per frame**  
   Ensure the vblank (or main timer) path calls `triggerRepaint()` at most once per frame.

4. **Optional: full repaint on layout/visibility change**  
   When applying a layout (Full/Minimal) or toggling module visibility, mark Dashboard dirty so the next frame is a full clear + full paint, avoiding partial-update artifacts.

5. **If on NVIDIA or specific drivers**  
   Some reports mention driver-specific blinking/ghosting on resize; ensuring a full clear and a single swap per frame, and a full repaint after resize, is the best application-side mitigation.

---

## References (summary)

- Stack Overflow / OpenGL: ghosting from missing clear, multiple swaps, and partial redraws.
- JUCE forum: “Problem clearing video with OpenGL”; “Calling paint() before openGL render”; “Native & OpenGL rendering interaction” (order: GL then paint).
- Best practice: clear color (and depth/stencil if used) at start of frame → draw → single swap per frame; repaint vacated regions when windows move.
