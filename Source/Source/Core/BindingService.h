/*
  ==============================================================================
    Source/Core/BindingService.h
    Role: Placeholder for centralised UI-to-backend binding.
    Current behaviour: SystemController binds UI (ConfigPanel, TransportPanel,
    Header, etc.) to context and callbacks in bindHeader, bindTransport,
    bindConfig, etc. This header can be expanded to a BindingService that
    owns topic-based wiring (e.g. Transport, MIDI I/O, Profile, Render mode)
    so SystemController delegates binding to it and stays focused on
    layout and lifecycle.
  ==============================================================================
*/
#pragma once

// UI event binding lives in SystemController (bindHeader, bindTransport,
// bindConfig, bindMixer, bindMappingManager, bindLfoPatching, bindMacros, etc.).
// Future: move per-domain binding into BindingService methods if desired.

namespace BindingService {

// Reserved for future: bindTransport(transportPanel, context),
// bindConfig(configPanel, context), bindMidiIo(configPanel, hardwareController), etc.

} // namespace BindingService
