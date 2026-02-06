# Source Directory Audit

Scrutiny of duplicate components, bad logic, broken features, and missing implementations.

---

## 1. Duplicate or overlapping components/classes

### 1.1 EventBus vs BridgeEventBus (dead code)

- **BridgeEventBus** (`Core/BridgeEventBus.h`): Used for `BridgeEvent` delivery (MidiRouter → NetworkWorker). Actively used in `MidiRouter.cpp` and `BridgeContext.cpp`.
- **EventBus** (`Core/EventBus.h`): Generic var-based publish-subscribe with enum events (TransportPlay, NoteOn, etc.). **Never referenced in implementation code**—only in its own header comments and README. No `EventBus::instance().subscribe()` or `emit()` calls anywhere.

**Recommendation:** Remove `EventBus.h` or document it as “reserved for future use.” If kept, add a single use (e.g. one `emit` on transport change) or mark the file with `// Reserved / unused` so it’s not mistaken for active infrastructure.

### 1.2 ConfigPanel vs ConfigControls (not duplicate)

- **ConfigPanel**: Main config UI; includes and uses **ConfigControls**.
- **ConfigControls**: Defines helper types (`OscAddressConfig`, `ControlPage`, etc.) and shared controls. It is a dependency of ConfigPanel, not a duplicate.

### 1.3 MidiDevicePickerPanel vs MidiPortsTablePanel (not duplicate)

- **MidiDevicePickerPanel**: Used for “MIDI Inputs…” and “MIDI Outputs…” dialogs (MenuBuilder).
- **MidiPortsTablePanel**: Table shown inside Config (e.g. Connections > MIDI). Different roles; both are valid.

### 1.4 AtomicLayout (unused)

- **AtomicLayout** / **AtomicLayoutState** (`Core/AtomicLayout.h`): Thread-safe layout state. **No includes or usages** anywhere in Source. Effectively dead code.

**Recommendation:** Remove `AtomicLayout.h` or add a single use (e.g. Performance panel / splice editor reading layout from a shared AtomicLayout). Otherwise delete to reduce confusion.

---

## 2. Placeholder / stub / missing implementations

### 2.1 BindingService (`Core/BindingService.h`)

- Empty namespace with comment: “Reserved for future: bindTransport(…), bindConfig(…).”
- All binding is done in **SystemController** (bindHeader, bindTransport, bindConfig, etc.). Header is documentation-only.

**Recommendation:** Keep as-is or move the comment into README/CONTEXT; no bug.

### 2.2 LayoutManager (`Core/LayoutManager.h`)

- Empty namespace; layout save/restore and wizard live in **SystemController** and **AppState**.
- Same as BindingService: placeholder for future extraction.

**Recommendation:** Keep as-is or document in README.

### 2.3 GamepadService (`Services/GamepadService.cpp`)

- **update()**: Stub; comment says “When gainput or platform API … is integrated, poll device here.”
- **applyDeadzoneAndSensitivity()**: Implemented and used.
- Gamepad is started (e.g. `startPolling(60)`) but `update()` does nothing.

**Recommendation:** Either wire a real input source (gainput/platform) or document in UI/Config that “Gamepad” is not yet functional so users aren’t misled.

### 2.4 LfoGeneratorPanel “Folder (preset placeholder)”

- Button `btnFolder` with text "…" and comment “Folder (preset placeholder).” No preset/folder logic wired.

**Recommendation:** Implement preset/folder or rename to “Presets (coming soon)” / hide until implemented.

### 2.5 MeterBarRenderer

- ROADMAP referred to it as “stub API”; implementation is **complete** (init, setLevels, render, release). No change needed.

---

## 3. Bad or fragile logic

### 3.1 Crash recovery / “crashed last session” (fixed in code)

- **Issue:** “Crashed last session” was driven by **AppState "crashed"**. That flag is set `true` in **BridgeContext ctor** and `false` in **BridgeContext dtor**. After a real crash the dtor never runs, so state is never saved; on next run we load **old** state and “crashed” does not reliably mean “last run crashed.”
- **Correct signal:** **CrashRecovery::hasRecoveryData()** (recovery file exists) means we did not shut down cleanly. Use that for the “WARNING: The bridge crashed last session” message and for any “restore?” logic.

**Fix applied:** Use `CrashRecovery::hasRecoveryData()` for the warning and set AppState `crashed` from that so persisted state stays consistent.

### 3.2 MixerPanel::removeAllStrips

- **Comment** said “removeAllStrips … maybe broken” and “loses the callbacks.” Implementation **clears** strips then re-adds via **createStrip(i)**, which rebinds callbacks. So behaviour is correct; the comment was outdated.

**Fix applied:** Comment updated to state that `removeAllStrips` reuses `createStrip` and keeps callbacks; no logic change.

---

## 4. Broken or inconsistent behaviour

### 4.1 None confirmed

- No other clearly broken flows were found. EventBus unused and AtomicLayout unused are cleanliness issues, not behavioural bugs.

---

## 5. Summary table

| Item                    | Type           | Severity   | Action |
|-------------------------|----------------|-----------|--------|
| EventBus                | Dead code      | Low       | **Done:** marked RESERVED/UNUSED in header |
| AtomicLayout            | Unused         | Low       | **Done:** marked UNUSED in header |
| BindingService           | Placeholder    | Info      | Optional doc only |
| LayoutManager            | Placeholder    | Info      | Optional doc only |
| GamepadService::update() | Stub           | Medium    | Implement or document “not yet” |
| LFO “Folder” button      | Placeholder    | Low       | Implement or label “coming soon” |
| Crash “crashed” logic    | Wrong signal   | Medium    | **Fixed:** use CrashRecovery |
| MixerPanel removeAllStrips comment | Outdated | Low | **Fixed:** comment updated |

---

## 6. Files to consider removing or trimming

- **Core/EventBus.h** – unused; remove or mark “reserved.”
- **Core/AtomicLayout.h** – unused; remove or use once and document.

No other files need removal; placeholders (BindingService, LayoutManager) are small and document intent.

---

## 7. Follow-up work completed (post-audit)

- **EventBus.h**: Banner added stating RESERVED/UNUSED and to use BridgeEventBus for bridge events.
- **AtomicLayout.h**: Banner added stating UNUSED and reserved for future layout coordination.
- **ConfigPanel**: `btnGamepadEnable` tooltip set to "Enable gamepad input (not yet functional; polling stub only)."
- **GamepadService.h**: Comment on `update()` that it is a stub and to wire gainput/platform API.
- **LfoGeneratorPanel**: `btnFolder` tooltip set to "Presets (coming soon)"; comment updated to "Presets (coming soon)."
