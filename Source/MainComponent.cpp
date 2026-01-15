/*
  ==============================================================================
    MainComponent.cpp
    Status: OPTIMIZED (Dynamic Lookahead Buffer, Aggressive Focus)
  ==============================================================================
*/

#include "MainComponent.h"



MainComponent::MainComponent() : link(120.0)
{
    bpmVal.referTo(parameters, "bpm", &undoManager, 120.0);
    parameters.addListener(this);

    // Do this ONCE at startup
    auto myImage = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);
    logoView.setImage(myImage);
    addAndMakeVisible(logoView);

    // --- FOCUS & KEYBOARD SETUP ---
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true); // Auto-grab on background click
    addKeyListener(this);
    keyboardState.addListener(this);

    // Prevent child components from stealing focus permanently
    horizontalKeyboard.setWantsKeyboardFocus(false);
    verticalKeyboard.setWantsKeyboardFocus(false);
    verticalKeyboard.setKeyWidth(30);

    takeSnapshot();

    // ==================== LINK & GUI ====================
    addAndMakeVisible(cmbQuantum);
    cmbQuantum.addItemList({ "2 Beats", "3 Beats", "4 Beats (Bar)", "5 Beats", "8 Beats" }, 1);
    cmbQuantum.setSelectedId(3);
    cmbQuantum.onChange = [this] {
        int sel = cmbQuantum.getSelectedId();
        if (sel == 1) quantum = 2.0; else if (sel == 2) quantum = 3.0; else if (sel == 3) quantum = 4.0; else if (sel == 4) quantum = 5.0; else if (sel == 5) quantum = 8.0;
        };

    addAndMakeVisible(btnLinkToggle);
    btnLinkToggle.setToggleState(true, juce::dontSendNotification);
    btnLinkToggle.onClick = [this] {
        bool enabled = btnLinkToggle.getToggleState();
        link.enable(enabled); link.enableStartStopSync(enabled);
        logPanel.log(enabled ? "Link Enabled" : "Link Disabled");
        };

    // LATENCY SLIDER: Controls Lookahead Buffer Size
    addAndMakeVisible(latencySlider);
    latencySlider.setRange(0.0, 200.0, 1.0); // Changed to 0-200ms positive for buffering
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
        if (!tapTimes.empty() && (nowMs - tapTimes.back() > 2000.0)) { tapTimes.clear(); tapCounter = 0; }
        tapTimes.push_back(nowMs); tapCounter++;
        if (tapCounter >= 4) {
            double sumDiff = 0.0;
            for (size_t i = 1; i < tapTimes.size(); ++i) sumDiff += (tapTimes[i] - tapTimes[i - 1]);
            double avgDiff = sumDiff / (double)(tapTimes.size() - 1);
            if (avgDiff > 50.0) {
                double bpm = 60000.0 / avgDiff;
                bpm = juce::jlimit(20.0, 444.0, bpm);
                auto state = link.captureAppSessionState(); state.setTempo(bpm, link.clock().micros()); link.commitAppSessionState(state);
                parameters.setProperty("bpm", bpm, nullptr);
            }
            tapTimes.clear(); tapCounter = 0;
        }
        grabKeyboardFocus();
        };

    addAndMakeVisible(btnDash);
    btnDash.onClick = [this] {
        if (currentView != AppView::Dashboard) { setView(AppView::Dashboard); }
        else {
            isSimpleMode = !isSimpleMode;
            if (isSimpleMode) setSize(495, 630); else setSize(805, 630);
            updateVisibility(); resized();
        }
        };

    auto toggleView = [this](AppView v) { if (currentView == v) setView(AppView::Dashboard); else setView(v); };
    addAndMakeVisible(btnOscCfg); btnOscCfg.onClick = [this, toggleView] { toggleView(AppView::OSC_Config); };
    addAndMakeVisible(btnHelp); btnHelp.onClick = [this, toggleView] { toggleView(AppView::Help); };

    addAndMakeVisible(btnRetrigger); btnRetrigger.setButtonText("Retrig");
    addAndMakeVisible(btnGPU);
    btnGPU.onClick = [this] { if (btnGPU.getToggleState()) openGLContext.attachTo(*this); else openGLContext.detach(); };

    addAndMakeVisible(grpNet);
    addAndMakeVisible(lblIp); addAndMakeVisible(edIp); edIp.setText("127.0.0.1");
    lblIp.setJustificationType(juce::Justification::centredRight); edIp.setJustification(juce::Justification::centred);
    addAndMakeVisible(lblPOut); addAndMakeVisible(edPOut); edPOut.setText("3330");
    lblPOut.setJustificationType(juce::Justification::centredRight); edPOut.setJustification(juce::Justification::centred);
    addAndMakeVisible(lblPIn); addAndMakeVisible(edPIn); edPIn.setText("5550");
    lblPIn.setJustificationType(juce::Justification::centredRight); edPIn.setJustification(juce::Justification::centred);

    addAndMakeVisible(btnConnect);
    btnConnect.setClickingTogglesState(true);
    btnConnect.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red);
    btnConnect.onClick = [this] {
        if (btnConnect.getToggleState()) {
            if (oscSender.connect(edIp.getText(), edPOut.getText().getIntValue())) {
                oscReceiver.connect(edPIn.getText().getIntValue());
                oscReceiver.addListener(this);
                isOscConnected = true; ledConnect.isConnected = true;
                btnConnect.setButtonText("Disconnect");
                logPanel.log("OSC Connected");
                logPanel.resetStats();
            }
            else btnConnect.setToggleState(false, juce::dontSendNotification);
        }
        else {
            oscSender.disconnect(); oscReceiver.disconnect();
            isOscConnected = false; ledConnect.isConnected = false; ledConnect.repaint();
            btnConnect.setButtonText("Connect");
            logPanel.log("OSC Disconnected");
        }
        ledConnect.repaint();
        grabKeyboardFocus();
        };
    addAndMakeVisible(ledConnect);

    addAndMakeVisible(grpIo);
    addAndMakeVisible(lblIn); addAndMakeVisible(cmbMidiIn);
    addAndMakeVisible(lblOut); addAndMakeVisible(cmbMidiOut);
    addAndMakeVisible(lblCh); addAndMakeVisible(cmbMidiCh);
    cmbMidiCh.addItem("Send All", 17); for (int i = 1; i <= 16; ++i) cmbMidiCh.addItem(juce::String(i), i); cmbMidiCh.setSelectedId(17, juce::dontSendNotification);
    cmbMidiIn.addItem("Virtual Keyboard", 1);
    auto inputs = juce::MidiInput::getAvailableDevices(); for (int i = 0; i < inputs.size(); ++i) cmbMidiIn.addItem(inputs[i].name, i + 2);
    auto outputs = juce::MidiOutput::getAvailableDevices(); for (int i = 0; i < outputs.size(); ++i) cmbMidiOut.addItem(outputs[i].name, i + 1);

    cmbMidiIn.onChange = [this] {
        midiInput.reset();
        if (cmbMidiIn.getSelectedId() > 1) {
            midiInput = juce::MidiInput::openDevice(juce::MidiInput::getAvailableDevices()[cmbMidiIn.getSelectedId() - 2].identifier, this);
            if (midiInput) midiInput->start();
        }
        grabKeyboardFocus(); // ALWAYS GRAB FOCUS
        };
    cmbMidiOut.onChange = [this] {
        midiOutput.reset();
        if (cmbMidiOut.getSelectedId() > 0) midiOutput = juce::MidiOutput::openDevice(juce::MidiOutput::getAvailableDevices()[cmbMidiOut.getSelectedId() - 1].identifier);
        };

    addAndMakeVisible(btnPlay); addAndMakeVisible(btnStop);
    addAndMakeVisible(btnPrev); addAndMakeVisible(btnSkip);
    addAndMakeVisible(btnResetFile); addAndMakeVisible(btnLoopPlaylist);
    addAndMakeVisible(btnClearPR);
    btnClearPR.onClick = [this] {
        juce::ScopedLock sl(midiLock); playbackSeq.clear(); sequenceLength = 0; trackGrid.loadSequence(playbackSeq); repaint();
        grabKeyboardFocus();
        };

    addAndMakeVisible(tempoSlider); tempoSlider.setRange(20, 444, 1); tempoSlider.setValue(120);
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 20);
    tempoSlider.onValueChange = [this] {
        if (tempoSlider.isMouseButtonDown()) {
            double val = tempoSlider.getValue();
            parameters.setProperty("bpm", val, nullptr);
            auto state = link.captureAppSessionState(); state.setTempo(val, link.clock().micros()); link.commitAppSessionState(state);
        }
        };

    addAndMakeVisible(lblTempo); lblTempo.setText("BPM:", juce::dontSendNotification);
    addAndMakeVisible(btnResetBPM);
    btnResetBPM.onClick = [this] {
        double target = (currentFileBpm > 0.0) ? currentFileBpm : 120.0;
        auto state = link.captureAppSessionState(); state.setTempo(target, link.clock().micros()); link.commitAppSessionState(state);
        parameters.setProperty("bpm", target, nullptr);
        grabKeyboardFocus();
        };

    addAndMakeVisible(btnPrOctUp); addAndMakeVisible(btnPrOctDown);
    btnPrOctUp.onClick = [this] { pianoRollOctaveShift++; grabKeyboardFocus(); };
    btnPrOctDown.onClick = [this] { pianoRollOctaveShift--; grabKeyboardFocus(); };

    // Inside MainComponent constructor
    btnPlay.onClick = [this] {
        auto session = link.captureAppSessionState();
        auto now = link.clock().micros();

        bool newPlayingState = !session.isPlaying();

        if (link.isEnabled())
            session.setIsPlayingAndRequestBeatAtTime(newPlayingState, now, 0, quantum);
        else
            isPlaying = newPlayingState;

        link.commitAppSessionState(session);

        // --- ADDED OSC SEND LOGIC ---
        if (isOscConnected) {
            juce::String addr = oscConfig.ePlay.getText();
            oscSender.send(addr, (float)(newPlayingState ? 1.0f : 0.0f));
        }
        grabKeyboardFocus();
        };

    btnStop.onClick = [this] {
        auto session = link.captureAppSessionState();
        if (link.isEnabled())
            session.setIsPlayingAndRequestBeatAtTime(false, link.clock().micros(), 0, quantum);
        else
            isPlaying = false;

        link.commitAppSessionState(session);
        stopPlayback();

        // --- ADDED OSC SEND LOGIC ---
        if (isOscConnected) {
            juce::String addr = oscConfig.eStop.getText();
            oscSender.send(addr, 1.0f); // Send '1' to trigger stop on remote device
        }
        grabKeyboardFocus();
        };

    btnPrev.onClick = [this] { loadMidiFile(juce::File(playlist.getPrevFile())); };
    btnSkip.onClick = [this] { loadMidiFile(juce::File(playlist.getNextFile())); };
    btnResetFile.onClick = [this] { if (playlist.files.size()) loadMidiFile(juce::File(playlist.files[0])); };

    addAndMakeVisible(trackGrid);
    addAndMakeVisible(horizontalKeyboard); addAndMakeVisible(verticalKeyboard);
    addAndMakeVisible(logPanel); addAndMakeVisible(playlist); addAndMakeVisible(sequencer);
    addAndMakeVisible(mixerViewport); mixer.setBounds(0, 0, 16 * mixer.stripWidth, 150);
    mixerViewport.setViewedComponent(&mixer, false); mixerViewport.setScrollBarsShown(false, true);

    addAndMakeVisible(lblArp); addAndMakeVisible(grpArp);
    addAndMakeVisible(btnArp); addAndMakeVisible(btnArpSync);
    addAndMakeVisible(sliderArpSpeed); addAndMakeVisible(sliderArpVel); addAndMakeVisible(cmbArpPattern);
    addAndMakeVisible(lblArpBpm); lblArpBpm.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblArpVel); lblArpVel.setJustificationType(juce::Justification::centred);

    sliderArpSpeed.setRange(10, 500, 10); sliderArpSpeed.setValue(150); sliderArpSpeed.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); sliderArpSpeed.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sliderArpVel.setRange(0, 127, 1); sliderArpVel.setValue(100); sliderArpVel.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); sliderArpVel.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    cmbArpPattern.addItem("Up", 1); cmbArpPattern.addItem("Down", 2); cmbArpPattern.addItem("Up/Down", 3);
    cmbArpPattern.addItem("Down/Up", 7); cmbArpPattern.addItem("Play Order", 5); cmbArpPattern.addItem("Random", 6);
    cmbArpPattern.addItem("Diverge", 4);
    cmbArpPattern.setSelectedId(1);

    mixer.onMixerActivity = [this](int ch, float val) { sendSplitOscMessage(juce::MidiMessage::controllerEvent(ch, 7, (int)val)); };
    mixer.onChannelToggle = [this](int ch, bool active) {
        toggleChannel(ch, active);
        if (isOscConnected) {
            juce::String addr = oscConfig.eCC.getText().replace("{X}", juce::String(ch));
            oscSender.send(addr, active ? 1.0f : 0.0f);
        }
        };

    addChildComponent(oscConfig); addChildComponent(helpText);
    helpText.setMultiLine(true); helpText.setReadOnly(true); helpText.setFont(juce::FontOptions(13.0f));
    helpText.setText("Patchworld Bi-Directional MIDI-OSC Bridge Player\n"
        "------------------------------------------------------\n\n"
        "FINDING YOUR LOCAL IPV4 ADDRESS:\n"
        "- Windows search Terminal, open and type ipconfig Enter to see addresses - Find your IPV4 Address.\n"
        "- In Headset open, Settings > Network Wi-Fi > Select current Network > Scroll to find 'IPv4 Address'.\n"
        "- IPV4 could appear as 192.168.0.0 / 10.0.0.0 / 172.16.0.0 (will vary)\n"
        "- Defualt IP is generic so often requires you to manually set ip (Disconnect/Connect to Reset) \n\n"
        "- NOTICE! Connection may not be Bi-Directional! Check each devices ipv4 it may only one-way communication based on the devices unique ipv4. Current known issue is .mid playback is out of sync with Ableton Link - Use DAW MIDI Passthrough via loopMIDI for seamless playback\n\n"
        "SETUP:\n"
        "- REQUIRES loopMIDI for DAW passthrough:\n"
        "- Use 'loopMIDI' (Tobias Erichsen) to create virtual MIDI ports.\n"
        "- Create two virtual midi ports - Patch - PC - (find the + bottom left of loopMidi)\n"
        "- Ensure IP matches and Port In (PIn) / Port Out (POut) match between devices (enter manually if fails).\n"
        "- In DAW of choice set MIDI to Out/In (Ch1-16) to your new loopMIDI Patch/PC ports\n"
        "- In OSC Bridge set MIDI In to Patch and MIDI Out to PC and press Connect to start OSC server\n"
        "- Join a World (Check Beesplease/BeeTeam's World) or Spawn in Midi-OSC Devices\n\n"
        "SOME FUNCTIONS:\n"
        "- OSC addresses can be changed to what ever you wish - Just make sure they match between devices!\n"
        "- Some on screen elements can be controled via OSC (check OSC Config)\n"
        "- .mid playlist controls are play/pause, stop (resets .mid), </> (playlist -/+), clear (clears .mid)\n"
        "- Sequencer: When playback start the seq will send /ch1note (#) \n"
        "- Mixer: Channel faders send /ch{X}cc, 'ON' buttons send /ch{X}cc (1)\n"
        "- Virtual Keyboard: Use keys A-L (white keys), W-P black keys) Octave - Z = Down, X = Up.\n"
		"- On Screen Oct-/Oct+ buttons shift playlist .mid files an octave up/down\n"
        "- Retrig when toggled will send a duplicate message when key is released (note off)\n\n"
        "THE BEST INFO:\n"
        "- This is an ongoing passion project. Expect bugs and performance issues...\n"
        "- Adjust the latency slider in link section to manually sync .mid playback .\n"
        "- Built using JUCE Framework - an Open Source Project\n"
        "- Librairies from Ableton Link - an Open Source Project\n"
        "- Check out our TouchOSC Ultimate Midi-OSC Bi-Directional Passthrough Controller (Discord for Download)\n"
        "- Made with <3 by Beesplease24601 - Devices by R.A.S (Find in Patch!)\n\n"
        "------------------------------------------------------");

    setSize(720, 630);
    link.enable(true); link.enableStartStopSync(true);

    // Start timers explicitly
    juce::Timer::startTimer(40); // 25Hz UI
    juce::HighResolutionTimer::startTimer(1); // 1000Hz Audio/MIDI
}

