# Release Readiness Guide

Suggested priorities for **release-ready quality** (beyond the completed roadmap).

---

## 1. Stability & robustness (high priority)

- **Audio callback safety** – `MainComponentCore.cpp`: `prepareToPlay` and `getNextAudioBlock` now guard on `context` so shutdown/race cannot dereference null. Ensure no other audio-thread code assumes non-null without a check.
- **MIDI send from audio thread** – All MIDI output goes through `MidiSendQueue` in `MidiDeviceService`: `sendMessage()` enqueues; a **dedicated high-priority drain thread** waits on a condition variable (wake-on-data + 1 ms timeout) and calls `sendMessageNow()` only on that thread. Sub-millisecond wake when data arrives; no blocking MIDI send from the audio thread; no UI-thread contention.
- **Device disconnect** – `MainComponent::changeListenerCallback` already handles audio device change; confirm OSC/MIDI device disconnect is handled and does not crash (e.g. null refs in MidiDeviceService / OscManager).
- **First run** – AppState/ConfigManager defaults and “reset layout” already in place; optional: “Reset to defaults” in a menu for support.

---

## 2. Build & code quality

- **Warnings** – Aim for zero compiler warnings in Release (`/W4` MSVC, `-Wall -Wextra` elsewhere). Fix or document any remaining.
- **Sanitizers (optional)** – Debug build with AddressSanitizer (ASan) and/or UndefinedBehaviorSanitizer (UBSan) to catch leaks and UB before release.
- **Assertions** – Use `jassert` in debug for invariants; avoid in hot audio path.

---

## 3. User experience

- **Tooltips** – TooltipManager already sets many; walk UI and add tooltips for any important control that lacks one.
- **Error messages** – Replace generic “Unknown Error” / technical strings with user-facing messages where the app surfaces errors (e.g. OSC connect failed, MIDI in use).
- **Graceful degradation** – If GPU/OpenGL fails, software rendering path exists; confirm no crash when OpenGL context is lost (e.g. driver update, sleep/wake).

---

## 4. Release mechanics

- **Version** – Single source: `ProjectInfo.h` and `CMakeLists.txt` (project VERSION and PRODUCT_NAME). Bump before each release and tag in git.
- **Changelog** – `CHANGELOG.md` in repo root; move [Unreleased] items into a new version section when releasing.
- **Packaging** – Installer (e.g. MSIX/Inno on Windows, .dmg/.pkg on macOS) and any code-signing/notarization required for distribution.

---

## 5. Documentation & QA

- **README** – User-facing: how to run, system requirements, optional “Quick start” (e.g. load a file, connect OSC).
- **CHANGELOG** – `CHANGELOG.md` in repo root; update [Unreleased] before each release and add a dated section for the new version.
- **Release checklist** – Run once before each release:
  - [ ] Start app (no crash).
  - [ ] Open / save project.
  - [ ] Play / stop transport; change BPM.
  - [ ] Connect MIDI in/out; send notes and CC.
  - [ ] Connect OSC (Patch → PC and PC → Patch).
  - [ ] Resize window; change theme if available.
  - [ ] Quit cleanly.

---

## 6. Optional post-release

- **Velocity/CC curve (14)** – Deferred feature; add when prioritised.
- **Automated tests** – Expand beyond AirlockTest if you add more unit/integration tests.
- **Performance** – Profile under load (many tracks, high BPM); fix any obvious spikes or freezes.

---

**Suggested order:**  
(1) Finish stability pass (null guards, device disconnect);  
(2) Zero warnings + quick sanitizer pass;  
(3) UX pass (tooltips, messages, first run);  
(4) Version + changelog + release checklist;  
(5) Packaging and signing for your target platforms.
