#pragma once
#include "../Audio/ClockSmoother.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#if JUCE_WINDOWS
#include <avrt.h>
#include <mmsystem.h>
#include <windows.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "avrt.lib")
#elif JUCE_LINUX
#include <pthread.h>
#include <sched.h>
#elif JUCE_MAC
#include <pthread.h>
#include <sys/qos.h>
#endif

class ClockWorker : public juce::Thread {
public:
  ClockWorker(ClockSmoother &s) : Thread("MIDI_Clock_Worker"), smoother(s) {
#if JUCE_WINDOWS
    timeBeginPeriod(1);
#endif
    startThread(juce::Thread::Priority::highest);
  }

  ~ClockWorker() override {
    signalThreadShouldExit();
    stopThread(2000);
#if JUCE_WINDOWS
    timeEndPeriod(1);
#endif
  }

  void run() override {
    try {
      setPriority(juce::Thread::Priority::highest);
#if JUCE_WINDOWS
      DWORD taskIndex = 0;
      (void)AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
#elif JUCE_LINUX
      struct sched_param param;
      param.sched_priority = sched_get_priority_max(SCHED_FIFO);
      pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
#elif JUCE_MAC
      pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

      while (!threadShouldExit()) {
        double now = juce::Time::getMillisecondCounterHiRes();

        if (isSendingClock) {
          // Get BPM from the smoother (External Slave) or Link
          double targetBpm =
              smoother.getIsLocked() ? smoother.getBpm() : internalBpm;

          // PLL Logic: If we are slightly ahead/behind the external pulse
          double error = smoother.getLastPulseTime() - nextTickTime;
          double driftCorrection = error * 0.02; // 2% correction per tick

          double msPerPulse = 60000.0 / (targetBpm * 24.0);

          // HYBRID WAIT
          double timeToNext = nextTickTime - now;

          if (timeToNext > 2.0) {
            // Sleep for bulk of time
            wait((int)(timeToNext - 1.0));
          } else if (timeToNext > 0) {
            // Cap spin-wait to ~1ms to avoid burning CPU; then short sleep and recheck
            double spinDeadline = now + 1.0;
            while (juce::Time::getMillisecondCounterHiRes() < nextTickTime) {
              if (juce::Time::getMillisecondCounterHiRes() >= spinDeadline)
                wait(1);
              else
                juce::Thread::yield();
            }
          }

          if (now >= nextTickTime) {
            if (onClockPulse)
              onClockPulse();
            nextTickTime += (msPerPulse - driftCorrection);
          }
        } else {
          wait(50); // Sleep long if idle
        }
      }
    } catch (...) {
      juce::Logger::writeToLog("CRITICAL: ClockWorker thread crashed!");
    }
  }

  void setBpm(double bpm) { internalBpm = bpm; }
  void setClockEnabled(bool en) { isSendingClock = en; }

  std::function<void()> onClockPulse;

private:
  ClockSmoother &smoother;
  double internalBpm = 120.0;
  double nextTickTime = 0.0;
  bool isSendingClock = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClockWorker)
};
