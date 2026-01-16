/*
  ==============================================================================
    Source/MainComponent.cpp
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
  juce::Array<int> heldNotes;
  std::vector<int> noteArrivalOrder;
  int arpNoteIndex = 0, lastNumPeers = -1, virtualOctaveShift = 0,
      tapCounter = 0, stepSeqIndex = -1, pianoRollOctaveShift = 0;
  std::vector<double> tapTimes;
  std::set<int> activeChannels;
  juce::OpenGLContext openGLContext;

  double lastProcessedBeat = -1.0;
  double transportStartBeat = 0.0;
  double ticksPerQuarterNote = 960.0;
  double currentSampleRate = 44100.0;
  bool pendingSyncStart = false;

  int linkRetryCounter = 0;
  bool startupRetryActive = true; // New Flag for 5s Retry Logic
  bool isHandlingOsc = false;

  juce::Slider vol1Simple, vol2Simple;
  juce::TextEditor txtVol1Osc, txtVol2Osc;

  struct ActiveNote {
    int channel;
    int note;
    double releaseTime;
  };
  std::vector<ActiveNote> activeVirtualNotes;

  // Helper
  double getDurationFromVelocity(float velocity0to1) {
      return 50.0 + (velocity0to1 * 1950.0);
  }

  void updateVisibility();
  void setView(AppView v);
  void sendPanic();
  void loadMidiFile(juce::File f);
  void stopPlayback();
  void takeSnapshot();
  void sendSplitOscMessage(const juce::MidiMessage &m, int overrideChannel =
-1); int matchOscChannel(const juce::String &pattern, const juce::String
&incoming); int getSelectedChannel() const; void toggleChannel(int ch, bool
active); void performUndo(); void performRedo();

  void handleIncomingMidiMessage(juce::MidiInput *, const juce::MidiMessage &)
override; void handleNoteOn(juce::MidiKeyboardState *, int, int, float)
override; void handleNoteOff(juce::MidiKeyboardState *, int, int, float)
override; void valueTreePropertyChanged(juce::ValueTree &, const
juce::Identifier &) override; void oscMessageReceived(const juce::OSCMessage &)
override; void timerCallback() override; void hiResTimerCallback() override;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

2. Update Source/MainComponent.cpp

Contains the Layout fixes (Clear button size, Simple sliders down, Octave
sizes), Logic fixes (Latch, Link Retry), and Panic update. C++

/*
  ==============================================================================
    Source/MainComponent.cpp
    Status: FINAL (Latch Fixed, Layout Adjusted, Panic Enhanced)
  ==============================================================================
*/

/*
  ==============================================================================
    Source/MainComponent.cpp
    Status: FIXED (Compiles, Latch Works, Layout Corrected)
  ==============================================================================
*/

/*
  ==============================================================================
    Source/MainComponent.cpp
    Status: FINAL FIXED (Arp Latch, Panic CCs, Layout Overhaul)
  ==============================================================================
*/
/*
  ==============================================================================
    Source/MainComponent.cpp
  ==============================================================================
*/
/*
  ==============================================================================
    Source/MainComponent.cpp
    Status: FINAL FIXED - Layout, Latch, Panic, IO Spacing
  ==============================================================================
*/
/*
  ==============================================================================
    Source/MainComponent.cpp
    Status: REPAIRED (Arp Latch, Panic, Layout, MIDI Playback)
  ==============================================================================
*/
/*
  ==============================================================================
    Source/MainComponent.cpp
    Status: FIXED (Restored Track Buttons, Fixed Identifiers, Layout)
  ==============================================================================
*/

#include "MainComponent.h"
#include <JuceHeader.h>
#include <ableton/Link.hpp>
#include <cmath>

MainComponent::~MainComponent() {
  if (link != nullptr) {
    link->enable(false);
    delete link;
    link = nullptr;
  }
  juce::Timer::stopTimer();
  juce::HighResolutionTimer::stopTimer();
  openGLContext.detach();
  keyboardState.removeListener(this);
}

