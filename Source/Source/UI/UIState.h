/*
  ==============================================================================
    Source/UI/UIState.h
    Role: Centralized UI state with observable pattern for view synchronization
  ==============================================================================
*/
#pragma once
#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <vector>

class UIState {
public:
  // View modes
  enum class ViewMode { Edit, Play, Mixer, Performance };
  
  // Render modes
  enum class RenderMode { Software, OpenGL, Metal, Vulkan };
  
  // State snapshot (immutable, copyable)
  struct Snapshot {
    ViewMode currentView = ViewMode::Edit;
    RenderMode renderMode = RenderMode::Software;
    bool midiLearnActive = false;
    bool isPlaying = false;
    bool isRecording = false;
    double currentBeat = 0.0;
    double bpm = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    bool metronomeEnabled = false;
    bool countInEnabled = false;
    float uiScale = 1.0f;
    bool reducedRefreshMode = false;
    bool gpuAvailable = true;
  };
  
  // Observer callback type
  using Observer = std::function<void(const Snapshot&)>;
  
  UIState() = default;
  
  // Atomic getters (lock-free reads)
  Snapshot getSnapshot() const {
    const juce::SpinLock::ScopedLockType lock(stateLock);
    return currentState;
  }
  
  ViewMode getCurrentView() const {
    const juce::SpinLock::ScopedLockType lock(stateLock);
    return currentState.currentView;
  }
  
  RenderMode getRenderMode() const {
    const juce::SpinLock::ScopedLockType lock(stateLock);
    return currentState.renderMode;
  }
  
  bool isMidiLearnActive() const {
    const juce::SpinLock::ScopedLockType lock(stateLock);
    return currentState.midiLearnActive;
  }
  
  bool isPlaying() const {
    const juce::SpinLock::ScopedLockType lock(stateLock);
    return currentState.isPlaying;
  }
  
  // Atomic setters (notify observers)
  void setCurrentView(ViewMode mode) {
    {
      const juce::SpinLock::ScopedLockType lock(stateLock);
      if (currentState.currentView == mode)
        return;
      currentState.currentView = mode;
    }
    notifyObservers();
  }
  
  void setRenderMode(RenderMode mode) {
    {
      const juce::SpinLock::ScopedLockType lock(stateLock);
      if (currentState.renderMode == mode)
        return;
      currentState.renderMode = mode;
    }
    notifyObservers();
  }
  
  void setMidiLearnActive(bool active) {
    {
      const juce::SpinLock::ScopedLockType lock(stateLock);
      if (currentState.midiLearnActive == active)
        return;
      currentState.midiLearnActive = active;
    }
    notifyObservers();
  }
  
  void setPlaying(bool playing) {
    {
      const juce::SpinLock::ScopedLockType lock(stateLock);
      if (currentState.isPlaying == playing)
        return;
      currentState.isPlaying = playing;
    }
    notifyObservers();
  }
  
  void setRecording(bool recording) {
    {
      const juce::SpinLock::ScopedLockType lock(stateLock);
      if (currentState.isRecording == recording)
        return;
      currentState.isRecording = recording;
    }
    notifyObservers();
  }
  
  void setCurrentBeat(double beat) {
    {
      const juce::SpinLock::ScopedLockType lock(stateLock);
      currentState.currentBeat = beat;
    }
    // Don't notify for beat changes (too frequent)
  }
  
  void setBPM(double bpm) {
    {
      const juce::SpinLock::ScopedLockType lock(stateLock);
      if (std::abs(currentState.bpm - bpm) < 0.01)
        return;
      currentState.bpm = bpm;
    }
    notifyObservers();
  }
  
  void setUIScale(float scale) {
    {
      const juce::SpinLock::ScopedLockType lock(stateLock);
      if (std::abs(currentState.uiScale - scale) < 0.01f)
        return;
      currentState.uiScale = scale;
    }
    notifyObservers();
  }
  
  void setReducedRefreshMode(bool enabled) {
    {
      const juce::SpinLock::ScopedLockType lock(stateLock);
      if (currentState.reducedRefreshMode == enabled)
        return;
      currentState.reducedRefreshMode = enabled;
    }
    notifyObservers();
  }
  
  void setGPUAvailable(bool available) {
    {
      const juce::SpinLock::ScopedLockType lock(stateLock);
      if (currentState.gpuAvailable == available)
        return;
      currentState.gpuAvailable = available;
    }
    notifyObservers();
  }
  
  // Observer management
  void addObserver(Observer observer) {
    const juce::SpinLock::ScopedLockType lock(observerLock);
    observers.push_back(std::move(observer));
  }
  
  void clearObservers() {
    const juce::SpinLock::ScopedLockType lock(observerLock);
    observers.clear();
  }
  
  // Singleton access
  static UIState& getInstance() {
    static UIState instance;
    return instance;
  }
  
private:
  mutable juce::SpinLock stateLock;
  mutable juce::SpinLock observerLock;
  Snapshot currentState;
  std::vector<Observer> observers;
  
  void notifyObservers() {
    Snapshot snapshot;
    std::vector<Observer> toNotify;
    {
      const juce::SpinLock::ScopedLockType lock(stateLock);
      snapshot = currentState;
    }
    {
      const juce::SpinLock::ScopedLockType lock(observerLock);
      toNotify = observers;
    }
    juce::MessageManager::callAsync([toNotify, snapshot]() {
      for (auto& o : toNotify)
        if (o)
          o(snapshot);
    });
  }
  
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UIState)
};
