/*
  ==============================================================================
    Source/Core/CommandDispatcher.cpp
    Implementation of CommandDispatcher::trigger()
  ==============================================================================
*/
#include "CommandDispatcher.h"

// Include FULL headers here so we can see the methods
#include "../Audio/AudioEngine.h"
#include "../Audio/MidiRouter.h"
#include "../Audio/PlaybackController.h"
#include "../Network/OscManager.h"
#include "../UI/Panels/MixerPanel.h"
#include "../UI/Panels/SequencerPanel.h"
#include "MixerViewModel.h"
#include "SequencerViewModel.h"

void CommandDispatcher::trigger(CommandID cmd, float value, int channel) {
  switch (cmd) {
  case CommandID::TransportPlay:
    if (engine)
      engine->play();
    break;
  case CommandID::TransportStop:
    if (engine)
      engine->stop();
    break;
  case CommandID::TransportReset:
    if (engine)
      engine->resetTransport();
    break;
  case CommandID::Panic:
    if (router)
      router->sendPanic();
    break;
  case CommandID::SetBpm:
    if (engine && value > 0)
      engine->setBpm(value);
    break;
  case CommandID::SetScaleQuantization:
    if (router)
      router->isQuantizationEnabled = (value > 0.5f);
    break;
  case CommandID::SequencerRandomize:
    if (sequencerViewModel)
      sequencerViewModel->randomizeCurrentPage();
    break;
  case CommandID::MixerMuteToggle:
    if (mixerViewModel && channel >= 0 && channel < 16) {
      bool current = mixerViewModel->isChannelActive(channel);
      mixerViewModel->setActive(channel, !current);
    }
    if (oscManager && channel >= 0 && channel < 16)
      oscManager->sendFloat("/mix/" + juce::String(channel + 1) + "/mute",
                            1.0f);
    break;
  case CommandID::PlaylistNext:
    if (playback)
      playback->skipToNext();
    break;
  case CommandID::PlaylistPrev:
    if (playback)
      playback->skipToPrevious();
    break;
  default:
    break;
  }
}