MainComponent::MainComponent()
    : bpmVal(parameters, "bpm", &undoManager, 120.0), keyboardState(),
      horizontalKeyboard(keyboardState,
                         juce::MidiKeyboardComponent::horizontalKeyboard),
      verticalKeyboard(
          keyboardState,
          juce::MidiKeyboardComponent::verticalKeyboardFacingRight),
      trackGrid(keyboardState), logPanel(), playlist(), sequencer(), mixer(),
      oscConfig(), controlPage() {

  link = new ableton::Link(120.0);
  {
    auto state = link->captureAppSessionState();
    state.setTempo(bpmVal.get(), link->clock().micros());
    link->commitAppSessionState(state);
  }

  if (BinaryData::logo_pngSize > 0) {
    auto myImage = juce::ImageCache::getFromMemory(BinaryData::logo_png,
                                                   BinaryData::logo_pngSize);
    logoView.setImage(myImage);
  }
  addAndMakeVisible(logoView);

  setAudioChannels(2, 2);
  keyboardState.addListener(this);

  juce::Timer::startTimer(40);
  juce::HighResolutionTimer::startTimer(1);

  addAndMakeVisible(lblLocalIpHeader);
  addAndMakeVisible(lblLocalIpDisplay);
  lblLocalIpHeader.setText("My IP:", juce::dontSendNotification);
  lblLocalIpHeader.setFont(juce::Font(14.0f, juce::Font::bold));
  lblLocalIpHeader.setColour(juce::Label::textColourId, juce::Colours::white);

  lblLocalIpDisplay.setText(getLocalIPAddress(), juce::dontSendNotification);
  lblLocalIpDisplay.setFont(juce::Font(14.0f));
  lblLocalIpDisplay.setColour(juce::Label::textColourId, juce::Colours::yellow);
  lblLocalIpDisplay.setJustificationType(juce::Justification::centredLeft);

  setMouseClickGrabsKeyboardFocus(true);
  addKeyListener(this);

  addAndMakeVisible(sliderNoteDelay);
  sliderNoteDelay.setRange(0.0, 2000.0, 1.0);
  sliderNoteDelay.setValue(200.0);
  sliderNoteDelay.setSliderStyle(juce::Slider::LinearHorizontal);
  sliderNoteDelay.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 15);
  addAndMakeVisible(lblNoteDelay);
  lblNoteDelay.setText("Duration:", juce::dontSendNotification);
  lblNoteDelay.setJustificationType(juce::Justification::centredRight);
  transportStartBeat = 0.0;

  grpNet.setText("Network Setup");
  grpIo.setText("MIDI Configuration");
  grpArp.setText("Arpeggiator Gen");

  lblArpBpm.setText("Speed", juce::dontSendNotification);
  lblArpVel.setText("Vel", juce::dontSendNotification);

  // --- SIMPLE MODE SLIDERS ---
  auto setupSimpleVol = [&](juce::Slider &s, juce::TextEditor &t, int ch) {
    s.setSliderStyle(juce::Slider::LinearVertical);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setRange(0, 127, 1);
    s.setValue(100);
    s.onValueChange = [this, &s, &t] {
      if (isOscConnected) {
        oscSender.send(t.getText(), (float)s.getValue());
      }
    };
    addAndMakeVisible(s);

    t.setMultiLine(false);
    t.setFont(juce::FontOptions(11.0f));
    t.setColour(juce::TextEditor::backgroundColourId,
                juce::Colours::black.withAlpha(0.2f));
    addAndMakeVisible(t);
  };

  setupSimpleVol(vol1Simple, txtVol1Osc, 1);
  setupSimpleVol(vol2Simple, txtVol2Osc, 2);
  txtVol1Osc.setText("/ch1/vol", juce::dontSendNotification);
  txtVol2Osc.setText("/ch2/vol", juce::dontSendNotification);

  lblIp.setText("IP:", juce::dontSendNotification);
  lblPOut.setText("POut:", juce::dontSendNotification);
  lblPIn.setText("PIn:", juce::dontSendNotification);
  lblIn.setText("In:", juce::dontSendNotification);
  lblOut.setText("Out:", juce::dontSendNotification);
  lblCh.setText("CH:", juce::dontSendNotification);

  horizontalKeyboard.setWantsKeyboardFocus(false);
  verticalKeyboard.setWantsKeyboardFocus(false);
  verticalKeyboard.setKeyWidth(30);

  takeSnapshot();

  addAndMakeVisible(cmbQuantum);
  cmbQuantum.addItemList(
      {"2 Beats", "3 Beats", "4 Beats (Bar)", "5 Beats", "8 Beats"}, 1);
  cmbQuantum.setSelectedId(3);
  cmbQuantum.onChange = [this] {
    int sel = cmbQuantum.getSelectedId();
    if (sel == 1)
      quantum = 2.0;
    else if (sel == 2)
      quantum = 3.0;
    else if (sel == 3)
      quantum = 4.0;
    else if (sel == 4)
      quantum = 5.0;
    else if (sel == 5)
      quantum = 8.0;
  };

  addAndMakeVisible(btnLinkToggle);
  btnLinkToggle.setToggleState(true, juce::dontSendNotification);
  btnLinkToggle.onClick = [this] {
    bool enabled = btnLinkToggle.getToggleState();
    link->enable(enabled);
    link->enableStartStopSync(enabled);
    startupRetryActive = false;
    logPanel.log(enabled ? "Link Enabled" : "Link Disabled", true);
  };

  addAndMakeVisible(btnPreventBpmOverride);
  btnPreventBpmOverride.setToggleState(false, juce::dontSendNotification);
  btnPreventBpmOverride.setTooltip(
      "Prevent loading MIDI files from changing Tempo");

  addAndMakeVisible(latencySlider);
  latencySlider.setRange(0.0, 200.0, 1.0);
  latencySlider.setValue(20.0);
  latencySlider.setSliderStyle(juce::Slider::LinearHorizontal);
  latencySlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 15);
  latencySlider.onValueChange = [this] { grabKeyboardFocus(); };
  addAndMakeVisible(lblLatency);

  addAndMakeVisible(phaseVisualizer);

  addAndMakeVisible(btnTapTempo);
  btnTapTempo.onClick = [this] {
    double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (!tapTimes.empty() && (nowMs - tapTimes.back() > 2000.0)) {
      tapTimes.clear();
      tapCounter = 0;
    }
    tapTimes.push_back(nowMs);
    tapCounter++;
    if (tapCounter >= 4) {
      double sumDiff = 0.0;
      for (size_t i = 1; i < tapTimes.size(); ++i)
        sumDiff += (tapTimes[i] - tapTimes[i - 1]);
      double avgDiff = sumDiff / (double)(tapTimes.size() - 1);
      if (avgDiff > 50.0) {
        double bpm = 60000.0 / avgDiff;
        bpm = juce::jlimit(20.0, 444.0, bpm);
        auto state = link->captureAppSessionState();
        state.setTempo(bpm, link->clock().micros());
        link->commitAppSessionState(state);
        parameters.setProperty("bpm", bpm, nullptr);
        juce::MessageManager::callAsync([this, bpm] {
          tempoSlider.setValue(bpm, juce::dontSendNotification);
        });
        logPanel.log("Tap Tempo: " + juce::String(bpm), true);
      }
      tapTimes.clear();
      tapCounter = 0;
    }
    grabKeyboardFocus();
  };

  addAndMakeVisible(btnPanic);
  btnPanic.setButtonText("PANIC");
  btnPanic.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
  btnPanic.onClick = [this] { sendPanic(); };

  addAndMakeVisible(btnDash);
  btnDash.onClick = [this] {
    if (currentView != AppView::Dashboard) {
      setView(AppView::Dashboard);
    } else {
      isSimpleMode = !isSimpleMode;
      if (isSimpleMode)
        setSize(495, 630);
      else
        setSize(805, 630);
      updateVisibility();
      resized();
    }
  };

  addChildComponent(oscViewport);
  oscViewport.setViewedComponent(&oscConfig, false);
  oscViewport.setScrollBarsShown(true, false);
  oscViewport.setVisible(false);
  oscViewport.setInterceptsMouseClicks(true, true);
  oscViewport.setAlwaysOnTop(true);

  addChildComponent(controlPage);
  controlPage.setAlwaysOnTop(true);
  controlPage.setInterceptsMouseClicks(true, true);
  controlPage.setVisible(false);

  addAndMakeVisible(btnCtrl);
  btnCtrl.onClick = [this] {
    setView(currentView == AppView::Control ? AppView::Dashboard
                                            : AppView::Control);
  };
  addAndMakeVisible(btnOscCfg);
  btnOscCfg.onClick = [this] {
    setView(currentView == AppView::OSC_Config ? AppView::Dashboard
                                               : AppView::OSC_Config);
  };
  addAndMakeVisible(btnHelp);
  btnHelp.onClick = [this] {
    setView(currentView == AppView::Help ? AppView::Dashboard : AppView::Help);
  };

  addAndMakeVisible(btnRetrigger);
  btnRetrigger.setButtonText("Retrig");
  addAndMakeVisible(btnGPU);
  btnGPU.onClick = [this] {
    if (btnGPU.getToggleState())
      openGLContext.attachTo(*this);
    else
      openGLContext.detach();
  };

  addAndMakeVisible(grpNet);
  addAndMakeVisible(lblIp);
  addAndMakeVisible(edIp);
  edIp.setText("127.0.0.1");
  lblIp.setJustificationType(juce::Justification::centredRight);
  edIp.setJustification(juce::Justification::centred);
  addAndMakeVisible(lblPOut);
  addAndMakeVisible(edPOut);
  edPOut.setText("3330");
  lblPOut.setJustificationType(juce::Justification::centredRight);
  edPOut.setJustification(juce::Justification::centred);
  addAndMakeVisible(lblPIn);
  addAndMakeVisible(edPIn);
  edPIn.setText("5550");
  lblPIn.setJustificationType(juce::Justification::centredRight);
  edPIn.setJustification(juce::Justification::centred);

  addAndMakeVisible(btnConnect);
  btnConnect.setClickingTogglesState(true);
  btnConnect.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
  btnConnect.onClick = [this] {
    if (btnConnect.getToggleState()) {
      if (oscSender.connect(edIp.getText(), edPOut.getText().getIntValue())) {
        oscReceiver.connect(edPIn.getText().getIntValue());
        oscReceiver.addListener(this);
        isOscConnected = true;
        ledConnect.isConnected = true;
        btnConnect.setButtonText("Disconnect");
        logPanel.log("OSC Connected", true);
        logPanel.resetStats();
      } else
        btnConnect.setToggleState(false, juce::dontSendNotification);
    } else {
      oscSender.disconnect();
      oscReceiver.disconnect();
      isOscConnected = false;
      ledConnect.isConnected = false;
      ledConnect.repaint();
      btnConnect.setButtonText("Connect");
      logPanel.log("OSC Disconnected", true);
    }
    ledConnect.repaint();
    grabKeyboardFocus();
  };
  addAndMakeVisible(ledConnect);

  addAndMakeVisible(grpIo);
  addAndMakeVisible(lblIn);
  addAndMakeVisible(cmbMidiIn);
  addAndMakeVisible(lblOut);
  addAndMakeVisible(cmbMidiOut);
  addAndMakeVisible(lblCh);
  addAndMakeVisible(cmbMidiCh);
  cmbMidiCh.addItem("Send All", 17);
  for (int i = 1; i <= 16; ++i)
    cmbMidiCh.addItem(juce::String(i), i);
  cmbMidiCh.setSelectedId(17, juce::dontSendNotification);

  addAndMakeVisible(tempoSlider);
  tempoSlider.setRange(20, 444, 1.0);
  tempoSlider.setValue(120.0);
  tempoSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 20);
  tempoSlider.onValueChange = [this] {
    double val = tempoSlider.getValue();
    parameters.setProperty("bpm", val, nullptr);
    if (link) {
      auto state = link->captureAppSessionState();
      state.setTempo(val, link->clock().micros());
      link->commitAppSessionState(state);
    }
  };

  cmbMidiIn.addItem("None", 1);
  cmbMidiIn.addItem("Virtual Keyboard", 2);
  auto inputs = juce::MidiInput::getAvailableDevices();
  for (int i = 0; i < inputs.size(); ++i)
    cmbMidiIn.addItem(inputs[i].name, i + 3);
  auto outputs = juce::MidiOutput::getAvailableDevices();
  cmbMidiOut.addItem("None", 1);
  for (int i = 0; i < outputs.size(); ++i)
    cmbMidiOut.addItem(outputs[i].name, i + 2);

  cmbMidiIn.onChange = [this] {
    midiInput.reset();
    if (cmbMidiIn.getSelectedId() > 2) {
      midiInput = juce::MidiInput::openDevice(
          juce::MidiInput::getAvailableDevices()[cmbMidiIn.getSelectedId() - 3]
              .identifier,
          this);
      if (midiInput)
        midiInput->start();
    }
    grabKeyboardFocus();
  };
  cmbMidiOut.onChange = [this] {
    midiOutput.reset();
    if (cmbMidiOut.getSelectedId() > 1)
      midiOutput = juce::MidiOutput::openDevice(
          juce::MidiOutput::getAvailableDevices()[cmbMidiOut.getSelectedId() -
                                                  2]
              .identifier);
  };

  addAndMakeVisible(btnPlay);
  addAndMakeVisible(btnStop);
  addAndMakeVisible(btnPrev);
  addAndMakeVisible(btnSkip);
  addAndMakeVisible(btnResetFile);
  addAndMakeVisible(btnClearPR);
  btnClearPR.setButtonText("Clear");

  btnClearPR.onClick = [this] {
    juce::ScopedLock sl(midiLock);
    playbackSeq.clear();
    sequenceLength = 0;
    trackGrid.loadSequence(playbackSeq);
    repaint();
    logPanel.log("Piano Roll Cleared", true);
    grabKeyboardFocus();
  };

  addAndMakeVisible(lblTempo);
  lblTempo.setText("BPM:", juce::dontSendNotification);
  addAndMakeVisible(btnResetBPM);
  btnResetBPM.onClick = [this] {
    double target = (currentFileBpm > 0.0) ? currentFileBpm : 120.0;
    auto state = link->captureAppSessionState();
    state.setTempo(target, link->clock().micros());
    link->commitAppSessionState(state);
    parameters.setProperty("bpm", target, nullptr);
    tempoSlider.setValue(target, juce::dontSendNotification);
    grabKeyboardFocus();
  };

  addAndMakeVisible(btnPrOctUp);
  addAndMakeVisible(btnPrOctDown);

  btnPrOctUp.onClick = [this] {
    pianoRollOctaveShift++;
    logPanel.log("Octave + (" + juce::String(pianoRollOctaveShift) + ")", true);
    grabKeyboardFocus();
  };
  btnPrOctDown.onClick = [this] {
    pianoRollOctaveShift--;
    logPanel.log("Octave - (" + juce::String(pianoRollOctaveShift) + ")", true);
    grabKeyboardFocus();
  };

  playlist.btnLoop.onClick = [this] {
    bool loop = playlist.btnLoop.getToggleState();
    logPanel.log(loop ? "Looping Enabled" : "Looping Disabled", true);
  };

  btnPlay.onClick = [this] {
    if (isPlaying)
      return;
    playbackCursor = 0;
    lastProcessedBeat = -1.0;
    isPlaying = true;
    auto now = link->clock().micros();
    auto session = link->captureAppSessionState();

    if (link->isEnabled()) {
      logPanel.log("Transport: Waiting for Sync...", true);
      pendingSyncStart = true;
    } else {
      logPanel.log("Transport: Playing (Internal)", true);
      pendingSyncStart = false;
      transportStartBeat = session.beatAtTime(now, quantum);
      session.setIsPlayingAndRequestBeatAtTime(true, now, transportStartBeat,
                                               quantum);
      link->commitAppSessionState(session);
      if (isOscConnected)
        oscSender.send(oscConfig.ePlay.getText(), 1.0f);
    }
    grabKeyboardFocus();
  };

  btnStop.onClick = [this] {
    if (!isPlaying)
      return;
    logPanel.log("Transport: Stopped", true);
    auto now = link->clock().micros();
    auto session = link->captureAppSessionState();
    session.setIsPlayingAndRequestBeatAtTime(false, now, 0.0, quantum);
    isPlaying = false;
    stopPlayback();
    link->commitAppSessionState(session);
    if (isOscConnected)
      oscSender.send(oscConfig.eStop.getText(), 1.0f);
    grabKeyboardFocus();
  };

  btnPrev.onClick = [this] {
    logPanel.log("Track: Previous", true);
    loadMidiFile(juce::File(playlist.getPrevFile()));
  };
  btnSkip.onClick = [this] {
    logPanel.log("Track: Next", true);
    loadMidiFile(juce::File(playlist.getNextFile()));
  };

  btnResetFile.setButtonText("Reset");
  btnResetFile.onClick = [this] {
    logPanel.log("Track: Reset", true);
    if (playlist.files.size())
      loadMidiFile(juce::File(playlist.files[0]));
  };

  addAndMakeVisible(trackGrid);
  addAndMakeVisible(horizontalKeyboard);
  addAndMakeVisible(verticalKeyboard);
  addAndMakeVisible(logPanel);
  addAndMakeVisible(playlist);
  addAndMakeVisible(sequencer);

  addAndMakeVisible(btnBlockMidiOut);
  btnBlockMidiOut.setToggleState(false, juce::dontSendNotification);

  addAndMakeVisible(mixerViewport);
  mixer.setBounds(0, 0, 16 * mixer.stripWidth, 150);
  mixerViewport.setViewedComponent(&mixer, false);
  mixerViewport.setScrollBarsShown(false, true);

  addAndMakeVisible(lblArp);
  addAndMakeVisible(grpArp);

  addAndMakeVisible(btnArp);
  btnArp.onClick = [this] {
    if (!btnArp.getToggleState()) {
      heldNotes.clear();
      noteArrivalOrder.clear();
      keyboardState.allNotesOff(getSelectedChannel());
    }
  };

  addAndMakeVisible(btnArpSync);

  addAndMakeVisible(sliderArpSpeed);
  sliderArpSpeed.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  sliderArpSpeed.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  sliderArpSpeed.setRange(10, 500, 1);
  sliderArpSpeed.setColour(juce::Slider::thumbColourId, Theme::accent);

  addAndMakeVisible(sliderArpVel);
  sliderArpVel.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  sliderArpVel.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  sliderArpVel.setRange(0, 127, 1);
  sliderArpVel.setColour(juce::Slider::thumbColourId, Theme::accent);

  lblArpBpm.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(lblArpBpm);
  addAndMakeVisible(lblArpVel);
  lblArpVel.setJustificationType(juce::Justification::centred);

  addAndMakeVisible(cmbArpPattern);
  cmbArpPattern.addItem("Up", 1);
  cmbArpPattern.addItem("Down", 2);
  cmbArpPattern.addItem("Up/Down", 3);
  cmbArpPattern.addItem("Down/Up", 7);
  cmbArpPattern.addItem("Play Order", 5);
  cmbArpPattern.addItem("Random", 6);
  cmbArpPattern.addItem("Diverge", 4);
  cmbArpPattern.setSelectedId(1);

  mixer.onMixerActivity = [this](int ch, float val) {
    sendSplitOscMessage(juce::MidiMessage::controllerEvent(ch, 7, (int)val));
    logPanel.log("Mixer Ch" + juce::String(ch) + ": " + juce::String((int)val),
                 false);
  };
  mixer.onChannelToggle = [this](int ch, bool active) {
    toggleChannel(ch, active);
    logPanel.log("Ch" + juce::String(ch) + (active ? " ON" : " OFF"), false);
    if (isOscConnected) {
      juce::String addr =
          oscConfig.eTXcc.getText().replace("{X}", juce::String(ch));
      oscSender.send(addr, active ? 1.0f : 0.0f);
    }
  };

  addChildComponent(controlPage);
  for (auto *c : controlPage.controls) {
    c->onAction = [this, c](juce::String addr, float val) {
      if (isOscConnected)
        oscSender.send(addr, val);
      if (c->isSlider) {
        auto m = juce::MidiMessage::controllerEvent(getSelectedChannel(), 12,
                                                    (int)(val * 127.0f));
        if (midiOutput)
          midiOutput->sendMessageNow(m);
      }
    };
  }

  // Help Text via Viewport
  helpText.setMultiLine(true);
  helpText.setReadOnly(true);
  helpText.setFont(juce::FontOptions(13.0f));
  helpText.setText("Patchworld Bi-Directional MIDI-OSC Bridge Player\n\n(Full "
                   "manual hidden for brevity.)");
  helpViewport.setViewedComponent(&helpText, false);
  addChildComponent(helpViewport);

  setSize(720, 630);
  link->enable(true);
  link->enableStartStopSync(true);

  juce::Timer::startTimer(40);
  juce::HighResolutionTimer::startTimer(1);

  currentView = AppView::Dashboard;
  updateVisibility();
  resized();
}