MainComponent::~MainComponent() {
    juce::Timer::stopTimer();
    juce::HighResolutionTimer::stopTimer();
    openGLContext.detach();
    keyboardState.removeListener(this);
}

// ==================== AGGRESSIVE FOCUS GRAB ====================
void MainComponent::mouseDown(const juce::MouseEvent&) {
    grabKeyboardFocus();
}

bool MainComponent::keyPressed(const juce::KeyPress& key, Component*) {
    // 1. Octave Shortcuts
    if (key.getKeyCode() == 'Z') { virtualOctaveShift = juce::jmax(-2, virtualOctaveShift - 1); return true; }
    if (key.getKeyCode() == 'X') { virtualOctaveShift = juce::jmin(2, virtualOctaveShift + 1); return true; }

    // 2. Map QWERTY keys to MIDI notes
    static const std::map<int, int> keyToNote = {
            {'A', 60}, {'W', 61}, {'S', 62}, {'E', 63}, {'D', 64}, {'F', 65}, {'T', 66}, {'G', 67},
            {'Y', 68}, {'H', 69}, {'U', 70}, {'J', 71}, {'K', 72}, {'O', 73}, {'L', 74}, {'P', 75}, {';', 76}
    };

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

void MainComponent::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) {
    if (property == juce::Identifier("bpm")) {
        double val = (double)tree.getProperty(property);
        if (!tempoSlider.isMouseButtonDown()) tempoSlider.setValue(val, juce::dontSendNotification);
    }
}

