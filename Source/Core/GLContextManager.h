/*
  ==============================================================================
    Source/Core/GLContextManager.h
    Role: OpenGL context lifecycle state (lost/recovery) for MainComponent.
  ==============================================================================
*/
#pragma once

#include <atomic>

struct GLContextManager {
  enum class State {
    Uninitialized,
    Initializing,
    Ready,
    Lost,
    Recovering
  };

  std::atomic<State> state{State::Uninitialized};

  void markReady() { state.store(State::Ready, std::memory_order_release); }
  void markLost() { state.store(State::Lost, std::memory_order_release); }
  void markRecovering() {
    state.store(State::Recovering, std::memory_order_release);
  }
  void markUninitialized() {
    state.store(State::Uninitialized, std::memory_order_release);
  }

  bool isReady() const {
    return state.load(std::memory_order_acquire) == State::Ready;
  }
  bool isLost() const {
    return state.load(std::memory_order_acquire) == State::Lost;
  }
};
