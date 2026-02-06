#pragma once
#include "../Audio/LockFreeRingBuffers.h"
#include "../Core/AppState.h"
#include "../Core/TimerHub.h"
#include "../Services/DeferredDeleter.h"
#include <atomic>
#include <functional>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>


// Lock-Free List for Audio Thread Safety (Outputs)
class SafeDeviceList {
public:
  using DeviceList = std::vector<std::shared_ptr<juce::MidiOutput>>;
  SafeDeviceList() {
    std::atomic_store(&currentList, std::make_shared<DeviceList>());
  }
  std::shared_ptr<DeviceList> get() const {
    return std::atomic_load(&currentList);
  }
  std::shared_ptr<DeviceList> update(const DeviceList &sourceList) {
    auto newList = std::make_shared<DeviceList>(sourceList);
    return std::atomic_exchange(&currentList, newList);
  }

private:
  std::shared_ptr<DeviceList> currentList;
};

class MidiDeviceService : public juce::MidiInputCallback {
public:
  MidiDeviceService();
  ~MidiDeviceService() override;

  void sendMessage(const juce::MidiMessage &m);

  /** Called by dedicated drain thread only (wait then drain). Public so
   * MidiDrainThread in .cpp can call it. */
  void runDrainLoop();

  // Default callback if opened internally
  void handleIncomingMidiMessage(juce::MidiInput *source,
                                 const juce::MidiMessage &message) override;

  void setInputEnabled(const juce::String &deviceID, bool enabled,
                       juce::MidiInputCallback *callback);
  void setOutputEnabled(const juce::String &deviceID, bool enabled);

  void setThruEnabled(bool enabled);
  void setBlockOutput(bool blocked);
  void setChannel(int channel);
  void reconcileHardware();
  void loadConfig(juce::MidiInputCallback *callback);
  void forceAllNotesOff();
  void setAppState(AppState *state);
  void setDeleter(DeferredDeleter *d) { deferredDeleter = d; }
  /** Called when a MIDI device could not be opened (e.g. in use elsewhere). */
  void setOnDeviceOpenError(std::function<void(const juce::String &)> fn) {
    onDeviceOpenError = std::move(fn);
  }

  /** Called on the message thread when the device list has changed (e.g. device
   * disconnected). Use to refresh Config menus and labels. */
  void setOnDeviceListChanged(std::function<void()> fn) {
    onDeviceListChanged = std::move(fn);
  }

  SafeDeviceList activeOutputs;
  MidiSendQueue sendQueue;

private:
  mutable std::mutex openInputsMutex;
  std::vector<std::unique_ptr<juce::MidiInput>> openInputs;

  juce::MidiInputCallback *lastInputCallback = nullptr;
  AppState *appState = nullptr;
  DeferredDeleter *deferredDeleter =
      nullptr; // Not strictly used for inputs, but kept for API match
  std::function<void(const juce::String &)> onDeviceOpenError;
  std::function<void()> onDeviceListChanged;

  void tickDeviceReconcile();
  void drainSendQueue();

  std::string hubId;
  std::unique_ptr<juce::Thread> drainThread;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDeviceService)
};
