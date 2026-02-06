/*
  ==============================================================================
    Source/Tests/AirlockTest.h
    Role: Stress test for OscAirlock (no deadlock/leak validation)
  ==============================================================================
*/
#pragma once
#include "../Network/OscAirlock.h"
#include "../Audio/OscTypes.h"
#include <atomic>
#include <thread>

struct AirlockTest {
  static void runStressTest() {
    OscAirlock airlock;
    std::atomic<bool> running{true};
    std::atomic<int> received{0};
    const int iterations = 100000;

    std::thread consumer([&] {
      while (running.load(std::memory_order_relaxed)) {
        airlock.process([&](const BridgeEvent &e) {
          (void)e;
          received.fetch_add(1, std::memory_order_relaxed);
        });
        std::this_thread::yield();
      }
    });

    for (int i = 0; i < iterations; ++i) {
      while (!airlock.push(
          BridgeEvent(EventType::NoteOn, EventSource::EngineSequencer, 1, 60,
                      1.0f))) {
        std::this_thread::yield();
      }
    }

    while (received.load(std::memory_order_relaxed) < iterations)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

    running.store(false, std::memory_order_release);
    consumer.join();

    jassert(received.load() == iterations);
  }
};
