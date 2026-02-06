#include "../Services/MidiDeviceService.h"
#include "../Core/TimerHub.h"
#include <algorithm>
#include <chrono>

namespace {

class MidiDrainThread : public juce::Thread {
public:
  explicit MidiDrainThread(MidiDeviceService *svc)
      : juce::Thread("MIDI drain"), service(svc) {}

  void run() override {
    while (!threadShouldExit() && service != nullptr)
      service->runDrainLoop();
  }

private:
  MidiDeviceService *service = nullptr;
};

} // namespace

MidiDeviceService::MidiDeviceService() {
  hubId = "MidiDeviceService_" + juce::Uuid().toDashedString().toStdString();
  TimerHub::instance().subscribe(
      hubId, [this] { tickDeviceReconcile(); }, TimerHub::Rate0_5Hz);

  drainThread = std::make_unique<MidiDrainThread>(this);
  drainThread->startThread(juce::Thread::Priority::highest);
}

MidiDeviceService::~MidiDeviceService() {
  if (drainThread && drainThread->isThreadRunning()) {
    drainThread->signalThreadShouldExit();
    sendQueue.wakeDrain();
    drainThread->waitForThreadToExit(3000);
  }
  TimerHub::instance().unsubscribe(hubId);
  activeOutputs.update({});
}

void MidiDeviceService::sendMessage(const juce::MidiMessage &m) {
  sendQueue.push(m);
}

void MidiDeviceService::runDrainLoop() {
  sendQueue.waitForData(std::chrono::microseconds(1000)); // 1 ms max latency
  drainSendQueue();
}

void MidiDeviceService::drainSendQueue() {
  auto list = activeOutputs.get();
  if (!list || list->empty())
    return;
  sendQueue.process([&list](const juce::MidiMessage &m) {
    for (auto &out : *list) {
      if (out)
        out->sendMessageNow(m);
    }
  });
}

void MidiDeviceService::tickDeviceReconcile() {
  if (!appState)
    return;

  // HOT SWAP: Periodically reconcile active devices list with AppState
  // This handles unplugging/replugging events
  auto inputs = juce::MidiInput::getAvailableDevices();
  auto outputs = juce::MidiOutput::getAvailableDevices();

  // 1. INPUTS
  juce::StringArray desiredInputs = appState->getActiveMidiIds(true);

  // A. Check for missing devices that appeared (lock to avoid race with
  // setInputEnabled)
  juce::StringArray toOpen;
  {
    std::lock_guard<std::mutex> lock(openInputsMutex);
    for (const auto &id : desiredInputs) {
      bool isOpen = false;
      for (auto &openDev : openInputs) {
        if (!openDev)
          continue;
        if (openDev->getIdentifier() == id) {
          isOpen = true;
          break;
        }
      }
      if (!isOpen) {
        for (auto &hw : inputs) {
          if (hw.identifier == id) {
            toOpen.add(id);
            break;
          }
        }
      }
    }
  }
  for (const auto &id : toOpen)
    setInputEnabled(id, true, lastInputCallback);

  // B. Remove inputs that are no longer available (device unplugged)
  juce::StringArray inputIdsToClose;
  {
    std::lock_guard<std::mutex> lock(openInputsMutex);
    juce::StringArray availableInputIds;
    for (auto &hw : inputs)
      availableInputIds.add(hw.identifier);
    for (auto &dev : openInputs) {
      if (dev && availableInputIds.indexOf(dev->getIdentifier()) < 0)
        inputIdsToClose.add(dev->getIdentifier());
    }
  }
  bool inputsChanged = !inputIdsToClose.isEmpty();
  for (const auto &id : inputIdsToClose)
    setInputEnabled(id, false, lastInputCallback);

  // 2. OUTPUTS: reopen desired if not open; remove outputs no longer available
  juce::StringArray desiredOutputs = appState->getActiveMidiIds(false);
  auto openOuts = activeOutputs.get();

  if (openOuts) {
    juce::StringArray availableOutputIds;
    for (auto &hw : outputs)
      availableOutputIds.add(hw.identifier);

    for (const auto &id : desiredOutputs) {
      bool isOpen = false;
      for (auto &openDev : *openOuts) {
        if (!openDev)
          continue;
        if (openDev->getIdentifier() == id) {
          isOpen = true;
          break;
        }
      }
      if (!isOpen) {
        for (auto &hw : outputs) {
          if (hw.identifier == id) {
            setOutputEnabled(id, true);
            break;
          }
        }
      }
    }

    // Remove outputs that disappeared (device unplugged)
    bool changed = false;
    auto newList = *openOuts;
    newList.erase(
        std::remove_if(newList.begin(), newList.end(),
                       [&availableOutputIds, &changed](
                           const std::shared_ptr<juce::MidiOutput> &dev) {
                         if (!dev)
                           return true;
                         bool gone = availableOutputIds.indexOf(
                                         dev->getIdentifier()) < 0;
                         if (gone)
                           changed = true;
                         return gone;
                       }),
        newList.end());
    if (changed) {
      auto oldList = activeOutputs.update(newList);
      if (oldList && deferredDeleter)
        deferredDeleter->deleteAsync(oldList);
      juce::StringArray activeIds;
      for (auto &dev : newList)
        if (dev)
          activeIds.add(dev->getIdentifier());
      appState->updateActiveMidiIds(activeIds, false);
      inputsChanged = true;
    }
  }

  if (inputsChanged && onDeviceListChanged) {
    auto cb = onDeviceListChanged;
    juce::MessageManager::callAsync([cb]() {
      if (cb)
        cb();
    });
  }
}

