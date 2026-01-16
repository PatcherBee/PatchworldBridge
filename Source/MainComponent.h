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

  // Logic (Moved up for initialization order safety)
  juce::MidiKeyboardState keyboardState;

  // GUI
  juce::ImageComponent logoView;
  juce::TextButton btnDash{"View"}, btnCtrl{"Control"}, btnOscCfg{"OSC"},
      btnHelp{"Help"};
  juce::ToggleButton btnRetrigger{"Retrig"}, btnGPU{"GPU"},
      btnLoopPlaylist{"Loop"};
  juce::Label lblLocalIpHeader, lblLocalIpDisplay, lblTempo, lblLatency,
      lblNoteDelay, lblArp, lblArpBpm, lblArpVel, lblIn, lblOut, lblCh, lblIp,
      lblPOut, lblPIn;
  juce::GroupComponent grpNet{{}, "Network"}, grpIo{{}, "MIDI"},
      grpArp{{}, "Arp"};
  juce::TextEditor edIp, edPOut, edPIn, helpText;
  juce::TextButton btnConnect{"Connect"}, btnPlay{"Play"}, btnStop{"Stop"},
      btnPrev{"<"}, btnSkip{">"}, btnResetFile{"Rst"}, btnClearPR{"Clr"},
      btnResetBPM{"Set"}, btnTapTempo{"Tap Tempo"}, btnPrOctUp{"Oct +"},
      btnPrOctDown{"Oct -"};
  juce::Slider tempoSlider, latencySlider, sliderNoteDelay, sliderArpSpeed,
      sliderArpVel;
  juce::ComboBox cmbQuantum, cmbMidiIn, cmbMidiOut, cmbMidiCh, cmbArpPattern;
  juce::ToggleButton btnLinkToggle{"Link"}, btnArp{"Arp"}, btnArpSync{"Sync"};

  ConnectionLight ledConnect;
  PhaseVisualizer phaseVisualizer;
  MidiKeyboardComponent horizontalKeyboard{
      keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard};
  MidiKeyboardComponent verticalKeyboard{
      keyboardState, juce::MidiKeyboardComponent::verticalKeyboardFacingRight};
  ComplexPianoRoll trackGrid{keyboardState};
  TrafficMonitor logPanel;
  MidiPlaylist playlist;
  StepSequencer sequencer;
  MixerContainer mixer;
  juce::Viewport mixerViewport;
  OscAddressConfig oscConfig;
  ControlPage controlPage;

  // Logic
  // juce::MidiKeyboardState keyboardState; // Moved up
  std::unique_ptr<juce::MidiInput> midiInput;
  std::unique_ptr<juce::MidiOutput> midiOutput;
  juce::OSCSender oscSender;
  juce::OSCReceiver oscReceiver;
  bool isOscConnected = false;
  juce::MidiMessageSequence playbackSeq;
  double sequenceLength = 0, currentFileBpm = 0, currentTransportTime = 0;
  int playbackCursor = 0;
  bool isPlaying = false;
  juce::CriticalSection midiLock;
  juce::Array<int> heldNotes;
  std::vector<int> noteArrivalOrder;
  int arpNoteIndex = 0, lastNumPeers = -1, virtualOctaveShift = 0,
      tapCounter = 0, stepSeqIndex = -1, pianoRollOctaveShift = 0;
  std::vector<double> tapTimes;
  std::set<int> activeChannels;
  std::vector<std::pair<double, juce::MidiMessage>> noteOffQueue;
  juce::OpenGLContext openGLContext;

  // Sync / Audio State
  bool isWaitingToStart = false;
  double lastPhase = 0.0;
  double lastLookaheadTime = -1.0;
  double currentSampleRate = 44100.0;

  void updateVisibility();
  void setView(AppView v);
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
  void fillMidiBufferFromSequence(juce::MidiBuffer &midiMessages,
                                  double currentBeat, int numSamples,
                                  double latencySec);
  void processEventsInRange(juce::MidiBuffer &buffer, double startTime,
                            double endTime);

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