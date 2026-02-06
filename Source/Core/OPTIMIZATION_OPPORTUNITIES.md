# Optimization Opportunities

Refactors that can improve performance and efficiency without changing behavior. Ordered by impact vs effort.

---

## 1. Audio thread: avoid duplicate engine reads (Low effort, small win)

**Where:** `MainComponent::getNextAudioBlock` (MainComponentCore.cpp)

**Current:** `getCurrentBeat()` and `getBpm()` are each called twice per block (once for metronome, once for countInManager).

**Change:** Read once per block and reuse:

```cpp
double beat = 0.0, bpm = 120.0;
if (context->engine) {
  beat = context->engine->getCurrentBeat();
  bpm = context->engine->getBpm();
}
// use beat, bpm for metronome and countInManager
```

**Benefit:** Fewer atomic/cache reads in the hottest path.

---

## 2. TrafficMonitor: reduce log string allocations (Medium effort, medium win)

**Where:** `TrafficMonitor::flushLogToDisplay` (TrafficMonitor.h)

**Current:** Builds the full log with a loop: `fullText += line + "\n"`, which reallocates and copies repeatedly (O(n²) in total length).

**Options:**
- Use `juce::StringArray::joinIntoString("\n")` and then `joinIntoString("\n") + "\n"` (single pre-size and copy).
- Or reserve: `fullText.preallocateBytes(estimatedSize)` then append (if JUCE supports it).
- Or keep a single `juce::String` and only append new lines since last flush (incremental update) so most flushes do one append instead of rebuilding all.

**Benefit:** Less allocation and copy on every 10 Hz log update when many messages are present.

---

## 3. TimerHub: faster tick iteration (Low effort, small win)

**Where:** `TimerHub::tick()` (TimerHub.h)

**Current:** Uses `std::map<std::string, Subscriber>`. Every VBlank tick iterates the map and runs callbacks whose `frameCount % divisor == 0`.

**Change:** Keep a separate `std::vector<std::pair<std::string, Subscriber>>` (or vector of Subscriber only) for iteration, updated when subscribe/unsubscribe is called. Iterate the vector in `tick()` so the hot path is cache-friendly and has no map indirection.

**Benefit:** Slightly lower CPU per frame when many subscribers exist; more predictable latency.

---

## 4. MidiRouter::sendPanic: avoid blocking audio (Medium effort, high value for glitch-free UX)

**Where:** `MidiRouter::sendPanic()` (MidiRouter.cpp)

**Current:** Takes `callbackLock` (CriticalSection) and holds it while sending many MIDI messages, clearing scheduler/tracker, and calling engine->stop(). If the audio thread is in `processAudioThreadEvents` or any path that later takes the same lock, the message thread can block the audio thread (or vice versa) and cause a short freeze or dropout.

**Change:**
- Ensure the audio thread never takes `callbackLock` in the process path (it currently doesn’t; lock is only in sendPanic). So the risk is message thread holding the lock while audio thread tries to do something that needs it. Audit: no audio-thread code path should take `callbackLock`.
- Alternatively, make panic “defer to message thread then one-shot”: post a command (e.g. to a lock-free queue or MessageManager) that runs sendPanic on the message thread, and have the audio thread only clear lock-free state (e.g. airlocks, held notes) so it never blocks on a mutex.

**Benefit:** No risk of priority inversion or audio dropouts when user hits Panic.

---

## 5. RepaintCoordinator: avoid full repaint when only one region dirty (Low effort, small–medium win)

**Where:** `MainComponent` repaint handler (MainComponent.cpp) and `RepaintCoordinator`

**Current:** When `Dashboard` is dirty, the handler calls `repaint()` on the whole main component. Other bits (PianoRoll, Mixer, etc.) repaint only their specific window/panel.

**Change:** If only a single non-Dashboard bit is dirty, repaint only that component (and perhaps a minimal parent), and only call full `repaint()` when Dashboard or multiple regions are dirty. Reduces overdraw when e.g. only the playhead moved.

**Benefit:** Less GPU/CPU work per frame when updates are localized.

---

