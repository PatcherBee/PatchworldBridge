/*
  ==============================================================================
    Source/Core/ThreadSafeUI.h
    Role: Run UI code from background threads safely (MessageManagerLock).
  ==============================================================================
*/
#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Run a functor on the message thread from the current (background) thread.
 * Blocks until the lock is gained, then runs F. Use when you must perform
 * UI updates synchronously from a worker thread (e.g. Thread::run(), audio callback).
 *
 * If the lock is not gained (e.g. message thread is blocked), the functor is not run.
 *
 * Prefer MessageManager::callAsync when you don't need to wait for the UI update.
 */
template <typename F>
void runOnMessageThreadIfLocked(F &&f) {
  juce::MessageManagerLock mml(juce::Thread::getCurrentThread());
  if (mml.lockWasGained())
    f();
}
