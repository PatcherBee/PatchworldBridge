# RTP MIDI & Bluetooth MIDI

## RTP MIDI (Network MIDI)

**Current state:** Partial support via `RtpManager` (see `Source/Network/RtpManager.h`).

- **Modes:** Off, OsDriver (use OS network MIDI ports), EmbeddedServer (bind to UDP 5004/5006).
- **Implemented:** Control protocol handling (Invite/Accept, Sync/CK), UDP socket, logging callback.
- **Not yet:** Full RTP-MIDI payload parsing (journal, recovery) and injection into `MidiRouter`. The `InternalServer` currently handles control messages only; MIDI payloads need to be parsed and forwarded to the existing MIDI pipeline.

**When extending:** Parse RTP-MIDI payloads (RFC 6295), map to `juce::MidiMessage` or `BridgeEvent`, and feed into `MidiRouter` or `MidiDeviceService` so they appear as a normal MIDI input source.

---

## Bluetooth MIDI

**Current state:** Supported via OS/JUCE integration. No separate BLE stack in the app.

### Platform behaviour

- **iOS / macOS:** `BluetoothMidiDevicePairingDialogue::isAvailable()` is true (iOS 8+, macOS 10.11+). The app opens JUCE’s native pairing dialogue; after pairing, Bluetooth MIDI devices are exposed by Core MIDI and appear in `juce::MidiInput::getAvailableDevices()`.
- **Android:** Same dialogue when available (SDK 23+, device supports MIDI over Bluetooth). Paired BLE MIDI devices then show up in `getAvailableDevices()`.
- **Windows:** No JUCE pairing dialogue (`isAvailable()` is false). The app opens **Settings > Bluetooth** (`ms-settings:bluetooth`). The user pairs a Bluetooth MIDI device there; on Windows 10+ the device may then appear as a standard MIDI port and be listed by `getAvailableDevices()`. If the device does not appear, ensure it advertises as a MIDI profile and that the correct driver/Bluetooth stack is used.

### In-app flow

1. **Config > Extended Input Devices:** “Pair Bluetooth MIDI…” opens the platform pairing UI (dialogue on iOS/Mac/Android, OS Bluetooth settings on Windows).
2. **Scan:** Refreshes the list from `getAvailableDevices()` and shows which names look like Bluetooth or controller devices (e.g. “BT MIDI: …”, “Controllers: …”).
3. **MIDI In:** The MIDI In menu is built from `getAvailableDevices()` each time it is opened, so newly paired Bluetooth MIDI devices show up after pairing (and optionally after clicking Scan). Enable them there like any other MIDI input.
4. **Device reconcile:** `MidiDeviceService::tickDeviceReconcile()` runs periodically and re-opens any desired device that appears in `getAvailableDevices()`, so Bluetooth MIDI devices that were previously enabled will be reconnected when they come back.

### Gamepads (Xbox / PlayStation)

Gamepads usually appear as HID or gamepad devices, not as MIDI. They do **not** show up in the MIDI device list. Use **Config > Enable Gamepad Input** so the app reads the gamepad via the gamepad API; no Bluetooth MIDI pairing is needed for that.

### Summary

Bluetooth MIDI is supported by using the OS/JUCE pairing path and the existing MIDI pipeline: once a device is paired, it is treated like any other MIDI device. No extra code path is required beyond pairing UI and clear “Scan / MIDI In to refresh” instructions.