void MidiDeviceService::handleIncomingMidiMessage(
    juce::MidiInput *source, const juce::MidiMessage &message) {
  juce::ignoreUnused(message);
  if (source == nullptr)
    return;
  // This callback is used when we open inputs directly
  // We typically don't process here, we let the BridgeContext->midiRouter
  // handle it But we need the callback signature.
}

void MidiDeviceService::setInputEnabled(const juce::String &deviceID,
                                        bool enabled,
                                        juce::MidiInputCallback *callback) {
  std::lock_guard<std::mutex> lock(openInputsMutex);

  // Virtual Keyboard: synthetic input (Z/X octave, on-screen keyboard); no
  // hardware to open
  if (deviceID == "VirtualKeyboard" || deviceID == "__virtual_keyboard__") {
    if (appState) {
      juce::StringArray activeIds;
      for (auto &dev : openInputs)
        if (dev)
          activeIds.add(dev->getIdentifier());
      int idx = activeIds.indexOf(deviceID);
      if (enabled && idx < 0)
        activeIds.add("VirtualKeyboard");
      else if (!enabled)
        activeIds.removeString("VirtualKeyboard");
      appState->updateActiveMidiIds(activeIds, true);
    }
    return;
  }

  // Always remove existing device first (prevents duplicates, clean re-enable)
  openInputs.erase(
      std::remove_if(openInputs.begin(), openInputs.end(),
                     [&deviceID](const std::unique_ptr<juce::MidiInput> &dev) {
                       return dev && dev->getIdentifier() == deviceID;
                     }),
      openInputs.end());

  if (!enabled) {
    if (appState) {
      juce::StringArray activeIds;
      for (auto &dev : openInputs)
        if (dev)
          activeIds.add(dev->getIdentifier());
      appState->updateActiveMidiIds(activeIds, true);
    }
    return;
  }

  // ENABLE: open device (we already removed any stale entry above)
  {
    auto list = juce::MidiInput::getAvailableDevices();
    for (auto &d : list) {
      if (d.identifier == deviceID || d.name == deviceID) {
        if (callback)
          lastInputCallback = callback;

        auto inputDevice = juce::MidiInput::openDevice(
            d.identifier, callback ? callback : this);

        if (inputDevice) {
          inputDevice->start();
          openInputs.push_back(std::move(inputDevice));

          if (appState)
            appState->setLastMidiInId(d.identifier);
        } else {
          juce::String msg = "MIDI input \"" + d.name +
                             "\" could not be opened. It may be in use "
                             "elsewhere. Try selecting it again in Config.";
          juce::Logger::writeToLog(msg);
          if (onDeviceOpenError)
            onDeviceOpenError(msg);
        }
        break;
      }
    }
  }

  // --- SYNC APP STATE ---
  if (appState) {
    juce::StringArray activeIds;
    for (auto &dev : openInputs) {
      if (dev)
        activeIds.add(dev->getIdentifier());
    }
    appState->updateActiveMidiIds(activeIds, true);
  }
}

