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
// --- START THE CONSTRUCTOR HERE ---
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
  // 2. Load Logo from BinaryData
  if (BinaryData::logo_pngSize > 0) {
    auto myImage = juce::ImageCache::getFromMemory(BinaryData::logo_png,
                                                   BinaryData::logo_pngSize);
    logoView.setImage(myImage);
  }
  addAndMakeVisible(logoView);

  // 3. Audio/MIDI Setup
  setAudioChannels(2, 2);
  keyboardState.addListener(this);

  juce::Timer::startTimer(40); // Explicitly call standard timer
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
  // --- FOCUS & KEYBOARD SETUP ---
  setMouseClickGrabsKeyboardFocus(true); // Auto-grab on background click
  addKeyListener(this);
  keyboardState.addListener(this);

  // Velocity / Note Delay Slider Setup
  addAndMakeVisible(sliderNoteDelay);
  sliderNoteDelay.setRange(0.0, 2000.0, 1.0);
  sliderNoteDelay.setValue(200.0); // Default 200ms
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

  // Setup Simple Mode Vol Sliders
  auto setupSimpleVol = [&](juce::Slider &s, juce::TextEditor &t, int ch) {
    s.setSliderStyle(juce::Slider::LinearVertical);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setRange(0, 100, 1);
    s.setValue(80);
    s.onValueChange = [this, ch, &s] {
      mixer.strips[ch - 1]->volSlider.setValue(s.getValue());
    };
    addAndMakeVisible(s);

    t.setMultiLine(false);
    t.setFont(juce::FontOptions(11.0f));
    t.setColour(juce::TextEditor::backgroundColourId,
                juce::Colours::black.withAlpha(0.2f));
    t.onTextChange = [this, ch, &t] {
      if (ch == 1)
        oscConfig.eVol1.setText(t.getText(), juce::dontSendNotification);
      else if (ch == 2)
        oscConfig.eVol2.setText(t.getText(), juce::dontSendNotification);
    };
    addAndMakeVisible(t);
  };
  setupSimpleVol(vol1Simple, txtVol1Osc, 1);
  setupSimpleVol(vol2Simple, txtVol2Osc, 2);
  txtVol1Osc.setText(oscConfig.eVol1.getText(), juce::dontSendNotification);
  txtVol2Osc.setText(oscConfig.eVol2.getText(), juce::dontSendNotification);

  lblIp.setText("IP:", juce::dontSendNotification);
  lblPOut.setText("POut:", juce::dontSendNotification);
  lblPIn.setText("PIn:", juce::dontSendNotification);
  lblIn.setText("In:", juce::dontSendNotification);
  lblOut.setText("Out:", juce::dontSendNotification);
  lblCh.setText("CH:", juce::dontSendNotification);

  // --- FOCUS & KEYBOARD SETUP ---

  // Prevent child components from stealing focus permanently
  horizontalKeyboard.setWantsKeyboardFocus(false);
  verticalKeyboard.setWantsKeyboardFocus(false);
  verticalKeyboard.setKeyWidth(30);

  takeSnapshot();

  // ==================== LINK & GUI ====================
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
    logPanel.log(enabled ? "Link Enabled" : "Link Disabled");
  };

  // LATENCY SLIDER: Controls Lookahead Buffer Size
  addAndMakeVisible(latencySlider);
  latencySlider.setRange(0.0, 200.0,
                         1.0);  // Changed to 0-200ms positive for buffering
  latencySlider.setValue(20.0); // Default 20ms buffer
  latencySlider.setSliderStyle(juce::Slider::LinearHorizontal);
  latencySlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 15);
  latencySlider.onValueChange = [this] {
    grabKeyboardFocus(); // Ensure sliding doesn't kill keyboard input
  };
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

        // Sync the main BPM slider UI
        juce::MessageManager::callAsync([this, bpm] {
          tempoSlider.setValue(bpm, juce::dontSendNotification);
        });
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

  // --- OSC & CONTROL OVERLAYS ---
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
    if (currentView == AppView::Control)
      setView(AppView::Dashboard);
    else
      setView(AppView::Control);
  };
  addAndMakeVisible(btnOscCfg);
  btnOscCfg.onClick = [this] {
    if (currentView == AppView::OSC_Config)
      setView(AppView::Dashboard);
    else
      setView(AppView::OSC_Config);
  };
  addAndMakeVisible(btnHelp);
  btnHelp.onClick = [this] {
    if (currentView == AppView::Help)
      setView(AppView::Dashboard);
    else
      setView(AppView::Help);
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
        logPanel.log("OSC Connected");
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
      logPanel.log("OSC Disconnected");
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

  // Enter key support for BPM display
  addAndMakeVisible(tempoSlider);
  tempoSlider.setRange(20, 444, 0.1); // Higher resolution
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
    if (cmbMidiIn.getSelectedId() == 2) {
      // Virtual Keyboard handled in handleNoteOn/Off
    } else if (cmbMidiIn.getSelectedId() > 2) {
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
  btnClearPR.onClick = [this] {
    juce::ScopedLock sl(midiLock);
    playbackSeq.clear();
    sequenceLength = 0;
    trackGrid.loadSequence(playbackSeq);
    repaint();
    grabKeyboardFocus();
  };

  // tempoSlider logic moved up for priority

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
    if (link) {
      auto state = link->captureAppSessionState();
      state.setTempo(target, link->clock().micros());
      link->commitAppSessionState(state);
    }
    grabKeyboardFocus();
  };

  addAndMakeVisible(btnPrOctUp);
  addAndMakeVisible(btnPrOctDown);
  btnPrOctUp.onClick = [this] {
    pianoRollOctaveShift++;
    grabKeyboardFocus();
  };
  btnPrOctDown.onClick = [this] {
    pianoRollOctaveShift--;
    grabKeyboardFocus();
  };
  btnPlay.onClick = [this] {
    if (isPlaying)
      return;

    logPanel.log("Transport: Waiting for Bar start...");
    pendingSyncStart = true;
    isPlaying = true;
    playbackCursor = 0;
    lastProcessedBeat = -1.0;
    grabKeyboardFocus();
  };

  btnStop.onClick = [this] {
    if (!isPlaying)
      return;
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
    loadMidiFile(juce::File(playlist.getPrevFile()));
  };
  btnSkip.onClick = [this] {
    loadMidiFile(juce::File(playlist.getNextFile()));
  };
  btnResetFile.onClick = [this] {
    if (playlist.files.size())
      loadMidiFile(juce::File(playlist.files[0]));
  };

  addAndMakeVisible(trackGrid);
  addAndMakeVisible(horizontalKeyboard);
  addAndMakeVisible(verticalKeyboard);
  addAndMakeVisible(logPanel);
  addAndMakeVisible(playlist);
  addAndMakeVisible(sequencer);
  addAndMakeVisible(mixerViewport);
  mixer.setBounds(0, 0, 16 * mixer.stripWidth, 150);
  mixerViewport.setViewedComponent(&mixer, false);
  mixerViewport.setScrollBarsShown(false, true);

  addAndMakeVisible(lblArp);
  addAndMakeVisible(grpArp);
  addAndMakeVisible(btnArp);
  addAndMakeVisible(btnArpSync);
  addAndMakeVisible(sliderArpSpeed);
  sliderArpSpeed.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  sliderArpSpeed.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 18);
  sliderArpSpeed.setRange(10, 500, 1);
  sliderArpSpeed.setColour(juce::Slider::thumbColourId, Theme::accent);

  addAndMakeVisible(sliderArpVel);
  sliderArpVel.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  sliderArpVel.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 18);
  sliderArpVel.setRange(0, 127, 1);
  sliderArpVel.setColour(juce::Slider::thumbColourId, Theme::accent);
  lblArpBpm.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(lblArpVel);
  lblArpVel.setJustificationType(juce::Justification::centred);

  sliderArpSpeed.setRange(10, 500, 10);
  sliderArpSpeed.setValue(150);
  sliderArpSpeed.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  sliderArpSpeed.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  sliderArpVel.setRange(0, 127, 1);
  sliderArpVel.setValue(100);
  sliderArpVel.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  sliderArpVel.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

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
  };
  mixer.onChannelToggle = [this](int ch, bool active) {
    toggleChannel(ch, active);
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

      // Also send as generic MIDI CC if it's a slider
      if (c->isSlider) {
        auto m = juce::MidiMessage::controllerEvent(getSelectedChannel(), 12,
                                                    (int)(val * 127.0f));
        if (midiOutput)
          midiOutput->sendMessageNow(m);
      }
    };
  }

  addChildComponent(helpText);
  helpText.setMultiLine(true);
  helpText.setReadOnly(true);
  helpText.setFont(juce::FontOptions(13.0f));
  helpText.setText(
      "Patchworld Bi-Directional MIDI-OSC Bridge Player\n"
      "------------------------------------------------------\n\n"
      "FINDING YOUR LOCAL IPV4 ADDRESS:\n"
      "- Windows search Terminal, open and type ipconfig Enter to see "
      "addresses - Find your IPV4 Address.\n"
      "- In Headset open, Settings > Network Wi-Fi > Select current Network > "
      "Scroll to find 'IPv4 Address'.\n"
      "- IPV4 could appear as 192.168.0.0 / 10.0.0.0 / 172.16.0.0 (will vary)\n"
      "- Defualt IP is generic so often requires you to manually set ip "
      "(Disconnect/Connect to Reset) \n\n"
      "- NOTICE! Connection may not be Bi-Directional! Check each devices ipv4 "
      "it may only one-way communication based on the devices unique ipv4. "
      "Current known issue is .mid playback is out of sync with Ableton Link - "
      "Use DAW MIDI Passthrough via loopMIDI for seamless playback\n\n"
      "SETUP:\n"
      "- REQUIRES loopMIDI for DAW passthrough:\n"
      "- Use 'loopMIDI' (Tobias Erichsen) to create virtual MIDI ports.\n"
      "- Create two virtual midi ports - Patch - PC - (find the + bottom left "
      "of loopMidi)\n"
      "- Ensure IP matches and Port In (PIn) / Port Out (POut) match between "
      "devices (enter manually if fails).\n"
      "- In DAW of choice set MIDI to Out/In (Ch1-16) to your new loopMIDI "
      "Patch/PC ports\n"
      "- In OSC Bridge set MIDI In to Patch and MIDI Out to PC and press "
      "Connect to start OSC server\n"
      "- Join a World (Check Beesplease/BeeTeam's World) or Spawn in Midi-OSC "
      "Devices\n\n"
      "SOME FUNCTIONS:\n"
      "- OSC addresses can be changed to what ever you wish - Just make sure "
      "they match between devices!\n"
      "- Some on screen elements can be controled via OSC (check OSC Config)\n"
      "- .mid playlist controls are play/pause, stop (resets .mid), </> "
      "(playlist -/+), clear (clears .mid)\n"
      "- Sequencer: When playback start the seq will send /ch1note (#) \n"
      "- Mixer: Channel faders send /ch{X}cc, 'ON' buttons send /ch{X}cc (1)\n"
      "- Virtual Keyboard: Use keys A-L (white keys), W-P black keys) Octave - "
      "Z = Down, X = Up.\n"
      "- On Screen Oct-/Oct+ buttons shift playlist .mid files an octave "
      "up/down\n"
      "- Retrig when toggled will send a duplicate message when key is "
      "released (note off)\n\n"
      "THE BEST INFO:\n"
      "- This is an ongoing passion project. Expect bugs and performance "
      "issues...\n"
      "- Adjust the latency slider in link section to manually sync .mid "
      "playback .\n"
      "- Built using JUCE Framework - an Open Source Project\n"
      "- Librairies from Ableton Link - an Open Source Project\n"
      "- Check out our TouchOSC Ultimate Midi-OSC Bi-Directional Passthrough "
      "Controller (Discord for Download)\n"
      "- Made with <3 by Beesplease24601 - Devices by R.A.S (Find in "
      "Patch!)\n\n"
      "------------------------------------------------------");

  setSize(720, 630);
  link->enable(true);
  link->enableStartStopSync(true);

  // Start timers explicitly
  juce::Timer::startTimer(40);              // 25Hz UI
  juce::HighResolutionTimer::startTimer(1); // 1000Hz Audio/MIDI

  // FINAL INIT
  currentView = AppView::Dashboard;
  updateVisibility();
  resized();
}