## 6. OscAirlock::process: cap batch size on hot path (Low effort, small win)

**Where:** `OscAirlock::process()` (OscAirlock.h)

**Current:** `process(processFn)` calls `processBatch(..., Capacity)` so a single audio block can drain up to 8192 events.

**Change:** Use a smaller default max per block (e.g. 256 or 512) so that one block never does too much work; the rest will be processed in the next block. Prevents occasional long audio callback if the queue was very full.

**Benefit:** More consistent callback times under heavy load.

---

## 7. ConfigPanel / heavy panels: lazy or incremental layout (Higher effort)

**Where:** ConfigPanel and other large panels that do a lot in `resized()`.

**Current:** Full layout recalc on every resize.

**Change:** Only recompute layout when bounds actually change (compare to last bounds). For very large panels, consider lazy-building sections that are off-screen or collapsed.

**Benefit:** Less CPU during resize drags and when many components are visible.

---

## 8. getClockSourceID: avoid allocations on message thread (Low effort, niche)

**Where:** `MidiRouter::getClockSourceID()` (MidiRouter.cpp) and callers in SystemController (UI).

**Current:** Returns `juce::String::fromUTF8(p)`, which allocates.

**Change:** For UI display, current usage is fine. If this were ever called from a hot path, provide a variant that writes into a preallocated buffer and returns a string view, or compare device ID without constructing a juce::String (e.g. strcmp with the stored C string). Currently only used from message thread, so low priority.

---

## Summary

| # | Area              | Effort | Impact  | Notes                          |
|---|-------------------|--------|---------|--------------------------------|
| 1 | Audio block reads | Low    | Small   | One-line style change          |
| 2 | Log string build  | Medium | Medium  | Fewer allocations at 10 Hz     |
| 3 | TimerHub iteration| Low    | Small   | Vector for tick path           |
| 4 | sendPanic lock    | Medium | High    | Avoid blocking audio           |
| 5 | Repaint scope     | Low    | Small–Med | Less overdraw                |
| 6 | Airlock batch cap | Low    | Small   | Smoother callbacks under load   |
| 7 | Panel layout      | High   | Medium  | Resize/layout cost             |
| 8 | getClockSourceID  | Low    | Niche   | Only if used on hot path later |

Implementing 1, 3, 5, and 6 gives good payoff for limited change; 2 and 4 are the next most valuable.

---

## Implemented (high → low impact)

- **#4 (sendPanic lock):** Done — Lock is held only while clearing `heldNotes`/`latchedNotes`/`sustainedNotes` and copying service pointers; MIDI sends, scheduler->clear(), tracker->clearAll(), engine->stop(), airlock->clear() run outside the lock so the audio thread is never blocked.
- **#2 (Log string build):** Done — `flushLogToDisplay()` builds the log with `juce::StringArray` and `joinIntoString("\n")` instead of repeated `fullText += line + "\n"`, reducing allocations and copy.
- **#5 (Repaint scope):** Done — Comment clarified; full `repaint()` is only invoked when `Dashboard` is dirty; other bits repaint only their component.
- **#3 (TimerHub iteration):** Done — `tick()` iterates a `std::vector<Subscriber>` (`tickOrder_`) rebuilt on subscribe/unsubscribe instead of the map, for cache-friendly hot path.
- **#1 (Audio block reads):** Done — beat/bpm are read once per block in `getNextAudioBlock` and reused for metronome and countInManager.
- **#6 (Airlock batch cap):** Done — `OscAirlock::process()` drains at most `DefaultBatchSize` (512) events per call; remaining events are processed in the next block.
- **#7 (Panel layout):** Done — `ConfigPanel::resized()` stores `lastLayoutBounds_` and returns immediately when `getLocalBounds() == lastLayoutBounds_`, avoiding redundant layout work on repeated resize events.
- **#8 (getClockSourceID):** Done — Added `getClockSourceIDPointer()` (lock-free, no allocation). MIDI clock-source check in `routeMidiFromDevice` now uses it with `source->getIdentifier().compare(juce::CharPointer_UTF8(allowed))` so the real-time path does not allocate.
