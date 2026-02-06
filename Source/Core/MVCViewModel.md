# MVC/MVP ViewModel Layer (Roadmap 21)

## Current State
- **TransportViewModel** – `Source/Core/TransportViewModel.h/.cpp`: Facade over BridgeContext exposing transport state (`isPlaying()`, `currentBeat()`, `currentBpm()`) and commands (`play()`, `stop()`, `togglePlay()`). SystemController holds it and uses it for transport bindings and shortcuts.
- **MixerViewModel** – `Source/Core/MixerViewModel.h/.cpp`: Exposes `reset()`, `isChannelActive(ch)`, `setActive(ch, active)`. Owned by BridgeContext; used by CommandDispatcher (MixerMuteToggle) and SystemController (reset button, mixer panel onResetRequested).
- **SequencerViewModel** – `Source/Core/SequencerViewModel.h/.cpp`: Exposes `updateData()`, `setRoll(div)`, `setTimeSignature(num, den)`, `setSwing(fraction)`, `setSequencerChannel(ch)`, `setMomentaryLoopSteps(steps)`, `requestExport()`, `randomizeCurrentPage()`. Owned by BridgeContext; used by CommandDispatcher (SequencerRandomize) and SystemController (bindPerformance sequencer→engine wiring).
- **TransportPanel** – Still takes `AudioEngine&`, `MidiRouter&`, `BridgeContext&`; actions are wired in SystemController. Optional refactor: construct TransportPanel with `TransportViewModel&` and use `viewModel.play()` etc. in action callbacks.

## Pattern
1. ViewModel holds reference to context (or specific services).
2. ViewModel exposes read-only state and command methods.
3. UI binds to ViewModel; no direct engine/midiRouter in panel code.
4. Commands can later be routed through a Command pattern (e.g. CommandDispatcher) for undo/logging.

## Next Steps
- Optional: Construct panels with ViewModel references instead of context for further decoupling.