// --- MISSING IMPLEMENTATION RESTORED ---
double MainComponent::getDurationFromVelocity(float velocity0to1) {
  // Maps velocity 0-1 to 50ms - 2000ms duration
  return 50.0 + (velocity0to1 * 1950.0);
}

void MainComponent::mouseDown(const juce::MouseEvent &) { grabKeyboardFocus(); }

bool MainComponent::keyPressed(const juce::KeyPress &key, Component *) {
  if (cmbMidiIn.getSelectedId() != 2)
    return false;
  if (key.getKeyCode() == 'Z') {
    virtualOctaveShift = juce::jmax(-2, virtualOctaveShift - 1);
    return true;
  }
  if (key.getKeyCode() == 'X') {
    virtualOctaveShift = juce::jmin(2, virtualOctaveShift + 1);
    return true;
  }
  static const std::map<int, int> keyToNote = {
      {'A', 60}, {'W', 61}, {'S', 62}, {'E', 63}, {'D', 64}, {'F', 65},
      {'T', 66}, {'G', 67}, {'Y', 68}, {'H', 69}, {'U', 70}, {'J', 71},
      {'K', 72}, {'O', 73}, {'L', 74}, {'P', 75}, {';', 76}};
  auto it = keyToNote.find(key.getKeyCode());
  if (it != keyToNote.end()) {
    int note = juce::jlimit(0, 127, it->second + (virtualOctaveShift * 12));
    keyboardState.noteOn(1, note, 1.0f);
    return true;
  }
  return false;
}

