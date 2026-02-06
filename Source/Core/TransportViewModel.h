/*
  ==============================================================================
    Source/Core/TransportViewModel.h
    Role: MVC/MVP – ViewModel for transport (play/stop state and commands).
    Decouples UI from direct engine/midiRouter access.
  ==============================================================================
*/
#pragma once

class BridgeContext;

/**
 * TransportViewModel: Exposes transport state and commands for UI binding.
 * Use from TransportPanel (or any transport UI) instead of touching
 * context.engine / context.midiRouter directly.
 */
class TransportViewModel {
public:
  explicit TransportViewModel(BridgeContext &ctx);

  bool isPlaying() const;
  double currentBeat() const;
  double currentBpm() const;

  void play();
  void stop();
  void togglePlay();

private:
  BridgeContext *context = nullptr;
};
