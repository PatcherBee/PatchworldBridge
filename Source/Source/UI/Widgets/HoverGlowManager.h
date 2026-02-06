/*
  ==============================================================================
    Source/UI/Widgets/HoverGlowManager.h
    Role: Consolidates all hover glow animations into a single 30Hz timer
          instead of per-widget 60Hz timers. Reduces CPU overhead significantly.
  ==============================================================================
*/
#pragma once
#include "../../Core/TimerHub.h"
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

class HoverGlowWidget; // Forward declaration

/**
 * Singleton manager for hover glow animations.
 * Instead of each HoverGlowButton/Slider having its own 60Hz timer,
 * this manager runs a single 30Hz timer and iterates all registered widgets.
 */
class HoverGlowManager {
public:
  static HoverGlowManager &instance() {
    static HoverGlowManager mgr;
    return mgr;
  }

  void registerWidget(HoverGlowWidget *w) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(widgets_.begin(), widgets_.end(), w) == widgets_.end())
      widgets_.push_back(w);
  }

  void unregisterWidget(HoverGlowWidget *w) {
    std::lock_guard<std::mutex> lock(mutex_);
    widgets_.erase(std::remove(widgets_.begin(), widgets_.end(), w),
                   widgets_.end());
  }

private:
  HoverGlowManager() {
    hubId_ = "HoverGlowManager";
    // 30Hz is plenty smooth for hover effects and saves 50% vs 60Hz
    TimerHub::instance().subscribe(
        hubId_, [this] { tick(); }, TimerHub::Medium30Hz);
  }

  ~HoverGlowManager() { TimerHub::instance().unsubscribe(hubId_); }

  void tick();

  std::vector<HoverGlowWidget *> widgets_;
  std::mutex mutex_;
  std::string hubId_;
};

/**
 * Interface for widgets that want managed hover glow.
 * Derive from this and implement tickGlow().
 */
class HoverGlowWidget {
public:
  virtual ~HoverGlowWidget() = default;

  /** Called by HoverGlowManager at 30Hz. Return true if repaint needed. */
  virtual bool tickGlow() = 0;

  /** Override to check if widget should animate (e.g., isVisible). */
  virtual bool shouldAnimate() const { return true; }
};

// Implementation of tick() after HoverGlowWidget is defined
inline void HoverGlowManager::tick() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto *w : widgets_) {
    if (w && w->shouldAnimate())
      w->tickGlow();
  }
}
