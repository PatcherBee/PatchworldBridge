/*
  ==============================================================================
    Source/Core/ThreadingConfig.h
    Role: User-configurable threading options for audio, UI, and worker threads.
    ARM/Apple Silicon: uses hardware_concurrency() and reserves cores for audio/UI.
  ==============================================================================
*/
#pragma once

#include <atomic>
#include <thread>

struct ThreadingConfig {
  enum Mode { SingleThread, MultiCore, Adaptive };

  // Thread mode
  std::atomic<Mode> mode{Adaptive};

  // Worker thread configuration
  int maxWorkerThreads = 1;
  bool enableHyperthreading = true; // Use logical cores (SMT/HT); on ARM Mac, hw is P+E cores

  // Audio thread priority (0=normal, 1=high, 2=realtime)
  enum class Priority { Normal, High, Realtime };
  std::atomic<Priority> audioThreadPriority{Priority::High};

  // UI thread affinity (pin to specific core, -1=auto)
  std::atomic<int> uiThreadAffinity{-1};

  // Audio thread affinity (pin to specific core, -1=auto)
  std::atomic<int> audioThreadAffinity{-1};

  void setMaxWorkerThreads(int n) {
    maxWorkerThreads = juce::jmax(1, n);
  }

  void setAudioThreadPriority(Priority p) {
    audioThreadPriority.store(p, std::memory_order_relaxed);
  }

  void setUIThreadAffinity(int coreId) {
    uiThreadAffinity.store(coreId, std::memory_order_relaxed);
  }

  void setAudioThreadAffinity(int coreId) {
    audioThreadAffinity.store(coreId, std::memory_order_relaxed);
  }

  void setHyperthreadingEnabled(bool enabled) {
    enableHyperthreading = enabled;
  }

  /** Reserved cores for audio and UI (not used by worker pool). */
  static constexpr int kReservedCores = 2;

  int detectOptimalThreads() const {
    int hw = (int)std::thread::hardware_concurrency();

    // If hyperthreading disabled, use physical cores only (estimate: hw/2; not used on ARM Mac)
    if (!enableHyperthreading) {
      hw = std::max(1, hw / 2);
    }

    // Leave cores for audio and UI (ARM/Apple Silicon: P-cores + E-cores; hw is total)
    int workers = std::max(1, hw - kReservedCores);

#if defined(__aarch64__) || defined(__arm64__) || (defined(JUCE_MAC) && defined(JUCE_ARM))
    // Apple Silicon: cap workers to avoid overloading E-cores (M1/M2 often 8–10 cores)
    workers = std::min(workers, 8);
#endif
    return workers;
  }

  int getEffectiveWorkerCount() const {
    Mode m = mode.load(std::memory_order_relaxed);
    if (m == SingleThread)
      return 0;
    if (m == Adaptive)
      return detectOptimalThreads();
    return juce::jmin(maxWorkerThreads, detectOptimalThreads());
  }
  
  // Get priority as JUCE thread priority value
  int getJUCEAudioPriority() const {
    switch (audioThreadPriority.load(std::memory_order_relaxed)) {
      case Priority::Normal: return 5;
      case Priority::High: return 7;
      case Priority::Realtime: return 10;
      default: return 7;
    }
  }
  
  // Singleton access
  static ThreadingConfig& getInstance() {
    static ThreadingConfig instance;
    return instance;
  }
};