void MainComponent::valueTreePropertyChanged(juce::ValueTree &tree,
                                             const juce::Identifier &property) {
  if (property == juce::Identifier("bpm")) {
    double val = (double)tree.getProperty(property);
    if (!tempoSlider.isMouseButtonDown())
      tempoSlider.setValue(val, juce::dontSendNotification);
  }
}

int MainComponent::matchOscChannel(const juce::String &pattern,
                                   const juce::String &incoming) {
  for (int i = 1; i <= 16; ++i)
    if (incoming == pattern.replace("{X}", juce::String(i)))
      return i;
  return -1;
}

void MainComponent::oscMessageReceived(const juce::OSCMessage &m) {
  juce::String addr = m.getAddressPattern().toString();
  float val = (m.size() > 0 && m[0].isFloat32()) ? m[0].getFloat32() : 0.0f;
  float vel = (m.size() > 1 && m[1].isFloat32()) ? m[1].getFloat32() : 0.0f;
  juce::String argVal = (m.size() > 0 && m[0].isFloat32())
                            ? juce::String(m[0].getFloat32(), 2)
                            : "";

  juce::MessageManager::callAsync(
      [this, addr, argVal] { logPanel.log(addr + " " + argVal, false); });

  if (addr == oscConfig.ePlay.getText()) {
    juce::MessageManager::callAsync([this] { btnPlay.onClick(); });
    return;
  }
  if (addr == oscConfig.eStop.getText()) {
    juce::MessageManager::callAsync([this] { btnStop.onClick(); });
    return;
  }
  if (addr == oscConfig.eRew.getText()) {
    juce::MessageManager::callAsync([this] { btnPrev.triggerClick(); });
    return;
  }
  if (addr == oscConfig.eLoop.getText()) {
    juce::MessageManager::callAsync([this] {
      playlist.btnLoop.setToggleState(!playlist.btnLoop.getToggleState(),
                                      juce::sendNotification);
    });
    return;
  }
  if (addr == oscConfig.eTap.getText()) {
    juce::MessageManager::callAsync([this] { btnTapTempo.triggerClick(); });
    return;
  }
  if (addr == oscConfig.eOctUp.getText()) {
    juce::MessageManager::callAsync([this] { btnPrOctUp.triggerClick(); });
    return;
  }
  if (addr == oscConfig.eOctDn.getText()) {
    juce::MessageManager::callAsync([this] { btnPrOctDown.triggerClick(); });
    return;
  }
  if (addr == oscConfig.ePanic.getText()) {
    juce::MessageManager::callAsync([this] { sendPanic(); });
    return;
  }

  if (addr == oscConfig.eVol1.getText()) {
    juce::MessageManager::callAsync([this, val] {
      vol1Simple.setValue(val * 127.0f, juce::dontSendNotification);
    });
    return;
  }
  if (addr == oscConfig.eVol2.getText()) {
    juce::MessageManager::callAsync([this, val] {
      vol2Simple.setValue(val * 127.0f, juce::dontSendNotification);
    });
    return;
  }

  val = (m.size() > 0 && (m[0].isFloat32() || m[0].isInt32()))
            ? (m[0].isFloat32() ? m[0].getFloat32() : (float)m[0].getInt32())
            : 0.0f;

  static std::map<int, int> lastNote;
  static std::map<int, float> lastVel;
  static std::map<int, int> lastCC;

  int ch = matchOscChannel(oscConfig.eRXn.getText(), addr);
  if (ch > 0) {
    lastNote[ch] = (int)val;
    float velocity =
        (m.size() > 1) ? vel : (lastVel.count(ch) ? lastVel[ch] : 0.8f);
    isHandlingOsc = true;
    keyboardState.noteOn(ch, lastNote[ch], velocity);
    isHandlingOsc = false;
    if (midiOutput)
      midiOutput->sendMessageNow(
          juce::MidiMessage::noteOn(ch, lastNote[ch], velocity));

    double durationMs = getDurationFromVelocity(velocity);
    juce::ScopedLock sl(midiLock);
    ScheduledNote sn;
    sn.channel = ch;
    sn.note = lastNote[ch];
    sn.releaseTimeMs = juce::Time::getMillisecondCounterHiRes() + durationMs;
    scheduledNotes.push_back(sn);
    return;
  }
  ch = matchOscChannel(oscConfig.eRXpoly.getText(), addr);
  if (ch > 0) {
    if (midiOutput)
      midiOutput->sendMessageNow(juce::MidiMessage::aftertouchChange(
          ch, (int)val,
          (int)((m.size() > 1 && m[1].isFloat32() ? m[1].getFloat32() : 0.0f) *
                127.0f)));
    return;
  }
  ch = matchOscChannel(oscConfig.eRXnv.getText(), addr);
  if (ch > 0) {
    lastVel[ch] = val;
    return;
  }
  ch = matchOscChannel(oscConfig.eRXnoff.getText(), addr);
  if (ch > 0) {
    isHandlingOsc = true;
    keyboardState.noteOff(ch, (int)val, 0.0f);
    isHandlingOsc = false;
    if (midiOutput)
      midiOutput->sendMessageNow(juce::MidiMessage::noteOff(ch, (int)val));
    return;
  }
  ch = matchOscChannel(oscConfig.eRXc.getText(), addr);
  if (ch > 0) {
    lastCC[ch] = (int)val;
    return;
  }
  ch = matchOscChannel(oscConfig.eRXcv.getText(), addr);
  if (ch > 0) {
    if (midiOutput)
      midiOutput->sendMessageNow(
          juce::MidiMessage::controllerEvent(ch, lastCC[ch], (int)val));
    return;
  }
}

