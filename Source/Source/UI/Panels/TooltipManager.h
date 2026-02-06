/*
  ==============================================================================
    Source/Components/TooltipManager.h
    Centralized Tooltip Management
  ==============================================================================
*/
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Forward declare MainComponent to avoid circular dependencies
class MainComponent;

class TooltipManager {
public:
  static void setupAllTooltips(MainComponent &main);

private:
  static void setupNavigationTooltips(MainComponent &main);
  static void setupTransportTooltips(MainComponent &main);
  static void setupMidiIOTooltips(MainComponent &main);
  static void setupArpTooltips(MainComponent &main);
  static void setupOscTooltips(MainComponent &main);
  static void setupLinkTooltips(MainComponent &main);
  static void setupRenderAndThreadingTooltips(MainComponent &main);
  static void setupMixerTooltips(MainComponent &main);
  static void setupKeyboardTooltips(MainComponent &main);
  static void setupLfoGeneratorTooltips(MainComponent &main);
  static void setupPerformanceTooltips(MainComponent &main);
  static void setupSequencerTooltips(MainComponent &main);
  static void setupControlTooltips(MainComponent &main);
  static void setupHeaderTooltips(MainComponent &main);
  static void setupTrafficMonitorTooltips(MainComponent &main);
  static void setupPlaylistTooltips(MainComponent &main);
};
