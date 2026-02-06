# Roadmap Continue List

Next items to tackle (in rough priority order).

## Recent improvements (done)
- **GPU piano roll** – SpliceEditor: double-buffer for `glNoteInstances`; GL thread swaps and draws without holding lock during VBO upload.
- **Lock-free MIDI** – MidiScheduler: SPSC command queue (AbstractFifo); producers (schedule/clear/allNotesOff) push lock-free; audio thread drains into pool in processBlock.
- **Synthesia/PlayMode** – PlayView: scroll speed tied to BPM (`setBpm`/`setScrollSpeedScale`); SpliceEditor: Quantize mode (Soft/Hard/Groove) and Humanize timing in context menu; Play zoom uses `setScrollSpeedScale` for BPM-consistent scroll.
- **Tooltips** – PlayView and SpliceEditor Quantize button tooltips; PerformancePanel Play zoom uses scroll-speed scale.
- **Threads** – MidiRouter: clock source ID is lock-free (`std::atomic<char*>`, copy-on-read). MIDI input callback no longer calls setClockSourceID (avoids malloc on real-time path); empty = allow any device; set specific source from Config. BridgeEventBus copies listener list under lock and invokes without lock.

## Build
- **Verify build** – Done. CMake build succeeds after ConfigManager `hasListener` fix, SystemController ViewModel includes, MainComponentCore Audio includes, Animation.h fade duration (int ms).

## File splitting (18)
- **MainComponent** – Done: `MainComponentGL.cpp`, `MainComponentEvents.cpp`, `MainComponentCore.cpp` (resized, applyLayout, audio lifecycle). Optional: ctor in MainComponentCore (defer).
- **SpliceEditor** – Done: mouse/key handlers in `SpliceEditorMouse.cpp` (SpliceEditorRendering already done).
- **MixerPanel** – Keep single header; consider `MixerStrip.cpp` if MixerStrip implementation grows.

## UI (25, 26)
- **ScaledComponent** – Done: StatusBarComponent now extends ScaledComponent and applies DPI scale to fonts in `applyScale()`.
- **Animation** – Done: MidiLearnOverlay uses `Animation::fade()` on show/hide; Header "Modules" menu uses `Animation::fade()` when toggling module window visibility (fade in on show, fade out then setVisible(false) on hide).

## MVC/MVP (21)
- **TransportViewModel** – Done: SystemController holds `transportViewModel`, uses it for Stop button and shortcut transport.play/transport.stop; Play button uses `transportViewModel->play()` when not count-in. Shortcuts use `transportViewModel->togglePlay()` and `transportViewModel->stop()`.
- **Other ViewModels** – Done: `MixerViewModel` (reset, isChannelActive, setActive) and `SequencerViewModel` (updateData, setRoll, setTimeSignature, setSwing, setSequencerChannel, setMomentaryLoopSteps, requestExport, randomizeCurrentPage). BridgeContext owns them; CommandDispatcher uses them for MixerMuteToggle and SequencerRandomize; SystemController bindMixer/bindPerformance use them for reset and sequencer→engine wiring.

## Other
- **THRU / MIDI clock out** – Done. Engine-generated clock (app BPM) is sent to MIDI out when THRU or Clock is on and not forwarding external clock (MidiRouter sends EngineSequencer clock/transport to midiService).
- **Velocity/CC curve drawing (14)** – Deferred. VelocityLane drawing modes (Point, Line, Curve, Ramp) for multi-note velocity editing.
- **Button/Slider/Knob (24)** – Done: `HoverGlowResponsiveSlider` added (ResponsiveSlider + hover glow); TransportPanel tempo slider now uses it. EnhancedSlider available for new sliders (velocity + shift fine).
- **ConfigManager (23)** – Done: BridgeContext has `configManager` wrapping `appState.getState()`. uiScale, clockSourceId, themeId, multicast, zeroconf, savedLayout migrated to configManager.get/set. AppState defaults and migration for missing keys after load.
- **GPU meter (28)** – Done: `MeterBarRenderer.h` stub API (init, setLevels, render, release) for future OpenGL instanced meter quads. See GPUOffload.md.
- **MainComponentCore** – Done: `resized()`, `applyLayout()`, `prepareToPlay()`, `getNextAudioBlock()`, `releaseResources()`, `isPlaying()` in MainComponentCore.cpp. Optional: extract ctor (defer unless needed).