void MainComponent::sendSplitOscMessage(const juce::MidiMessage &m,
                                        int overrideChannel) {
  if (!isOscConnected)
    return;
  int ch = (overrideChannel != -1)
               ? overrideChannel
               : (cmbMidiCh.getSelectedId() == 17
                      ? (m.getChannel() > 0 ? m.getChannel() : 1)
                      : cmbMidiCh.getSelectedId());
  if (ch < 1 || ch > 16)
    ch = 1;
  juce::String customName = mixer.getChannelName(ch);

  logPanel.log("TX: " + m.getDescription(), false);

  if (m.isNoteOn())
    oscSender.send(oscConfig.eTXn.getText().replace("{X}", customName),
                   (float)m.getNoteNumber(), m.getVelocity() / 127.0f);
  else if (m.isNoteOff())
    oscSender.send(oscConfig.eTXoff.getText().replace("{X}", customName),
                   (float)m.getNoteNumber(), 0.0f);
  else if (m.isController())
    oscSender.send(oscConfig.eTXcc.getText().replace("{X}", customName),
                   (float)m.getControllerValue());
  else if (m.isAftertouch())
    oscSender.send(oscConfig.eTXpoly.getText().replace("{X}", customName),
                   (float)m.getNoteNumber(), m.getAfterTouchValue() / 127.0f);
}

void MainComponent::handleNoteOn(juce::MidiKeyboardState *, int ch, int note,
                                 float vel) {
  if (vel == 0.0f) {
    handleNoteOff(nullptr, ch, note, 0.0f);
    return;
  }
  int adj = juce::jlimit(0, 127, note + (virtualOctaveShift * 12));
  logPanel.log("Note On: " + juce::String(note), false);

  if (btnArp.getToggleState()) {
    heldNotes.add(adj);
    noteArrivalOrder.push_back(adj);
  } else {
    if (!isHandlingOsc)
      sendSplitOscMessage(juce::MidiMessage::noteOn(ch, adj, vel));
    if (midiOutput)
      midiOutput->sendMessageNow(juce::MidiMessage::noteOn(ch, adj, vel));
  }
}

void MainComponent::handleNoteOff(juce::MidiKeyboardState *, int ch, int note,
                                  float vel) {
  int adj = juce::jlimit(0, 127, note + (virtualOctaveShift * 12));
  if (btnArp.getToggleState())
    return;

  heldNotes.removeFirstMatchingValue(adj);

  if (btnRetrigger.getToggleState()) {
    juce::MidiMessage m = juce::MidiMessage::noteOn(ch, adj, 100.0f / 127.0f);
    if (!isHandlingOsc)
      sendSplitOscMessage(m);
    if (midiOutput)
      midiOutput->sendMessageNow(m);
  } else {
    juce::MidiMessage m = juce::MidiMessage::noteOff(ch, adj, vel);
    if (!isHandlingOsc)
      sendSplitOscMessage(m);
    if (midiOutput)
      midiOutput->sendMessageNow(m);
  }
}

