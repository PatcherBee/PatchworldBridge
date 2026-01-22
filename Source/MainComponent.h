/*
  ==============================================================================
    Source/MainComponent.h
    Status: FIXED (Removed Duplicate btnExtSync)
  ==============================================================================
*/
#pragma once

#include <JuceHeader.h>
#include <ableton/Link.hpp>
#include <memory>

#include "ControlProfile.h"

namespace juce {
class FileChooser;
}

#include "Common.h"
#include "Components/Controls.h"
#include "Components/MidiLearnOverlay.h"
#include "Components/MidiMappingManager.h"
#include "Components/MidiScheduler.h"
#include "Components/Mixer.h"
#include "Components/PlaybackEngine.h"
#include "Components/Sequencer.h"
#include "Components/Tools.h"
#include "SubComponents.h"

class MainComponent : public juce::AudioAppComponent,
                      public juce::Timer,
                      public juce::MidiKeyboardState::Listener,
                      public juce::OSCReceiver::Listener<
                          juce::OSCReceiver::MessageLoopCallback>,
                      public juce::MidiInputCallback,
                      public juce::Slider::Listener,
                      public juce::FileDragAndDropTarget,
                      public juce::KeyListener,
                      public juce::ValueTree::Listener,
                      public juce::AsyncUpdater {
public:
  //==============================================================================
  MainComponent();
  ~MainComponent() override;

  // MIDI FIFO (Lock-Free)
  juce::AbstractFifo mainMidiFifo{4096};
  std::array<juce::MidiMessage, 4096> mainMidiBuffer;
  void handleAsyncUpdate() override;
  void processMidiQueue(int start, int size);
  void handleMidiClock(const juce::MidiMessage &m);
  double getDurationFromVelocity(float velocity0to1);

  //==============================================================================
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
  // Sync / Playback Logic Helpers (V5.2)
  void updateSequencerStep(double masterBeat, double quantum);
  void handleSequenceEnd(double masterBeat);

  // Slider Listener (Declarations ONLY)
  void sliderValueChanged(juce::Slider *slider) override;
  void sliderDragStarted(juce::Slider *slider) override;
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
  juce::UndoManager undoManager;
  juce::ValueTree parameters{"Params"};
  juce::CachedValue<double> bpmVal;
  PlaybackEngine engine;
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
  MidiIndicator midiInLight, midiOutLight;

  // FIXED: Declared only once
  void toggleExtSync();
  juce::TextButton btnExtSync{"Ext"};

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

  // NOTE: btnExtSync Removed from here to prevent redefinition (It is above)

  juce::ToggleButton btnMidiScalingToggle{"Scale 0-127"}; // New Toggle
  juce::TextButton btnPlay, btnStop, btnPrev, btnSkip, btnClearPR, btnResetBPM,
      btnConnect, btnPreventBpmOverride, btnBlockMidiOut, btnGPU, btnArp,
      btnArpSync, btnArpLatch, btnArpBlock;
  juce::TextButton btnPrOctUp, btnPrOctDown, btnSplit, btnRetrigger,
      btnMidiThru, btnMidiClock, btnMidiLearn,
      btnResetLearned; // Added Reset Learned
  juce::ToggleButton btnResetMixerOnLoad{"Reset Mixer on Track Load"};
  juce::TextButton btnSaveProfile{"Save Profile"};
  juce::TextButton btnLoadProfile{"Load Profile"};
  juce::TextButton btnDeleteProfile{"X"}; // New Delete Button
  std::unique_ptr<juce::FileChooser> fileChooser;

  PhaseVisualizer phaseVisualizer;
  ConnectionLight ledConnect;
  juce::DrawableRectangle wheelFutureBox;

  // Logic Variables
  bool isSimpleMode = false;
  bool isOscConnected = false;
  bool isMidiLearnMode = false;
  bool isPlaying = false;
  bool pendingSyncStart = false;
  bool isHandlingOsc = false;
  bool startupRetryActive = true;
  bool isFullRangeMidi = false;
  juce::String lastClickedControlID = "";
  juce::Component *lastClickedComponent = nullptr;
  bool isMidiScaling127 =
      false;         // Default: 0-1 (False) switchable to 0-127 (True)
  int blockMode = 0; // 0=Off, 1=Block All, 2=Block Playback Only

  double quantum = 4.0;
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

  std::vector<int> noteArrivalOrder;
  juce::Array<int> heldNotes;
  int numFingersDown = 0; // Added for Arp Latch logic

  // ComboBoxes
  juce::ComboBox cmbQuantum, cmbMidiIn, cmbMidiOut, cmbMidiCh, cmbArpPattern,
      cmbClockMode, cmbTheme, cmbControlProfile;
  juce::Slider sliderClockOffset; // For manual offset
  juce::Label lblClockOffset;

  MidiMappingManager mappingManager;
  MidiLearnOverlay midiLearnOverlay;

  enum ClockMode { Smooth, PhaseLocked };
  ClockMode clockMode = Smooth;

  enum class AppView { Dashboard, Control, OSC_Config, Help };
  AppView currentView = AppView::Dashboard;

  ControlProfile currentProfile;
  void applyControlProfile(const ControlProfile &p);

  // Profile Management
  void saveNewControllerProfile();
  void deleteSelectedProfile();
  void updateProfileComboBox();
  void loadCustomProfile(juce::File f);
  void saveProfile(const juce::File &file); // Legacy single-profile save
  void saveFullProfile(juce::File file);    // NEW: Full save
  void loadFullProfile(juce::File file);    // NEW: Full load
  juce::File getProfileDirectory();
  void exportSequencerToMidi(juce::File file); // Added Feature
  juce::File profileFile; // To keep track of current profile file

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
  void setParameterValue(juce::String paramID,
                         float normValue); // Bridge for Mapping
  int matchOscChannel(const juce::String &pattern,
                      const juce::String &incoming);
  void sendSplitOscMessage(const juce::MidiMessage &m,
                           int overrideChannel = -1);

  // Snapshot/Undo members
  void takeSnapshot();
  void performUndo();
  void performRedo();
  void launchMidiExport();

  std::unique_ptr<juce::FileChooser> fc; // Added for async file choosing

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};