// ==================== AGGRESSIVE FOCUS GRAB ====================
void MainComponent::mouseDown(const juce::MouseEvent &) { grabKeyboardFocus(); }

bool MainComponent::keyPressed(const juce::KeyPress &key, Component *) {
  // 1. Octave Shortcuts
  if (key.getKeyCode() == 'Z') {
    virtualOctaveShift = juce::jmax(-2, virtualOctaveShift - 1);
    return true;
  }
  if (key.getKeyCode() == 'X') {
    virtualOctaveShift = juce::jmin(2, virtualOctaveShift + 1);
    return true;
  }

  // 2. Map QWERTY keys to MIDI notes
  static const std::map<int, int> keyToNote = {
      {'A', 60}, {'W', 61}, {'S', 62}, {'E', 63}, {'D', 64}, {'F', 65},
      {'T', 66}, {'G', 67}, {'Y', 68}, {'H', 69}, {'U', 70}, {'J', 71},
      {'K', 72}, {'O', 73}, {'L', 74}, {'P', 75}, {';', 76}};

  int keyCode = key.getKeyCode();
  auto it = keyToNote.find(keyCode);
  if (it != keyToNote.end()) {
    int note = it->second + (virtualOctaveShift * 12);
    note = juce::jlimit(0, 127, note);
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
  juce::String argVal = "";

  if (m.size() > 0) {
    if (m[0].isFloat32())
      argVal = juce::String(m[0].getFloat32(), 2);
    else if (m[0].isInt32())
      argVal = juce::String(m[0].getInt32());
  }

  juce::MessageManager::callAsync(
      [this, addr, argVal] { logPanel.log("RX: " + addr + " " + argVal); });

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
      vol1Simple.setValue(val * 100.0f, juce::sendNotification);
    });
    return;
  }
  if (addr == oscConfig.eVol2.getText()) {
    juce::MessageManager::callAsync([this, val] {
      vol2Simple.setValue(val * 100.0f, juce::sendNotification);
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
    float velocity = lastVel.count(ch) ? lastVel[ch] : 0.8f;
    keyboardState.noteOn(ch, lastNote[ch], velocity);
    double durationMs = 50.0 + (velocity * 800.0);
    ActiveNote an;
    an.channel = ch;
    an.note = lastNote[ch];
    an.releaseTime = juce::Time::getMillisecondCounterHiRes() + durationMs;
    activeVirtualNotes.push_back(an);
    return;
  }

  ch = matchOscChannel(oscConfig.eRXnv.getText(), addr);
  if (ch > 0) {
    lastVel[ch] = val;
    return;
  }

  ch = matchOscChannel(oscConfig.eRXnoff.getText(), addr);
  if (ch > 0) {
    keyboardState.noteOff(ch, (int)val, 0.0f);
    return;
  }

  ch = matchOscChannel(oscConfig.eRXc.getText(), addr);
  if (ch > 0) {
    lastCC[ch] = (int)val;
    return;
  }

  ch = matchOscChannel(oscConfig.eRXcv.getText(), addr);
  if (ch > 0) {
    juce::MidiMessage msg =
        juce::MidiMessage::controllerEvent(ch, lastCC[ch], (int)val);
    if (midiOutput)
      midiOutput->sendMessageNow(msg);
    return;
  }
} // <--- End of oscMessageReceived

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
  juce::String addr, valStr;

  if (m.isNoteOn()) {
    addr = oscConfig.eTXn.getText().replace("{X}", customName);
    oscSender.send(addr, (float)m.getNoteNumber(), m.getVelocity() / 127.0f);
  } else if (m.isNoteOff()) {
    if (btnRetrigger.getToggleState()) {
      addr = oscConfig.eTXn.getText().replace("{X}", customName);
      oscSender.send(addr, (float)m.getNoteNumber(), 100.0f / 127.0f);
    } else {
      addr = oscConfig.eTXoff.getText().replace("{X}", customName);
      oscSender.send(addr, (float)m.getNoteNumber(), 0.0f);
    }
  }
} // <--- End of sendSplitOscMessage

