#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Standard Library First
#include <atomic>
#include <cstdint>
#include <vector>

// JUCE Headers
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

// LINUX HEADER FIX
#if JUCE_LINUX
#include <pthread.h>
#include <sched.h>
#endif

#if JUCE_WINDOWS
#include <windows.h>
#include <avrt.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#elif JUCE_MAC || JUCE_LINUX
#include <pthread.h>
#endif

#if JUCE_MAC
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <sys/qos.h>
#endif

struct PlatformGuard {
  PlatformGuard() {
    juce::FloatVectorOperations::disableDenormalisedNumberSupport();

    // Run once per thread: boost audio/realtime priority
    thread_local static bool configured = false;
    if (!configured) {
      // Use platform-specific priority boosting
#if JUCE_WINDOWS
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
      DWORD taskIndex = 0;
      if (AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex))
        (void)taskIndex;  // MMCSS: Pro Audio profile for lowest latency
#elif JUCE_MAC
      pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif JUCE_LINUX
      struct sched_param param;
      param.sched_priority = sched_get_priority_max(SCHED_FIFO);
      pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
#endif
      configured = true;
    }
  }

  juce::ScopedNoDenormals noDenormals;

  static inline void setThreadAffinity(uint32_t coreMask = 0x0F) {
#if JUCE_WINDOWS
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)coreMask);
#elif JUCE_MAC
    // macOS (Mach API)
    thread_affinity_policy_data_t policy = {(integer_t)coreMask};
    thread_policy_set(pthread_mach_thread_np(pthread_self()),
                      THREAD_AFFINITY_POLICY, (thread_policy_t)&policy,
                      THREAD_AFFINITY_POLICY_COUNT);
#elif JUCE_LINUX
    // Linux (Pthread API)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i < 32; ++i) { // Support up to 32 cores in mask
      if ((coreMask >> i) & 1)
        CPU_SET(i, &cpuset);
    }
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
  }

  struct PlatformTimer {
    static void enableHighPrecision() {
#if JUCE_WINDOWS
      timeBeginPeriod(1); // 1ms resolution
#endif
    }

    static void disableHighPrecision() {
#if JUCE_WINDOWS
      timeEndPeriod(1);
#endif
    }

    static double getTimeMs() {
#if JUCE_WINDOWS
      LARGE_INTEGER freq, count;
      QueryPerformanceFrequency(&freq);
      QueryPerformanceCounter(&count);
      return (double)count.QuadPart / (double)freq.QuadPart * 1000.0;
#else
      return juce::Time::getMillisecondCounterHiRes();
#endif
    }
  };
};