int MainComponent::matchOscChannel(const juce::String& pattern, const juce::String& incoming) {
    for (int i = 1; i <= 16; ++i) if (incoming == pattern.replace("{X}", juce::String(i))) return i;
    return -1;
}

void MainComponent::oscMessageReceived(const juce::OSCMessage& m) {
    juce::String addr = m.getAddressPattern().toString();
    juce::String argVal = "";
    if (m.size() > 0) {
        if (m[0].isFloat32()) argVal = juce::String(m[0].getFloat32(), 2);
        else if (m[0].isInt32()) argVal = juce::String(m[0].getInt32());
    }
    juce::MessageManager::callAsync([this, addr, argVal] { logPanel.log("RX: " + addr + " " + argVal); });

    if (addr == oscConfig.ePlay.getText()) { juce::MessageManager::callAsync([this] { btnPlay.triggerClick(); }); return; }
    if (addr == oscConfig.eStop.getText()) { juce::MessageManager::callAsync([this] { btnStop.triggerClick(); }); return; }
    if (addr == oscConfig.eRew.getText()) { juce::MessageManager::callAsync([this] { btnPrev.triggerClick(); }); return; }
    if (addr == oscConfig.eLoop.getText()) { juce::MessageManager::callAsync([this] { btnLoopPlaylist.setToggleState(!btnLoopPlaylist.getToggleState(), juce::sendNotification); }); return; }
    if (addr == oscConfig.eTap.getText()) { juce::MessageManager::callAsync([this] { btnTapTempo.triggerClick(); }); return; }
    if (addr == oscConfig.eOctUp.getText()) { juce::MessageManager::callAsync([this] { btnPrOctUp.triggerClick(); }); return; }
    if (addr == oscConfig.eOctDn.getText()) { juce::MessageManager::callAsync([this] { btnPrOctDown.triggerClick(); }); return; }

    float val = 0.0f;
    if (m.size() > 0 && (m[0].isFloat32() || m[0].isInt32())) val = m[0].isFloat32() ? m[0].getFloat32() : (float)m[0].getInt32();

    int ch = matchOscChannel(oscConfig.eN.getText(), addr);
    if (ch > 0) {
        juce::MidiMessage midi = juce::MidiMessage::noteOn(ch, (int)val, (m.size() > 1 && m[1].isFloat32()) ? m[1].getFloat32() : 1.0f);
        if (midiOutput) midiOutput->sendMessageNow(midi);
        return;
    }
    ch = matchOscChannel(oscConfig.eOff.getText(), addr);
    if (ch > 0) {
        juce::MidiMessage midi = juce::MidiMessage::noteOff(ch, (int)val);
        if (midiOutput) midiOutput->sendMessageNow(midi);
        return;
    }
    ch = matchOscChannel(oscConfig.eCC.getText(), addr);
    if (ch > 0) {
        juce::MidiMessage midi = juce::MidiMessage::controllerEvent(ch, (int)val, (m.size() > 1) ? (int)(m[1].isFloat32() ? m[1].getFloat32() : m[1].getInt32()) : 0);
        if (midiOutput) midiOutput->sendMessageNow(midi);
        return;
    }
}

