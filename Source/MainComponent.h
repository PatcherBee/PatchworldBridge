/*
  ==============================================================================
    Source/MainComponent.h
    Status: FIXED (Restored Missing Identifiers & Structs)
  ==============================================================================
*/
#pragma once
#include "SubComponents.h"
#include <JuceHeader.h>
#include <ableton/Link.hpp>

class MainComponent : public juce::AudioAppComponent,
                      public juce::FileDragAndDropTarget,
                      public juce::MidiInputCallback,
                      public juce::MidiKeyboardState::Listener,
                      public juce::OSCReceiver::Listener<
                          juce::OSCReceiver::MessageLoopCallback>,
                      public juce::KeyListener,
                      public juce::ValueTree::Listener,
                      public juce::Timer,
                      public juce::HighResolutionTimer {
public:
  MainComponent();
  ~MainComponent() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &) override;
  bool keyPressed(const juce::KeyPress &, Component *) override;
  bool isInterestedInFileDrag(const juce::StringArray &) override;
  void filesDropped(const juce::StringArray &, int, int) override;

  // AudioAppComponent overrides
  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void
  getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
  void releaseResources() override;

  juce::String getLocalIPAddress();

private:
  ableton::Link *link;
  double quantum = 4.0;
  juce::UndoManager undoManager;
  juce::ValueTree parameters{"Params"};
  juce::CachedValue<double> bpmVal;

  bool isSimpleMode = false;
  enum class AppView { Dashboard, Control, OSC_Config, Help };
  AppView currentView = AppView::Dashboard;

  juce::MidiKeyboardState keyboardState;

  // GUI
  juce::ImageComponent logoView;
  juce::TextButton btnDash{"Dashboard"}, btnCtrl{"Control"},
      btnOscCfg{"OSC Config"}, btnHelp{"Help"}, btnPanic;
  juce::ToggleButton btnRetrigger{"Retrig"}, btnGPU{"GPU"};
  juce::Label lblLocalIpHeader, lblLocalIpDisplay, lblTempo, lblLatency,
      lblNoteDelay, lblArp, lblArpBpm, lblArpVel, lblIn, lblOut, lblCh, lblIp,
      lblPOut, lblPIn;
  juce::GroupComponent grpNet{{}, "Network"}, grpIo{{}, "MIDI"},
      grpArp{{}, "Arp"};
  juce::TextEditor edIp, edPOut, edPIn, helpText;
  juce::Viewport helpViewport;
  juce::TextButton btnConnect{"Connect"}, btnPlay{"Play"}, btnStop{"Stop"},
      btnPrev{"<"}, btnSkip{">"}, btnResetFile{"Reset"}, btnClearPR{"Clear"},
      btnResetBPM{"Reset BPM"}, btnTapTempo{"Tap Tempo"}, btnPrOctUp{"Oct +"},
      btnPrOctDown{"Oct -"};
  juce::Slider tempoSlider, latencySlider, sliderNoteDelay, sliderArpSpeed,
      sliderArpVel;
  juce::ComboBox cmbQuantum, cmbMidiIn, cmbMidiOut, cmbMidiCh, cmbArpPattern;

  // Toggles
  juce::ToggleButton btnLinkToggle{"Link"}, btnArp{"Latch"}, btnArpSync{"Sync"};
  juce::ToggleButton btnPreventBpmOverride{"Lock BPM"};
  juce::ToggleButton btnBlockMidiOut{"Block Out"};

  ConnectionLight ledConnect;
  PhaseVisualizer phaseVisualizer;

  // Custom wrappers defined in SubComponents.h
  CustomKeyboard horizontalKeyboard{
      keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard};
  CustomKeyboard verticalKeyboard{
      keyboardState, juce::MidiKeyboardComponent::verticalKeyboardFacingRight};

  ComplexPianoRoll trackGrid{keyboardState};
  TrafficMonitor logPanel;
  MidiPlaylist playlist;
  StepSequencer sequencer;
  MixerContainer mixer;
  juce::Viewport mixerViewport;
  OscAddressConfig oscConfig;
  juce::Viewport oscViewport;
  ControlPage controlPage;

  // Logic
  std::unique_ptr<juce::MidiInput> midiInput;
  std::unique_ptr<juce::MidiOutput> midiOutput;
  juce::OSCSender oscSender;
  juce::OSCReceiver oscReceiver;
  bool isOscConnected = false;
  juce::MidiMessageSequence playbackSeq;
  double sequenceLength = 0, currentFileBpm = 0;
  int playbackCursor = 0;
  bool isPlaying = false;
  juce::CriticalSection midiLock;

  // Arp State
  juce::Array<int> heldNotes;
  std::vector<int> noteArrivalOrder;
  int arpNoteIndex = 0;

  int lastNumPeers = -1, virtualOctaveShift = 0, tapCounter = 0,
      stepSeqIndex = -1, pianoRollOctaveShift = 0;
  std::vector<double> tapTimes;
  std::set<int> activeChannels;
  juce::OpenGLContext openGLContext;

  double lastProcessedBeat = -1.0;
  double transportStartBeat = 0.0;
  double ticksPerQuarterNote = 960.0;
  double currentSampleRate = 44100.0;
  bool pendingSyncStart = false;

  int linkRetryCounter = 0;
  bool startupRetryActive = true;
  bool isHandlingOsc = false;

  juce::Slider vol1Simple, vol2Simple;
  juce::TextEditor txtVol1Osc, txtVol2Osc;

  struct ActiveNote {
    int channel;
    int note;
    double releaseTime;
  };
  std::vector<ActiveNote> activeVirtualNotes;

  // --- RESTORED: This was causing the C2065 error ---
  struct ScheduledNote {
    int channel;
    int note;
    double releaseTimeMs;
  };
  std::vector<ScheduledNote> scheduledNotes;

  // --- RESTORED: This was causing the C3861 error ---
  double getDurationFromVelocity(float velocity0to1);

  void updateVisibility();
  void setView(AppView v);
  void sendPanic();
  void loadMidiFile(juce::File f);
  void stopPlayback();
  void takeSnapshot();
  void sendSplitOscMessage(const juce::MidiMessage &m,
                           int overrideChannel = -1);
  int matchOscChannel(const juce::String &pattern,
                      const juce::String &incoming);
  int getSelectedChannel() const;
  void toggleChannel(int ch, bool active);
  void performUndo();
  void performRedo();

  void handleIncomingMidiMessage(juce::MidiInput *,
                                 const juce::MidiMessage &) override;
  void handleNoteOn(juce::MidiKeyboardState *, int, int, float) override;
  void handleNoteOff(juce::MidiKeyboardState *, int, int, float) override;
  void valueTreePropertyChanged(juce::ValueTree &,
                                const juce::Identifier &) override;
  void oscMessageReceived(const juce::OSCMessage &) override;
  void timerCallback() override;
  void hiResTimerCallback() override;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};