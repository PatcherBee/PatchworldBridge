/*
  ==============================================================================
    Source/MainComponent.h
    Status: FIXED (Removed inline bodies to prevent double-definition errors)
  ==============================================================================
*/
#pragma once

#include <JuceHeader.h>
#include <ableton/Link.hpp>

// Use flat includes if files are in the same directory
#include "Common.h"
#include "Components/MidiScheduler.h"
#include "Controls.h"
#include "Mixer.h"
#include "Sequencer.h"
#include "SubComponents.h"
#include "Tools.h"

class MainComponent : public juce::AudioAppComponent,
                      public juce::Timer,
                      public juce::HighResolutionTimer,
                      public juce::MidiKeyboardState::Listener,
                      public juce::OSCReceiver::Listener<
                          juce::OSCReceiver::MessageLoopCallback>,
                      public juce::MidiInputCallback,
                      public juce::Slider::Listener,
                      public juce::FileDragAndDropTarget,
                      public juce::KeyListener,
                      public juce::ValueTree::Listener {
public:
  MainComponent();
  ~MainComponent() override;

  // Standard JUCE Lifecycle
  void paint(juce::Graphics &g) override;
  void resized() override;

  // AudioAppComponent overrides (Declarations ONLY)
  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void
  getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
  void releaseResources() override;

  // Listeners
  void handleNoteOn(juce::MidiKeyboardState *source, int midiChannel,
                    int midiNoteNumber, float velocity) override;
  void handleNoteOff(juce::MidiKeyboardState *source, int midiChannel,
                     int midiNoteNumber, float velocity) override;
  void oscMessageReceived(const juce::OSCMessage &message) override;
  void handleIncomingMidiMessage(juce::MidiInput *source,
                                 const juce::MidiMessage &message) override;

  void timerCallback() override;
  void hiResTimerCallback() override;

  // Slider Listener (Declarations ONLY)
  void sliderValueChanged(juce::Slider *slider) override;
  void sliderDragEnded(juce::Slider *slider) override;

  // Drag & Drop (Declarations ONLY)
  bool isInterestedInFileDrag(const juce::StringArray &files) override;
  void filesDropped(const juce::StringArray &files, int x, int y) override;

  // Input (Declarations ONLY)
  bool keyPressed(const juce::KeyPress &key,
                  Component *originatingComponent) override;
  void valueTreePropertyChanged(juce::ValueTree &treeThatChanged,
                                const juce::Identifier &property) override;
  void mouseDown(const juce::MouseEvent &event) override;

private:
  // Logic & Sync
  ableton::Link *link = nullptr; // Raw pointer to match existing .cpp logic
  juce::UndoManager undoManager;
  juce::ValueTree parameters{"Params"};
  juce::CachedValue<double> bpmVal;

  // Internal Objects
  juce::OSCReceiver oscReceiver;
  juce::OSCSender oscSender;
  juce::MidiKeyboardState keyboardState;
  std::unique_ptr<juce::MidiInput> midiInput;
  std::unique_ptr<juce::MidiOutput> midiOutput;

  // Custom UI Components
  CustomKeyboard horizontalKeyboard;
  CustomKeyboard verticalKeyboard;
  ComplexPianoRoll trackGrid;
  TrafficMonitor logPanel;
  MidiPlaylist playlist;
  StepSequencer sequencer;
  MixerContainer mixer;
  OscAddressConfig oscConfig;
  ControlPage controlPage;

  // Viewports and Overlays
  juce::Viewport oscViewport, helpViewport, mixerViewport;
  juce::TextEditor helpText;
  juce::TextEditor helpTextDisplay; // Added for Help content
  juce::ImageComponent logoView;
  juce::Label lblLocalIpHeader, lblLocalIpDisplay;

  // Groups & Layout Labels
  juce::GroupComponent grpNet, grpIo, grpArp, grpPlaceholder;
  juce::Label lblArpBpm, lblArpVel, lblIp, lblPOut, lblPIn, lblIn, lblOut,
      lblCh, lblTempo, lblLatency, lblArp, lblNoteDelay, lblLinkBeat, lblVol1,
      lblVol2, lblArpSpdLabel, lblArpVelLabel;

  // Controls
  juce::Slider vol1Simple, vol2Simple, tempoSlider, nudgeSlider,
      sliderNoteDelay, sliderArpSpeed, sliderArpVel, sliderLatencyComp;
  juce::Label lblLatencyComp;
  juce::Slider sliderPitchH, sliderModH, sliderPitchV, sliderModV;
  juce::TextEditor txtVol1Osc, txtVol2Osc, edIp, edPOut, edPIn;
  juce::TextButton btnVol1CC, btnVol2CC, btnLinkToggle, btnTapTempo, btnPanic,
      btnDash, btnCtrl, btnOscCfg, btnHelp, btnMidiScaling;
  juce::ToggleButton btnMidiScalingToggle{"Scale 0-127"}; // New Toggle
  juce::TextButton btnPlay, btnStop, btnPrev, btnSkip, btnClearPR, btnResetBPM,
      btnConnect, btnPreventBpmOverride, btnBlockMidiOut, btnGPU, btnArp,
      btnArpSync;
  juce::TextButton btnPrOctUp, btnPrOctDown, btnSplit, btnRetrigger,
      btnMidiThru, btnMidiClock;
  juce::ToggleButton btnResetMixerOnLoad{"Reset Mixer on Track Load"};

  // Custom Visualizers
  PhaseVisualizer phaseVisualizer;
  ConnectionLight ledConnect;
  juce::DrawableRectangle wheelFutureBox;

  // Logic Variables
  bool isSimpleMode = false;
  bool isOscConnected = false;
  bool isPlaying = false;
  bool pendingSyncStart = false;
  bool isHandlingOsc = false;
  bool startupRetryActive = true;
  bool isFullRangeMidi = false;
  bool isMidiScaling127 =
      false; // Default: 0-1 (False) switchable to 0-127 (True)

  double quantum = 4.0;
  double transportStartBeat = 0.0;
  double beatsPlayedOnPause = 0.0;
  double lastProcessedBeat = -1.0;
  double baseBpm = 120.0;
  double accumulatedClock = 0.0; // For MIDI Clock Pulse
  double ticksPerQuarterNote = 960.0;
  double sequenceLength = 0.0;
  double currentFileBpm = 0.0;
  double currentSampleRate = 44100.0;
  double internalPlaybackStartTimeMs = 0.0; // For Link-disabled playback

  int tapCounter = 0;
  int lastNumPeers = 0;
  int linkRetryCounter = 0;
  int playbackCursor = 0;
  int pianoRollOctaveShift = 0;
  int virtualOctaveShift = 0;

  // MIDI Clock In Sync
  std::vector<double> clockInTimes;
  double midiInBpm = 120.0;
  int clockPulseCounter = 0;
  bool isSyncingToMidiIn = false;

  std::vector<double> tapTimes;
  juce::MidiMessageSequence playbackSeq;
  std::set<int> activeChannels;
  juce::CriticalSection midiLock;
  juce::OpenGLContext openGLContext;
  juce::TooltipWindow tooltipWindow{this, 500}; // Added TooltipWindow

  // Arpeggiator State
  double lastArpNoteTime = 0.0;
  int currentArpIndex = 0;
  int lastReceivedCC[17] = {0}; // Track last CC for /chXcv

  MidiScheduler midiScheduler;
  std::vector<int> noteArrivalOrder;
  juce::Array<int> heldNotes;

  // ComboBoxes
  juce::ComboBox cmbQuantum, cmbMidiIn, cmbMidiOut, cmbMidiCh, cmbArpPattern,
      cmbClockMode;
  juce::Slider sliderClockOffset; // For manual offset
  juce::Label lblClockOffset;

  enum ClockMode { Smooth, PhaseLocked };
  ClockMode clockMode = Smooth;

  enum class AppView { Dashboard, Control, OSC_Config, Help };
  AppView currentView = AppView::Dashboard;

  // Private Helper Methods
  void setView(AppView v);
  void updateVisibility();
  void loadMidiFile(juce::File f);
  void startPlayback();
  void pausePlayback();
  void clearPlaybackSequence();
  void sendPanic();
  void stopPlayback();
  void toggleChannel(int ch, bool active);
  int getSelectedChannel() const;
  juce::String getLocalIPAddress();
  double getDurationFromVelocity(float velocity0to1);
  int matchOscChannel(const juce::String &pattern,
                      const juce::String &incoming);
  void sendSplitOscMessage(const juce::MidiMessage &m,
                           int overrideChannel = -1);
  void updateSequencerStep(double currentBeat, double quantum);

  // Snapshot/Undo members
  void takeSnapshot();
  void performUndo();
  void performRedo();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};