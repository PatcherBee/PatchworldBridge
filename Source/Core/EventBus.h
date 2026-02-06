/*
  ==============================================================================
    Source/Core/EventBus.h
    Role: Centralized event system for decoupled messaging (roadmap 11.1)
    STATUS: RESERVED / UNUSED. No subscribe() or emit() calls in codebase.
    Bridge events use BridgeEventBus (BridgeEvent). Use this when adding
    generic UI/transport events to avoid direct coupling.
  ==============================================================================
*/
#pragma once

#include <functional>
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

/**
 * EventBus: Publish-subscribe event system for loose coupling.
 * 
 * Usage:
 *   EventBus::instance().subscribe(Event::TransportPlay, [](const var& data) {
 *     // Handle event
 *   });
 *   EventBus::instance().emit(Event::TransportPlay);
 */
class EventBus {
public:
  enum class Event {
    // Transport
    TransportPlay,
    TransportPause,
    TransportStop,
    TransportSeek,
    TransportReset,

    // MIDI
    NoteOn,
    NoteOff,
    ControlChange,
    PitchBend,
    AfterTouch,

    // Engine
    BpmChanged,
    TimeSignatureChanged,
    LoopChanged,

    // Devices
    MidiDeviceConnected,
    MidiDeviceDisconnected,
    OscConnected,
    OscDisconnected,

    // UI
    ThemeChanged,
    RenderModeChanged,
    ViewChanged,
    WindowResized,

    // System
    Panic,
    SaveRequested,
    LoadRequested,
    UndoPerformed,
    RedoPerformed
  };

  using Listener = std::function<void(const juce::var &data)>;
  using ListenerId = int;

  static EventBus &instance() {
    static EventBus bus;
    return bus;
  }

  /** Subscribe to an event. Returns ID for unsubscribe. */
  ListenerId subscribe(Event event, Listener listener) {
    const juce::ScopedLock sl(lock);
    int id = nextId++;
    listeners[event].push_back({id, std::move(listener)});
    return id;
  }

  /** Unsubscribe by ID. */
  void unsubscribe(Event event, ListenerId id) {
    const juce::ScopedLock sl(lock);
    auto &list = listeners[event];
    list.erase(std::remove_if(list.begin(), list.end(),
                              [id](const Entry &e) { return e.id == id; }),
               list.end());
  }

  /** Emit an event with optional data. */
  void emit(Event event, const juce::var &data = {}) {
    std::vector<Listener> toCall;
    {
      const juce::ScopedLock sl(lock);
      for (const auto &entry : listeners[event]) {
        toCall.push_back(entry.listener);
      }
    }
    for (auto &listener : toCall) {
      listener(data);
    }
  }

  /** Emit on message thread (safe from audio thread). */
  void emitAsync(Event event, const juce::var &data = {}) {
    juce::MessageManager::callAsync([this, event, data] { emit(event, data); });
  }

private:
  EventBus() = default;

  struct Entry {
    ListenerId id;
    Listener listener;
  };
  std::map<Event, std::vector<Entry>> listeners;
  juce::CriticalSection lock;
  int nextId = 1;
};
