/*
  ==============================================================================
    Source/Core/MixerViewModel.cpp
  ==============================================================================
*/
#include "MixerViewModel.h"
#include "BridgeContext.h"
#include "../UI/Panels/MixerPanel.h"

MixerViewModel::MixerViewModel(BridgeContext &ctx) : context(&ctx) {}

void MixerViewModel::reset() {
  if (context && context->mixer)
    context->mixer->resetMapping(true);
}

bool MixerViewModel::isChannelActive(int ch) const {
  return context && context->mixer && context->mixer->isChannelActive(ch);
}

void MixerViewModel::setActive(int ch, bool active) {
  if (context && context->mixer)
    context->mixer->setActive(ch, active);
}
