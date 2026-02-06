# CPU Usage: Issues and Solutions

**Goal:** Pro-grade, ultra-lightweight, real-time bridge app with modern reactive GUI, without sacrificing stability or features.

---

## 1. VBlank / Idle Path

### Issue 1.1 — Software render path still runs `processUpdates()` when “active” but not playing
When using software rendering (no OpenGL), the vblank callback only skips work when `!isPlaying && !hasVisuals && !mouseActive`. If the user has an **animated theme** (10–13), `hasVisuals` is always true, so we never enter the idle path and run `processUpdates()` + TimerHub + repaint at 60 Hz even when nothing is playing.

**Solution:** Treat “no real activity” more aggressively: e.g. consider `hasVisuals` only when the Control/Dashboard area is actually visible and the user hasn’t interacted recently, or add a “reduced UI updates” mode when transport is stopped and no panels need live data (e.g. run TimerHub at 10 Hz instead of 60 Hz when not playing and no drag).

### Issue 1.2 — `dynamicBg.updateAnimation()` called every 2 frames when theme is static
In `MainComponent.cpp` (software path), when `!hasVisuals` we still call `dynamicBg.updateAnimation(0.033f)` every 2 frames. That call returns immediately for static themes but still does a few checks and can trigger `ThemeManager::updateAnimation()`; it also encourages unnecessary repaint scheduling.

**Solution:** When `!hasVisuals` (static theme), do **not** call `dynamicBg.updateAnimation()` at all in the software vblank path. Only call it when `hasVisuals` is true (animated theme). Same idea already applied in the GL path; align the software path.

### Issue 1.3 — GL path idle return (already fixed)
When using OpenGL, the vblank path now returns early when idle (after threshold) and does **not** call `processUpdates()` or `triggerRepaint()`. Ensure this behaviour is present in your worktree and that no other code path re-triggers full-rate updates when truly idle.

**Implemented:** All of list 1 is in place: (1.1) Reduced mode runs full `processUpdates()` only every 6th vblank when `!isPlaying && !mouseActive`, with `repaintCoordinator.flush()` every frame so dirty regions still repaint; (1.2) software path only calls `dynamicBg.updateAnimation()` when `hasVisuals`; (1.3) GL path has idle early-return and only calls `dynamicBg.updateAnimation()` when `hasVisuals` (no every-2-frames call when static theme).

---

## 2. TimerHub and processUpdates()

### Issue 2.1 — `TimerHub::tick()` copies subscriber list every frame
`tick()` does `std::vector<Subscriber> snapshot = tickOrder_;` every call. When not idle, this runs at 60 Hz and allocates/copies once per frame.

**Solution:** Iterate `tickOrder_` in place. Document that callbacks must not call `subscribe`/`unsubscribe` from within their own callback (or use a “dirty” flag and rebuild next tick). Eliminates per-frame copy and reduces allocations.

### Issue 2.2 — Too many subscribers at 60 Hz
`repaintCoordinator` and possibly others use `High60Hz` (divisor 1), so they run every vblank. When not idle, that means a lot of work every frame (flush, repaint regions, StatusBar logic, etc.).

**Solution:**  
- Run **repaintCoordinator** at 60 Hz only when something is actually dirty or when playing; when idle (no dirty, not playing), the vblank path already returns early so this is conditional on “not idle.”  
- Consider **StatusBar** at 10 Hz instead of 60 Hz unless transport is playing or BPM/scale is changing; reduce to 10 Hz when transport stopped.  
- Audit all `High60Hz` subscribers: keep 60 Hz only for playhead, meters, and drag feedback; move “ambient” UI (status text, scale, stats) to 10–30 Hz.

### Issue 2.3 — `processUpdates()` does too much every frame when not minimised
When not idle and not minimised, every vblank runs: TimerHub::tick(), refreshUndoRedoButtons(), log drain (if log visible), visual buffer + trackGrid.repaint() (if editor visible), playhead update (if playing), and more. That’s a lot for “app open, transport stopped.”

**Solution:**  
- **When not playing:** run a “light” tick every vblank (e.g. only repaintCoordinator flush if dirty, plus one 10 Hz “slow” TimerHub tick elsewhere), and run the full `processUpdates()` (undo/redo, log drain, visual buffer, playhead) at 10 Hz or 15 Hz.  
- **When playing:** keep current behaviour for playhead and visual feedback so real-time feel is preserved.  
- Gate log drain and visual buffer processing on “panel visible” and cap work per frame (you already cap log entries; ensure visual buffer doesn’t explode).