void MainComponent::sendSplitOscMessage(const juce::MidiMessage& m, int overrideChannel) {
    if (!isOscConnected) return;
    int ch = (overrideChannel != -1) ? overrideChannel : (cmbMidiCh.getSelectedId() == 17 ? (m.getChannel() > 0 ? m.getChannel() : 1) : cmbMidiCh.getSelectedId());
    if (ch < 1 || ch > 16) ch = 1;
    juce::String customName = mixer.getChannelName(ch);
    juce::String addr, val;

    if (m.isNoteOn()) {
        addr = oscConfig.eN.getText().replace("{X}", customName);
        val = juce::String(m.getNoteNumber()) + " " + juce::String(m.getVelocity());
        oscSender.send(addr, (float)m.getNoteNumber(), (float)m.getVelocity());
    }
    else if (m.isNoteOff()) {
        addr = (btnRetrigger.getToggleState() ? oscConfig.eN : oscConfig.eOff).getText().replace("{X}", customName);
        val = juce::String(m.getNoteNumber()) + " 0";
        oscSender.send(addr, (float)m.getNoteNumber(), 0.0f);
    }
    else if (m.isController()) {
        addr = oscConfig.eCC.getText().replace("{X}", customName);
        val = juce::String(m.getControllerNumber()) + " " + juce::String(m.getControllerValue());
        oscSender.send(addr, (float)m.getControllerNumber(), (float)m.getControllerValue());
    }

    // LOGGING
    if (addr.isNotEmpty()) {
        juce::MessageManager::callAsync([this, addr, val] { logPanel.log("! " + addr + " " + val); });
    }
}

