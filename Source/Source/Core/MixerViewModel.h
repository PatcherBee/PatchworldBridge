/*
  ==============================================================================
    Source/Core/MixerViewModel.h
    Role: MVC/MVP – ViewModel for mixer (reset, mute/solo state and commands).
    Decouples UI and CommandDispatcher from direct MixerPanel access.
  ==============================================================================
*/
#pragma once

class BridgeContext;

/**
 * MixerViewModel: Exposes mixer commands and state for UI and command binding.
 * Use from SystemController (reset button) and CommandDispatcher (MixerMuteToggle)
 * instead of touching context.mixer directly.
 */
class MixerViewModel {
public:
  explicit MixerViewModel(BridgeContext &ctx);

  void reset();
  bool isChannelActive(int ch) const;
  void setActive(int ch, bool active);

private:
  BridgeContext *context = nullptr;
};