---

## 3. Repaints and Overdraw

### Issue 3.1 — Full-window repaint when only one region is dirty
When `RepaintCoordinator` marks only a small region (e.g. Playhead, Mixer strip), the handler may still trigger a full main-component repaint or large invalidations.

**Solution:** (Already partially done per OPTIMIZATION_OPPORTUNITIES.md.) Ensure `repaintDirtyRegions()` only repaints the components that are actually dirty (per dirty bit). Avoid full `ui->repaint()` unless Dashboard or multiple regions are dirty. Reduces overdraw and GPU/CPU.

### Issue 3.2 — DynamicBackground calls `repaint()` inside `updateAnimation()`
For animated themes, `DynamicBackground::updateAnimation()` calls `repaint()` every time it runs. That’s correct for animation, but it should be the only driver when theme is animated; ensure no other path forces extra repaints of the same area.

**Solution:** Keep single call site for `updateAnimation()` when `hasVisuals` is true; ensure CRT/OpenGL and DynamicBackground don’t both invalidate the same region redundantly.

### Issue 3.3 — trackGrid.repaint() every frame when editor visible and playing
In `processUpdates()`, when performance panel and editor are visible, `perfPanel->trackGrid.repaint()` is called every frame. That’s required for real-time visual feedback but can be heavy if the grid is large.

**Solution:** Keep per-frame repaint when playing for real-time feel. Optimise **trackGrid.paint()** instead: reduce allocations, use cached paths or bitmaps where possible, and only redraw the playhead/notes that changed. Consider a dirty-rect for the grid so only the moving playhead strip is repainted if the rest is static.

---

## 4. Audio and Realtime Threads

### Issue 4.1 — HighResolutionTimer(1) when transport is playing
`AudioEngine` uses `juce::HighResolutionTimer::startTimer(1)` (1 ms ≈ 1000 Hz) when playing, for Link/transport sync. That’s appropriate for beat-accurate start/stop but adds a high-frequency callback on the message thread.

**Solution:** Keep 1 ms when Link is enabled or when pending start/stop. If Link is disabled and there’s no pending start/stop, consider a slower timer (e.g. 10–20 ms) for simple playback tick, or drive transport purely from the audio callback and remove the high-res timer when not needed. Document so future changes don’t accidentally increase timer rate.

### Issue 4.2 — Audio callback and message thread contention
Any lock or shared state between the audio thread and the message thread (e.g. MIDI panic, airlock, engine state) can cause priority inversion and dropouts.

**Solution:** (Already partially addressed per OPTIMIZATION_OPPORTUNITIES.md.) Ensure audio thread never blocks on a lock held by the message thread. Use lock-free queues (airlocks) for MIDI/OSC; keep UI updates on the message thread and audio thread only reads/writes lock-free or wait-free structures.

---

## 5. OSC / MIDI / Network

### Issue 5.1 — OSC receiver thread
JUCE’s OSC receiver typically uses a background thread for socket reads. If that thread wakes in a tight loop (e.g. non-blocking recv with short sleep), it can add CPU.

**Solution:** Rely on blocking recv with a sensible timeout so the thread sleeps when no traffic. If you use a custom loop, avoid busy-wait; use OS blocking or a condition variable.

### Issue 5.2 — MIDI device polling
If MIDI device list or state is polled at high rate (e.g. 60 Hz), that can add unnecessary work.

**Solution:** Use TimerHub at low rate (e.g. 0.5 Hz or 1 Hz) for device reconcile; only re-scan when the user opens the MIDI menu or when a device hot-plug callback fires. Don’t poll at 60 Hz.

---

## 6. GUI “Feel” Without Burning CPU

### Issue 6.1 — Hover/glow at 60 Hz
Components like `HoverGlowButton` and sliders may subscribe to TimerHub at `High60Hz` for smooth hover/glow. That runs every frame when not idle.

**Solution:**  
- Use 30 Hz for hover/glow (still feels smooth).  
- Only run hover/glow tick when the component is actually visible and under the mouse (or when any control in the window was recently hovered). If no hover for N seconds, unsubscribe or skip until next mouse move.  
- Prefer CSS-like “transition” semantics: schedule one repaint after a state change instead of driving from a 60 Hz timer when the value isn’t changing.

