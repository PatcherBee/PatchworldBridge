# JUCE CPU Bottlenecks — What Causes High CPU Spikes

Quick reference for the main bottlenecks that can cause high CPU in Patchworld Bridge. See **CPU_ISSUES_AND_SOLUTIONS.md** and **OPTIMIZATION_OPPORTUNITIES.md** for full detail and fixes.

---

## 1. **VBlank at 60 Hz driving “full” work** (main spike source when “active”)

**What:** The vblank callback runs at display refresh (e.g. 60 Hz). When the app is not “idle”, every frame runs:

- `processUpdates()` → **TimerHub::tick()** (all subscribers)
- **RepaintCoordinator** flush (High60Hz)
- **StatusBar** update, undo/redo refresh, log drain, visual buffer, playhead, LFO phase, mixer visuals, splice editor visuals, transport/BPM sync
- Then `repaint()` or `openGLContext.triggerRepaint()`

**Why it spikes:** One 60 Hz tick does a lot. Any extra work (animations, log, many subscribers) adds up.

**Already mitigated:** When **not** playing and **not** dragging, “reduced mode” runs full `processUpdates()` only every **15th** vblank (~4 Hz); other frames return early. Idle path (no play, no visuals, no drag) skips almost all work after a short threshold.

**Remaining risk:** With an **animated theme** (10–13), `hasVisuals` is true so the app rarely enters idle → 60 Hz full updates continue.

---

## 2. **TimerHub::tick() and High60Hz subscribers**

**What:** Every time `processUpdates()` runs, it calls `TimerHub::tick()`. Subscribers with **High60Hz** (divisor 1) run **every** frame. Current High60Hz subscribers include:

- **repaintCoordinator** — flush, then repaint regions (or full repaint if Dashboard dirty)

**Why it spikes:** Anything at 60 Hz is expensive. More High60Hz subscribers = more work per frame.

**Mitigation:** TimerHub already uses a vector for the hot path (no per-frame copy). Keep 60 Hz only for playhead, repaint flush, and drag; move status/stats to 10–30 Hz where possible.

---

## 3. **processUpdates() doing too much per frame**

**What:** When not minimised and not in reduced mode, each call does:

- Refresh undo/redo buttons and tooltips
- Drain log buffer (up to 50 entries) if log visible
- Drain visual buffer and **trackGrid.repaint()** if editor visible
- Playhead update, LFO phase, sequencer step visualization
- Mixer **updateVisuals()**, splice editor **updateVisuals()**
- Transport/BPM/Link UI sync, status bar **setBpmAndTransport()**

**Why it spikes:** A single frame pays for all of the above. Log drain + trackGrid repaint + multiple `updateVisuals()` are heavy when visible.

**Mitigation:** Reduced mode already throttles this to ~4 Hz when not playing and not dragging. When playing, this is required for real-time feel.

---

## 4. **AudioEngine HighResolutionTimer(1)** (~1000 Hz when playing)

**What:** `AudioEngine` uses `juce::HighResolutionTimer::startTimer(1)` (1 ms) when transport is playing, for Link/transport sync.

**Why it spikes:** 1 ms ≈ 1000 callbacks per second on the **message thread**, so any work in that callback can cause CPU spikes.

**Mitigation:** Keep 1 ms only when Link is enabled or start/stop is pending. Document so future changes don’t increase timer rate. See CPU_ISSUES_AND_SOLUTIONS.md §4.1.

---

## 5. **Repaints and overdraw**

**What:** Full-window repaint (`repaint()` on MainComponent) is triggered when Dashboard is dirty. trackGrid and other panels repaint when visible and when their dirty bits are set.

**Why it spikes:** Full repaint + many components = more GPU/CPU. trackGrid repaint every frame while playing is required but costly if the grid is large.

**Mitigation:** `repaintDirtyRegions()` only does full `repaint()` when Dashboard (or multiple regions) is dirty; otherwise repaints specific components. Further gains: dirty-rect for trackGrid, cache static content (see CPU_ISSUES §3.3, OPTIMIZATION_OPPORTUNITIES §5).

---

## 6. **Animated theme preventing idle**

**What:** For themes 10–13, `dynamicBg.hasActiveParticles()` is true → `hasVisuals` is true → the vblank path **never** treats the app as idle.

**Why it spikes:** Idle early-return is skipped, so 60 Hz full updates (processUpdates + TimerHub + repaint) keep running even when the user isn’t playing or dragging.

**Mitigation:** Only call `dynamicBg.updateAnimation()` when `hasVisuals` (already done). Consider treating “idle” more aggressively for animated themes (e.g. 10 Hz UI when not playing and no recent interaction). See CPU_ISSUES §1.1.

---

## 7. **Log and visual buffer processing**

**What:** When the OSC Log window is visible, each processUpdates() drains up to 50 log entries and updates the log text. When the editor is visible, the visual buffer is drained and trackGrid is repainted.

**Why it spikes:** String building and layout in the log can be heavy. trackGrid repaint is a full panel paint.

**Mitigation:** Log drain is capped at 50 per frame. Reduce log string allocations (e.g. preallocate, incremental update) per OPTIMIZATION_OPPORTUNITIES §2.

---

## 8. **ConfigPanel / heavy panels on resize**

**What:** Large panels (e.g. ConfigPanel) do full layout in `resized()` on every resize event.

**Why it spikes:** Resize drag can fire many resize events per second → repeated full layout and repaint.

**Mitigation:** Skip layout when bounds unchanged (e.g. compare to last bounds). See CPU_ISSUES §7.1, OPTIMIZATION_OPPORTUNITIES §7.

---

## Summary table

| Bottleneck                    | When it spikes              | Mitigation status        |
|------------------------------|-----------------------------|--------------------------|
| VBlank 60 Hz full updates    | Not idle (e.g. animated UI) | Reduced + idle path      |
| TimerHub + processUpdates    | Every non-idle frame        | Throttled in reduced mode|
| HighResolutionTimer(1)       | When transport playing      | Keep only when needed    |
| Full repaint / overdraw      | Dashboard dirty, big grid   | Dirty regions, local repaint |
| Animated theme = never idle  | Themes 10–13                | updateAnimation gated    |
| Log / visual buffer          | Log or editor visible       | Cap + allocation fixes   |
| Heavy panel resize           | Resizing large panels       | Skip unchanged layout    |

For concrete code changes and priorities, use **CPU_ISSUES_AND_SOLUTIONS.md** and **OPTIMIZATION_OPPORTUNITIES.md**.
