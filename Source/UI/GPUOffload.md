# GPU Offload Strategy

## Current GPU Usage (what actually runs on GPU)
- **CRTBackground** – Full-screen shader (resolution, time, vignette, scanline, aurora). **On GPU** via `MainComponent::renderOpenGL()`.
- **MeterBarRenderer** – Mixer meter bars: instanced quads with per-channel colours; levels from `MixerPanel::getMeterLevels()`, viewport set to mixer meter area. **On GPU** when OpenGL context is active; `setGpuMetersActive(true)` skips CPU meter paint in `MeterComponent::paint()`.
- **ComplexPianoRoll (trackGrid)** – Instanced note quads when visible. **On GPU** when OpenGL context is active and `trackGrid` is visible (note: trackGrid is currently hidden in PerformancePanel; the visible piano roll is SpliceEditor).
- **SpliceEditor** – Edit-mode note quads: instanced draw from `glNoteInstances` (filled in `paint()` from `RenderState`). **On GPU** when OpenGL context is active and Performance panel is in Edit mode; `paintEditMode(..., drawNotesOnCpu=false)` skips the CPU note loop.
- **MainComponent::renderOpenGL()** – Clears, CRT shader (every 2nd frame in Eco), meter bars, trackGrid notes (if visible), SpliceEditor notes (if Edit mode), then JUCE draws the rest of the UI on CPU.

## Not yet on GPU (safe to offload)
- **Waveform / analysis** – Any future waveform view: render to texture or geometry on GPU.

## CPU vs GPU Recommendation
| Component           | Prefer   | Notes                                      |
|--------------------|----------|--------------------------------------------|
| CRTBackground      | GPU      | Already shader-based.                      |
| Piano roll notes   | GPU      | Instanced quads when OpenGL active.        |
| Meter bars         | GPU      | Optional: one instanced draw per strip set.|
| Text / labels      | CPU      | JUCE text; keep on CPU.                   |
| Buttons / sliders  | CPU      | Complex paths; keep on CPU.                |
| DiagnosticOverlay  | CPU      | Text + simple shapes.                     |

## Offloading UI Elements to GPU
1. **Meter bars** – In MixerStrip, replace `paint()` meter drawing with a shared “meter quad” renderer that uploads level data to a VBO and draws instanced (one quad per segment). Requires OpenGL context in mixer path.
2. **Waveform / analysis** – Any future waveform view can use a GPU texture or geometry buffer.
3. **Piano roll** – Already has `ComplexPianoRoll::initGL` and instance data; ensure it’s used when context is attached and visibility is true.

## Integration
- MainComponent owns the OpenGL context; CRTBackground and any GPU UI (e.g. meter renderer) should be driven from `renderOpenGL()` or a shared GL render pass.
- Use a single “UI overlay” pass that draws CRT + instanced note quads + optional meter quads, then let JUCE handle the rest on CPU.

## Offloading UI Elements to GPU (Implementation)
- **Meter bars**: **Done.** `MeterBarRenderer` draws instanced quads in `renderOpenGL()`; per-channel colours from `Theme::getChannelColor()`; viewport set to mixer meter area; MixerStrip skips meter paint when `gpuMetersActive`.
- **Piano roll notes (trackGrid)**: **Done.** `ComplexPianoRoll::initGL`/`renderGL`/`releaseGL` called from MainComponent OpenGL lifecycle when trackGrid is visible (trackGrid is currently hidden in UI).
- **SpliceEditor notes**: **Done.** `SpliceEditor::initGL`/`renderGL`/`releaseGL`; instance data built in `paint()` when `gpuNotesActive`; `paintEditMode(..., drawNotesOnCpu=false)` skips CPU note drawing; viewport set to splice editor bounds in MainComponent coords.
- **CRTBackground**: Already GPU; uniform caching (resolution, vignette, etc.) is done to reduce state changes.
