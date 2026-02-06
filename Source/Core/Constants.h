/*
  ==============================================================================
    Source/Core/Constants.h
    Role: Named constants to replace magic numbers (roadmap 9.3).
  ==============================================================================
*/
#pragma once

namespace Constants {
  // Tap Tempo
  constexpr int kMaxTapSamples = 4;
  constexpr double kTapTimeoutMs = 2000.0;

  // OSC Echo Prevention
  constexpr double kEchoWindowMs = 50.0;
  constexpr uint64_t kOscTypeFloat = 0x100000000ULL;
  constexpr uint64_t kOscTypeInt = 0x200000000ULL;

  // Timer Rates (frames at 60fps)
  constexpr int kTimer60Hz = 1;
  constexpr int kTimer30Hz = 2;
  constexpr int kTimer15Hz = 4;
  constexpr int kTimer10Hz = 6;
  constexpr int kTimer1Hz = 60;

  // UI
  constexpr int kDefaultBpm = 120;
  constexpr int kMinBpm = 20;
  constexpr int kMaxBpm = 300;
  constexpr float kDefaultNoteHeight = 12.0f;
  constexpr int kPianoKeyCount = 128;

  // Audio
  constexpr int kDefaultBufferSize = 512;
  constexpr int kDefaultSampleRate = 48000;
  constexpr int kMaxMidiChannels = 16;

  // Network
  constexpr int kDefaultOscPortOut = 8000;
  constexpr int kDefaultOscPortIn = 9000;
  constexpr int kNetworkTimeoutMs = 3000;

  // State Management
  constexpr int kMaxUndoSteps = 100;
  constexpr int kStatePoolSize = 8;
  constexpr int kAutoSaveIntervalMs = 30000;
}