void MainComponent::handleNoteOn(juce::MidiKeyboardState*, int ch, int note, float vel) {
    if (vel == 0.0f) { handleNoteOff(nullptr, ch, note, 0.0f); return; }
    int adj = juce::jlimit(0, 127, note + (virtualOctaveShift * 12));
    int act = (cmbMidiCh.getSelectedId() == 17) ? ch : cmbMidiCh.getSelectedId(); if (act <= 0) act = 1;

    // KEYBOARD INPUT GOES TO ARP
    if (btnArp.getToggleState()) {
        heldNotes.add(adj);
        noteArrivalOrder.push_back(adj);
    }
    else {
        juce::MidiMessage m = juce::MidiMessage::noteOn(act, adj, vel);
        sendSplitOscMessage(m); if (midiOutput) midiOutput->sendMessageNow(m);
    }
}

void MainComponent::handleNoteOff(juce::MidiKeyboardState*, int ch, int note, float vel) {
    int adj = juce::jlimit(0, 127, note + (virtualOctaveShift * 12));
    int act = (cmbMidiCh.getSelectedId() == 17) ? ch : cmbMidiCh.getSelectedId(); if (act <= 0) act = 1;

    if (btnArp.getToggleState()) {
        heldNotes.removeValue(adj);
        auto it = std::find(noteArrivalOrder.begin(), noteArrivalOrder.end(), adj);
        if (it != noteArrivalOrder.end()) noteArrivalOrder.erase(it);
    }
    else {
        juce::MidiMessage m = juce::MidiMessage::noteOff(act, adj, vel);
        sendSplitOscMessage(m); if (midiOutput) midiOutput->sendMessageNow(m);
    }
}

// ==================== 1ms HIGH RES TIMER (PLAYBACK + LOOKAHEAD) ====================
void MainComponent::hiResTimerCallback() {
    auto session = link.captureAppSessionState();
    auto now = link.clock().micros();
    double linkBpm = session.tempo();

    // 1. LATENCY-COMPENSATED PHASE
    // Currently using slider value as milliseconds
    double latencyVal = latencySlider.getValue(); // e.g., 20.0 means 20ms lookahead
    double latencyMicros = latencyVal * 1000.0;

    // 2. PLAYBACK LOGIC with LOOKAHEAD
    if (isPlaying) {
        double dt = 0.001 * (linkBpm / 120.0);
        // DYNAMIC LOOKAHEAD: Read 'latencyVal' milliseconds into the future
        double lookaheadSec = juce::jmax(0.001, latencyVal / 1000.0);

        double limitTime = currentTransportTime + dt + lookaheadSec;
        juce::ScopedLock sl(midiLock);

        while (playbackCursor < playbackSeq.getNumEvents()) {
            auto* ev = playbackSeq.getEventPointer(playbackCursor);
            if (ev->message.getTimeStamp() > limitTime) break; // Future

            int ch = ev->message.getChannel(); if (ch <= 0) ch = 1;
            int n = juce::jlimit(0, 127, ev->message.getNoteNumber() + (pianoRollOctaveShift * 12));

            juce::MidiMessage m;
            if (ev->message.isNoteOn()) m = juce::MidiMessage::noteOn(ch, n, (juce::uint8)ev->message.getVelocity());
            else if (ev->message.isNoteOff()) m = juce::MidiMessage::noteOff(ch, n, (juce::uint8)ev->message.getVelocity());
            else m = ev->message;

            int selectedCh = cmbMidiCh.getSelectedId();
            sendSplitOscMessage(m, (selectedCh == 17) ? ch : selectedCh);
            if (midiOutput) midiOutput->sendMessageNow(m);

            playbackCursor++;
        }
        currentTransportTime = currentTransportTime + dt;

        if (currentTransportTime > playbackSeq.getEndTime() && sequenceLength > 0) {
            if (btnLoopPlaylist.getToggleState()) { currentTransportTime = 0; playbackCursor = 0; }
            else {
                if (link.isEnabled()) { auto s = link.captureAppSessionState(); s.setIsPlayingAndRequestBeatAtTime(false, link.clock().micros(), 0, quantum); link.commitAppSessionState(s); }
                isPlaying = false;
            }
        }

        // Sequencer (Visual)
        double currentBeat = session.beatAtTime(now, quantum);
        int currentStep = (int)std::floor(currentBeat) % sequencer.numSteps;
        if (currentStep != stepSeqIndex) {
            stepSeqIndex = currentStep;
            juce::MessageManager::callAsync([this, currentStep] { sequencer.setActiveStep(currentStep); });
            if (sequencer.isStepActive(currentStep)) {
                int note = (int)sequencer.noteSlider.getValue();
                sendSplitOscMessage(juce::MidiMessage::noteOn(getSelectedChannel(), note, 0.8f));
            }
        }
    }
    else {
        // Handle Start Logic if Link enabled (Quantized Start)
        const double compensatedPhase = session.phaseAtTime(now + (std::chrono::microseconds((int64_t)latencyMicros)), quantum);
        if (link.isEnabled() && session.isPlaying() && !isPlaying && compensatedPhase < 0.1) isPlaying = true;
    }

    // 3. ARP GENERATOR
    if (btnArp.getToggleState() && !heldNotes.isEmpty()) {
        static int arpCounter = 0;
        int threshold = 0;

        if (btnArpSync.getToggleState()) {
            // Sync to 1/16th: 60000/BPM / 4. 
            // Since we run at ~1ms (HighRes), we count ticks directly.
            threshold = (int)(15000.0 / linkBpm);
        }
        else {
            threshold = (int)sliderArpSpeed.getValue();
        }

        if (++arpCounter >= threshold) {
            arpCounter = 0;
            int note = 60; int N = heldNotes.size();
            std::vector<int> notes; for (auto n : heldNotes) notes.push_back(n);
            if (cmbArpPattern.getSelectedId() == 5) notes = noteArrivalOrder;

            if (notes.size() > 0) {
                note = notes[arpNoteIndex % N]; arpNoteIndex++;
                float vel = (float)sliderArpVel.getValue() / 127.0f;
                sendSplitOscMessage(juce::MidiMessage::noteOn(getSelectedChannel(), note, vel));
            }
        }
    }
}