### Issue 6.2 — Animations (fade in/out, theme)
Fade and theme animations are important for “physical, reactive” feel. They should not run at 60 Hz when no animation is active.

**Solution:**  
- Use a single “animation driver” (e.g. one TimerHub subscriber at 30 Hz) that only runs while any animation is in progress (e.g. fade or theme transition). When no animation is active, that subscriber does nothing or is unsubscribed.  
- Theme accent/breathing only for animated themes (10–13) and only when that window is visible; already largely gated by `hasActiveParticles()` / `isAnimatedTheme()`.

---

## 7. Heavy Panels and Layout

### Issue 7.1 — ConfigPanel / large panels on resize
Large panels that do full layout in `resized()` on every resize event can spike CPU during drag-resize.

**Solution:** (Already done for ConfigPanel per OPTIMIZATION_OPPORTUNITIES.md — compare to `lastLayoutBounds_` and skip when unchanged.) Apply the same pattern to other heavy panels: only recompute layout when bounds actually change; optionally throttle resize (e.g. do layout at 15 Hz during drag).

### Issue 7.2 — SpliceEditor / piano roll repaint
The piano roll and splice editor are complex and repaint often when playing or editing.

**Solution:**  
- Use dirty regions: only repaint the area that changed (e.g. playhead column, or the lane that was edited).  
- Cache static content (grid, note backgrounds) to a cached image and only redraw the changing overlay.  
- When not playing and not editing, avoid repainting at 60 Hz; repaint only on user input or when data changes.

---

## 8. Checklist Summary (Priority Order)

| Priority | Issue | Solution | Preserves goal |
|----------|--------|----------|----------------|
| P0 | Idle path not entered with animated theme | Add “reduced update” mode when not playing (e.g. 10 Hz TimerHub when no activity) | Realtime when playing; low CPU when not |
| P0 | Software path calls `updateAnimation()` when !hasVisuals | Don’t call `dynamicBg.updateAnimation()` when `!hasVisuals` | Same look; fewer wasted calls |
| P1 | TimerHub snapshot copy every frame | Iterate `tickOrder_` in place; no subscribe/unsubscribe from callbacks | Same behaviour; less alloc/copy |
| P1 | Too much work in processUpdates() when not playing | Split “full update” vs “light tick”; run full at 10–15 Hz when not playing | Realtime when playing; lighter when not |
| P1 | RepaintCoordinator / StatusBar at 60 Hz always | StatusBar and non-critical UI at 10 Hz when not playing | Snappy when playing; lower CPU idle |
| P2 | Hover/glow at 60 Hz | 30 Hz and/or only when component visible + recently hovered | Still feels reactive |
| P2 | trackGrid full repaint every frame | Dirty rect + cache static content | Real-time visual preserved |
| P2 | HighResolutionTimer(1) when playing | Keep for Link/pending; consider slower when not needed | Beat-accurate sync kept |
| P3 | SpliceEditor / ConfigPanel layout | Already optimised; extend to other panels | Ease of use and stability |

---

## 9. Implementation Order (Concrete First Steps)

1. **Software vblank:** In `MainComponent.cpp` vblank callback, when `!hasVisuals`, remove the `dynamicBg.updateAnimation(0.033f)` call (and the `animFrame` branch that triggers it). Only call `dynamicBg.updateAnimation(dt)` when `hasVisuals` is true.
2. **TimerHub::tick():** In `TimerHub.h`, replace the snapshot copy with direct iteration over `tickOrder_` and add a one-line comment: “Callbacks must not subscribe/unsubscribe.”
3. **RepaintCoordinator / “light tick”:** Introduce a “reduced update” mode: when `!context->engine->getIsPlaying()` and no drag, run TimerHub with a “slow” tick (e.g. only every 6th frame) for everything except a minimal flush when dirty. Optionally, run the full `processUpdates()` body at 10 Hz when not playing and run only `repaintCoordinator.flush()` at 60 Hz when dirty.
4. **StatusBar:** Change StatusBar’s TimerHub subscription from `High60Hz` to `Rate10Hz` when transport is stopped (requires passing “isPlaying” or a flag into the StatusBar tick, or a separate “slow” StatusBar subscriber that runs at 10 Hz).

These four steps give the largest CPU reduction while keeping real-time behaviour when playing and preserving the modern, reactive GUI when the user is interacting.
