# OSC Support: Current vs Full / Future Use

## Current implementation (Patchworld-oriented)

The bridge uses **basic OSC** tuned for Patchworld and MIDI↔OSC conversion:

- **Addresses**: Schema-based, channel + suffix (e.g. `/ch1n`, `/ch1noff`, `/ch1c`, `/ch1wheel`, `/ch1press`, `/ch1pc`, `/ch1pat`). Global: `/clock/bpm`, `/tempo`, `/play`, `/stop` (outgoing).
- **Arguments**: Only **float32** and **int32** are read for bridge logic. Strings are used only for instance-id echo rejection.
- **Outgoing**: Notes, CC, pitch, aftertouch, program change, transport; all as int/float. No blob, no string payloads.
- **Unknown messages**: Any message that does not match the bridge schema is **not** converted to MIDI or BridgeEvents. Previously it was dropped.

So we do **not** currently support a full OSC 1.0/1.1 feature set (arbitrary paths, string args, blobs, wildcards) in the core bridge path.

## Full OSC for future / other uses

- **Patchworld** may later support addresses with strings and more; the bridge is designed so that can be added without replacing the current schema.
- **Other hosts** (TouchOSC, Lemur, Max, Pd, custom apps) can use the same schema today, and can use the extension point below for custom behaviour.

### Extension point: `onUnknownOscMessage`

To support **full OSC** and future/other uses without changing the bridge parser:

- **OscManager** has an optional callback:  
  `onUnknownOscMessage(const juce::OSCMessage &)`.
- It is invoked for:
  - Any address that does **not** start with the schema’s inNotePrefix (e.g. `/ch`), and
  - Any address that **does** match the prefix but has an **unknown suffix** (e.g. `/ch1custom`).
- The callback receives the **raw** `juce::OSCMessage` (address + all argument types: float32, int32, string, blob, etc.). The receiver can:
  - Parse arbitrary paths and string arguments.
  - Implement Patchworld-specific or app-specific semantics.
  - Log or forward unknown messages for debugging.

So **full OSC** (arbitrary addresses, strings, blobs) is supported for **future and other uses** by registering a handler with `onUnknownOscMessage`; the built-in bridge path remains simple and Patchworld-focused (basic OSC only).

## Summary

| Feature              | Bridge (current) | Full OSC / future      |
|----------------------|------------------|-------------------------|
| Schema addresses     | Yes (/chN+suffix)| Via `onUnknownOscMessage` |
| Global (/clock/bpm…) | Yes              | Yes                     |
| Float/int args       | Yes              | Yes                     |
| String args          | No (echo only)   | In custom handler       |
| Blob args            | No               | In custom handler       |
| Arbitrary paths      | No               | In custom handler       |

The app supports **full OSC** for future/other uses via the generic callback; the core path stays basic OSC for Patchworld and MIDI bridging until Patchworld (or others) add string-aware and richer semantics.
