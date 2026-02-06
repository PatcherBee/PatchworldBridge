/*
  ==============================================================================
    Source/Core/SequencerViewModel.h
    Role: MVC/MVP – ViewModel for step sequencer (engine sync and commands).
    Decouples UI from direct engine/sequencer access.
  ==============================================================================
*/
#pragma once

class BridgeContext;

/**
 * SequencerViewModel: Exposes sequencer→engine commands for UI binding.
 * Use from SystemController (bindPerformance) and CommandDispatcher (SequencerRandomize)
 * instead of touching context.engine / context.sequencer directly.
 */
class SequencerViewModel {
public:
  explicit SequencerViewModel(BridgeContext &ctx);

  void updateData();
  void setRoll(int div);
  void setTimeSignature(int num, int den);
  void setSwing(float fraction);
  void setSequencerChannel(int ch);
  void setSequencerChannel(int slot, int ch);
  void setMomentaryLoopSteps(int steps);
  void requestExport();
  void randomizeCurrentPage();

private:
  BridgeContext *context = nullptr;
};