void MainComponent::handleNoteOn(juce::MidiKeyboardState *, int ch, int note,
                                 float vel) {
  if (vel == 0.0f) {
    handleNoteOff(nullptr, ch, note, 0.0f);
    return;
  }
  int adj = juce::jlimit(0, 127, note + (virtualOctaveShift * 12));
  if (btnArp.getToggleState()) {
    heldNotes.add(adj);
    noteArrivalOrder.push_back(adj);
  } else {
    juce::MidiMessage m = juce::MidiMessage::noteOn(ch, adj, vel);
    sendSplitOscMessage(m);
    if (midiOutput)
      midiOutput->sendMessageNow(m);
  }
}

void MainComponent::handleNoteOff(juce::MidiKeyboardState *, int ch, int note,
                                  float vel) {
  int adj = juce::jlimit(0, 127, note + (virtualOctaveShift * 12));
  if (btnArp.getToggleState()) {
    heldNotes.removeFirstMatchingValue(adj);
  } else {
    juce::MidiMessage m = juce::MidiMessage::noteOff(ch, adj, vel);
    sendSplitOscMessage(m);
    if (midiOutput)
      midiOutput->sendMessageNow(m);
  }
}

void MainComponent::hiResTimerCallback() {
  if (!link)
    return;
  double nowMs = juce::Time::getMillisecondCounterHiRes();
  for (auto it = activeVirtualNotes.begin(); it != activeVirtualNotes.end();) {
    if (nowMs >= it->releaseTime) {
      keyboardState.noteOff(it->channel, it->note, 0.0f);
      it = activeVirtualNotes.erase(it);
    } else {
      ++it;
    }
  }

  auto session = link->captureAppSessionState();
  auto now = link->clock().micros();
  double linkBpm = session.tempo();

  double latencyVal = latencySlider.getValue();
  auto lookahead = std::chrono::microseconds((long)(latencyVal * 1000.0));
  auto futureTime = now + lookahead;
  double lookaheadMs = latencyVal;

  if (isPlaying) {
    double currentBeat = session.beatAtTime(now, quantum);
    double phase = session.phaseAtTime(now, quantum);

    if (pendingSyncStart) {
      if (phase < 0.05) {
        transportStartBeat = currentBeat;
        lastProcessedBeat = -1.0;
        pendingSyncStart = false;
        logPanel.log("Transport: Bar Start Sync.");

        if (!session.isPlaying()) {
          session.setIsPlayingAndRequestBeatAtTime(true, now, currentBeat,
                                                   quantum);
          link->commitAppSessionState(session);
        }
        if (isOscConnected)
          oscSender.send(oscConfig.ePlay.getText(), 1.0f);
      } else {
        return; // Wait for the actual bar start
      }
    }

    double playbackBeats = currentBeat - transportStartBeat;

    if (playbackBeats < 0) {
      // Still waiting if somehow currentBeat < transportStartBeat
      return;
    }

    double lookaheadBeats = (lookaheadMs / 1000.0) * (linkBpm / 60.0);
    double rangeEnd = playbackBeats + lookaheadBeats;

    juce::ScopedLock sl(midiLock);

    // FIXED: The 'while' keyword must be before the bracket!
    while (playbackCursor < playbackSeq.getNumEvents()) {
      auto *ev = playbackSeq.getEventPointer(playbackCursor);
      double eventBeat = ev->message.getTimeStamp() / ticksPerQuarterNote;

      // If the note is further in the future than our lookahead window, stop
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

        // Trigger sync-accurate messages
        sendSplitOscMessage(m, ch);
        if (midiOutput)
          midiOutput->sendMessageNow(m);
      }
      playbackCursor++;
    }

    lastProcessedBeat = rangeEnd;

    if (playbackCursor >= playbackSeq.getNumEvents() && sequenceLength > 0) {
      if (playlist.btnLoop.getToggleState()) {
        playbackCursor = 0;
        lastProcessedBeat = -1.0;
        transportStartBeat = currentBeat;
      } else {
        isPlaying = false;
        session.setIsPlayingAndRequestBeatAtTime(false, now, currentBeat,
                                                 quantum);
        link->commitAppSessionState(session);
      }
    }
  } else if (link->isEnabled() && session.isPlaying()) {
    // SYNCED START: Wait for exactly the next quantum boundary
    double beats = session.beatAtTime(futureTime, quantum);
    double phase = session.phaseAtTime(futureTime, quantum);

    // Attempt to align with next bar if we just started
    if (phase < 0.05) {
      isPlaying = true;
      playbackCursor = 0;
      lastProcessedBeat = -1.0;
      transportStartBeat = std::floor(beats / quantum) * quantum;
    }
  }

  // VISUALS + SEQUENCER
  if (link->isEnabled() && isPlaying) {
    double b = session.beatAtTime(now, quantum);

    // Calculate rate multiplier
    double rateMult = 1.0;
    switch (sequencer.cmbRate.getSelectedId()) {
    case 1:
      rateMult = 0.25;
      break; // 1/1 (1 step per bar of 4 beats)
    case 2:
      rateMult = 0.5;
      break; // 1/2
    case 3:
      rateMult = 1.0;
      break; // 1/4 (Default)
    case 4:
      rateMult = 2.0;
      break; // 1/8
    case 5:
      rateMult = 4.0;
      break; // 1/16
    case 6:
      rateMult = 8.0;
      break; // 1/32
    }

    // ROLL OVERRIDE
    if (sequencer.activeRollDiv > 0) {
      // Use a high-speed clock for the roll effect
      double rollRate = (double)sequencer.activeRollDiv;
      int rollStep = (int)std::floor(b * rollRate) % sequencer.numSteps;
      if (rollStep != stepSeqIndex) {
        stepSeqIndex = rollStep;
        juce::MessageManager::callAsync(
            [this, rollStep] { sequencer.setActiveStep(rollStep); });
        if (sequencer.isStepActive(rollStep)) {
          int noteNum = (int)sequencer.noteSlider.getValue();
          sendSplitOscMessage(
              juce::MidiMessage::noteOn(getSelectedChannel(), noteNum, 0.8f));
        }
      }
    } else {
      // NORMAL SEQUENCER
      int step = (int)std::floor(b * rateMult) % sequencer.numSteps;
      if (step != stepSeqIndex) {
        stepSeqIndex = step;
        juce::MessageManager::callAsync(
            [this, step] { sequencer.setActiveStep(step); });
        if (sequencer.isStepActive(step)) {
          int noteNum = (int)sequencer.noteSlider.getValue();
          sendSplitOscMessage(
              juce::MidiMessage::noteOn(getSelectedChannel(), noteNum, 0.8f));
        }
      }
    }
  } else {
    // Transport stopped - ensure sequencer visuals reflect pause
    stepSeqIndex = -1;
    juce::MessageManager::callAsync([this] { sequencer.setActiveStep(-1); });
  }

  // ARP
  if (btnArp.getToggleState() && !heldNotes.isEmpty()) {
    static int arpCounter = 0;
    int threshold = 0;
    if (btnArpSync.getToggleState()) {
      threshold = (int)(15000.0 / (linkBpm > 0 ? linkBpm : 120.0));
    } else {
      threshold = (int)sliderArpSpeed.getValue();
    }
    if (++arpCounter >= threshold) {
      arpCounter = 0;
      int noteIdx = arpNoteIndex % heldNotes.size();
      int note =
          (cmbArpPattern.getSelectedId() == 5 && !noteArrivalOrder.empty())
              ? noteArrivalOrder[arpNoteIndex % noteArrivalOrder.size()]
              : heldNotes[noteIdx];
      arpNoteIndex++;
      float vel = (float)sliderArpVel.getValue() / 127.0f;
      sendSplitOscMessage(
          juce::MidiMessage::noteOn(getSelectedChannel(), note, vel));
    }
  }
}

