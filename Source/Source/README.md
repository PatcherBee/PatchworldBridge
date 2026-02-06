# PatchworldBridge Source Layout

OSC–MIDI–Link bridge: real-time, cross-platform (Win/Mac/Linux), OpenGL/Vulkan, .mid playback, Ableton Link, RTP/Bluetooth MIDI, sequencer, mixer, arp, chord generator, splice editor.

## Directory Overview

| Folder | Role |
|--------|------|
| **Audio/** | AudioEngine, MidiRouter, MidiScheduler, PlaybackController, ClockSmoother, Metronome, CountInManager, NoteTracker. Real-time MIDI/audio paths. |
| **Core/** | BridgeContext (service container), SystemController (UI/layout binding), AppState, BridgeEventBus, EventBus, MidiHardwareController, LayoutManager, BindingService (placeholder), RepaintCoordinator, TimerHub, ConfigManager, CommandQueue, ShortcutManager, MenuBuilder. |
| **Network/** | OscManager, NetworkWorker, OscAirlock, RtpManager, schema/lookup. OSC and RTP-MIDI. |
| **Services/** | MidiDeviceService, MidiMappingService, ProfileService, GamepadService, AutoSaver, LatencyCalibrator, MidiFilter, DeferredDeleter. |
| **UI/** | MainComponent (phased init), Panels/* (Config, Transport, Performance, Mixer, Sequencer, etc.), Widgets/* (ModuleWindow, PianoRoll, SpliceEditor, etc.), RenderConfig, RenderBackend, VulkanContext. |
| **Resources/** | App manifest, desktop entry. |
| **Tests/** | In-repo tests (Airlock, ClockSmoother, MidiMappingCurve, RunAll). |

## Key Flows

- **Bridge events**: `MidiRouter` → `BridgeEventBus::emit()` → subscribers (e.g. BridgeContext → NetworkWorker). Single path; no manual `onBridgeEvent` wiring.
- **Thread safety**: BridgeContext uses an event-bus guard (`eventBusGuard_`) so callbacks are no-op after destroy. Use weak refs or guards for any cross-thread callback into owned objects.
- **Init**: MainComponent constructor runs phased init: `initContextAndNetworkPanel`, `initPanels`, `initModuleWindows`, `wireModuleWindowCallbacks`, `wireHeaderAndViewSwitching`, etc.
- **Hardware**: MIDI device toggles and config load go through **MidiHardwareController** (Core); **MidiDeviceService** (Services) does reconcile and open/close.

## Adding Features

- New panels: add under `UI/Panels/`, register in MainComponent init phases.
- New event consumers: subscribe to `BridgeEventBus` or `EventBus` (Core).
- New services: own in BridgeContext, wire in SystemController or panel code.
- Layout/wizard: extend **LayoutManager** (Core) or keep logic in SystemController until a dedicated manager is needed.
- UI binding: extend **BindingService** (Core) to own transport/config/MIDI-IO wiring; SystemController then delegates.

## Shutdown / thread safety

- BridgeContext destructor: (1) stop engine, (2) clear device and clock callbacks, (3) nullify midi service refs, (4) all notes off, (5) stop workers, (6) invalidate event-bus guard and unsubscribe, (7) flush logs and save. Clearing `setOnDeviceOpenError({})` and `onClockPulse = nullptr` before stopping workers avoids callbacks running after destroy.