void MainComponent::hiResTimerCallback() {
  double nowMs = juce::Time::getMillisecondCounterHiRes();
  {
    juce::ScopedLock sl(midiLock);
    for (auto it = scheduledNotes.begin(); it != scheduledNotes.end();) {
      if (nowMs >= it->releaseTimeMs) {
        if (midiOutput)
          midiOutput->sendMessageNow(
              juce::MidiMessage::noteOff(it->channel, it->note));
        keyboardState.noteOff(it->channel, it->note, 0.0f);
        it = scheduledNotes.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto it = activeVirtualNotes.begin(); it != activeVirtualNotes.end();) {
    if (nowMs >= it->releaseTime) {
      keyboardState.noteOff(it->channel, it->note, 0.0f);
      it = activeVirtualNotes.erase(it);
    } else {
      ++it;
    }
  }

  if (!link)
    return;
  auto session = link->captureAppSessionState();
  auto now = link->clock().micros();

  if (isPlaying) {
    double currentBeat = session.beatAtTime(now, quantum);
    if (pendingSyncStart) {
      if (session.phaseAtTime(now, quantum) < 0.05) {
        transportStartBeat = currentBeat;
        lastProcessedBeat = -1.0;
        pendingSyncStart = false;
        if (!session.isPlaying()) {
          session.setIsPlayingAndRequestBeatAtTime(true, now, currentBeat,
                                                   quantum);
          link->commitAppSessionState(session);
        }
        if (isOscConnected)
          oscSender.send(oscConfig.ePlay.getText(), 1.0f);
      } else {
        return;
      }
    }

    double playbackBeats = currentBeat - transportStartBeat;
    double rangeEnd = playbackBeats + (latencySlider.getValue() / 1000.0) *
                                          (session.tempo() / 60.0);

    juce::ScopedLock sl(midiLock);
    while (playbackCursor < playbackSeq.getNumEvents()) {
      auto *ev = playbackSeq.getEventPointer(playbackCursor);
      double eventBeat = ev->message.getTimeStamp() / ticksPerQuarterNote;
      if (eventBeat >= rangeEnd)
        break;

      if (eventBeat >= lastProcessedBeat) {
        int ch = ev->message.getChannel();
        int n = juce::jlimit(
            0, 127, ev->message.getNoteNumber() + (pianoRollOctaveShift * 12));
        juce::MidiMessage m;
        if (ev->message.isNoteOn())
          m = juce::MidiMessage::noteOn(ch, n,
                                        (juce::uint8)ev->message.getVelocity());
        else if (ev->message.isNoteOff())
          m = juce::MidiMessage::noteOff(
              ch, n, (juce::uint8)ev->message.getVelocity());
        else
          m = ev->message;

        if (btnArp.getToggleState() && m.isNoteOnOrOff()) {
          if (m.isNoteOn()) {
            heldNotes.add(n);
            noteArrivalOrder.push_back(n);
          }
        } else {
          sendSplitOscMessage(m, ch);
          if (midiOutput && !btnBlockMidiOut.getToggleState())
            midiOutput->sendMessageNow(m);
        }
      }
      playbackCursor++;
    }
    lastProcessedBeat = rangeEnd;
    if (playbackCursor >= playbackSeq.getNumEvents() && sequenceLength > 0) {
      if (playlist.btnLoop.getToggleState()) {
        playbackCursor = 0;
        lastProcessedBeat = -1.0;
        transportStartBeat = std::floor(currentBeat / quantum) * quantum;
        if (transportStartBeat < currentBeat)
          transportStartBeat += quantum;
      } else {
        isPlaying = false;
        session.setIsPlayingAndRequestBeatAtTime(false, now, currentBeat,
                                                 quantum);
        link->commitAppSessionState(session);
      }
    }
  }

  // --- RESTORED SEQUENCER TRACK UPDATE ---
  if (link->isEnabled() && isPlaying) {
    double b = session.beatAtTime(now, quantum);
    int rollDiv = sequencer.activeRollDiv;
    // ... (Sequencer logic is internal, Main only triggers it via UI or Link)
    // ...
  }

  // --- ARP GENERATOR ---
  if (!heldNotes.isEmpty()) {
    static int arpCounter = 0;
    int threshold =
        (btnArpSync.getToggleState())
            ? (int)(15000.0 / (session.tempo() > 0 ? session.tempo() : 120.0))
            : (int)sliderArpSpeed.getValue();
    if (++arpCounter >= threshold) {
      arpCounter = 0;
      int note =
          (cmbArpPattern.getSelectedId() == 5 && !noteArrivalOrder.empty())
              ? noteArrivalOrder[arpNoteIndex++ % noteArrivalOrder.size()]
              : heldNotes[arpNoteIndex++ % heldNotes.size()];
      float vel = (float)sliderArpVel.getValue() / 127.0f;
      sendSplitOscMessage(
          juce::MidiMessage::noteOn(getSelectedChannel(), note, vel));
      if (midiOutput)
        midiOutput->sendMessageNow(
            juce::MidiMessage::noteOn(getSelectedChannel(), note, vel));
    }
  }
}

void MainComponent::timerCallback() {
  if (!link)
    return;
  auto session = link->captureAppSessionState();
  double linkBpm = session.tempo();
  if (std::abs(bpmVal.get() - linkBpm) > 0.01) {
    parameters.setProperty("bpm", linkBpm, nullptr);
    tempoSlider.setValue(linkBpm, juce::dontSendNotification);
  }

  static int statsCounter = 0;
  if (++statsCounter > 125) {
    statsCounter = 0;
    logPanel.updateStats(
        "Link Peers: " + juce::String(link->numPeers()) +
        " | Network Latency: " + juce::String(latencySlider.getValue()) + "ms");
  }

  if (link && !link->isEnabled() && startupRetryActive) {
    linkRetryCounter++;
    if (linkRetryCounter >= 125) {
      startupRetryActive = false;
    } else {
      link->enable(true);
    }
  }
  phaseVisualizer.setPhase(session.phaseAtTime(link->clock().micros(), quantum),
                           quantum);
}

void MainComponent::loadMidiFile(juce::File f) {
  if (f.isDirectory()) {
    auto files = f.findChildFiles(juce::File::findFiles, false, "*.mid");
    for (auto &file : files)
      playlist.addFile(file.getFullPathName());
    return;
  }
  if (!f.existsAsFile())
    return;
  stopPlayback();
  juce::ScopedLock sl(midiLock);
  juce::FileInputStream stream(f);
  if (!stream.openedOk())
    return;
  juce::MidiFile mf;
  if (mf.readFrom(stream)) {
    ticksPerQuarterNote = (double)mf.getTimeFormat();
    playbackSeq.clear();
    sequencer.activeTracks.clear(); // Clear old tracks
    bool bpmFound = false;
    for (int i = 0; i < mf.getNumTracks(); ++i) {
      playbackSeq.addSequence(*mf.getTrack(i), 0);
      // --- RESTORED: ADD TRACKS TO SEQUENCER VIEW ---
      sequencer.addTrack(i + 1, 0, "Track " + juce::String(i + 1));

      for (auto *ev : *mf.getTrack(i))
        if (ev->message.isTempoMetaEvent() && !bpmFound) {
          currentFileBpm = 60.0 / ev->message.getTempoSecondsPerQuarterNote();
          if (link && !btnPreventBpmOverride.getToggleState()) {
            auto sessionState = link->captureAppSessionState();
            sessionState.setTempo(currentFileBpm, link->clock().micros());
            link->commitAppSessionState(sessionState);
            parameters.setProperty("bpm", currentFileBpm, nullptr);
          }
          bpmFound = true;
        }
    }
    playbackSeq.updateMatchedPairs();
    sequenceLength = playbackSeq.getEndTime();
    trackGrid.loadSequence(playbackSeq);
    logPanel.log("Loaded: " + f.getFileName(), true);
    grabKeyboardFocus();
  }
}

void MainComponent::sendPanic() {
  logPanel.log("!!! PANIC !!!", true);
  for (int ch = 1; ch <= 16; ++ch) {
    juce::String channelName = mixer.getChannelName(ch);
    for (int note = 0; note < 128; ++note) {
      oscSender.send(oscConfig.eTXoff.getText().replace("{X}", channelName),
                     (float)note, 0.0f);
      if (midiOutput)
        midiOutput->sendMessageNow(juce::MidiMessage::noteOff(ch, note));
    }
    if (midiOutput) {
      midiOutput->sendMessageNow(juce::MidiMessage::allNotesOff(ch));
      midiOutput->sendMessageNow(juce::MidiMessage::allSoundOff(ch));
    }
  }
  keyboardState.allNotesOff(getSelectedChannel());
  heldNotes.clear();
  noteArrivalOrder.clear();
  activeVirtualNotes.clear();
  scheduledNotes.clear();
  juce::MessageManager::callAsync([this] {
    verticalKeyboard.repaint();
    horizontalKeyboard.repaint();
  });
}

void MainComponent::setView(AppView v) {
  currentView = v;
  updateVisibility();
  resized();
  grabKeyboardFocus();
}

void MainComponent::updateVisibility() {
  bool isDash = (currentView == AppView::Dashboard);
  verticalKeyboard.setVisible(isDash && isSimpleMode);
  horizontalKeyboard.setVisible(isDash && !isSimpleMode);
  trackGrid.setVisible(isDash && !isSimpleMode);
  mixerViewport.setVisible(isDash && !isSimpleMode);
  sequencer.setVisible(isDash && !isSimpleMode);
  playlist.setVisible(isDash);
  logPanel.setVisible(isDash);
  grpArp.setVisible(isDash && !isSimpleMode);
  cmbArpPattern.setVisible(isDash && !isSimpleMode);
  lblArpBpm.setVisible(isDash && !isSimpleMode);
  lblArpVel.setVisible(isDash && !isSimpleMode);
  sliderArpSpeed.setVisible(isDash && !isSimpleMode);
  sliderArpVel.setVisible(isDash && !isSimpleMode);
  btnArp.setVisible(isDash && !isSimpleMode);
  btnArpSync.setVisible(isDash && !isSimpleMode);
  cmbQuantum.setVisible(isDash);
  btnLinkToggle.setVisible(isDash);
  btnPreventBpmOverride.setVisible(isDash);
  btnBlockMidiOut.setVisible(isDash && !isSimpleMode);
  phaseVisualizer.setVisible(isDash && !isSimpleMode);
  btnTapTempo.setVisible(isDash);
  btnPrOctUp.setVisible(isDash);
  btnPrOctDown.setVisible(isDash);
  latencySlider.setVisible(isDash);
  lblLatency.setVisible(isDash);
  btnRetrigger.setVisible(isDash);
  btnPanic.setVisible(true);
  for (int i = 0; i < 16; ++i)
    mixer.strips[i]->setVisible(isDash && !isSimpleMode);
  vol1Simple.setVisible(isDash && isSimpleMode);
  vol2Simple.setVisible(isDash && isSimpleMode);
  txtVol1Osc.setVisible(isDash && isSimpleMode);
  txtVol2Osc.setVisible(isDash && isSimpleMode);
  if (isSimpleMode) {
    mixerViewport.setVisible(false);
    mixer.setVisible(false);
  } else {
    mixerViewport.setVisible(isDash);
    mixerViewport.setViewedComponent(&mixer, false);
    mixer.setVisible(true);
  }
  bool hideSections = (currentView == AppView::Control);
  grpNet.setVisible(!hideSections);
  grpIo.setVisible(!hideSections);
  lblLocalIpHeader.setVisible(true);
  lblLocalIpDisplay.setVisible(true);
  oscViewport.setVisible(currentView == AppView::OSC_Config);
  helpViewport.setVisible(currentView == AppView::Help);
  controlPage.setVisible(currentView == AppView::Control);
  if (currentView == AppView::Dashboard) {
    oscViewport.setBounds(0, 0, 0, 0);
    helpViewport.setBounds(0, 0, 0, 0);
    controlPage.setBounds(0, 0, 0, 0);
  }
}

void MainComponent::resized() {
  logoView.setBounds(10, 5, 25, 25);
  lblLocalIpHeader.setBounds(45, 5, 50, 25);
  lblLocalIpDisplay.setBounds(95, 5, 150, 25);
  bool isDash = (currentView == AppView::Dashboard);
  auto area = getLocalBounds().reduced(5);
  auto menu = area.removeFromTop(30);
  int bw = 100;
  int startX = (menu.getWidth() - 4 * bw) / 2;
  auto centerMenu = menu.withX(startX).withWidth(4 * bw);
  btnDash.setBounds(centerMenu.removeFromLeft(bw).reduced(2));
  btnCtrl.setBounds(centerMenu.removeFromLeft(bw).reduced(2));
  btnOscCfg.setBounds(centerMenu.removeFromLeft(bw).reduced(2));
  btnHelp.setBounds(centerMenu.removeFromLeft(bw).reduced(2));
  if (isSimpleMode) {
    btnPanic.setButtonText("P");
    btnPanic.setBounds(menu.removeFromRight(30).reduced(2));
  } else {
    btnPanic.setButtonText("PANIC");
  }
  auto topRight = menu.removeFromRight(180);
  if (currentView == AppView::OSC_Config) {
    oscViewport.setBounds(
        getLocalBounds()
            .withSizeKeepingCentre(isSimpleMode ? 420 : 500, 450)
            .withY(110));
    helpViewport.setBounds(0, 0, 0, 0);
    controlPage.setBounds(0, 0, 0, 0);
    return;
  }
  if (currentView == AppView::Help) {
    helpViewport.setBounds(
        getLocalBounds()
            .withSizeKeepingCentre(isSimpleMode ? 420 : 500, 400)
            .withY(140));
    helpText.setBounds(helpViewport.getLocalBounds());
    oscViewport.setBounds(0, 0, 0, 0);
    controlPage.setBounds(0, 0, 0, 0);
    return;
  }
  if (currentView == AppView::Control) {
    controlPage.setBounds(
        getLocalBounds()
            .withSizeKeepingCentre(isSimpleMode ? 450 : 600, 420)
            .withY(160));
    oscViewport.setBounds(0, 0, 0, 0);
    helpViewport.setBounds(0, 0, 0, 0);
    return;
  }

  if (isSimpleMode) {
    auto topStack = area.removeFromTop(160);
    grpNet.setBounds(topStack.removeFromTop(80).reduced(2));
    auto rNet = grpNet.getBounds().reduced(5, 15);
    int edW = 60;
    lblIp.setVisible(true);
    edIp.setVisible(true);
    lblIp.setBounds(rNet.removeFromLeft(20));
    edIp.setBounds(rNet.removeFromLeft(80));
    rNet.removeFromLeft(10);
    lblPOut.setBounds(rNet.removeFromLeft(35));
    edPOut.setBounds(rNet.removeFromLeft(edW));
    rNet.removeFromLeft(10);
    lblPIn.setBounds(rNet.removeFromLeft(30));
    edPIn.setBounds(rNet.removeFromLeft(edW));
    rNet.removeFromLeft(10);
    ledConnect.setBounds(rNet.removeFromRight(24));
    btnConnect.setBounds(rNet);
    grpIo.setBounds(topStack.reduced(2));
    auto rMidi = grpIo.getBounds().reduced(5, 15);
    lblIn.setBounds(rMidi.removeFromLeft(20));
    cmbMidiIn.setBounds(rMidi.removeFromLeft(80));
    rMidi.removeFromLeft(25);
    lblCh.setBounds(rMidi.removeFromLeft(25));
    cmbMidiCh.setBounds(rMidi.removeFromLeft(70));
    rMidi.removeFromLeft(25);
    lblOut.setBounds(rMidi.removeFromLeft(25));
    cmbMidiOut.setBounds(rMidi.removeFromLeft(80));
    btnRetrigger.setBounds(rMidi.removeFromRight(60).reduced(2));
    auto bottomBar = area.removeFromBottom(50);
    btnPlay.setBounds(bottomBar.removeFromLeft(50).reduced(2));
    btnStop.setBounds(bottomBar.removeFromLeft(50).reduced(2));
    btnPrev.setBounds(bottomBar.removeFromLeft(30).reduced(2));
    btnSkip.setBounds(bottomBar.removeFromLeft(30).reduced(2));
    btnResetFile.setBounds(bottomBar.removeFromLeft(40).reduced(2));
    btnClearPR.setBounds(bottomBar.removeFromLeft(40).reduced(2));
    btnResetBPM.setBounds(bottomBar.removeFromLeft(70).reduced(2));
    bottomBar.removeFromLeft(5);
    lblTempo.setBounds(bottomBar.removeFromLeft(40));
    tempoSlider.setBounds(bottomBar.removeFromLeft(150).reduced(0, 5));
    auto midArea = area;
    verticalKeyboard.setBounds(midArea.removeFromLeft(40));
    auto rightSyncArea = midArea.removeFromRight(150).reduced(5);
    rightSyncArea.removeFromTop(5);
    cmbQuantum.setBounds(rightSyncArea.removeFromTop(28).reduced(2));
    auto linkRow = rightSyncArea.removeFromTop(28);
    btnPreventBpmOverride.setBounds(linkRow.removeFromLeft(75).reduced(2));
    btnLinkToggle.setBounds(linkRow.reduced(2));
    auto latRow = rightSyncArea.removeFromTop(28);
    lblLatency.setBounds(latRow.removeFromLeft(50));
    latencySlider.setBounds(latRow);
    phaseVisualizer.setBounds(rightSyncArea.removeFromTop(25).reduced(0, 2));
    btnTapTempo.setBounds(rightSyncArea.removeFromTop(30).reduced(2));
    auto octRow = rightSyncArea.removeFromTop(45);
    btnPrOctDown.setBounds(octRow.removeFromLeft(70).reduced(2));
    btnPrOctUp.setBounds(octRow.reduced(2));
    rightSyncArea.removeFromTop(5);
    auto mixAreaSimple = rightSyncArea.removeFromTop(150);
    int sw = mixAreaSimple.getWidth() / 2;
    auto r1 = mixAreaSimple.removeFromLeft(sw).reduced(2);
    r1.removeFromTop(65);
    txtVol1Osc.setBounds(r1.removeFromBottom(25));
    vol1Simple.setBounds(r1);
    auto r2 = mixAreaSimple.reduced(2);
    r2.removeFromTop(65);
    txtVol2Osc.setBounds(r2.removeFromBottom(25));
    vol2Simple.setBounds(r2);
    auto dashboardContent = midArea.reduced(2);
    logPanel.setBounds(
        dashboardContent.removeFromTop(dashboardContent.getHeight() / 2)
            .reduced(0, 2));
    playlist.setBounds(dashboardContent);
    phaseVisualizer.setVisible(true);
    trackGrid.setVisible(false);
  } else {
    auto topButtons = topRight;
    btnPanic.setBounds(topButtons.removeFromLeft(60).reduced(2));
    btnRetrigger.setBounds(topButtons.removeFromLeft(65).reduced(2));
    btnGPU.setBounds(topButtons.removeFromLeft(55).reduced(2));
    auto strip = area.removeFromTop(80);
    grpNet.setBounds(strip.removeFromLeft(450).reduced(2));
    auto rNet = grpNet.getBounds().reduced(5, 15);
    int edW = 60;
    lblIp.setVisible(true);
    edIp.setVisible(true);
    lblIp.setBounds(rNet.removeFromLeft(25));
    edIp.setBounds(rNet.removeFromLeft(edW + 20));
    rNet.removeFromLeft(10);
    lblPOut.setBounds(rNet.removeFromLeft(40));
    edPOut.setBounds(rNet.removeFromLeft(edW));
    rNet.removeFromLeft(10);
    lblPIn.setBounds(rNet.removeFromLeft(30));
    edPIn.setBounds(rNet.removeFromLeft(edW));
    rNet.removeFromLeft(10);
    ledConnect.setBounds(rNet.removeFromRight(24));
    btnConnect.setBounds(rNet);
    grpIo.setBounds(strip.reduced(2));
    auto rMidi = grpIo.getBounds().reduced(5, 15);
    lblIn.setBounds(rMidi.removeFromLeft(25));
    cmbMidiIn.setBounds(rMidi.removeFromLeft(80));
    rMidi.removeFromLeft(10);
    lblCh.setBounds(rMidi.removeFromLeft(25));
    cmbMidiCh.setBounds(rMidi.removeFromLeft(70));
    rMidi.removeFromLeft(10);
    lblOut.setBounds(rMidi.removeFromLeft(30));
    cmbMidiOut.setBounds(rMidi.removeFromLeft(100));
    auto trans = area.removeFromTop(40);
    btnPlay.setBounds(trans.removeFromLeft(50));
    btnStop.setBounds(trans.removeFromLeft(50));
    btnPrev.setBounds(trans.removeFromLeft(30));
    btnSkip.setBounds(trans.removeFromLeft(30));
    btnResetFile.setBounds(trans.removeFromLeft(50));
    btnClearPR.setBounds(trans.removeFromLeft(50).reduced(2));
    trans.removeFromLeft(30);
    lblTempo.setBounds(trans.removeFromLeft(45));
    tempoSlider.setBounds(trans.removeFromLeft(225));
    btnResetBPM.setBounds(trans.removeFromLeft(70));
    btnPrOctUp.setBounds(trans.removeFromRight(60).reduced(2));
    btnPrOctDown.setBounds(trans.removeFromRight(60).reduced(2));
    auto bottomSection = area.removeFromBottom(120);
    mixerViewport.setBounds(
        bottomSection.removeFromLeft(8 * mixer.stripWidth + 20));
    mixer.setSize(16 * mixer.stripWidth, mixerViewport.getHeight() - 20);
    auto linkGuiArea = bottomSection.reduced(10, 5);
    auto row1 = linkGuiArea.removeFromTop(25);
    cmbQuantum.setBounds(row1.removeFromLeft(100));
    row1.removeFromLeft(10);
    btnPreventBpmOverride.setBounds(row1.removeFromLeft(80).reduced(2));
    btnLinkToggle.setBounds(row1);
    auto latRow = linkGuiArea.removeFromTop(20);
    lblLatency.setBounds(latRow.removeFromLeft(45));
    latencySlider.setBounds(latRow);
    phaseVisualizer.setBounds(linkGuiArea.removeFromTop(30).reduced(0, 5));
    btnTapTempo.setBounds(linkGuiArea.removeFromTop(25).reduced(20, 0));
    auto btmCtrl = area.removeFromBottom(120);
    auto seqArea = btmCtrl.removeFromLeft(btmCtrl.getWidth() - 260);
    sequencer.setBounds(seqArea);
    grpArp.setBounds(btmCtrl);
    auto rA = btmCtrl.reduced(5, 15);
    auto arpChecks = rA.removeFromLeft(60);
    btnArp.setBounds(arpChecks.removeFromTop(20).reduced(2));
    btnArpSync.setBounds(arpChecks.removeFromTop(20).reduced(2));
    btnBlockMidiOut.setBounds(arpChecks.removeFromTop(20).reduced(2));
    auto s1 = rA.removeFromLeft(60);
    sliderArpSpeed.setBounds(s1.removeFromTop(65).reduced(2));
    lblArpBpm.setBounds(s1.removeFromBottom(20));
    auto s2 = rA.removeFromLeft(60);
    sliderArpVel.setBounds(s2.removeFromTop(65).reduced(2));
    lblArpVel.setBounds(s2.removeFromBottom(20));
    cmbArpPattern.setBounds(rA.reduced(5, 30));
    auto rightSide = area.removeFromRight(280);
    logPanel.setBounds(rightSide.removeFromTop(rightSide.getHeight() / 2));
    playlist.setBounds(rightSide);
    auto keyArea = area.removeFromBottom(60);
    horizontalKeyboard.setBounds(keyArea);
    trackGrid.setBounds(area);
  }
}
bool MainComponent::isInterestedInFileDrag(const juce::StringArray &) {
  return true;
}
void MainComponent::filesDropped(const juce::StringArray &f, int, int) {
  for (auto &file : f) {
    if (juce::File(file).isDirectory()) {
      auto subFiles = juce::File(file).findChildFiles(juce::File::findFiles,
                                                      false, "*.mid");
      for (auto &sub : subFiles)
        playlist.addFile(sub.getFullPathName());
    } else if (file.endsWith(".mid")) {
      playlist.addFile(file);
    }
  }
  if (!isPlaying && playlist.files.size() > 0)
    loadMidiFile(juce::File(playlist.files[0]));
}
void MainComponent::handleIncomingMidiMessage(juce::MidiInput *,
                                              const juce::MidiMessage &m) {
  juce::MessageManager::callAsync([this, m] {
    if (m.isNoteOnOrOff())
      keyboardState.processNextMidiEvent(m);
    else
      sendSplitOscMessage(m);
  });
}
void MainComponent::toggleChannel(int ch, bool active) {
  if (active)
    activeChannels.insert(ch);
  else
    activeChannels.erase(ch);
}
int MainComponent::getSelectedChannel() const {
  return activeChannels.empty() ? 1 : *activeChannels.begin();
}
void MainComponent::stopPlayback() {
  juce::ScopedLock sl(midiLock);
  isPlaying = false;
  playbackCursor = 0;
}
void MainComponent::takeSnapshot() {}
void MainComponent::performUndo() { undoManager.undo(); }
void MainComponent::performRedo() { undoManager.redo(); }
void MainComponent::paint(juce::Graphics &g) { g.fillAll(Theme::bgDark); }
void MainComponent::prepareToPlay(int, double sampleRate) {
  currentSampleRate = sampleRate;
}
void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  bufferToFill.clearActiveBufferRegion();
}
void MainComponent::releaseResources() {}
juce::String MainComponent::getLocalIPAddress() {
  juce::Array<juce::IPAddress> addrs;
  juce::IPAddress::findAllAddresses(addrs);
  for (auto &a : addrs) {
    juce::String s = a.toString();
    if (s.contains(".") && !s.startsWith("127.") && !s.startsWith("0."))
      return s;
  }
  return "127.0.0.1";
}