void MainComponent::timerCallback() {
  if (!link)
    return;
  auto session = link->captureAppSessionState();
  auto now = link->clock().micros();
  double linkBpm = session.tempo();

  if (std::abs(bpmVal.get() - linkBpm) > 0.01) {
    parameters.setProperty("bpm", linkBpm, nullptr);
    tempoSlider.setValue(linkBpm, juce::dontSendNotification);
  }

  int peers = link->numPeers();
  if (peers != lastNumPeers) {
    logPanel.log("Link: " + juce::String(peers) + " Peer(s)");
    lastNumPeers = peers;
  }

  // Link Connection Retry (approx 5 seconds)
  if (link && !link->isEnabled()) {
    linkRetryCounter++;
    if (linkRetryCounter >= 50) { // 100ms * 50 = 5s
      linkRetryCounter = 0;
      logPanel.log("Link: Retrying connection...");
      link->enable(true);
    }
  }

  const double phase = session.phaseAtTime(now, quantum);
  phaseVisualizer.setPhase(phase, quantum);
}

void MainComponent::loadMidiFile(juce::File f) {
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
    if (ticksPerQuarterNote <= 0)
      ticksPerQuarterNote = 960.0;

    // NO LONGER CONVERTING TO SECONDS - WE USE BEATS DIRECTLY
    playbackSeq.clear();
    bool bpmFound = false;
    for (int i = 0; i < mf.getNumTracks(); ++i) {
      playbackSeq.addSequence(*mf.getTrack(i), 0);
      for (auto *ev : *mf.getTrack(i))
        if (ev->message.isTempoMetaEvent() && !bpmFound) {
          currentFileBpm = 60.0 / ev->message.getTempoSecondsPerQuarterNote();
          if (link) {
            auto sessionState = link->captureAppSessionState();
            sessionState.setTempo(currentFileBpm, link->clock().micros());
            link->commitAppSessionState(sessionState);
          }
          parameters.setProperty("bpm", currentFileBpm, nullptr);
          bpmFound = true;
        }
    }
    playbackSeq.updateMatchedPairs();
    sequenceLength = playbackSeq.getEndTime();
    trackGrid.loadSequence(playbackSeq);
    logPanel.log("Loaded: " + f.getFileName());
    grabKeyboardFocus();
  }
}