void MainComponent::timerCallback() {
    auto session = link.captureAppSessionState();
    auto now = link.clock().micros();
    double linkBpm = session.tempo();

    if (std::abs(bpmVal.get() - linkBpm) > 0.01) parameters.setProperty("bpm", linkBpm, nullptr);
    int peers = link.numPeers();
    if (peers != lastNumPeers) { logPanel.log("Link: " + juce::String(peers) + " Peer(s)"); lastNumPeers = peers; }

    const double phase = session.phaseAtTime(now, quantum);
    phaseVisualizer.setPhase(phase, quantum);
}

void MainComponent::loadMidiFile(juce::File f) {
    if (!f.existsAsFile()) return; stopPlayback(); juce::ScopedLock sl(midiLock);
    juce::FileInputStream stream(f); if (!stream.openedOk()) return; juce::MidiFile mf;
    if (mf.readFrom(stream)) {
        if (mf.getNumTracks() == 0) return;
        mf.convertTimestampTicksToSeconds(); playbackSeq.clear(); bool bpmFound = false;
        for (int i = 0; i < mf.getNumTracks(); ++i) {
            playbackSeq.addSequence(*mf.getTrack(i), 0);
            for (auto* ev : *mf.getTrack(i)) if (ev->message.isTempoMetaEvent() && !bpmFound) {
                currentFileBpm = 60.0 / ev->message.getTempoSecondsPerQuarterNote();
                auto s = link.captureAppSessionState(); s.setTempo(currentFileBpm, link.clock().micros()); link.commitAppSessionState(s);
                parameters.setProperty("bpm", currentFileBpm, nullptr);
                bpmFound = true;
            }
        }
        playbackSeq.updateMatchedPairs(); sequenceLength = playbackSeq.getEndTime();
        trackGrid.loadSequence(playbackSeq);
        logPanel.log("Loaded: " + f.getFileName());
        grabKeyboardFocus();
    }
}

void MainComponent::setView(AppView v) { currentView = v; updateVisibility(); resized(); grabKeyboardFocus(); }
void MainComponent::updateVisibility() {
    bool isDash = (currentView == AppView::Dashboard);

    verticalKeyboard.setVisible(isDash && isSimpleMode);
    horizontalKeyboard.setVisible(isDash && !isSimpleMode);
    trackGrid.setVisible(isDash && !isSimpleMode);

    mixerViewport.setVisible(isDash && !isSimpleMode);
    sequencer.setVisible(isDash && !isSimpleMode);
    cmbQuantum.setVisible(isDash); btnLinkToggle.setVisible(isDash); phaseVisualizer.setVisible(isDash);
    grpArp.setVisible(isDash && !isSimpleMode);
    btnArp.setVisible(isDash && !isSimpleMode);
    btnArpSync.setVisible(isDash && !isSimpleMode);
    sliderArpSpeed.setVisible(isDash && !isSimpleMode); sliderArpVel.setVisible(isDash && !isSimpleMode);
    cmbArpPattern.setVisible(isDash && !isSimpleMode);
    lblArp.setVisible(isDash && !isSimpleMode); lblArpBpm.setVisible(isDash && !isSimpleMode); lblArpVel.setVisible(isDash && !isSimpleMode);
    btnGPU.setVisible(!isSimpleMode && isDash);
    btnPrOctUp.setVisible(isDash); btnPrOctDown.setVisible(isDash);
    btnTapTempo.setVisible(isDash);
    latencySlider.setVisible(isDash); lblLatency.setVisible(isDash);

    bool isOverlay = (currentView == AppView::OSC_Config || currentView == AppView::Help);
    if (isOverlay) {
        horizontalKeyboard.setVisible(false); verticalKeyboard.setVisible(false); trackGrid.setVisible(false);
        mixerViewport.setVisible(false); sequencer.setVisible(false);
        playlist.setVisible(false); logPanel.setVisible(false); grpArp.setVisible(false);
        cmbQuantum.setVisible(false); btnLinkToggle.setVisible(false); phaseVisualizer.setVisible(false);
        btnPrOctUp.setVisible(false); btnPrOctDown.setVisible(false); btnTapTempo.setVisible(false);
        latencySlider.setVisible(false); lblLatency.setVisible(false);
    }
    else { playlist.setVisible(true); logPanel.setVisible(true); }
    oscConfig.setVisible(currentView == AppView::OSC_Config); helpText.setVisible(currentView == AppView::Help);
}

