# Naming Conventions

## BridgeContext members (logic domains)
- **engine** – AudioEngine (playback, sequence, BPM)
- **midiRouter** – MidiRouter (MIDI I/O, routing, Link, OSC bridge)
- **oscManager** – OSC send/receive
- **mixer** – MixerPanel (channel strips, levels)
- **sequencer** – SequencerPanel (step grid)
- **mappingManager** – MidiMappingService (MIDI learn)
- **profileManager** – ProfileService (profiles)
- **playbackController** – PlaybackController (transport, count-in)
- **playlist** – MidiPlaylist (file list)

## Suffixes
- **Manager** – service that owns or coordinates (OscManager, MidiMappingService as “mappingManager”)
- **Handler** – processes events (midiRouter is MidiRouter instance)
- **Panel** – UI container (MixerPanel, SequencerPanel)
- **Controller** – coordinates flow (PlaybackController, SystemController)

## No “Was X” comments
Legacy renames are not annotated in code; this file is the single reference.
