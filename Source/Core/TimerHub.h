/*
  ==============================================================================
    Source/Core/TimerHub.h
    Role: Centralized UI tick - consolidates per-component timers into
          master VBlank-driven updates. Reduces timer proliferation and CPU.
  ==============================================================================
*/
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

class TimerHub {
public:
  enum Priority {
    High60Hz = 1,      // ~60 Hz
    Medium30Hz = 2,    // ~30 Hz
    Low15Hz = 4,       // ~15 Hz
    Rate10Hz = 6,      // ~10 Hz (100ms)
    Rate5Hz = 12,      // ~5 Hz (log/indicators when idle)
    Rate2Hz = 30,      // ~2 Hz (500ms)
    Low1Hz = 60,       // ~1 Hz
    Rate0_5Hz = 120,   // ~0.5 Hz (2s)
    Rate0_33Hz = 180,  // ~0.33 Hz (3s)
    Rate0_2Hz = 300,      // ~0.2 Hz (5s)
    Rate0_017Hz = 3600,   // ~0.017 Hz (60s)
    Rate0_008Hz = 7200,   // ~0.008 Hz (120s)
  };

  void subscribe(const std::string &id, std::function<void()> callback,
                 Priority p = Medium30Hz) {
    int divisor = static_cast<int>(p);
    subscribers[id] = {std::move(callback), divisor > 0 ? divisor : 1};
    rebuildTickOrder();
  }

  void unsubscribe(const std::string &id) {
    subscribers.erase(id);
    rebuildTickOrder();
  }

  void tick() {
    ++frameCount;
    // Callbacks must not call subscribe/unsubscribe from within their callback.
    for (const Subscriber &sub : tickOrder_) {
      if (frameCount % sub.divisor != 0)
        continue;
      if (!sub.callback)
        continue;
      sub.callback();
    }
  }

  static TimerHub &instance() {
    static TimerHub hub;
    return hub;
  }

private:
  struct Subscriber {
    std::function<void()> callback;
    int divisor{1};
  };
  void rebuildTickOrder() {
    tickOrder_.clear();
    tickOrder_.reserve(subscribers.size());
    for (auto &kv : subscribers)
      tickOrder_.push_back(kv.second);
  }

  std::map<std::string, Subscriber> subscribers;
  std::vector<Subscriber> tickOrder_;
  int frameCount = 0;
};
