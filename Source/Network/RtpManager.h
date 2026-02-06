#pragma once
#include "../Audio/MidiRouter.h"
#include "../Services/MidiDeviceService.h"
#include <juce_core/juce_core.h>

struct AppleMidiHeader {
  uint16_t signature = 0xffff;
  uint16_t command = 0;
  // Command IDs: 1=Invite, 2=Accept, 3=Reject, 4=End, 0xCK=Sync
};

class RtpManager {
public:
  enum class Mode { Off, OsDriver, EmbeddedServer };

  RtpManager(MidiDeviceService &config, MidiRouter &handler)
      : midiConfig(config), internalServer(handler) {}

  ~RtpManager() { internalServer.stop(); }

  // THE SWITCHING LOGIC
  void setMode(Mode newMode) {
    if (currentMode == newMode)
      return;

    // 1. Cleanup Old Mode
    if (currentMode == Mode::EmbeddedServer) {
      internalServer.stop();
    } else if (currentMode == Mode::OsDriver) {
      // Optional: Disconnect OS ports if strictly exclusive
    }

    // 2. Start New Mode
    if (newMode == Mode::EmbeddedServer) {
      bool success = internalServer.start(5004);
      if (!success) {
        // FALLBACK: If 5004 is busy (OS Driver running?), try 5006
        DBG("RTP: Port 5004 busy, trying 5006...");
        success = internalServer.start(5006);
      }

      if (success) {
        if (onLog)
          onLog("RTP-MIDI: Internal Server Listening on Port " +
                    juce::String(internalServer.getPort()),
                false);
      } else {
        if (onLog)
          onLog("RTP-MIDI Error: Could not bind ports! OS Driver might be "
                "active.",
                true);
        // Revert to Off or notify UI
        newMode = Mode::Off;
      }
    } else if (newMode == Mode::OsDriver) {
      enableOsNetworkPorts();
      if (onLog)
        onLog("RTP-MIDI: Using OS Driver", false);
    }

    currentMode = newMode;
  }

  // Callback for logging to UI
  std::function<void(juce::String, bool)> onLog;

private:
  Mode currentMode = Mode::Off;
  MidiDeviceService &midiConfig;
  void enableOsNetworkPorts() {
    auto inputs = juce::MidiInput::getAvailableDevices();
    for (auto &dev : inputs) {
      if (dev.name.containsIgnoreCase("Network") ||
          dev.name.containsIgnoreCase("rtp")) {
        midiConfig.setInputEnabled(dev.identifier, true, nullptr);
      }
    }
  }

  // --- INTERNAL SERVER (Method 2 Implementation) ---
  class InternalServer : public juce::Thread {
  public:
    InternalServer(MidiRouter &h) : Thread("RTP_Internal"), handler(h) {}

    bool start(int port) {
      if (socket.bindToPort(port)) {
        boundPort = port;
        startThread();
        return true;
      }
      return false;
    }

    void stop() {
      signalThreadShouldExit();
      socket.shutdown(); // Breaks the wait loop
      stopThread(2000);
    }

    int getPort() const { return boundPort; }

    void handleControlMessage(uint16_t cmd, const std::vector<uint8_t> &data,
                              int size, const juce::String &ip, int port) {
      if (cmd == 1) { // INVITATION
        // Reply with ACCEPT (Command 2)
        // Packet: FFFF 0002 [Version 4B] [InitiatorToken 4B] [SSRC 4B]
        // [Name...]

        juce::MemoryOutputStream resp;
        resp.writeShortBigEndian((short)0xffffu);
        resp.writeShortBigEndian((short)0x0002); // Accept
        resp.writeIntBigEndian(2);               // Protocol Version 2

        // Copy Initiator Token (bytes 8-11 of request)
        if (size >= 12) {
          resp.write(&data[8], 4);
        } else {
          resp.writeInt(0);
        }

        resp.writeIntBigEndian(0x12345678); // Our SSRC
        resp.writeString("PatchworldBridge");

        socket.write(ip, port, resp.getData(), (int)resp.getDataSize());
      } else if (cmd == 0x434B) { // SYNC ("CK")
        // Reply to keepalive/sync with same timestamp
        // Usually Count(1) + Padding(3) + Timestamp(8)
        if (size >= 12) {
          // Echo back specific sync logic (Simplified: just echo count +
          // timestamp) Ideally we add our own timestamp delta
          socket.write(ip, port, data.data(), size);
        }
      }
    }

    void run() override {
      setPriority(juce::Thread::Priority::highest);
      std::vector<uint8_t> buffer(4096);

      while (!threadShouldExit()) {
        int ready = socket.waitUntilReady(true, 500);
        if (ready < 0)
          break; // Error
        if (ready == 0)
          continue; // Timeout

        juce::String senderIP;
        int senderPort;
        int bytes = socket.read(buffer.data(), (int)buffer.size(), false,
                                senderIP, senderPort);

        if (bytes >= 4) {
          uint16_t signature = (uint16_t)((buffer[0] << 8) | buffer[1]);
          uint16_t command = (uint16_t)((buffer[2] << 8) | buffer[3]);

          if (signature == 0xffff) {
            // AppleMIDI Control Message
            handleControlMessage(command, buffer, bytes, senderIP, senderPort);
          } else {
            // RTP-MIDI Payload
          }
        }
      }
    }

  private:
    juce::DatagramSocket socket;
    MidiRouter &handler;
    int boundPort = 0;
  } internalServer;
};
