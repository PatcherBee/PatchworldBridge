/*
  ==============================================================================
    Source/Core/TransportViewModel.cpp
    Role: MVC/MVP – Transport ViewModel implementation.
  ==============================================================================
*/
#include "TransportViewModel.h"
#include "../Audio/AudioEngine.h"
#include "../Audio/PlaybackController.h"
#include "BridgeContext.h"

TransportViewModel::TransportViewModel(BridgeContext &ctx) : context(&ctx) {}

bool TransportViewModel::isPlaying() const {
  return context && context->engine && context->engine->getIsPlaying();
}

double TransportViewModel::currentBeat() const {
  return (context && context->engine) ? context->engine->getCurrentBeat() : 0.0;
}

double TransportViewModel::currentBpm() const {
  return (context && context->engine) ? context->engine->getBpm() : 120.0;
}

void TransportViewModel::play() {
  if (context && context->playbackController)
    context->playbackController->startPlayback();
}

void TransportViewModel::stop() {
  if (context && context->playbackController)
    context->playbackController->stopPlayback();
  if (context && context->engine)
    context->engine->resetTransport();
}

void TransportViewModel::togglePlay() {
  if (isPlaying())
    stop();
  else
    play();
}
