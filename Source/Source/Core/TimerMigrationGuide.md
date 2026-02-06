# UI Timer Consolidation Guide

## Overview
Consolidate individual `juce::Timer` instances into `TimerHub` for better CPU efficiency and reduced timer overhead.

## Current Status
- **TimerHub exists**: `Source/Core/TimerHub.h`
- **Already integrated**: StatusBar subscribes at 10Hz
- **Master tick**: SystemController drives TimerHub in its timer callback
- **Migration complete**: All periodic UI/component timers use TimerHub (ModuleWindow, TrafficMonitor, LinkBeatIndicator, DiagnosticOverlay, MixerPanel, Indicators, TimelineComponent, HoverGlowButton, AutoSaver, AudioWatchdog, DeferredDeleter, MidiDeviceService, LatencyCalibrator, GamepadService, AppState, OscManager). No `juce::Timer` inheritance remains in UI components.
- **One-shot delays**: `juce::Timer::callAfterDelay(ms, callback)` is intentionally kept for one-off delays (e.g. SpliceEditor repaint throttle, ChordGeneratorPanel, LatencyCalibrator); these are not periodic timers and do not need TimerHub.

## Components Using Individual Timers (To Migrate) — DONE

### High Priority (60Hz updates)
1. **ModuleWindow** - Focus glow animation, drag state
2. **TrafficMonitor** - Real-time network activity
3. **LinkBeatIndicator** - Beat sync animation
4. **DiagnosticOverlay** - Performance metrics

### Medium Priority (30Hz updates)
5. **MixerPanel** - VU meter updates
6. **Indicators** (various) - LED/status indicators

### Low Priority (15Hz or less)
7. **AutoSaver** - Periodic state save
8. **AudioWatchdog** - Health check
9. **DeferredDeleter** - Cleanup tasks
10. **MidiDeviceService** - Device polling
11. **GamepadService** - Input polling
12. **LatencyCalibrator** - Calibration updates

## Migration Pattern

### Before (Individual Timer)
```cpp
class MyComponent : public juce::Component, public juce::Timer {
public:
  MyComponent() {
    startTimerHz(60); // 60 FPS
  }
  
  void timerCallback() override {
    // Update logic
    repaint();
  }
};
```

### After (TimerHub Subscriber)
```cpp
class MyComponent : public juce::Component {
public:
  MyComponent() {
    TimerHub::instance().subscribe(
      "MyComponent_" + juce::String((int64_t)this),
      [this]() {
        // Update logic
        if (isVisible())
          repaint();
      },
      TimerHub::High60Hz
    );
  }
  
  ~MyComponent() override {
    TimerHub::instance().unsubscribe("MyComponent_" + juce::String((int64_t)this));
  }
};
```

## Benefits
- **Reduced CPU**: Single master timer instead of 10-15 individual timers
- **Better scheduling**: Prioritized update buckets (60Hz, 30Hz, 15Hz, 10Hz, 1Hz)
- **Easier profiling**: Centralized tick point for performance monitoring
- **Conditional updates**: Easy to skip updates when not visible

## Implementation Steps
1. Identify all `juce::Timer` usages (grep for `: public juce::Timer`)
2. Convert high-frequency timers first (60Hz components)
3. Test each migration individually
4. Remove `juce::Timer` inheritance after migration
5. Verify no performance regression

## Audit: Current TimerHub Subscribers (as of last audit)
- **SystemController**: master tick (drives hub), UI watchdog, crash recovery
- **ModuleWindow**: focus/close button visibility (10Hz)
- **MixerPanel**: resize settle (10Hz), VU (10Hz when visible)
- **TrafficMonitor**, **LinkBeatIndicator**, **DiagnosticOverlay**: log/beat/diag updates
- **Indicators**, **TimelineComponent**: status/timeline animation
- **HoverGlowButton**: hover glow (per-button)
- **SignalPathLegend**: legend refresh
- **MainComponent**: repaint coordinator / tick (per instance)
- **AudioEngine**: Link watchdog
- **AppState**: auto-save tick
- **MidiDeviceService**: device reconcile
- **LatencyCalibrator**: timeout (one-shot style)
- **AudioWatchdog**, **AutoSaver**, **DeferredDeleter**: health/save/cleanup
- **OscManager**: zeroconf tick
- **GamepadService**: input poll

No further consolidation required unless adding new timers; prefer subscribing to TimerHub over new `juce::Timer` instances.

## Notes
- TimerHub is already driven by SystemController's master timer
- Use unique IDs for subscriptions (e.g., component name + pointer address)
- Always unsubscribe in destructor to prevent dangling callbacks
- Consider visibility checks in callbacks to skip hidden components
