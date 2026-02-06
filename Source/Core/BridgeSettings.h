#pragma once
#include <atomic>
#include <cstdint>

/**
 * BridgeSettings
 *
 * Lightweight container for atomic settings accessed by multiple threads
 * (UI, Audio, Network). This header MUST NOT include any other project
 * headers to avoid circular dependencies.
 */
struct BridgeSettings {
  // Latency & Quality
  std::atomic<bool> lookaheadBypass{false};
  std::atomic<float> networkLookaheadMs{30.0f};

  // Safety & Filtering
  std::atomic<bool> echoGateActive{true};
  std::atomic<bool> jitterFilterActive{true};
  std::atomic<bool> bundleOsc{true};

  // Routing
  std::atomic<bool> blockMidiOut{false};
  std::atomic<bool> blockOscOut{false};
  std::atomic<bool> midiScaling127{false};

  // UI Feedback
  std::atomic<float> masterLevel{1.0f};
};

/**
 * EngineShadowState
 *
 * Thread-safe "mirror" of the playback engine's state.
 * Updated by the Audio Thread, read by UI/Network for sync visualization.
 */
struct EngineShadowState {
  std::atomic<double> bpm{120.0};
  std::atomic<bool> isPlaying{false};
  std::atomic<double> currentBeat{0.0};
  std::atomic<int> signatureNumerator{4};
  std::atomic<int> signatureDenominator{4};

  // Loop points
  std::atomic<double> loopStartBeat{0.0};
  std::atomic<double> loopEndBeat{4.0};
  std::atomic<bool> isLooping{true};
};