void MainComponent::resized() {
    logoView.setBounds(10, 5, 25, 25); // Top-left corner of the window

    auto area = getLocalBounds().reduced(5);
    auto menu = area.removeFromTop(30);

    int bw = 140;
    int startX = (menu.getWidth() - 3 * bw) / 2;
    auto centerMenu = menu.withX(startX).withWidth(3 * bw);

    btnDash.setBounds(centerMenu.removeFromLeft(bw).reduced(2));
    btnOscCfg.setBounds(centerMenu.removeFromLeft(bw).reduced(2));
    btnHelp.setBounds(centerMenu.removeFromLeft(bw).reduced(2));

    auto topRight = menu.removeFromRight(140);

    if (currentView == AppView::OSC_Config) { oscConfig.setBounds(getLocalBounds().withSizeKeepingCentre(isSimpleMode ? 420 : 600, 550)); return; }
    if (currentView == AppView::Help) { helpText.setBounds(getLocalBounds().withSizeKeepingCentre(isSimpleMode ? 420 : 600, 500)); return; }

    if (isSimpleMode) {
        auto topStack = area.removeFromTop(160);
        grpNet.setBounds(topStack.removeFromTop(80).reduced(2));
        auto rNet = grpNet.getBounds().reduced(5, 15);
        int lblW = 35; int edW = 60;
        lblIp.setBounds(rNet.removeFromLeft(lblW)); edIp.setBounds(rNet.removeFromLeft(edW + 20)); rNet.removeFromLeft(10);
        lblPOut.setBounds(rNet.removeFromLeft(lblW + 5)); edPOut.setBounds(rNet.removeFromLeft(edW)); rNet.removeFromLeft(10);
        lblPIn.setBounds(rNet.removeFromLeft(lblW)); edPIn.setBounds(rNet.removeFromLeft(edW)); rNet.removeFromLeft(10);
        ledConnect.setBounds(rNet.removeFromRight(24)); btnConnect.setBounds(rNet);

        grpIo.setBounds(topStack.reduced(2)); auto rMidi = grpIo.getBounds().reduced(5, 15);
        lblIn.setBounds(rMidi.removeFromLeft(20)); cmbMidiIn.setBounds(rMidi.removeFromLeft(100)); rMidi.removeFromLeft(10);
        lblCh.setBounds(rMidi.removeFromLeft(20)); cmbMidiCh.setBounds(rMidi.removeFromLeft(70)); rMidi.removeFromLeft(10);
        lblOut.setBounds(rMidi.removeFromLeft(30)); cmbMidiOut.setBounds(rMidi.removeFromLeft(100));

        btnRetrigger.setBounds(rMidi.removeFromRight(80).reduced(5, 0));

        auto bottomBar = area.removeFromBottom(50);
        btnPlay.setBounds(bottomBar.removeFromLeft(50)); btnStop.setBounds(bottomBar.removeFromLeft(50));
        btnPrev.setBounds(bottomBar.removeFromLeft(30)); btnSkip.setBounds(bottomBar.removeFromLeft(30));
        btnResetFile.setBounds(bottomBar.removeFromLeft(50)); btnClearPR.setBounds(bottomBar.removeFromLeft(50));
        btnResetBPM.setBounds(bottomBar.removeFromLeft(60));
        btnLoopPlaylist.setBounds(bottomBar.removeFromLeft(50));
        bottomBar.removeFromLeft(5);
        lblTempo.setBounds(bottomBar.removeFromLeft(45)); tempoSlider.setBounds(bottomBar.removeFromLeft(100));

        auto contentArea = area;
        verticalKeyboard.setBounds(contentArea.removeFromLeft(40));
        playlist.setBounds(contentArea.removeFromBottom(contentArea.getHeight() * 0.4));
        auto midArea = contentArea;
        auto linkArea = midArea.removeFromRight(140).reduced(5);
        logPanel.setBounds(midArea);

        cmbQuantum.setBounds(linkArea.removeFromTop(25)); linkArea.removeFromTop(5);
        btnLinkToggle.setBounds(linkArea.removeFromTop(25));
        auto latRow = linkArea.removeFromTop(20);
        lblLatency.setBounds(latRow.removeFromLeft(45)); latencySlider.setBounds(latRow);

        phaseVisualizer.setBounds(linkArea.removeFromTop(30).reduced(0, 5)); linkArea.removeFromTop(5);
        btnTapTempo.setBounds(linkArea.removeFromTop(25)); linkArea.removeFromTop(5);
        btnPrOctDown.setBounds(linkArea.removeFromLeft(60)); btnPrOctUp.setBounds(linkArea.removeFromLeft(60));
    }
    else {
        btnRetrigger.setBounds(topRight.removeFromLeft(80).reduced(2)); btnGPU.setBounds(topRight.removeFromLeft(60).reduced(2));
        auto strip = area.removeFromTop(80);
        grpNet.setBounds(strip.removeFromLeft(450).reduced(2));
        auto rNet = grpNet.getBounds().reduced(5, 15);
        int lblW = 35; int edW = 60;
        lblIp.setBounds(rNet.removeFromLeft(lblW)); edIp.setBounds(rNet.removeFromLeft(edW + 20)); rNet.removeFromLeft(10);
        lblPOut.setBounds(rNet.removeFromLeft(lblW + 5)); edPOut.setBounds(rNet.removeFromLeft(edW)); rNet.removeFromLeft(10);
        lblPIn.setBounds(rNet.removeFromLeft(lblW)); edPIn.setBounds(rNet.removeFromLeft(edW)); rNet.removeFromLeft(10);
        ledConnect.setBounds(rNet.removeFromRight(24)); btnConnect.setBounds(rNet);
        grpIo.setBounds(strip.reduced(2)); auto rMidi = grpIo.getBounds().reduced(5, 15);
        lblIn.setBounds(rMidi.removeFromLeft(20)); cmbMidiIn.setBounds(rMidi.removeFromLeft(100)); rMidi.removeFromLeft(10);
        lblCh.setBounds(rMidi.removeFromLeft(20)); cmbMidiCh.setBounds(rMidi.removeFromLeft(70)); rMidi.removeFromLeft(10);
        lblOut.setBounds(rMidi.removeFromLeft(30)); cmbMidiOut.setBounds(rMidi.removeFromLeft(100));

        auto trans = area.removeFromTop(40);
        btnPlay.setBounds(trans.removeFromLeft(50)); btnStop.setBounds(trans.removeFromLeft(50));
        btnPrev.setBounds(trans.removeFromLeft(30)); btnSkip.setBounds(trans.removeFromLeft(30));
        btnResetFile.setBounds(trans.removeFromLeft(50)); btnClearPR.setBounds(trans.removeFromLeft(50));
        btnLoopPlaylist.setBounds(trans.removeFromLeft(50));
        trans.removeFromLeft(30); lblTempo.setBounds(trans.removeFromLeft(45)); tempoSlider.setBounds(trans.removeFromLeft(225)); btnResetBPM.setBounds(trans.removeFromLeft(70));
        btnPrOctUp.setBounds(trans.removeFromRight(60).reduced(2)); btnPrOctDown.setBounds(trans.removeFromRight(60).reduced(2));

        auto bottomSection = area.removeFromBottom(120);
        mixerViewport.setBounds(bottomSection.removeFromLeft(8 * mixer.stripWidth + 20));
        mixer.setSize(16 * mixer.stripWidth, mixerViewport.getHeight() - 20);
        auto linkGuiArea = bottomSection.reduced(10, 5);
        auto row1 = linkGuiArea.removeFromTop(25);
        cmbQuantum.setBounds(row1.removeFromLeft(100)); row1.removeFromLeft(10); btnLinkToggle.setBounds(row1);
        auto latRow = linkGuiArea.removeFromTop(20);
        lblLatency.setBounds(latRow.removeFromLeft(45)); latencySlider.setBounds(latRow);
        phaseVisualizer.setBounds(linkGuiArea.removeFromTop(30).reduced(0, 5));
        btnTapTempo.setBounds(linkGuiArea.removeFromTop(25).reduced(20, 0));

        auto btmCtrl = area.removeFromBottom(80);
        sequencer.setBounds(btmCtrl.removeFromLeft(btmCtrl.getWidth() - 300));
        grpArp.setBounds(btmCtrl); auto rA = btmCtrl.reduced(5, 15);

        auto col1 = rA.removeFromLeft(60);
        btnArp.setBounds(col1.removeFromTop(30));
        btnArpSync.setBounds(col1.removeFromTop(30));

        auto s1 = rA.removeFromLeft(90);
        sliderArpSpeed.setBounds(s1.removeFromTop(40)); lblArpBpm.setBounds(s1);
        auto s2 = rA.removeFromLeft(70);
        sliderArpVel.setBounds(s2.removeFromTop(40)); lblArpVel.setBounds(s2);
        cmbArpPattern.setBounds(rA.removeFromLeft(80));

        auto rightSide = area.removeFromRight(280);
        logPanel.setBounds(rightSide.removeFromTop(rightSide.getHeight() / 2));
        playlist.setBounds(rightSide);

        auto keyArea = area.removeFromBottom(60);
        horizontalKeyboard.setBounds(keyArea);
        trackGrid.setBounds(area);
    }
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray&) {
    return true;
}

void MainComponent::filesDropped(const juce::StringArray& f, int, int) {
    if (f[0].endsWith(".mid")) {
        playlist.addFile(f[0]);
        loadMidiFile(juce::File(f[0]));
    }
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) {
    juce::MessageManager::callAsync([this, m] {
        if (m.isNoteOnOrOff()) keyboardState.processNextMidiEvent(m);
        else sendSplitOscMessage(m);
        });
}

void MainComponent::toggleChannel(int ch, bool active) {
    if (active) activeChannels.insert(ch);
    else activeChannels.erase(ch);
}

int MainComponent::getSelectedChannel() const {
    return activeChannels.empty() ? 1 : *activeChannels.begin();
}

void MainComponent::stopPlayback() {
    juce::ScopedLock sl(midiLock);
    isPlaying = false;
    currentTransportTime = 0;
    playbackCursor = 0;
}

void MainComponent::takeSnapshot() {}

void MainComponent::performUndo()
{
    undoManager.undo();
}

void MainComponent::performRedo()
{
    undoManager.redo();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(Theme::bgDark);
}