void MainComponent::sendPanic() {
  logPanel.log("!!! PANIC !!!");
  keyboardState.allNotesOff(getSelectedChannel());

  // Kill OSC notes on all common channels
  for (int ch = 1; ch <= 16; ++ch) {
    // Address format like /ch1note matching user config
    juce::String addr = "/ch" + juce::String(ch) + "note";
    oscSender.send(addr, 0.0f);

    // MIDI CC 123 (All Notes Off)
    auto mAllOff = juce::MidiMessage::allNotesOff(ch);
    auto mSoundOff = juce::MidiMessage::allSoundOff(ch);

    // Send to MIDI Out
    if (midiOutput) {
      midiOutput->sendMessageNow(mAllOff);
      midiOutput->sendMessageNow(mSoundOff);
    }

    // Also send Note Off with 0 velocity explicitly for all notes (FASTTT)
    // In a real panic, we might want to iterate some common notes,
    // but AllNotesOff CC is usually sufficient for MIDI hardware.
  }

  // Refresh UI
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
  bool isOverlay = !isDash;

  // Main Dashboard elements
  verticalKeyboard.setVisible(isDash && isSimpleMode);
  horizontalKeyboard.setVisible(isDash && !isSimpleMode);
  trackGrid.setVisible(isDash && !isSimpleMode);
  mixerViewport.setVisible(isDash && !isSimpleMode);
  sequencer.setVisible(isDash && !isSimpleMode);
  playlist.setVisible(isDash); // Always visible on Dashboard
  logPanel.setVisible(isDash);
  grpArp.setVisible(isDash && !isSimpleMode);

  // Arp specific (Ensure pattern dropdown is back)
  cmbArpPattern.setVisible(isDash && !isSimpleMode);
  lblArpBpm.setVisible(isDash && !isSimpleMode);
  lblArpVel.setVisible(isDash && !isSimpleMode);
  sliderArpSpeed.setVisible(isDash && !isSimpleMode);
  sliderArpVel.setVisible(isDash && !isSimpleMode);
  btnArp.setVisible(isDash && !isSimpleMode);
  btnArpSync.setVisible(isDash && !isSimpleMode);

  // Link / Tempo elements
  cmbQuantum.setVisible(isDash);
  btnLinkToggle.setVisible(isDash);
  phaseVisualizer.setVisible(isDash && !isSimpleMode);
  btnTapTempo.setVisible(isDash);
  btnPrOctUp.setVisible(isDash);
  btnPrOctDown.setVisible(isDash);
  latencySlider.setVisible(isDash);
  lblLatency.setVisible(isDash);
  btnRetrigger.setVisible(isDash);
  btnPanic.setVisible(isDash);

  // Mixer visibility
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

  // Hide sections when in Control View to make room
  bool hideSections = (currentView == AppView::Control);
  grpNet.setVisible(!hideSections);
  grpIo.setVisible(!hideSections);
  lblLocalIpHeader.setVisible(!isSimpleMode && !hideSections);
  lblLocalIpDisplay.setVisible(isDash && !isSimpleMode && !hideSections);

  // Overlays
  oscViewport.setVisible(currentView == AppView::OSC_Config);
  helpText.setVisible(currentView == AppView::Help);
  controlPage.setVisible(currentView == AppView::Control);

  // If overlays are hidden, force bounds to zero to ensure no click-blocking
  if (currentView == AppView::Dashboard) {
    oscViewport.setBounds(0, 0, 0, 0);
    helpText.setBounds(0, 0, 0, 0);
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

  // Panic button in top menu for Simple mode - Narrow
  if (isSimpleMode) {
    btnPanic.setButtonText("P");
    btnPanic.setBounds(menu.removeFromRight(30).reduced(2));
  } else {
    btnPanic.setButtonText("PANIC");
  }

  auto topRight =
      menu.removeFromRight(180); // Increased from 140 to move panic left

  if (currentView == AppView::OSC_Config) {
    int overlayW = isSimpleMode ? 420 : 500;
    auto r = getLocalBounds().withSizeKeepingCentre(overlayW, 450).withY(110);
    oscViewport.setBounds(r);
    helpText.setBounds(0, 0, 0, 0);
    controlPage.setBounds(0, 0, 0, 0);
    return;
  }
  if (currentView == AppView::Help) {
    int helpW = isSimpleMode ? 420 : 500;
    helpText.setBounds(
        getLocalBounds().withSizeKeepingCentre(helpW, 400).withY(140));
    oscViewport.setBounds(0, 0, 0, 0);
    controlPage.setBounds(0, 0, 0, 0);
    return;
  }
  if (currentView == AppView::Control) {
    int ctrlW = isSimpleMode ? 450 : 600;
    controlPage.setBounds(
        getLocalBounds().withSizeKeepingCentre(ctrlW, 420).withY(160));
    oscViewport.setBounds(0, 0, 0, 0);
    helpText.setBounds(0, 0, 0, 0);
    return;
  }

  if (isSimpleMode) {
    auto topStack = area.removeFromTop(160);
    grpNet.setBounds(topStack.removeFromTop(80).reduced(2));
    auto rNet = grpNet.getBounds().reduced(5, 15);
    int lblW = 35;
    int edW = 60;
    // IP RESTORED & VISIBLE correctly
    lblIp.setVisible(true);
    edIp.setVisible(true);
    // IP & Ports Labels
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
    rMidi.removeFromLeft(5);
    lblCh.setBounds(rMidi.removeFromLeft(25));
    cmbMidiCh.setBounds(rMidi.removeFromLeft(70));
    rMidi.removeFromLeft(5);
    lblOut.setBounds(rMidi.removeFromLeft(25));
    cmbMidiOut.setBounds(rMidi.removeFromLeft(80));
    btnRetrigger.setBounds(rMidi.removeFromRight(60).reduced(2));

    auto bottomBar = area.removeFromBottom(50);
    btnPlay.setBounds(bottomBar.removeFromLeft(50).reduced(2));
    btnStop.setBounds(bottomBar.removeFromLeft(50).reduced(2));
    btnPrev.setBounds(bottomBar.removeFromLeft(30).reduced(2));
    btnSkip.setBounds(bottomBar.removeFromLeft(30).reduced(2));
    btnResetFile.setButtonText("Rst");
    btnResetFile.setBounds(bottomBar.removeFromLeft(40).reduced(2));
    btnClearPR.setButtonText("Clr");
    btnClearPR.setBounds(bottomBar.removeFromLeft(40).reduced(2));
    btnResetBPM.setBounds(bottomBar.removeFromLeft(70).reduced(2));
    bottomBar.removeFromLeft(5);
    lblTempo.setBounds(bottomBar.removeFromLeft(40));
    tempoSlider.setBounds(
        bottomBar.removeFromLeft(150).reduced(0, 5)); // Widened to 150

    // Middle Section - Log and Playlist stack
    auto midArea = area;
    verticalKeyboard.setBounds(midArea.removeFromLeft(40));

    auto rightSyncArea = midArea.removeFromRight(150).reduced(5);
    // ... sync area positioning remains similar ...
    rightSyncArea.removeFromTop(5);
    cmbQuantum.setBounds(rightSyncArea.removeFromTop(28).reduced(2));
    btnLinkToggle.setBounds(rightSyncArea.removeFromTop(28).reduced(2));
    auto latRow = rightSyncArea.removeFromTop(28);
    lblLatency.setBounds(latRow.removeFromLeft(50));
    latencySlider.setBounds(latRow);
    phaseVisualizer.setBounds(rightSyncArea.removeFromTop(25).reduced(0, 2));
    btnTapTempo.setBounds(rightSyncArea.removeFromTop(30).reduced(2));

    // Oct Row - Reduced Height
    auto octRow = rightSyncArea.removeFromTop(45);
    btnPrOctDown.setBounds(octRow.removeFromLeft(70).reduced(2));
    btnPrOctUp.setBounds(octRow.reduced(2));

    // MIXER 1 & 2 in Simple Mode - Sliders + OSC Input
    rightSyncArea.removeFromTop(5);
    auto mixAreaSimple = rightSyncArea.removeFromTop(150);
    int sw = mixAreaSimple.getWidth() / 2;
    auto r1 = mixAreaSimple.removeFromLeft(sw).reduced(2);
    vol1Simple.setBounds(r1.removeFromTop(r1.getHeight() - 25));
    txtVol1Osc.setBounds(r1);

    auto r2 = mixAreaSimple.reduced(2);
    vol2Simple.setBounds(r2.removeFromTop(r2.getHeight() - 25));
    txtVol2Osc.setBounds(r2);

    auto dashboardContent = midArea.reduced(2);
    logPanel.setBounds(
        dashboardContent.removeFromTop(dashboardContent.getHeight() / 2)
            .reduced(0, 2));
    playlist.setBounds(dashboardContent);

    phaseVisualizer.setVisible(true); // Restore for sync visibility
    trackGrid.setVisible(false);      // Hide grid in Image 0 layout
  } else {
    // Normal View - Panic left of Retrig to fit GPU
    auto topButtons = topRight;
    btnPanic.setBounds(topButtons.removeFromLeft(60).reduced(2));
    btnRetrigger.setBounds(topButtons.removeFromLeft(65).reduced(2));
    btnGPU.setBounds(topButtons.removeFromLeft(55).reduced(2));
    auto strip = area.removeFromTop(80);
    grpNet.setBounds(strip.removeFromLeft(450).reduced(2));
    auto rNet = grpNet.getBounds().reduced(5, 15);
    int lblW = 35;
    int edW = 60;
    // Ensure IP is visible in Normal Mode
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
    cmbMidiIn.setBounds(rMidi.removeFromLeft(100));
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
    btnClearPR.setBounds(trans.removeFromLeft(100).reduced(2));
    trans.removeFromLeft(30);
    lblTempo.setBounds(trans.removeFromLeft(45));
    tempoSlider.setBounds(trans.removeFromLeft(225));
    btnResetBPM.setBounds(trans.removeFromLeft(70));
    btnPrOctUp.setBounds(trans.removeFromRight(50).reduced(2));
    btnPrOctDown.setBounds(trans.removeFromRight(50).reduced(2));

    auto bottomSection = area.removeFromBottom(120);
    mixerViewport.setBounds(
        bottomSection.removeFromLeft(8 * mixer.stripWidth + 20));
    mixer.setSize(16 * mixer.stripWidth, mixerViewport.getHeight() - 20);
    auto linkGuiArea = bottomSection.reduced(10, 5);
    auto row1 = linkGuiArea.removeFromTop(25);
    cmbQuantum.setBounds(row1.removeFromLeft(100));
    row1.removeFromLeft(10);
    btnLinkToggle.setBounds(row1);
    auto latRow = linkGuiArea.removeFromTop(20);
    lblLatency.setBounds(latRow.removeFromLeft(45));
    latencySlider.setBounds(latRow);
    phaseVisualizer.setBounds(linkGuiArea.removeFromTop(30).reduced(0, 5));
    btnTapTempo.setBounds(linkGuiArea.removeFromTop(25).reduced(20, 0));

    auto btmCtrl = area.removeFromBottom(120);
    sequencer.setBounds(btmCtrl.removeFromLeft(btmCtrl.getWidth() - 260));
    grpArp.setBounds(btmCtrl);
    auto rA = btmCtrl.reduced(5, 15);

    auto arpChecks = rA.removeFromLeft(60);
    btnArp.setBounds(arpChecks.removeFromTop(30).reduced(2));
    btnArpSync.setBounds(arpChecks.removeFromTop(30).reduced(2));

    auto s1 = rA.removeFromLeft(85);
    sliderArpSpeed.setBounds(s1.removeFromTop(65).reduced(2));
    lblArpBpm.setBounds(s1.removeFromBottom(20));

    auto s2 = rA.removeFromLeft(85);
    sliderArpVel.setBounds(s2.removeFromTop(65).reduced(2));
    lblArpVel.setBounds(s2.removeFromBottom(20));

    cmbArpPattern.setBounds(rA.reduced(0, 15));

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
  if (f[0].endsWith(".mid")) {
    playlist.addFile(f[0]);
    loadMidiFile(juce::File(f[0]));
  }
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
void MainComponent::prepareToPlay(int samplesPerBlockExpected,
                                  double sampleRate) {
  currentSampleRate = sampleRate;
}

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  auto session = link->captureAudioSessionState(); // Capture state for the
                                                   // start of the block
  auto now = link->clock().micros();

  if (isPlaying && session.isPlaying()) {
    // Use Latency Slider as a 'Safety Offset' in microseconds
    auto lookahead =
        std::chrono::microseconds((long)(latencySlider.getValue() * 1000.0));

    double startBeat = session.beatAtTime(now + lookahead, quantum);
    auto duration = std::chrono::microseconds(
        (long)((bufferToFill.numSamples / currentSampleRate) * 1000000.0));
    double endBeat = session.beatAtTime(now + lookahead + duration, quantum);

    juce::ScopedLock sl(midiLock);
    while (playbackCursor < playbackSeq.getNumEvents()) {
      auto *ev = playbackSeq.getEventPointer(playbackCursor);
      double eventBeat = ev->message.getTimeStamp() / ticksPerQuarterNote;

      if (eventBeat >= endBeat)
        break;

      // Calculate the precise sample offset within this buffer
      double ratio = (eventBeat - startBeat) / (endBeat - startBeat);
      int sampleOffset = juce::jlimit(0, bufferToFill.numSamples - 1,
                                      (int)(ratio * bufferToFill.numSamples));

      // Trigger Split OSC and MIDI immediately
      sendSplitOscMessage(ev->message);
      if (midiOutput)
        midiOutput->sendMessageNow(ev->message);

      playbackCursor++;
    }
  }
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
