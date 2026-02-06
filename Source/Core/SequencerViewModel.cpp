/*
  ==============================================================================
    Source/Core/SequencerViewModel.cpp
  ==============================================================================
*/
#include "SequencerViewModel.h"
#include "BridgeContext.h"
#include "../Audio/AudioEngine.h"
#include "../UI/Panels/SequencerPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

SequencerViewModel::SequencerViewModel(BridgeContext &ctx) : context(&ctx) {}

void SequencerViewModel::updateData() {
  if (!context->engine)
    return;
  int n = context->getNumSequencerSlots();
  for (int slot = 0; slot < n; ++slot) {
    SequencerPanel *seq = context->getSequencer(slot);
    if (seq)
      context->engine->updateSequencerData(slot, seq->getEngineSnapshot());
  }
}

void SequencerViewModel::setRoll(int div) {
  if (context->engine)
    context->engine->setRoll(div);
}

void SequencerViewModel::setTimeSignature(int num, int den) {
  if (context->engine)
    context->engine->setTimeSignature(num, den);
}

void SequencerViewModel::setSwing(float fraction) {
  if (context->engine)
    context->engine->setSwing(fraction);
}

void SequencerViewModel::setSequencerChannel(int ch) {
  if (context->engine)
    context->engine->setSequencerChannel(0, ch);
}

void SequencerViewModel::setSequencerChannel(int slot, int ch) {
  if (context->engine && slot >= 0 && slot < context->getNumSequencerSlots())
    context->engine->setSequencerChannel(slot, ch);
}

void SequencerViewModel::setMomentaryLoopSteps(int steps) {
  if (context->engine)
    context->engine->setMomentaryLoopSteps(steps);
}

void SequencerViewModel::requestExport() {
  if (!context->engine || !context->sequencer)
    return;
  context->sequencer->setExportBpm(context->engine->getBpm());
  auto chooser = std::make_shared<juce::FileChooser>(
      "Export Sequence as MIDI", juce::File(), "*.mid");
  chooser->launchAsync(juce::FileBrowserComponent::saveMode,
                      [this, chooser](const juce::FileChooser &fc) {
                        auto result = fc.getResult();
                        if (result != juce::File() && context->sequencer)
                          context->sequencer->exportToMidi(result);
                      });
}

void SequencerViewModel::randomizeCurrentPage() {
  if (context->sequencer)
    context->sequencer->randomizeCurrentPage();
}