void MidiDeviceService::setOutputEnabled(const juce::String &deviceID,
                                         bool enabled) {
  auto current = activeOutputs.get();
  if (!current)
    return;
  auto newList = *current;

  if (enabled) {
    bool alreadyOpen = false;
    for (auto &dev : newList) {
      if (dev && dev->getIdentifier() == deviceID) {
        alreadyOpen = true;
        break;
      }
    }

    if (!alreadyOpen) {
      auto list = juce::MidiOutput::getAvailableDevices();
      for (auto &d : list) {
        if (d.identifier == deviceID || d.name == deviceID) {
          auto out = juce::MidiOutput::openDevice(d.identifier);
          if (out) {
            newList.push_back(std::move(out));
            if (appState)
              appState->setLastMidiOutId(d.identifier);
          } else {
            juce::String msg = "MIDI output \"" + d.name +
                               "\" could not be opened. It may be in use "
                               "elsewhere. Try selecting it again in Config.";
            juce::Logger::writeToLog(msg);
            if (onDeviceOpenError)
              onDeviceOpenError(msg);
          }
          break;
        }
      }
    }
  } else {
    // DISABLE
    newList.erase(
        std::remove_if(
            newList.begin(), newList.end(),
            [&deviceID](const std::shared_ptr<juce::MidiOutput> &dev) {
              return dev && dev->getIdentifier() == deviceID;
            }),
        newList.end());
  }

  // Commit changes
  auto oldList = activeOutputs.update(newList);
  if (oldList && deferredDeleter) {
    deferredDeleter->deleteAsync(oldList);
  }

  if (appState) {
    juce::StringArray activeIds;
    for (auto &dev : newList) {
      if (dev)
        activeIds.add(dev->getIdentifier());
    }
    appState->updateActiveMidiIds(activeIds, false);
  }
}

void MidiDeviceService::setThruEnabled(bool enabled) {
  if (appState)
    appState->setMidiThru(enabled);
}

void MidiDeviceService::setBlockOutput(bool blocked) {
  juce::ignoreUnused(blocked);
  // Logic to block output
}

void MidiDeviceService::setChannel(int channel) {
  if (appState)
    appState->setMidiOutChannel(channel);
}

void MidiDeviceService::reconcileHardware() { tickDeviceReconcile(); }

void MidiDeviceService::loadConfig(juce::MidiInputCallback *callback) {
  reconcileHardware();
  if (appState && callback) {
    juce::String inId = appState->getLastMidiInId();
    if (inId.isNotEmpty())
      setInputEnabled(inId, true, callback);
  }
  // First-run only: if no devices selected but devices available, enable first
  // in/out once
  if (appState && callback) {
    juce::StringArray inIds = appState->getActiveMidiIds(true);
    juce::StringArray outIds = appState->getActiveMidiIds(false);
    bool alreadyAutoSelected =
        (bool)appState->getState().getProperty("midiAutoSelectedOnce", false);
    if (!alreadyAutoSelected && inIds.isEmpty() && outIds.isEmpty()) {
      auto inputs = juce::MidiInput::getAvailableDevices();
      auto outputs = juce::MidiOutput::getAvailableDevices();
      if (!inputs.isEmpty()) {
        setInputEnabled(inputs[0].identifier, true, callback);
        appState->setLastMidiInId(inputs[0].identifier);
      }
      if (!outputs.isEmpty()) {
        setOutputEnabled(outputs[0].identifier, true);
        appState->setLastMidiOutId(outputs[0].identifier);
      }
      appState->getState().setProperty("midiAutoSelectedOnce", true, nullptr);
    }
  }
}

void MidiDeviceService::forceAllNotesOff() {
  sendMessage(juce::MidiMessage::allNotesOff(1));
}

void MidiDeviceService::setAppState(AppState *state) {
  appState = state;
  // Force immediate check now that we have state.
  // This ensures devices are opened before the main UI displays them.
  tickDeviceReconcile();
}
