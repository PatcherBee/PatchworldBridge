/*
  ==============================================================================
    Source/MainComponent.cpp
    Status: FIXED (Compiled & Verified)
  ==============================================================================
*/

#include "MainComponent.h"
#include <ableton/Link.hpp>
#include <algorithm>
#include <map>

//==============================================================================
// CONTROL PROFILE IMPLEMENTATION
//==============================================================================
ControlProfile ControlProfile::fromJson(const juce::String &jsonString) {
  ControlProfile p;
  auto parsed = juce::JSON::parse(jsonString);
  if (juce::DynamicObject *obj = parsed.getDynamicObject()) {
    if (obj->hasProperty("name"))
      p.name = obj->getProperties()["name"].toString();
    if (obj->hasProperty("ccCutoff"))
      p.ccCutoff = obj->getProperties()["ccCutoff"];
    if (obj->hasProperty("ccResonance"))
      p.ccResonance = obj->getProperties()["ccResonance"];
    if (obj->hasProperty("ccAttack"))
      p.ccAttack = obj->getProperties()["ccAttack"];
    if (obj->hasProperty("ccRelease"))
      p.ccRelease = obj->getProperties()["ccRelease"];
    if (obj->hasProperty("ccLevel"))
      p.ccLevel = obj->getProperties()["ccLevel"];
    if (obj->hasProperty("ccPan"))
      p.ccPan = obj->getProperties()["ccPan"];
    if (obj->hasProperty("isTransportLink"))
      p.isTransportLink = obj->getProperties()["isTransportLink"];
    if (obj->hasProperty("ccPlay"))
      p.ccPlay = obj->getProperties()["ccPlay"];
    if (obj->hasProperty("ccStop"))
      p.ccStop = obj->getProperties()["ccStop"];
    if (obj->hasProperty("ccRecord"))
      p.ccRecord = obj->getProperties()["ccRecord"];
  }
  return p;
}

ControlProfile ControlProfile::fromFile(const juce::File &f) {
  if (f.existsAsFile()) {
    return fromJson(f.loadFileAsString());
  }
  return getDefault();
}

//==============================================================================
// DESTRUCTOR
//==============================================================================
MainComponent::~MainComponent() {
  juce::Timer::stopTimer();
  openGLContext.detach();
  keyboardState.removeListener(this);
}

//==============================================================================
// HELPER: Velocity to Duration
//==============================================================================
double MainComponent::getDurationFromVelocity(float velocity0to1) {
  const double minDurationMs = 30.0;
  const double maxDurationMs = 800.0;
  float clippedVel = std::max(0.0f, std::min(1.0f, velocity0to1));
  return minDurationMs + (clippedVel * (maxDurationMs - minDurationMs));
}

//==============================================================================
// CONSTRUCTOR
//==============================================================================
MainComponent::MainComponent()
    : bpmVal(parameters, "bpm", &undoManager, 120.0), keyboardState(),
      horizontalKeyboard(keyboardState,
                         juce::MidiKeyboardComponent::horizontalKeyboard),
      verticalKeyboard(
          keyboardState,
          juce::MidiKeyboardComponent::verticalKeyboardFacingRight),
      trackGrid(keyboardState), logPanel(), playlist(), sequencer(), mixer(),
      oscConfig(), controlPage(), midiLearnOverlay(mappingManager, *this) {

  // --- Engine Setup ---
  engine.sequencer = &sequencer;
  engine.onMidiEvent = [this](const juce::MidiMessage &m, int ch) {
    if (m.isNoteOn())
      keyboardState.noteOn(ch, m.getNoteNumber(), m.getVelocity() / 127.0f);
    else if (m.isNoteOff())
      keyboardState.noteOff(ch, m.getNoteNumber(), 0.0f);

    sendSplitOscMessage(m, ch);
    if (midiOutput && !btnBlockMidiOut.getToggleState())
      midiOutput->sendMessageNow(m);

    midiOutLight.activate();
  };

  engine.isChannelActive = [this](int ch) { return mixer.isChannelActive(ch); };

  engine.onMidiTransport = [this](bool start) {
    if (midiOutput && btnMidiClock.getToggleState()) {
      midiOutput->sendMessageNow(juce::MidiMessage(start ? 0xFA : 0xFC));
    }
    if (isOscConnected) {
      oscSender.send(
          start ? oscConfig.ePlay.getText() : oscConfig.eStop.getText(), 1.0f);
    }
  };

  engine.onSequenceEnd = [this] {
    double nowBeat = engine.getCurrentBeat();
    juce::MessageManager::callAsync(
        [this, nowBeat] { handleSequenceEnd(nowBeat); });
  };

  engine.setBpm(bpmVal.get());

  // --- OpenGL Init ---
  openGLContext.attachTo(*this);

  // --- Logo ---
  if (BinaryData::logo_pngSize > 0) {
    auto myImage = juce::ImageCache::getFromMemory(BinaryData::logo_png,
                                                   BinaryData::logo_pngSize);
    logoView.setImage(myImage);
  }
  addAndMakeVisible(logoView);

  setAudioChannels(2, 2);
  keyboardState.addListener(this);

  // --- IP Headers ---
  addAndMakeVisible(lblLocalIpHeader);
  addAndMakeVisible(lblLocalIpDisplay);
  lblLocalIpHeader.setText("My IP:", juce::dontSendNotification);
  lblLocalIpHeader.setFont(juce::Font(14.0f, juce::Font::bold));
  lblLocalIpHeader.setColour(juce::Label::textColourId, juce::Colours::white);
  lblLocalIpDisplay.setText(getLocalIPAddress(), juce::dontSendNotification);
  lblLocalIpDisplay.setFont(juce::Font(14.0f));
  lblLocalIpDisplay.setColour(juce::Label::textColourId, juce::Colours::white);
  lblLocalIpDisplay.setJustificationType(juce::Justification::centredLeft);

  addAndMakeVisible(midiInLight);
  addAndMakeVisible(midiOutLight);
  setMouseClickGrabsKeyboardFocus(true);
  addKeyListener(this);
  addAndMakeVisible(btnExtSync);
  btnExtSync.setButtonText("Ext");
  btnExtSync.setTooltip("Sync BPM to External MIDI Clock");
  btnExtSync.setClickingTogglesState(true);
  // Yellow/Amber color to indicate "Sync Active"
  btnExtSync.setColour(juce::TextButton::buttonOnColourId,
                       juce::Colours::orange.brighter(0.2f));
  btnExtSync.onClick = [this] { toggleExtSync(); };

  // --- Note Delay ---
  addAndMakeVisible(sliderNoteDelay);
  sliderNoteDelay.setRange(0.0, 2000.0, 1.0);
  sliderNoteDelay.setValue(200.0);
  sliderNoteDelay.getProperties().set("paramID", "note_delay");
  sliderNoteDelay.setSliderStyle(juce::Slider::LinearHorizontal);
  sliderNoteDelay.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 15);
  addAndMakeVisible(lblNoteDelay);
  lblNoteDelay.setText("Duration:", juce::dontSendNotification);
  lblNoteDelay.setJustificationType(juce::Justification::centredRight);

  // --- Latency Comp Slider (Help/Control) ---
  addChildComponent(
      sliderLatencyComp); // Not always visible, maybe in Control Page?
  sliderLatencyComp.setRange(0.0, 200.0, 1.0);
  sliderLatencyComp.setValue(20.0); // Default
  sliderLatencyComp.setSliderStyle(juce::Slider::LinearHorizontal);
  sliderLatencyComp.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 15);
  sliderLatencyComp.setTooltip("Lookahead Latency Compensation (ms)");

  addChildComponent(lblLatencyComp);
  lblLatencyComp.setText("Latency Comp (ms):", juce::dontSendNotification);

  controlPage.addAndMakeVisible(sliderLatencyComp); // Add to Control Page
  controlPage.addAndMakeVisible(lblLatencyComp);

  // --- Group Headers ---
  grpNet.setText("Network Setup");
  grpIo.setText("MIDI Configuration");
  grpArp.setText("Arpeggiator Gen");

  // Placeholder Box
  grpPlaceholder.setText("");
  grpPlaceholder.setColour(juce::GroupComponent::outlineColourId,
                           juce::Colours::grey.withAlpha(0.3f));
  addChildComponent(grpPlaceholder);

  lblArpBpm.setText("Speed", juce::dontSendNotification);
  lblArpVel.setText("Vel", juce::dontSendNotification);

  addAndMakeVisible(lblArpSpdLabel);
  addAndMakeVisible(lblArpVelLabel);
  lblArpSpdLabel.setText("Speed", juce::dontSendNotification);
  lblArpVelLabel.setText("Vel", juce::dontSendNotification);
  lblArpSpdLabel.setJustificationType(juce::Justification::centred);
  lblArpVelLabel.setJustificationType(juce::Justification::centred);
  lblArpSpdLabel.setFont(juce::FontOptions(11.0f));
  lblArpVelLabel.setFont(juce::FontOptions(11.0f));

  wheelFutureBox.setFill(juce::Colours::grey.withAlpha(0.1f));
  wheelFutureBox.setStrokeType(juce::PathStrokeType(1.0f));
  wheelFutureBox.setStrokeFill(juce::Colours::grey.withAlpha(0.4f));
  addAndMakeVisible(wheelFutureBox);

  // --- BUTTON TEXT ---
  btnDash.setButtonText("Dashboard");
  btnCtrl.setButtonText("Control");
  btnOscCfg.setButtonText("OSC Config");
  btnHelp.setButtonText("Help");

  btnPlay.setButtonText("Play");
  btnStop.setButtonText("Stop");
  btnPrev.setButtonText("<");
  btnSkip.setButtonText(">");
  btnClearPR.setButtonText("Clear");
  btnResetBPM.setButtonText("Reset BPM");
  btnTapTempo.setButtonText("Tap Tempo");
  btnPrOctUp.setButtonText("Oct +");
  btnPrOctDown.setButtonText("Oct -");
  btnPanic.setButtonText("Panic"); // Set initial text

  btnConnect.setButtonText("Connect");
  btnRetrigger.setButtonText("Retrig");
  btnGPU.setButtonText("GPU");
  btnBlockMidiOut.setButtonText("Block Out");
  btnMidiScaling.setButtonText("MIDI Scale: 0-1");
  btnMidiScaling.setClickingTogglesState(true);
  btnMidiScaling.onClick = [this] {
    isFullRangeMidi = btnMidiScaling.getToggleState();
    btnMidiScaling.setButtonText(isFullRangeMidi ? "MIDI Scale: 0-127"
                                                 : "MIDI Scale: 0-1");
  };
  btnLinkToggle.setButtonText("Link");
  btnMidiThru.setButtonText("Thru"); // New Thru button
  btnSplit.setButtonText("Split");
  btnPreventBpmOverride.setTooltip(
      "Prevent loading MIDI files from changing Tempo");

  // --- SIMPLE MODE SLIDERS ---
  auto setupSimpleVol = [&](juce::Slider &s, juce::TextEditor &t, int ch) {
    s.setSliderStyle(juce::Slider::LinearVertical);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setRange(0, 127, 1);
    s.setValue(100);
    s.onValueChange = [this, &s, &t, ch] {
      float valNorm = (float)s.getValue() / 127.0f;
      // OSC
      if (isOscConnected) {
        oscSender.send(t.getText(), valNorm);
      }
      // MIDI
      if (midiOutput) {
        midiOutput->sendMessageNow(
            juce::MidiMessage::controllerEvent(ch, 7, (int)s.getValue()));
      }
      logPanel.log("Simple Vol " + juce::String(ch) + ": " +
                       juce::String((int)s.getValue()),
                   false);
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

  addAndMakeVisible(btnVol1CC);
  btnVol1CC.setButtonText("CC20");
  btnVol1CC.onClick = [this] {
    if (midiOutput)
      midiOutput->sendMessageNow(
          juce::MidiMessage::controllerEvent(1, 20, 127));
    if (isOscConnected)
      oscSender.send("/ch1/cc20", 1.0f);
    logPanel.log("! CC20 Sent", false);
    // Reset?
    juce::MessageManager::callAsync([this] {
      if (midiOutput)
        midiOutput->sendMessageNow(
            juce::MidiMessage::controllerEvent(1, 20, 0));
      if (isOscConnected)
        oscSender.send("/ch1/cc20", 0.0f);
    });
  };
  addAndMakeVisible(btnVol2CC);
  btnVol2CC.setButtonText("CC21");
  btnVol2CC.onClick = [this] {
    if (midiOutput)
      midiOutput->sendMessageNow(
          juce::MidiMessage::controllerEvent(2, 21, 127));
    if (isOscConnected)
      oscSender.send("/ch2/cc21", 1.0f);
    logPanel.log("! CC21 Sent", false);
    juce::MessageManager::callAsync([this] {
      if (midiOutput)
        midiOutput->sendMessageNow(
            juce::MidiMessage::controllerEvent(2, 21, 0));
      if (isOscConnected)
        oscSender.send("/ch2/cc21", 0.0f);
    });
  };

  // --- Labels ---
  lblIp.setText("IP:", juce::dontSendNotification);
  lblPOut.setText("POut:", juce::dontSendNotification);
  lblPIn.setText("PIn:", juce::dontSendNotification);
  lblIn.setText("In:", juce::dontSendNotification);
  lblOut.setText("Out:", juce::dontSendNotification);
  lblCh.setText("CH:", juce::dontSendNotification);

  // --- Arp Patterns ---
  addAndMakeVisible(cmbArpPattern);
  cmbArpPattern.addItem("Up", 1);
  cmbArpPattern.addItem("Down", 2);
  cmbArpPattern.addItem("UpDown", 3);
  cmbArpPattern.addItem("Random", 4);
  cmbArpPattern.addItem("Chord", 5);
  cmbArpPattern.setSelectedId(1, juce::dontSendNotification);

  // --- Keyboard ---
  horizontalKeyboard.setWantsKeyboardFocus(false);
  verticalKeyboard.setWantsKeyboardFocus(false);
  verticalKeyboard.setKeyWidth(30);

  // --- Quantum / Link ---
  addAndMakeVisible(cmbQuantum);
  cmbQuantum.addItemList({"1 Beat", "2 Beats", "3 Beats", "4 Beats", "5 Beats",
                          "8 Beats", "16 Beats"},
                         1);
  cmbQuantum.setSelectedId(4);
  cmbQuantum.onChange = [this] {
    int sel = cmbQuantum.getSelectedId();
    switch (sel) {
    case 1:
      quantum = 1.0;
      break;
    case 2:
      quantum = 2.0;
      break;
    case 3:
      quantum = 3.0;
      break;
    case 4:
      quantum = 4.0;
      break;
    case 5:
      quantum = 5.0;
      break;
    case 6:
      quantum = 8.0;
      break;
    case 7:
      quantum = 16.0;
      break;
    }
    logPanel.log("Quantum Changed: " + cmbQuantum.getText(), false);
  };

  addAndMakeVisible(lblLinkBeat);
  lblLinkBeat.setText("Bar: -.--", juce::dontSendNotification);
  lblLinkBeat.setJustificationType(juce::Justification::centred);

  addAndMakeVisible(btnLinkToggle);
  btnLinkToggle.setClickingTogglesState(true);
  btnLinkToggle.setColour(juce::TextButton::buttonColourId,
                          juce::Colours::grey);
  btnLinkToggle.setColour(juce::TextButton::buttonOnColourId,
                          juce::Colours::cyan.darker(0.2f));
  btnLinkToggle.setToggleState(true, juce::dontSendNotification);
  btnLinkToggle.onClick = [this] {
    bool enabled = btnLinkToggle.getToggleState();
    if (auto *link = engine.getLink())
      link->enable(enabled);
    logPanel.log("! Ableton Link: " + (juce::String)(enabled ? "ON" : "OFF"),
                 true);
  };

  addAndMakeVisible(btnPreventBpmOverride);
  btnPreventBpmOverride.setToggleState(false, juce::dontSendNotification);
  btnPreventBpmOverride.onClick = [this] {
    logPanel.log(
        "! BPM Lock: " +
            juce::String(btnPreventBpmOverride.getToggleState() ? "ON" : "OFF"),
        false);
  };

  auto configureToggleButton = [this](juce::TextButton &btn, juce::String text,
                                      juce::Colour onColor,
                                      juce::String logName) {
    btn.setButtonText(text);
    btn.setClickingTogglesState(true);
    btn.setColour(juce::TextButton::buttonOnColourId, onColor);
    btn.setColour(juce::TextButton::buttonColourId,
                  juce::Colours::grey); // Default Off
    btn.onClick = [this, &btn, logName] {
      logPanel.log(
          "! " + logName + ": " + (btn.getToggleState() ? "ON" : "OFF"), true);
      // Force repaint to show color change definitely if needed (JUCE usually
      // handles this with LookAndFeel, but explicit is fine)
      btn.repaint();
    };
    addAndMakeVisible(btn);
  };

  configureToggleButton(btnBlockMidiOut, "Block", juce::Colours::red,
                        "Block MIDI Out");
  configureToggleButton(btnArpBlock, "Block Out",
                        juce::Colours::red.darker(0.3f), "Arp Block Out");
  configureToggleButton(btnMidiThru, "Thru", juce::Colours::green.darker(0.4f),
                        "MIDI Thru");
  configureToggleButton(btnMidiClock, "Clock",
                        juce::Colours::orange.darker(0.1f), "MIDI Clock Out");
  configureToggleButton(btnSplit, "Split", juce::Colours::blue, "Split Mode");
  configureToggleButton(btnRetrigger, "Retrig", juce::Colours::red,
                        "Retrigger");

  btnMidiClock.setTooltip("Send MIDI Clock (F8) to MIDI Out");
  btnMidiThru.setTooltip("Echo MIDI Input to Output");

  // --- Nudge Slider ---
  addAndMakeVisible(nudgeSlider);
  tempoSlider.setRange(20.0, 444.0, 0.1);
  tempoSlider.setValue(120.0);
  tempoSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  tempoSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 20);
  tempoSlider.getProperties().set("paramID", "tempo");
  nudgeSlider.setRange(-0.10, 0.10, 0.001);
  nudgeSlider.setValue(0.0);
  nudgeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  nudgeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  nudgeSlider.addListener(this);
  nudgeSlider.onValueChange = [this] {
    if (auto *link = engine.getLink()) {
      double mult = 1.0 + nudgeSlider.getValue();
      double target = baseBpm * mult;
      auto state = link->captureAppSessionState();
      state.setTempo(target, link->clock().micros());
      link->commitAppSessionState(state);
    }
  };
  nudgeSlider.onDragStart = [this] {
    if (auto *link = engine.getLink()) {
      auto state = link->captureAppSessionState();
      baseBpm = state.tempo();
    }
    logPanel.log("! Nudge Start", false);
  };

  addAndMakeVisible(lblLatency);
  lblLatency.setText("Nudge", juce::dontSendNotification);

  addAndMakeVisible(phaseVisualizer);

  // --- Tap Tempo ---
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
        if (auto *link = engine.getLink()) {
          auto state = link->captureAppSessionState();
          state.setTempo(bpm, link->clock().micros());
          link->commitAppSessionState(state);
        }
        parameters.setProperty("bpm", bpm, nullptr);
        juce::MessageManager::callAsync([this, bpm] {
          tempoSlider.setValue(bpm, juce::dontSendNotification);
        });
        logPanel.log("! Tap Tempo: " + juce::String(bpm, 1), true);
      }
      tapTimes.clear();
      tapCounter = 0;
    }
    grabKeyboardFocus();
  };

  // --- Dashboard Navigation ---
  addAndMakeVisible(btnPanic);
  btnPanic.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
  btnPanic.onClick = [this] { sendPanic(); };

  addAndMakeVisible(btnDash);
  btnDash.onClick = [this] {
    if (currentView != AppView::Dashboard) {
      setView(AppView::Dashboard);
    } else {
      isSimpleMode = !isSimpleMode;
      if (isSimpleMode)
        setSize(500, 650);
      else
        setSize(800, 750);
      updateVisibility();
      resized();
      logPanel.log("! Switched to " +
                       juce::String(isSimpleMode ? "Simple" : "Default") +
                       " Mode",
                   true);
    }
  };
  // MIDI MONITOR CONNECTION
  mappingManager.onMidiLogCallback = [this](juce::String msg) {
    logPanel.log(msg,
                 false); // Log without triggering a full UI redraw if possible
  };

  // --- Overlays ---
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

  // Retrigger configured above

  addAndMakeVisible(btnGPU);
  btnGPU.setClickingTogglesState(true);
  btnGPU.setButtonText("GPU");
  btnGPU.onClick = [this] {
    if (btnGPU.getToggleState()) {
      openGLContext.attachTo(*this);
      logPanel.log("! GPU Rendering: ON", true);
    } else {
      openGLContext.detach();
      logPanel.log("! GPU Rendering: OFF", true);
    }
  };

  addAndMakeVisible(btnPanic);
  btnPanic.setButtonText("Panic");
  btnPanic.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
  btnPanic.onClick = [this] { sendPanic(); };

  lblPIn.setText("In", juce::dontSendNotification);
  lblPOut.setText("Out", juce::dontSendNotification);
  btnBlockMidiOut.setButtonText("Block");

  addAndMakeVisible(btnRetrigger);
  btnRetrigger.setButtonText("Retrig");
  btnRetrigger.setClickingTogglesState(true);
  btnRetrigger.setColour(juce::TextButton::buttonOnColourId, Theme::accent);
  btnRetrigger.getProperties().set("paramID", "retrig");
  btnRetrigger.onClick = [this] {
    logPanel.log("! Retrigger: " +
                     juce::String(btnRetrigger.getToggleState() ? "ON" : "OFF"),
                 false);
  };

  addAndMakeVisible(btnMidiLearn);
  btnMidiLearn.setButtonText("Learn");
  btnMidiLearn.setClickingTogglesState(true);
  btnMidiLearn.setColour(juce::TextButton::buttonOnColourId,
                         juce::Colours::orange);
  btnMidiLearn.onClick = [this] {
    isMidiLearnMode = btnMidiLearn.getToggleState();
    mappingManager.setLearnModeActive(isMidiLearnMode);
    midiLearnOverlay.setOverlayActive(isMidiLearnMode);
    midiLearnOverlay.setVisible(isMidiLearnMode);
    if (isMidiLearnMode)
      logPanel.log("! MIDI Learn: ON - Click a slider then move a knob", true);
  };

  addAndMakeVisible(btnResetLearned);
  btnResetLearned.setButtonText("Reset Learn");
  btnResetLearned.setColour(juce::TextButton::buttonColourId,
                            juce::Colours::darkred.withAlpha(0.6f));
  btnResetLearned.onClick = [this] {
    mappingManager.resetMappings();
    logPanel.log("! Learned Mappings Cleared", true);
  };

  // --- Network ---
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
        logPanel.log("! OSC Connected (" + edIp.getText() + ":" +
                         edPOut.getText() + ")",
                     true);
        // User Fix: Connect clears log -> REMOVED logPanel.resetStats();
      } else {
        btnConnect.setToggleState(false, juce::dontSendNotification);
        logPanel.log("! OSC Connection Failed", true);
      }
    } else {
      oscSender.disconnect();
      oscReceiver.disconnect();
      isOscConnected = false;
      ledConnect.isConnected = false;
      ledConnect.repaint();
      btnConnect.setButtonText("Connect");
      logPanel.log("! OSC Disconnected", true);
    }
    ledConnect.repaint();
    grabKeyboardFocus();
  };
  addAndMakeVisible(ledConnect);

  // --- MIDI I/O ---
  addAndMakeVisible(grpIo);

  tempoSlider.getProperties().set("paramID", "tempo");
  addAndMakeVisible(tempoSlider);
  tempoSlider.setRange(20, 444, 1.0);
  tempoSlider.setValue(120.0);
  tempoSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 20);
  tempoSlider.onValueChange = [this] {
    double val = tempoSlider.getValue();
    parameters.setProperty("bpm", val, nullptr);
    engine.setBpm(val);
  };

  addAndMakeVisible(lblIn);
  addAndMakeVisible(cmbMidiIn);
  cmbMidiIn.addItem("None", 1);
  cmbMidiIn.addItem("Virtual Keyboard", 2);
  auto inputs = juce::MidiInput::getAvailableDevices();
  for (int i = 0; i < inputs.size(); ++i)
    cmbMidiIn.addItem(inputs[i].name, i + 3);

  cmbMidiIn.onChange = [this] {
    midiInput.reset();
    juce::String deviceName = cmbMidiIn.getText();
    if (cmbMidiIn.getSelectedId() > 2) {
      midiInput = juce::MidiInput::openDevice(
          juce::MidiInput::getAvailableDevices()[cmbMidiIn.getSelectedId() - 3]
              .identifier,
          this);
      if (midiInput)
        midiInput->start();
    }
    logPanel.log("! MIDI In: " + deviceName, true);
    grabKeyboardFocus();
  };

  addAndMakeVisible(lblOut);
  addAndMakeVisible(cmbMidiOut);
  auto outputs = juce::MidiOutput::getAvailableDevices();
  cmbMidiOut.addItem("None", 1);
  for (int i = 0; i < outputs.size(); ++i)
    cmbMidiOut.addItem(outputs[i].name, i + 2);

  cmbMidiOut.onChange = [this] {
    midiOutput.reset();
    juce::String deviceName = cmbMidiOut.getText();
    if (cmbMidiOut.getSelectedId() > 1)
      midiOutput = juce::MidiOutput::openDevice(
          juce::MidiOutput::getAvailableDevices()[cmbMidiOut.getSelectedId() -
                                                  2]
              .identifier);
    logPanel.log("! MIDI Out: " + deviceName, true);
  };

  addAndMakeVisible(lblCh);
  addAndMakeVisible(cmbMidiCh);
  cmbMidiCh.clear();
  cmbMidiCh.addItem("All", 17);
  for (int i = 1; i <= 16; ++i)
    cmbMidiCh.addItem(juce::String(i), i);
  cmbMidiCh.setSelectedId(17, juce::dontSendNotification);
  cmbMidiCh.onChange = [this] {
    logPanel.log("! MIDI Ch Changed: " + cmbMidiCh.getText(), false);
  };

  // --- Playback Controls ---
  addAndMakeVisible(btnPlay);
  btnPlay.onClick = [this] {
    if (engine.getIsPlaying() && !pendingSyncStart) {
      pausePlayback();
    } else {
      startPlayback();
    }
  };
  addAndMakeVisible(btnStop);
  btnStop.onClick = [this] { stopPlayback(); };
  addAndMakeVisible(btnPrev);
  btnPrev.onClick = [this] {
    bool wasPlaying = engine.getIsPlaying();
    loadMidiFile(juce::File(playlist.getPrevFile()));
    if (wasPlaying)
      startPlayback();
  };
  addAndMakeVisible(btnSkip);
  btnSkip.onClick = [this] {
    bool wasPlaying = engine.getIsPlaying();
    loadMidiFile(juce::File(playlist.getNextFile()));
    if (wasPlaying)
      startPlayback();
  };
  addAndMakeVisible(btnClearPR);
  btnClearPR.onClick = [this] { clearPlaybackSequence(); };
  addAndMakeVisible(btnResetBPM);
  btnResetBPM.onClick = [this] {
    double target = (currentFileBpm > 0.0) ? currentFileBpm : 120.0;
    if (auto *link = engine.getLink()) {
      auto state = link->captureAppSessionState();
      state.setTempo(target, link->clock().micros());
      link->commitAppSessionState(state);
    }
    parameters.setProperty("bpm", target, nullptr);
    tempoSlider.setValue(target, juce::dontSendNotification);
    logPanel.log("! BPM Reset to " + juce::String(target, 1), true);
    grabKeyboardFocus();
  };

  addAndMakeVisible(btnPreventBpmOverride);
  btnPreventBpmOverride.setButtonText("BPM Lock");
  btnPreventBpmOverride.setClickingTogglesState(true);

  addAndMakeVisible(btnSplit);
  btnSplit.setButtonText("Split");
  btnSplit.setClickingTogglesState(true);
  btnSplit.onClick = [this] {
    if (btnSplit.getToggleState())
      logPanel.log("! Split Mode: Ch1 into Ch 1/2", true);
    else
      logPanel.log("! Split Mode: OFF", true);
  };

  addAndMakeVisible(btnPrOctUp);
  btnPrOctUp.setButtonText("Oct +");
  addAndMakeVisible(btnPrOctDown);
  btnPrOctDown.setButtonText("Oct -");

  // --- Pitch/Mod Wheels ---
  auto setupWheel = [this](juce::Slider &s, bool isPitch) {
    s.setSliderStyle(juce::Slider::LinearVertical);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    if (isPitch) {
      s.setRange(-8192, 8191, 1);
      s.setValue(0, juce::dontSendNotification);
    } else {
      s.setRange(0, 127, 1);
      s.setValue(0, juce::dontSendNotification);
    }
    addAndMakeVisible(s);
  };
  setupWheel(sliderPitchH, true);
  setupWheel(sliderModH, false);
  setupWheel(sliderPitchV, true);
  setupWheel(sliderModV, false);

  auto onWheelChange = [this](bool horizontal) {
    int ch = getSelectedChannel();
    int pVal =
        (int)(horizontal ? sliderPitchH.getValue() : sliderPitchV.getValue()) +
        8192;
    int mVal =
        (int)(horizontal ? sliderModH.getValue() : sliderModV.getValue());
    auto mp = juce::MidiMessage::pitchWheel(ch, pVal);
    auto mm = juce::MidiMessage::controllerEvent(ch, 1, mVal);
    if (midiOutput && !btnBlockMidiOut.getToggleState()) {
      midiOutput->sendMessageNow(mp);
      midiOutput->sendMessageNow(mm);
    }
    sendSplitOscMessage(mp);
    sendSplitOscMessage(mm);
    if (horizontal) {
      sliderPitchV.setValue(sliderPitchH.getValue(),
                            juce::dontSendNotification);
      sliderModV.setValue(sliderModH.getValue(), juce::dontSendNotification);
    } else {
      sliderPitchH.setValue(sliderPitchV.getValue(),
                            juce::dontSendNotification);
      sliderModH.setValue(sliderModV.getValue(), juce::dontSendNotification);
    }
  };
  sliderPitchH.onValueChange = [this, onWheelChange] { onWheelChange(true); };
  sliderModH.onValueChange = [this, onWheelChange] { onWheelChange(true); };
  sliderPitchV.onValueChange = [this, onWheelChange] { onWheelChange(false); };
  sliderModV.onValueChange = [this, onWheelChange] { onWheelChange(false); };

  sliderPitchH.onDragEnd = [this] {
    sliderPitchH.setValue(0, juce::sendNotification);
    logPanel.log("! Pitch Wheel Reset", false);
  };
  sliderPitchV.onDragEnd = [this] {
    sliderPitchV.setValue(0, juce::sendNotification);
  };

  // Octave Buttons
  btnPrOctUp.onClick = [this] {
    pianoRollOctaveShift++;
    virtualOctaveShift = pianoRollOctaveShift;
    logPanel.log("! Octave + (" + juce::String(pianoRollOctaveShift) + ")",
                 true);
    grabKeyboardFocus();
  };
  btnPrOctDown.onClick = [this] {
    pianoRollOctaveShift--;
    virtualOctaveShift = pianoRollOctaveShift;
    logPanel.log("! Octave - (" + juce::String(pianoRollOctaveShift) + ")",
                 true);
    grabKeyboardFocus();
  };

  // --- Components Add ---
  addAndMakeVisible(trackGrid);
  trackGrid.setKeyboardComponent(&horizontalKeyboard);
  trackGrid.wheelStripWidth = 50; // Align with Keyboard's
  addAndMakeVisible(btnResetMixerOnLoad);
  btnResetMixerOnLoad.setToggleState(mixer.isResetOnLoad,
                                     juce::dontSendNotification);
  mixer.isResetOnLoad = btnResetMixerOnLoad.getToggleState();

  // Clock Offset & Mode Controls
  addAndMakeVisible(cmbClockMode);
  cmbClockMode.addItem("Smooth (Default)", 1);
  cmbClockMode.addItem("Phase Locked (Hard)", 2);
  cmbClockMode.setSelectedId(1, juce::dontSendNotification);
  cmbClockMode.onChange = [this] {
    clockMode = (ClockMode)(cmbClockMode.getSelectedId() - 1);
    logPanel.log("! Clock Mode: " + cmbClockMode.getText(), true);
  };

  addAndMakeVisible(sliderClockOffset);
  sliderClockOffset.setRange(-50.0, 50.0, 0.1);
  sliderClockOffset.setValue(0.0);
  sliderClockOffset.setSliderStyle(juce::Slider::LinearBar);
  sliderClockOffset.setTextValueSuffix(" ms");

  addAndMakeVisible(lblClockOffset);
  lblClockOffset.setFont(juce::FontOptions(12.0f));
  lblClockOffset.setText("Clock Offset", juce::dontSendNotification);

  // Help Profiles
  addAndMakeVisible(cmbTheme);
  cmbTheme.addItem("Disco Dark (Default)", 1);
  cmbTheme.addItem("Light Luminator", 2);
  cmbTheme.addItem("Midnight", 3);
  cmbTheme.addItem("Forest", 4);
  cmbTheme.addItem("Vaporwave Neon", 5);
  cmbTheme.addItem("Great Pumpkin", 6);
  cmbTheme.addItem("Sandy Beach", 7);
  cmbTheme.addItem("Rose", 8);
  cmbTheme.addItem("Dark Plum", 9);
  cmbTheme.addItem("Deep Water", 10);
  cmbTheme.setSelectedId(1, juce::dontSendNotification);
  cmbTheme.onChange = [this] {
    int id = cmbTheme.getSelectedId();
    if (id == 1) { // Dark
      Theme::bgDark = juce::Colour::fromString("FF121212");
      Theme::bgPanel = juce::Colour::fromString("FF1E1E1E");
      Theme::accent = juce::Colour::fromString("FF007ACC");
      Theme::grid = juce::Colour::fromString("FF333333");
    } else if (id == 2) {                                   // Light (Dimmed)
      Theme::bgDark = juce::Colour::fromString("FFEEEEEE"); // Less bright
      Theme::bgPanel = juce::Colour::fromString("FFDDDDDD");
      Theme::accent = juce::Colour::fromString("FF666666"); // Greyish accent
      Theme::grid = juce::Colour::fromString("FFBBBBBB");
    } else if (id == 3) { // Midnight
      Theme::bgDark = juce::Colour::fromString("FF050510");
      Theme::bgPanel = juce::Colour::fromString("FF0A0A1A");
      Theme::accent = juce::Colour::fromString("FF5050FF");
      Theme::grid = juce::Colour::fromString("FF151525");
    } else if (id == 4) { // Forest
      Theme::bgDark = juce::Colour::fromString("FF051005");
      Theme::bgPanel = juce::Colour::fromString("FF0A1A0A");
      Theme::accent = juce::Colour::fromString("FF20C040");
      Theme::grid = juce::Colour::fromString("FF102010");
    } else if (id == 5) { // Rainbow
      Theme::bgDark = juce::Colour::fromString("FF1A1A2E");
      Theme::bgPanel = juce::Colour::fromString("FF16213E");
      Theme::accent = juce::Colour::fromString("FF0F3460");
      Theme::grid = juce::Colour::fromString("FFE94560").withAlpha(0.2f);
    } else if (id == 6) {                                   // Harvest Orange
      Theme::bgDark = juce::Colour::fromString("FF2E1A05"); // Dark Brown
      Theme::bgPanel = juce::Colour::fromString("FF3E270E");
      Theme::accent = juce::Colour::fromString("FFFF8C00"); // Dark Orange
      Theme::grid = juce::Colour::fromString("FF5C3A10");
    } else if (id == 7) {                                   // Sandy Beach
      Theme::bgDark = juce::Colour::fromString("FF2C241B"); // Dark Sand
      Theme::bgPanel = juce::Colour::fromString("FF3D342B");
      Theme::accent = juce::Colour::fromString("FFEEDD82"); // Light Gold
      Theme::grid = juce::Colour::fromString("FF594D3F");
    } else if (id == 8) {                                   // Pink & Plentiful
      Theme::bgDark = juce::Colour::fromString("FF200510"); // Deep Pink/Black
      Theme::bgPanel = juce::Colour::fromString("FF300A1A");
      Theme::accent = juce::Colour::fromString("FFFF1493"); // Deep Pink
      Theme::grid = juce::Colour::fromString("FF501025");
    } else if (id == 9) {                                   // Space Purple
      Theme::bgDark = juce::Colour::fromString("FF100018"); // Deep Space
      Theme::bgPanel = juce::Colour::fromString("FF200530");
      Theme::accent = juce::Colour::fromString("FF9932CC"); // Dark Orchid
      Theme::grid = juce::Colour::fromString("FF301040");
    } else if (id == 10) {                                  // Underwater
      Theme::bgDark = juce::Colour::fromString("FF001020"); // Deep Blue
      Theme::bgPanel = juce::Colour::fromString("FF002040");
      Theme::accent = juce::Colour::fromString("FF00BFFF"); // Deep Sky Blue
      Theme::grid = juce::Colour::fromString("FF003060");
    }
    repaint();
    mixer.repaint();
    verticalKeyboard.repaint();
    horizontalKeyboard.repaint();
    trackGrid.repaint();
  };

  addAndMakeVisible(cmbControlProfile);
  // Default Factory Profiles (IDs 1-99)
  cmbControlProfile.addItem("Default Mapping", 1);
  cmbControlProfile.addItem("Roland JD-Xi", 2);
  cmbControlProfile.addItem("Generic Keyboard", 3);
  cmbControlProfile.addItem("FL Studio", 4);
  cmbControlProfile.addItem("Ableton Live", 5);

  // Custom Profiles will be appended by updateProfileComboBox()
  updateProfileComboBox();

  cmbControlProfile.setSelectedId(1, juce::dontSendNotification);
  cmbControlProfile.onChange = [this] {
    int id = cmbControlProfile.getSelectedId();
    if (id < 1000) {
      // Factory Profiles
      if (id == 1)
        applyControlProfile(ControlProfile::getDefault());
      else if (id == 2)
        applyControlProfile(ControlProfile::getRolandJDXi());
      else if (id == 3)
        applyControlProfile(ControlProfile::getGenericKeyboard());
      else if (id == 4)
        applyControlProfile(ControlProfile::getFLStudio());
      else if (id == 5)
        applyControlProfile(ControlProfile::getAbletonLive());
    } else {
      // Custom Profiles (IDs >= 1000)
      auto name = cmbControlProfile.getText();
      auto file = getProfileDirectory().getChildFile(name + ".json");
      if (!file.existsAsFile())
        file = getProfileDirectory().getChildFile(name + ".midimap");

      if (file.existsAsFile()) {
        loadFullProfile(file);
      }
    }

    // Logic for other factory profiles if needed
    logPanel.log("! Profile Loaded: " + cmbControlProfile.getText(), true);
  };

  addAndMakeVisible(btnSaveProfile);
  btnSaveProfile.setButtonText("Export Controller");
  btnSaveProfile.onClick = [this] { saveNewControllerProfile(); };

  addAndMakeVisible(btnDeleteProfile);
  btnDeleteProfile.setColour(juce::TextButton::buttonColourId,
                             juce::Colours::darkred.withAlpha(0.5f));
  btnDeleteProfile.setTooltip("Delete selected custom profile");
  btnDeleteProfile.onClick = [this] { deleteSelectedProfile(); };

  // BINDING SEQUENCER RESET
  sequencer.btnResetCH.onClick = [this] { mixer.resetMapping(); };

  // Setup Visualizers
  addAndMakeVisible(phaseVisualizer);
  addAndMakeVisible(horizontalKeyboard);
  addAndMakeVisible(verticalKeyboard);

  // Activity Lights
  addAndMakeVisible(midiInLight);
  midiInLight.setTooltip("MIDI Input Activity");
  addAndMakeVisible(midiOutLight);
  midiOutLight.setTooltip("MIDI Output Activity");

  // Pitch/Mod Wheels (Horizontal)
  addAndMakeVisible(sliderPitchH);
  sliderPitchH.setSliderStyle(juce::Slider::LinearBarVertical);
  sliderPitchH.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  addAndMakeVisible(sliderModH);
  sliderModH.setSliderStyle(juce::Slider::LinearBarVertical);
  sliderModH.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  addAndMakeVisible(logPanel);
  addAndMakeVisible(playlist);
  playlist.onLoopModeChanged = [this](juce::String state) {
    logPanel.log("! Playlist Mode: " + state, true);
  };
  addAndMakeVisible(sequencer);
  sequencer.onExportRequest = [this] {
    fc = std::make_unique<juce::FileChooser>(
        "Export MIDI Sequence",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.mid");
    fc->launchAsync(
        juce::FileBrowserComponent::saveMode |
            juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser &chooser) {
          auto result = chooser.getResult();
          if (result != juce::File{}) {
            sequencer.exportToMidi(result);
            logPanel.log("! Sequence Exported: " + result.getFileName(), true);

            // Auto-load into playlist
            playlist.addFile(result.getFullPathName());
            loadMidiFile(result);
          }
        });
  };

  addAndMakeVisible(mixerViewport);
  mixer.setBounds(0, 0, 16 * mixer.stripWidth, 300);
  mixerViewport.setViewedComponent(&mixer, false);
  mixerViewport.setScrollBarsShown(false, true);

  addAndMakeVisible(lblArp);
  addAndMakeVisible(grpArp);

  // --- Arpeggiator ---
  btnArp.setButtonText("Latch");
  btnArp.setClickingTogglesState(true);
  btnArp.setColour(juce::TextButton::buttonOnColourId, Theme::accent);
  btnArp.onClick = [this] {
    if (!btnArp.getToggleState()) {
      heldNotes.clear();
      noteArrivalOrder.clear();
      keyboardState.allNotesOff(getSelectedChannel());
      logPanel.log("! Arp Latch: OFF", false);
    } else {
      heldNotes.clear();
      noteArrivalOrder.clear();
      for (int i = 0; i < 128; ++i) {
        if (keyboardState.isNoteOn(1, i)) {
          heldNotes.add(i);
          noteArrivalOrder.push_back(i);
        }
      }
      logPanel.log("! Arp Latch: ON", false);
    }
  };

  btnArpSync.setButtonText("Sync");
  btnArpSync.setClickingTogglesState(true);
  btnArpSync.setColour(juce::TextButton::buttonOnColourId, Theme::accent);
  btnArpSync.onClick = [this] {
    logPanel.log("! Arp Sync: " +
                     juce::String(btnArpSync.getToggleState() ? "ON" : "OFF"),
                 false);
  };

  sliderArpSpeed.setColour(juce::Slider::thumbColourId, Theme::accent);

  sliderArpVel.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  sliderArpVel.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  sliderArpVel.setRange(0, 127, 1);
  sliderArpVel.setValue(90);
  sliderArpVel.setColour(juce::Slider::thumbColourId, Theme::accent);

  sliderArpSpeed.getProperties().set("paramID", "arp_speed");
  sliderArpVel.getProperties().set("paramID", "arp_vel");
  sliderNoteDelay.getProperties().set("paramID", "note_delay");
  btnArp.getProperties().set("paramID", "arp_on");
  btnArpSync.getProperties().set("paramID", "arp_sync");
  btnArpLatch.getProperties().set("paramID", "arp_latch");

  // BPM & Simple Faders
  tempoSlider.getProperties().set("paramID", "bpm");
  vol1Simple.getProperties().set("paramID", "vol1");
  vol2Simple.getProperties().set("paramID", "vol2");

  // Transport Tags
  btnPlay.getProperties().set("paramID", "play");
  btnStop.getProperties().set("paramID", "stop");

  // Mixer Strips tagging
  for (int i = 0; i < mixer.strips.size(); ++i) {
    auto *s = mixer.strips[i];
    if (s != nullptr) {
      s->volSlider.getProperties().set("paramID",
                                       "Mixer_" + juce::String(i + 1) + "_Vol");
      s->btnActive.getProperties().set("paramID",
                                       "Mixer_" + juce::String(i + 1) + "_On");
    }
  }

  addAndMakeVisible(sliderArpSpeed);
  addAndMakeVisible(sliderArpVel);
  addAndMakeVisible(sliderNoteDelay);
  addAndMakeVisible(btnArp);
  addAndMakeVisible(btnArpSync);
  addAndMakeVisible(btnArpLatch);

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
  cmbArpPattern.setJustificationType(juce::Justification::centred);
  cmbArpPattern.onChange = [this] {
    logPanel.log("! Arp Pattern: " + cmbArpPattern.getText(), false);
  };

  // --- Mixer Events ---
  mixer.onMixerActivity = [this](int ch, float val) {
    // When mixer is moved, send OSC mapped to the visual channel
    int mappedCh = mixer.getMappedChannel(ch);
    sendSplitOscMessage(juce::MidiMessage::controllerEvent(ch, 7, (int)val),
                        mappedCh);
    logPanel.log("Mixer Ch" + juce::String(ch) + " (Slot " +
                     juce::String(mappedCh) +
                     ") Vol: " + juce::String((int)val),
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

  // --- Help ---
  helpText.setMultiLine(true);
  helpText.setReadOnly(true);
  helpText.setFont(juce::FontOptions(13.0f));
  helpText.setText("Patchworld Bridge");
  helpViewport.setViewedComponent(&helpTextDisplay, false);
  helpTextDisplay.setMultiLine(true);
  helpTextDisplay.setReadOnly(true);
  helpTextDisplay.setText("Instructions will be added here.");
  helpTextDisplay.setColour(juce::TextEditor::backgroundColourId,
                            juce::Colours::black.withAlpha(0.6f));
  addChildComponent(helpViewport);

  // --- Help Tooltips ---
  btnDash.setTooltip("Full View/Simple View");
  btnCtrl.setTooltip("CC Control Page");
  btnOscCfg.setTooltip("Set OSC Addresses");
  btnHelp.setTooltip("If you are reading this you may need help");

  // --- MIDI Mapping & Learn ---
  mappingManager.setParameterValueCallback = [this](juce::String paramID,
                                                    float val) {
    setParameterValue(paramID, val);
  };
  mappingManager.onMidiLogCallback = [this](juce::String msg) {
    logPanel.log(msg, msg.startsWith("!"));
  };
  mappingManager.onMappingChanged = [this] {
    auto name = cmbControlProfile.getText();
    if (name.isNotEmpty() && name != "Default Mapping") {
      saveFullProfile(getProfileDirectory().getChildFile(name + ".json"));
      logPanel.log("! Auto-Saved Profile: " + name, false);
    }
  };

  btnMidiLearn.onClick = [this] {
    isMidiLearnMode = btnMidiLearn.getToggleState();
    mappingManager.setLearnModeActive(isMidiLearnMode);
    midiLearnOverlay.setOverlayActive(isMidiLearnMode);
    midiLearnOverlay.setVisible(isMidiLearnMode);
    if (isMidiLearnMode)
      logPanel.log("! MIDI Learn: ON - Click a slider then move a knob", true);
  };
  btnPlay.setTooltip("Starts .mid Playback & Sequencer");
  btnStop.setTooltip("Stops .mid Playback & Sequencer");
  btnPrev.setTooltip("Previous Track");
  btnSkip.setTooltip("Skip Track");
  btnClearPR.setTooltip("Clears loaded song");
  btnResetBPM.setTooltip("Resets BPM to 120 if No .mid Loaded");
  btnTapTempo.setTooltip("Tap Tempo 4 Times to Set BPM");
  btnPrOctUp.setTooltip("Increase .mid Octave");
  btnPrOctDown.setTooltip("Decrease .mid Octave");
  btnPanic.setTooltip("Kills All Hung Notes on MIDI Out");
  btnConnect.setTooltip("Start/Stops OSC Server");
  btnRetrigger.setTooltip("Retriggers notes on Note Off");
  btnGPU.setTooltip(
      "Toggle GPU Rendering - May or May Not Improve Gui Performance");
  btnBlockMidiOut.setTooltip("Mutes MIDI out");
  btnLinkToggle.setTooltip("Toggle Ableton Link - Check Log for # Connections");
  btnSplit.setTooltip("Splits CH1 Notes C4 & Below to Ch2");
  btnPreventBpmOverride.setTooltip("Prevent MIDI Files From Changing Tempo");
  cmbQuantum.setTooltip("Ableton Link Quantum");
  nudgeSlider.setTooltip("Nudge BPM Temporarily");
  btnArp.setTooltip("Toggle Arp Key Hold");
  btnArpSync.setTooltip("Sync Arpeggiator to Link BPM");
  sliderArpSpeed.setTooltip("Arp Speed (Not Linked to BPM)");
  sliderArpVel.setTooltip("Arp Velocity Out");
  cmbArpPattern.setTooltip("Select Arp Pattern");
  sliderNoteDelay.setTooltip("Note Duration (Virtual Keyboard)");
  cmbMidiIn.setTooltip("Select MIDI Input Device");
  cmbMidiOut.setTooltip("Select MIDI Output Device");
  cmbMidiCh.setTooltip("Select MIDI out");
  edIp.setTooltip("OSC Destination IPv4 Address (Put Headsets IPv4 here)");
  edPOut.setTooltip("OSC Out");
  edPIn.setTooltip("OSC In");
  lblLocalIpDisplay.setTooltip(
      "This Devices Current IPv4 Address (VPN Might Block Connection)");
  tempoSlider.setTooltip("Set Global Tempo");
  sliderPitchH.setTooltip("Pitch Bend");
  sliderModH.setTooltip("Modulation");
  sliderPitchV.setTooltip("Pitch Bend");
  sliderModV.setTooltip("Modulation");
  vol1Simple.setTooltip("An OSC CC Slider");
  vol2Simple.setTooltip("An OSC CC Slider");
  txtVol1Osc.setTooltip("Set OSC Address");
  txtVol2Osc.setTooltip("Set OSC Address");
  btnMidiThru.setTooltip("Enable MIDI Thru (Sends midi in clock to out)");
  btnVol1CC.setTooltip("Send CC20 /ch1cc (20)");
  btnVol2CC.setTooltip("Send CC21 /ch1cc (21)");
  btnMidiClock.setTooltip("Sends MIDI Clock to MIDI Out");

  // --- Final Init ---
  setSize(800, 750);
  juce::Timer::startTimer(40);
  currentView = AppView::Dashboard;
  updateVisibility();
  resized();

  btnMidiScaling.setButtonText("Scaling 1");
  btnMidiScaling.setColour(juce::TextButton::buttonColourId,
                           juce::Colours::transparentBlack);
  btnMidiScaling.setColour(juce::TextButton::textColourOffId, Theme::accent);

  addAndMakeVisible(btnMidiScalingToggle);
  btnMidiScalingToggle.setButtonText("Scale: 0-1 (Default)");
  btnMidiScalingToggle.onClick = [this] {
    isMidiScaling127 = btnMidiScalingToggle.getToggleState();
    btnMidiScalingToggle.setButtonText(
        isMidiScaling127 ? "Scale: 0-127 (Raw)" : "Scale: 0-1 (Default)");
    logPanel.log("! MIDI Scaling: " +
                     juce::String(isMidiScaling127 ? "0-127" : "0-1"),
                 true);
  };

  btnSaveProfile.onClick = [this] {
    auto name = cmbControlProfile.getText();
    if (name == "Default") {
      logPanel.log("Cannot overwrite Default profile. Please type a new name.",
                   false);
      return;
    }
    saveFullProfile(getProfileDirectory().getChildFile(name + ".json"));
    updateProfileComboBox();
    cmbControlProfile.setText(name, juce::dontSendNotification);
  };

  btnLoadProfile.onClick = [this] {
    auto file = getProfileDirectory().getChildFile(cmbControlProfile.getText() +
                                                   ".json");
    if (file.existsAsFile())
      loadFullProfile(file);
  };

  btnDeleteProfile.onClick = [this] {
    auto file = getProfileDirectory().getChildFile(cmbControlProfile.getText() +
                                                   ".json");
    if (file.existsAsFile()) {
      file.deleteFile();
      updateProfileComboBox();
      logPanel.log("! Profile Deleted", true);
    }
  };

  btnResetLearned.setButtonText("Reset MidiMap");
  btnResetLearned.onClick = [this] {
    mappingManager.resetMappings();
    logPanel.log("! MIDI Mappings Reset", true);
  };

  btnDeleteProfile.setColour(juce::TextButton::buttonColourId,
                             juce::Colours::red.withAlpha(0.6f));
  btnSaveProfile.setColour(juce::TextButton::buttonColourId,
                           Theme::accent.withAlpha(0.8f));

  addAndMakeVisible(cmbTheme);
  addAndMakeVisible(cmbControlProfile);
  addAndMakeVisible(btnSaveProfile);
  addAndMakeVisible(btnLoadProfile);
  addAndMakeVisible(btnDeleteProfile);
  addAndMakeVisible(btnResetLearned);
  addAndMakeVisible(btnResetMixerOnLoad);
  addAndMakeVisible(cmbClockMode);
  addAndMakeVisible(sliderClockOffset);
  addAndMakeVisible(lblClockOffset);

  updateProfileComboBox();

  addChildComponent(midiLearnOverlay);
  midiLearnOverlay.setAlwaysOnTop(false); // Managed by toFront

  // --- SEQUENCER OVERRIDES ---
  // Replaces the basic btnRec.onClick
  sequencer.btnRec.onClick = [this] {
    bool state = sequencer.btnRec.getToggleState();
    sequencer.isRecording = state;

    if (state) {
      logPanel.log("! Recording Armed", true);
      startPlayback(); // Triggers Count-In if enabled
    } else {
      logPanel.log("! Recording Stopped", true);
    }
  };
}

//==============================================================================
// HELPER FUNCTIONS
//==============================================================================
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

//==============================================================================
// OSC MESSAGE HANDLING (FIXED 1-16 & MULTI-ARG CC)
//==============================================================================
void MainComponent::oscMessageReceived(const juce::OSCMessage &m) {
  // If we are in MIDI Learn mode, ignore incoming OSC to prevent visual
  // clutter/conflicts
  if (isMidiLearnMode)
    return;

  juce::String addr = m.getAddressPattern().toString();

  // RAII for isHandlingOsc
  struct OscLock {
    bool &flag;
    OscLock(bool &f) : flag(f) { flag = true; }
    ~OscLock() { flag = false; }
  };

  // LOGGING
  if (!addr.contains("wheel") && !addr.contains("press")) {
    juce::String logStr = "! OSC Rx: " + addr;
    for (int i = 0; i < m.size(); ++i) {
      logStr += " ";
      if (m[i].isFloat32())
        logStr += juce::String(m[i].getFloat32(), 2);
      else if (m[i].isInt32())
        logStr += juce::String(m[i].getInt32());
    }
    // Log blocked for performance
    // juce::MessageManager::callAsync(
    //      [this, logStr] { logPanel.log(logStr, false); });
  }

  // ROBUST VALUE SCALING (Fixes "stuck" values)
  auto getVal = [&](int argIdx) -> float {
    if (argIdx >= m.size())
      return 0.0f;
    float v = 0.0f;
    bool isInt = m[argIdx].isInt32();
    if (isInt)
      v = (float)m[argIdx].getInt32();
    else if (m[argIdx].isFloat32())
      v = m[argIdx].getFloat32();

    // If the value is > 1.0, it's definitely 0-127 scale.
    if (v > 1.0f)
      return juce::jlimit(0.0f, 1.0f, v / 127.0f);

    // If we are in explicit 127 scaling mode, treat everything as 0-127
    if (isMidiScaling127)
      return juce::jlimit(0.0f, 1.0f, v / 127.0f);

    // In 0-1 mode, treat Int 1 as 1.0
    if (v == 1.0f && isInt)
      return 1.0f;

    return juce::jlimit(0.0f, 1.0f, v);
  };

  if (addr == oscConfig.ePlay.getText() || addr.endsWith("/play")) {
    juce::MessageManager::callAsync([this] { btnPlay.triggerClick(); });
    return;
  }
  if (addr == oscConfig.eStop.getText() || addr.endsWith("/stop")) {
    juce::MessageManager::callAsync([this] { btnStop.triggerClick(); });
    return;
  }
  if (addr == oscConfig.eTap.getText() || addr.endsWith("/tap")) {
    juce::MessageManager::callAsync([this] { btnTapTempo.triggerClick(); });
    return;
  }

  // CHANNEL-BASED LOGIC (/chX...)
  if (addr.startsWith("/ch")) {
    juce::String chStr = addr.substring(3);
    int cmdIdx = 0;
    while (cmdIdx < chStr.length() &&
           juce::CharacterFunctions::isDigit(chStr[cmdIdx]))
      cmdIdx++;
    int ch = chStr.substring(0, cmdIdx).getIntValue();
    juce::String cmd = chStr.substring(cmdIdx);

    if (ch >= 1 && ch <= 16) {
      OscLock lock(isHandlingOsc);
      if (cmd == "n") { // Note On pairs: [n1, v1, n2, v2...]
        for (int i = 0; i < m.size(); i += 2) {
          int note = m[i].isInt32() ? m[i].getInt32() : (int)m[i].getFloat32();
          float vel = (i + 1 < m.size()) ? getVal(i + 1) : 0.8f;
          if (vel > 0.001f) {
            keyboardState.noteOn(ch, note, vel);
            if (midiOutput && !btnBlockMidiOut.getToggleState())
              midiOutput->sendMessageNow(
                  juce::MidiMessage::noteOn(ch, note, vel));
            double dur = getDurationFromVelocity(vel);
            engine.scheduleNoteOff(
                ch, note, juce::Time::getMillisecondCounterHiRes() + dur);
          } else {
            keyboardState.noteOff(ch, note, 0.0f);
            if (midiOutput && !btnBlockMidiOut.getToggleState())
              midiOutput->sendMessageNow(
                  juce::MidiMessage::noteOff(ch, note, 0.0f));
          }
        }
        return;
      }
      if (cmd == "noff") { // Note Off pairs: [n1, n2...]
        for (int i = 0; i < m.size(); ++i) {
          int note = m[i].isInt32() ? m[i].getInt32() : (int)m[i].getFloat32();
          keyboardState.noteOff(ch, note, 0.0f);
          if (midiOutput && !btnBlockMidiOut.getToggleState())
            midiOutput->sendMessageNow(
                juce::MidiMessage::noteOff(ch, note, 0.0f));
        }
        return;
      }
      if (cmd == "c") { // CC pairs: [cc1, val1, cc2, val2...]
        for (int i = 0; i < m.size(); i += 2) {
          int cc = m[i].isInt32() ? m[i].getInt32() : (int)m[i].getFloat32();
          float val = (i + 1 < m.size()) ? getVal(i + 1) : 0.0f;
          lastReceivedCC[ch] = cc; // REMEMBER!
          if (midiOutput && !btnBlockMidiOut.getToggleState())
            midiOutput->sendMessageNow(juce::MidiMessage::controllerEvent(
                ch, cc, (int)(val * 127.0f)));
        }
        return;
      }
      if (cmd == "cv") { // CC Value Only for remembered CC
        float val = getVal(0);
        int cc = lastReceivedCC[ch];
        if (midiOutput && !btnBlockMidiOut.getToggleState())
          midiOutput->sendMessageNow(
              juce::MidiMessage::controllerEvent(ch, cc, (int)(val * 127.0f)));
        return;
      }
      if (cmd == "wheel") {
        float val = getVal(0);
        if (midiOutput && !btnBlockMidiOut.getToggleState())
          midiOutput->sendMessageNow(
              juce::MidiMessage::pitchWheel(ch, (int)(val * 16383.0f)));
        return;
      }
      if (cmd == "press") {
        float val = getVal(0);
        if (midiOutput && !btnBlockMidiOut.getToggleState())
          midiOutput->sendMessageNow(juce::MidiMessage::channelPressureChange(
              ch, (int)(val * 127.0f)));
        return;
      }
    }
  }

  // TEMPLATE-BASED LOGIC (Replace {X} in User Addresses)
  for (int i = 1; i <= 16; ++i) {
    juce::String sCh = juce::String(i);
    OscLock lock(isHandlingOsc);

    if (addr == oscConfig.eRXn.getText().replace("{X}", sCh)) {
      for (int a = 0; a < m.size(); a += 2) {
        int note = m[a].isInt32() ? m[a].getInt32() : (int)m[a].getFloat32();
        float vel = (a + 1 < m.size()) ? getVal(a + 1) : 0.8f;
        keyboardState.noteOn(i, note, vel);
        if (midiOutput && !btnBlockMidiOut.getToggleState())
          midiOutput->sendMessageNow(juce::MidiMessage::noteOn(i, note, vel));
        engine.scheduleNoteOff(i, note,
                               juce::Time::getMillisecondCounterHiRes() +
                                   getDurationFromVelocity(vel));
      }
      return;
    }
    if (addr == oscConfig.eRXnoff.getText().replace("{X}", sCh)) {
      for (int a = 0; a < m.size(); ++a) {
        int note = m[a].isInt32() ? m[a].getInt32() : (int)m[a].getFloat32();
        keyboardState.noteOff(i, note, 0.0f);
        if (midiOutput && !btnBlockMidiOut.getToggleState())
          midiOutput->sendMessageNow(juce::MidiMessage::noteOff(i, note, 0.0f));
      }
      return;
    }
    if (addr == oscConfig.eRXc.getText().replace("{X}", sCh)) {
      for (int a = 0; a < m.size(); a += 2) {
        int cc = m[a].isInt32() ? m[a].getInt32() : (int)m[a].getFloat32();
        float v = (a + 1 < m.size()) ? getVal(a + 1) : 0.0f;
        lastReceivedCC[i] = cc; // REMEMBER!
        if (midiOutput && !btnBlockMidiOut.getToggleState())
          midiOutput->sendMessageNow(
              juce::MidiMessage::controllerEvent(i, cc, (int)(v * 127.0f)));
      }
      return;
    }
    if (addr == oscConfig.eRXcv.getText().replace("{X}", sCh)) {
      int cc = lastReceivedCC[i];
      int v = (int)(getVal(0) * 127.0f);
      if (midiOutput && !btnBlockMidiOut.getToggleState())
        midiOutput->sendMessageNow(
            juce::MidiMessage::controllerEvent(i, cc, v));
      return;
    }
    if (addr == oscConfig.eRXwheel.getText().replace("{X}", sCh)) {
      if (midiOutput && !btnBlockMidiOut.getToggleState())
        midiOutput->sendMessageNow(
            juce::MidiMessage::pitchWheel(i, (int)(getVal(0) * 16383.0f)));
      return;
    }
    if (addr == oscConfig.eRXpress.getText().replace("{X}", sCh)) {
      if (midiOutput && !btnBlockMidiOut.getToggleState())
        midiOutput->sendMessageNow(juce::MidiMessage::channelPressureChange(
            i, (int)(getVal(0) * 127.0f)));
      return;
    }
  }
}

//==============================================================================
// MIDI OUT LOGIC
//==============================================================================
void MainComponent::handleNoteOn(juce::MidiKeyboardState *, int ch, int note,
                                 float vel) {
  if (vel == 0.0f) {
    handleNoteOff(nullptr, ch, note, 0.0f);
    return;
  }

  // SPLIT LOGIC FIX:
  // User reported: "one side of keyboard being sent to midi and the other
  // deactivated". Consolidate target determination.

  if (btnSplit.getToggleState() && ch == 1) {
    if (note < 64)
      ch = 2; // Split Point
  }

  logPanel.log("! IN Note On: " + juce::String(note) + " (Ch" +
                   juce::String(ch) + ")",
               false);

  if (btnArp.getToggleState()) {
    // Poly Arp Latch: Clear if no fingers were down and this is a new note
    if (numFingersDown == 0) {
      heldNotes.clear();
      noteArrivalOrder.clear();
    }
    numFingersDown++;
    heldNotes.add(note);
    noteArrivalOrder.push_back(note);
  } else {
    // Send to OSC (Bidirectional) and MIDI Out
    if (!isHandlingOsc)
      sendSplitOscMessage(juce::MidiMessage::noteOn(ch, note, vel), ch);
    // Force 'ch' into sendSplitOscMessage to ensure it uses the SPLIT
    // channel.

    if (midiOutput && !btnBlockMidiOut.getToggleState()) {
      midiOutput->sendMessageNow(juce::MidiMessage::noteOn(ch, note, vel));
    }
  }

  // Sequencer Recording
  if (sequencer.isRecording) {
    int step = sequencer.currentStep;
    if (step >= 0) {
      sequencer.recordNoteOnStep(step, note, vel);

      // Auto-update note slider if linked
      if (sequencer.btnLinkRoot.getToggleState()) {
        juce::MessageManager::callAsync([this, note] {
          sequencer.noteSlider.setValue(note, juce::dontSendNotification);
        });
      }
    }
  }
}

void MainComponent::handleNoteOff(juce::MidiKeyboardState *, int ch, int note,
                                  float vel) {
  if (btnArp.getToggleState()) {
    if (numFingersDown > 0)
      numFingersDown--;
    // In Latch mode, we DON'T remove from heldNotes on release
    return;
  }

  // CRITICAL FIX: Match the Split logic from handleNoteOn
  if (btnSplit.getToggleState() && ch == 1) {
    if (note < 64)
      ch = 2;
  }

  heldNotes.removeFirstMatchingValue(note);
  logPanel.log("! IN Note Off: " + juce::String(note) + " (Ch" +
                   juce::String(ch) + ")",
               false);

  if (btnRetrigger.getToggleState()) {
    juce::MidiMessage m = juce::MidiMessage::noteOn(ch, note, 100.0f / 127.0f);
    if (!isHandlingOsc)
      sendSplitOscMessage(m, ch);
    if (midiOutput && !btnBlockMidiOut.getToggleState())
      midiOutput->sendMessageNow(m);
  } else {
    juce::MidiMessage m = juce::MidiMessage::noteOff(ch, note, vel);
    // Send to OSC (Bidirectional) and MIDI Out
    if (!isHandlingOsc)
      sendSplitOscMessage(m, ch);

    // Centralized MIDI Hardware Output
    if (midiOutput && !btnBlockMidiOut.getToggleState())
      midiOutput->sendMessageNow(m);
  }
}

void MainComponent::sendSplitOscMessage(const juce::MidiMessage &m,
                                        int overrideChannel) {
  if (!isOscConnected)
    return;

  auto sendTo = [this, &m, overrideChannel](int rawCh) {
    int ch = (overrideChannel != -1) ? overrideChannel
                                     : mixer.getMappedChannel(rawCh);

    if (ch < 1 || ch > 16)
      ch = 1;

    juce::String chS = juce::String(ch);
    float scale = isMidiScaling127 ? 127.0f : 1.0f;
    juce::String logMsg = "! OSC OUT (Ch" + chS + "): ";

    if (m.isNoteOn()) {
      oscSender.send("/ch" + chS + "note", (float)m.getNoteNumber());
      oscSender.send("/ch" + chS + "nvalue",
                     (m.getVelocity() / 127.0f) * scale);
      logMsg += "Note On " + juce::String(m.getNoteNumber());
    } else if (m.isNoteOff()) {
      oscSender.send("/ch" + chS + "noteoff", (float)m.getNoteNumber());
      logMsg += "Note Off " + juce::String(m.getNoteNumber());
    } else if (m.isController()) {
      int ccNum = m.getControllerNumber();
      lastReceivedCC[ch] = ccNum; // SYNC!
      oscSender.send("/ch" + chS + "cc", (float)ccNum);
      oscSender.send("/ch" + chS + "ccvalue",
                     (m.getControllerValue() / 127.0f) * scale);
      logMsg += "CC " + juce::String(ccNum);
    } else if (m.isPitchWheel()) {
      oscSender.send("/ch" + chS + "pitch",
                     (float)(m.getPitchWheelValue() - 8192) / 8192.0f);
      logMsg += "Pitch Wheel";
    } else if (m.isChannelPressure()) {
      oscSender.send("/ch" + chS + "pressure",
                     (m.getChannelPressureValue() / 127.0f) * scale);
      logMsg += "Pressure";
    }

    midiOutLight.activate();
    // Log blocked for performance
    // logPanel.log(logMsg, false);
  };

  int baseCh = (overrideChannel != -1)
                   ? overrideChannel
                   : (cmbMidiCh.getSelectedId() == 17
                          ? (m.getChannel() > 0 ? m.getChannel() : 1)
                          : cmbMidiCh.getSelectedId());

  if (btnSplit.getToggleState() && baseCh == 1) {
    if (m.isNoteOnOrOff()) {
      int n = m.getNoteNumber();
      sendTo(n < 64 ? 2 : 1);
    } else {
      sendTo(1);
      sendTo(2);
    }
  } else {
    sendTo(baseCh);
  }
}

//==============================================================================
// TIMER CALLBACKS
//==============================================================================
// V5.2 CONSOLIDATED TIMER CALLBACK
// -----------------------------------------------------------------------------

void MainComponent::timerCallback() {
  auto *link = engine.getLink();
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
    logPanel.updateStats("Peers: " + juce::String(link->numPeers()));
  }

  int currentPeers = (int)link->numPeers();
  if (currentPeers != lastNumPeers) {
    logPanel.log("! Link Peers: " + juce::String(currentPeers), true);
    lastNumPeers = currentPeers;
  }

  phaseVisualizer.setPhase(session.phaseAtTime(link->clock().micros(), quantum),
                           quantum);

  // Update Playhead visually
  trackGrid.playbackCursor =
      (float)(engine.getCurrentBeat() * engine.getTicksPerQuarter());
}

// ==============================================================================
// VISIBILITY & LAYOUT
// ==============================================================================
void MainComponent::updateVisibility() {
  bool isDash = (currentView == AppView::Dashboard);
  bool isSimple = isSimpleMode;
  bool isOverlayActive = (currentView != AppView::Dashboard);

  // Overlays
  oscViewport.setVisible(currentView == AppView::OSC_Config);
  helpViewport.setVisible(currentView == AppView::Help);
  controlPage.setVisible(currentView == AppView::Control);

  // Global Header
  btnDash.setVisible(true);
  btnCtrl.setVisible(true);
  btnOscCfg.setVisible(true);
  btnHelp.setVisible(true);
  btnPanic.setVisible(true);

  // Dashboard Shared
  bool showTransport = isDash || isOverlayActive;
  lblTempo.setVisible(showTransport);
  tempoSlider.setVisible(showTransport);
  btnResetBPM.setVisible(isDash);
  btnLinkToggle.setVisible(isDash);
  btnTapTempo.setVisible(isDash);
  phaseVisualizer.setVisible(isDash);

  btnPlay.setVisible(isDash);
  btnStop.setVisible(isDash);
  btnPrev.setVisible(isDash);
  btnSkip.setVisible(isDash);
  btnClearPR.setVisible(isDash);

  btnSplit.setVisible(isDash);
  btnRetrigger.setVisible(isDash);
  btnMidiLearn.setVisible(isDash);
  btnMidiThru.setVisible(isDash);
  btnBlockMidiOut.setVisible(isDash);
  btnPrOctUp.setVisible(isDash);
  btnPrOctDown.setVisible(isDash);

  // Default Exclusive
  bool isDefault = isDash && !isSimple;
  trackGrid.setVisible(isDash);
  horizontalKeyboard.setVisible(isDefault);
  sequencer.setVisible(isDefault);
  mixerViewport.setVisible(isDefault);
  grpArp.setVisible(isDefault);
  sliderPitchH.setVisible(isDefault);
  sliderModH.setVisible(isDefault);
  logPanel.setVisible(isDash);
  playlist.setVisible(isDash);
  grpIo.setVisible(isDash);
  midiInLight.setVisible(true);
  midiOutLight.setVisible(true);
  grpNet.setVisible(isDefault);

  // Arp Gen Controls Visibility
  btnArpLatch.setVisible(isDefault);
  btnArpSync.setVisible(isDefault);
  btnArpBlock.setVisible(isDefault);
  sliderArpSpeed.setVisible(isDefault);
  sliderArpVel.setVisible(isDefault);
  lblArpSpdLabel.setVisible(isDefault);
  lblArpVelLabel.setVisible(isDefault);
  cmbArpPattern.setVisible(isDefault);

  // Simple Mode Exclusive
  bool isSimpleDash = isDash && isSimple;
  verticalKeyboard.setVisible(isSimpleDash);
  sliderPitchV.setVisible(isSimpleDash);
  sliderModV.setVisible(isSimpleDash);
  vol1Simple.setVisible(isSimpleDash);
  vol2Simple.setVisible(isSimpleDash);
  txtVol1Osc.setVisible(isSimpleDash);
  txtVol2Osc.setVisible(isSimpleDash);
  btnVol1CC.setVisible(isSimpleDash);
  btnVol2CC.setVisible(isSimpleDash);

  // Help View Exclusive
  bool isHelp = (currentView == AppView::Help);
  cmbTheme.setVisible(isHelp);
  cmbControlProfile.setVisible(isHelp);
  btnSaveProfile.setVisible(isHelp);
  btnLoadProfile.setVisible(isHelp);
  btnDeleteProfile.setVisible(isHelp);
  btnGPU.setVisible(isHelp);
  btnMidiScalingToggle.setVisible(isHelp);
  btnResetLearned.setVisible(isHelp);
  btnResetMixerOnLoad.setVisible(isHelp);
  cmbClockMode.setVisible(isHelp);
  sliderClockOffset.setVisible(isHelp);
  lblClockOffset.setVisible(isHelp);
}

void MainComponent::resized() {
  midiLearnOverlay.setBounds(getLocalBounds());
  auto area = getLocalBounds();

  // ==============================================================================
  // 1. GLOBAL HEADER (Top 35px)
  // ==============================================================================
  auto headerRow = area.removeFromTop(35).reduced(2);

  // --- LEFT: Logo & IP
  auto logoArea = headerRow.removeFromLeft(35);
  logoView.setBounds(logoArea);
  ledConnect.setVisible(true);
  ledConnect.toFront(false);
  ledConnect.setBounds(logoArea.getX() + 2, logoArea.getY() + 2, 8, 8);

  if (!isSimpleMode) {
    lblLocalIpHeader.setVisible(true);
    lblLocalIpDisplay.setVisible(true);
    lblLocalIpHeader.setBounds(headerRow.removeFromLeft(45));
    lblLocalIpDisplay.setBounds(headerRow.removeFromLeft(100));
  } else {
    lblLocalIpHeader.setVisible(false);
    lblLocalIpDisplay.setVisible(false);
  }

  // --- RIGHT: Header Buttons ---
  if (!isSimpleMode && currentView == AppView::Dashboard) {
    btnGPU.setBounds(headerRow.removeFromRight(45).reduced(1));

    // Lights to right of Panic
    auto lightArea = headerRow.removeFromRight(22).reduced(0, 4);
    int lightH = lightArea.getHeight() / 2;
    midiInLight.setVisible(true);
    midiInLight.setBounds(lightArea.removeFromTop(9).reduced(1));
    midiOutLight.setVisible(true);
    midiOutLight.setBounds(lightArea.reduced(1));

    btnPanic.setBounds(headerRow.removeFromRight(60).reduced(1));
    btnExtSync.setVisible(true);
    btnExtSync.setBounds(headerRow.removeFromRight(40).reduced(1));
    btnMidiThru.setBounds(headerRow.removeFromRight(45).reduced(1));
    btnMidiLearn.setBounds(headerRow.removeFromRight(50).reduced(1));
    btnRetrigger.setBounds(headerRow.removeFromRight(55).reduced(1));
    btnSplit.setBounds(headerRow.removeFromRight(60).reduced(1));
  } else {
    // Simple Mode / Overlay Header
    btnGPU.setBounds(headerRow.removeFromRight(45).reduced(1));
    auto lightArea = headerRow.removeFromRight(20).reduced(0, 4);
    midiInLight.setBounds(lightArea.removeFromTop(8).reduced(1));
    midiOutLight.setBounds(lightArea.reduced(1));
    btnPanic.setBounds(headerRow.removeFromRight(50).reduced(1));
    btnExtSync.setVisible(false);
  }

  // --- CENTER: Navigation ---
  int btnW = 90;
  auto navArea = headerRow.withSizeKeepingCentre(4 * btnW, 30);
  btnDash.setBounds(navArea.removeFromLeft(btnW).reduced(1));
  btnCtrl.setBounds(navArea.removeFromLeft(btnW).reduced(1));
  btnOscCfg.setBounds(navArea.removeFromLeft(btnW).reduced(1));
  btnHelp.setBounds(navArea.removeFromLeft(btnW).reduced(1));

  // Handle Overlays
  if (currentView != AppView::Dashboard) {
    auto overlayRect = getLocalBounds().reduced(20).withY(50);

    // SPECIAL REQUEST: Move BPM Slider to Top Left in Menu Mode
    tempoSlider.setVisible(true);
    tempoSlider.setBounds(50, 5, 140, 25); // Overlaying the IP area

    oscViewport.setBounds(overlayRect);
    helpViewport.setBounds(overlayRect);
    controlPage.setBounds(overlayRect);

    if (currentView == AppView::Help) {
      auto helpArea = overlayRect.reduced(40);
      auto topRow = helpArea.removeFromTop(30);

      cmbTheme.setBounds(topRow.removeFromLeft(150).reduced(2));
      cmbControlProfile.setBounds(topRow.removeFromLeft(150).reduced(2));
      btnLoadProfile.setBounds(topRow.removeFromLeft(60).reduced(2));
      btnSaveProfile.setBounds(topRow.removeFromLeft(60).reduced(2));
      btnDeleteProfile.setBounds(topRow.removeFromLeft(30).reduced(2));

      auto midRow = helpArea.removeFromTop(30).translated(0, 10);
      cmbClockMode.setBounds(midRow.removeFromLeft(120).reduced(2));
      sliderClockOffset.setBounds(midRow.removeFromLeft(100).reduced(2));
      lblClockOffset.setBounds(sliderClockOffset.getBounds()
                                   .withY(midRow.getY() - 15)
                                   .withHeight(15));

      auto botRow = helpArea.removeFromTop(30).translated(0, 20);
      btnGPU.setBounds(botRow.removeFromLeft(60).reduced(2));
      btnMidiScalingToggle.setBounds(botRow.removeFromLeft(100).reduced(2));
      btnResetMixerOnLoad.setBounds(botRow.removeFromLeft(160).reduced(2));

      btnResetLearned.setVisible(true);
      btnResetLearned.setBounds(helpArea.getCentreX() - 60,
                                helpArea.getBottom() - 40, 120, 30);
    } else {
      btnResetLearned.setVisible(false);
    }
    return;
  }
  btnResetLearned.setVisible(false);

  auto mainContent = area; // Remaining space below header

  // ==============================================================================
  // 2. DASHBOARD LAYOUT
  // ==============================================================================
  if (isSimpleMode) {
    // --- TOP ROW: MIDI CONFIG ---
    auto configRow = mainContent.removeFromTop(65).reduced(2);
    grpIo.setBounds(configRow);
    auto rIo = grpIo.getBounds().reduced(10, 22);
    lblIn.setBounds(rIo.removeFromLeft(20));
    cmbMidiIn.setBounds(rIo.removeFromLeft(90));
    rIo.removeFromLeft(10);
    lblOut.setBounds(rIo.removeFromLeft(20));
    cmbMidiOut.setBounds(rIo.removeFromLeft(90));
    rIo.removeFromLeft(10);
    lblCh.setBounds(rIo.removeFromLeft(20));
    cmbMidiCh.setBounds(rIo.removeFromLeft(45));

    // --- TRANSPORT ROW ---
    auto transRow = mainContent.removeFromTop(40).reduced(2);
    btnPlay.setBounds(transRow.removeFromLeft(50).reduced(1));
    btnStop.setBounds(transRow.removeFromLeft(50).reduced(1));
    transRow.removeFromLeft(10);
    tempoSlider.setBounds(transRow.removeFromLeft(140).reduced(0, 5));
    btnResetBPM.setBounds(transRow.removeFromLeft(65).reduced(1));
    transRow.removeFromLeft(15);
    phaseVisualizer.setBounds(transRow.removeFromLeft(75).reduced(1, 5));
    btnLinkToggle.setBounds(transRow.removeFromLeft(50).reduced(1));

    // --- BOTTOM ROW: SIMPLE MIXER ---
    auto bottomRow = mainContent.removeFromBottom(90).reduced(5);
    auto v1Area = bottomRow.removeFromLeft(bottomRow.getWidth() / 2).reduced(5);
    auto v2Area = bottomRow.reduced(5);

    vol1Simple.setBounds(v1Area.removeFromLeft(30));
    txtVol1Osc.setBounds(v1Area.removeFromTop(20));
    btnVol1CC.setBounds(v1Area.removeFromTop(20).withWidth(60));

    vol2Simple.setBounds(v2Area.removeFromLeft(30));
    txtVol2Osc.setBounds(v2Area.removeFromTop(20));
    btnVol2CC.setBounds(v2Area.removeFromTop(20).withWidth(60));

    // --- SIDEBAR (RIGHT) ---
    auto rightSidebar = mainContent.removeFromRight(150).reduced(2);
    logPanel.setBounds(
        rightSidebar.removeFromTop(rightSidebar.getHeight() / 2).reduced(2));
    playlist.setBounds(rightSidebar.reduced(2));

    // --- KEYBOARD (LEFT) ---
    auto kbArea = mainContent.removeFromLeft(70).reduced(2);
    sliderPitchV.setBounds(kbArea.removeFromLeft(18).reduced(0, 5));
    sliderModV.setBounds(kbArea.removeFromLeft(18).reduced(0, 5));
    verticalKeyboard.setBounds(kbArea);

    // --- MAIN CENTER: TRACK GRID ---
    trackGrid.setBounds(mainContent.reduced(2));
  } else {
    // --- CONFIG ROW (Network + MIDI) --- (Image 3 Fix)
    auto configRow = mainContent.removeFromTop(80).reduced(5);

    // Network Group (Left - Much Wider)
    grpNet.setBounds(configRow.removeFromLeft(420));
    auto rNet = grpNet.getBounds().reduced(10, 20);
    edIp.setBounds(rNet.removeFromLeft(90).reduced(0, 2));
    lblPOut.setBounds(rNet.removeFromLeft(30));
    edPOut.setBounds(rNet.removeFromLeft(45).reduced(0, 2));
    lblPIn.setBounds(rNet.removeFromLeft(30));
    edPIn.setBounds(rNet.removeFromLeft(45).reduced(0, 2));
    rNet.removeFromLeft(5);
    btnConnect.setBounds(rNet.removeFromRight(80).reduced(0, 2));

    // MIDI Group (Right)
    grpIo.setBounds(configRow.reduced(2, 0));
    auto rIo = grpIo.getBounds().reduced(10, 22);
    lblIn.setBounds(rIo.removeFromLeft(20));
    cmbMidiIn.setBounds(rIo.removeFromLeft(90));
    lblCh.setBounds(rIo.removeFromLeft(25));
    cmbMidiCh.setBounds(rIo.removeFromLeft(48));
    lblOut.setBounds(rIo.removeFromLeft(25));
    cmbMidiOut.setBounds(rIo.removeFromLeft(90));
    btnBlockMidiOut.setBounds(rIo.removeFromRight(55).reduced(0, 2));

    // --- TRANSPORT ROW ---
    // Pinned BELOW Config
    auto transRow = mainContent.removeFromTop(45).reduced(2);

    btnPlay.setBounds(transRow.removeFromLeft(50).reduced(1));
    btnStop.setBounds(transRow.removeFromLeft(50).reduced(1));
    btnPrev.setBounds(transRow.removeFromLeft(35).reduced(1));
    btnSkip.setBounds(transRow.removeFromLeft(35).reduced(1));
    btnClearPR.setBounds(transRow.removeFromLeft(60).reduced(1));

    transRow.removeFromLeft(10);
    tempoSlider.setBounds(transRow.removeFromLeft(140).reduced(0, 8));
    btnResetBPM.setBounds(transRow.removeFromLeft(65).reduced(1));

    transRow.removeFromLeft(15);
    phaseVisualizer.setVisible(true);
    phaseVisualizer.setBounds(transRow.removeFromLeft(80).reduced(2, 5));
    btnLinkToggle.setBounds(transRow.removeFromLeft(50).reduced(1));

    btnPrOctUp.setBounds(transRow.removeFromRight(40).reduced(1));
    btnPrOctDown.setBounds(transRow.removeFromRight(40).reduced(1));

    // --- MAIN CONTENT SPLIT ---
    // Footer (Mixer) at bottom
    auto footerArea = mainContent.removeFromBottom(145);

    // Right Column (Log, Playlist, Arp)
    auto rightColumn = mainContent.removeFromRight(260).reduced(2);

    logPanel.setBounds(
        rightColumn.removeFromTop(175).reduced(2)); // Tighter Log
    playlist.setBounds(
        rightColumn.removeFromTop(195).reduced(2)); // Playlist Mid
    grpArp.setBounds(rightColumn.reduced(2));       // Arp Gen Bottom

    auto ar = grpArp.getBounds().reduced(10, 22);
    auto c1 = ar.removeFromLeft(80); // Left Column: Latch/Sync/Block
    btnArpLatch.setBounds(c1.removeFromTop(20).reduced(1));
    btnArpSync.setBounds(c1.removeFromTop(20).reduced(1));
    btnArpBlock.setBounds(c1.removeFromTop(20).reduced(1));

    auto c2 = ar.removeFromLeft(45).translated(5, 0); // Knobs
    sliderArpSpeed.setBounds(c2.removeFromTop(45));
    lblArpSpdLabel.setBounds(c2.removeFromTop(15));

    auto c3 = ar.removeFromLeft(45).translated(5, 0);
    sliderArpVel.setBounds(c3.removeFromTop(45));
    lblArpVelLabel.setBounds(c3.removeFromTop(15));

    cmbArpPattern.setBounds(
        ar.removeFromRight(70).withSize(70, 25).withY(ar.getY() + 10));

    // --- CENTER COLUMN (Sequencer & Grid) ---
    auto seqArea = mainContent.removeFromBottom(160).reduced(2);
    sequencer.setBounds(seqArea);

    // Sequencer Internal Footer logic - USING LOCAL BOUNDS FOR CORRECT
    // PARENT-RELATIVE LAYOUT
    auto rSeq = sequencer.getLocalBounds();
    auto seqFoot = rSeq.removeFromBottom(30).reduced(2);
    sequencer.btnPage.setBounds(seqFoot.removeFromLeft(35).reduced(1));
    sequencer.swingSlider.setBounds(seqFoot.removeFromLeft(80).reduced(2));
    sequencer.btnCountIn.setBounds(seqFoot.removeFromLeft(50).reduced(1));
    sequencer.btnRec.setBounds(seqFoot.removeFromLeft(45).reduced(1));
    sequencer.btnExport.setBounds(seqFoot.removeFromLeft(55).reduced(1));
    sequencer.btnForceGrid.setBounds(seqFoot.removeFromLeft(55).reduced(1));
    sequencer.btnClearAll.setBounds(seqFoot.removeFromLeft(65).reduced(1));
    sequencer.btnResetCH.setBounds(seqFoot.removeFromRight(70).reduced(1));

    auto kbArea = mainContent.removeFromBottom(45).reduced(2);
    sliderPitchH.setBounds(kbArea.removeFromLeft(22).reduced(2));
    sliderModH.setBounds(kbArea.removeFromLeft(22).reduced(2));
    horizontalKeyboard.setBounds(kbArea);

    // Timeline fills the rest
    trackGrid.setBounds(mainContent.reduced(2));

    // --- FOOTER CONTENT ---
    mixerViewport.setBounds(
        footerArea.removeFromLeft(footerArea.getWidth() - 260));
    mixer.setSize(16 * mixer.stripWidth, mixerViewport.getHeight() - 10);

    auto actionArea = footerArea.reduced(5);
    btnTapTempo.setBounds(actionArea.removeFromBottom(22).reduced(10, 0));
    nudgeSlider.setBounds(actionArea.removeFromBottom(25).reduced(2));

    auto profTop = actionArea.removeFromTop(25);
    cmbQuantum.setBounds(profTop.removeFromLeft(80));
    btnPreventBpmOverride.setBounds(profTop);

    auto profBot = actionArea.removeFromTop(28).translated(0, 4);
    cmbControlProfile.setBounds(profBot.removeFromLeft(125));
    btnDeleteProfile.setBounds(profBot.removeFromLeft(25));
    btnSaveProfile.setBounds(profBot);
  }
}

void MainComponent::toggleExtSync() {
  bool active = btnExtSync.getToggleState();
  logPanel.log("! Ext Hardware Clock Sync: " +
                   juce::String(active ? "Enabled" : "Disabled"),
               true);
}

void MainComponent::processMidiQueue(int start, int size) {
  for (int i = 0; i < size; ++i) {
    auto &m = mainMidiBuffer[start + i];

    // Keyboard Visuals
    if (m.isNoteOnOrOff()) {
      isHandlingOsc = true;
      keyboardState.processNextMidiEvent(m);
      isHandlingOsc = false;
    } else {
      // 2. Hardware MIDI Thru & Split Logic (For CCs, Pitch, etc.)
      // Notes are handled by keyboardState listener (handleNoteOn), so we skip
      // them here
      if (midiOutput && btnMidiThru.getToggleState() &&
          !btnBlockMidiOut.getToggleState()) {

        // If Split is active on Ch 1, duplicate CCs to Ch 2
        if (btnSplit.getToggleState() && m.getChannel() == 1) {
          midiOutput->sendMessageNow(m); // Send to Ch 1

          auto m2 = m;
          m2.setChannel(2);
          midiOutput->sendMessageNow(m2); // Send to Ch 2
        } else {
          midiOutput->sendMessageNow(m); // Normal Thru
        }
      }
    }

    // 3. OSC Forwarding (Handles its own split logic internally)
    if (!isHandlingOsc)
      sendSplitOscMessage(m);

    // 4. Transport Profile Linking
    if (currentProfile.isTransportLink && m.isController()) {
      if (m.getControllerNumber() == currentProfile.ccPlay &&
          m.getControllerValue() >= 64)
        startPlayback();
      if (m.getControllerNumber() == currentProfile.ccStop &&
          m.getControllerValue() >= 64)
        stopPlayback();
    }
  }
}

void MainComponent::loadMidiFile(juce::File f) {
  mixer.removeAllStrips();
  if (f.isDirectory()) {
    auto files = f.findChildFiles(juce::File::findFiles, false, "*.mid");
    for (auto &file : files)
      playlist.addFile(file.getFullPathName());
    logPanel.log("! Loaded Folder: " + f.getFileName(), true);
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
    short timeFormat = mf.getTimeFormat();

    if (timeFormat < 0) {
      ticksPerQuarterNote = 960.0; // Default to standard PPQ
      logPanel.log("! SMPTE MIDI Detected: Converting to 960 PPQ", true);
    } else {
      ticksPerQuarterNote = (double)timeFormat;
    }

    playbackSeq.clear();
    sequencer.activeTracks.clear();
    bool bpmFound = false;
    for (int i = 0; i < mf.getNumTracks(); ++i) {
      playbackSeq.addSequence(*mf.getTrack(i), 0);

      juce::String trackName = "Track " + juce::String(i + 1);

      for (auto *ev : *mf.getTrack(i)) {
        if (ev->message.isMetaEvent() && ev->message.getMetaEventType() == 3) {
          trackName = ev->message.getTextFromTextMetaEvent();
          if (trackName.isNotEmpty())
            break;
        }
      }

      if (i < 16)
        mixer.strips[i]->setTrackName(trackName);
      for (auto *ev : *mf.getTrack(i))
        if (ev->message.isTempoMetaEvent() && !bpmFound) {
          currentFileBpm = 60.0 / ev->message.getTempoSecondsPerQuarterNote();
          if (!btnPreventBpmOverride.getToggleState()) {
            engine.setBpm(currentFileBpm);
            parameters.setProperty("bpm", currentFileBpm, nullptr);
            double val = currentFileBpm;
            juce::MessageManager::callAsync([this, val] {
              tempoSlider.setValue(val, juce::dontSendNotification);
            });
          }
          bpmFound = true;
        }
    }
    playbackSeq.updateMatchedPairs();
    sequenceLength = playbackSeq.getEndTime();

    // PUSH TO ENGINE
    engine.setMidiSequence(playbackSeq, ticksPerQuarterNote);

    lastProcessedBeat = -1.0;
    trackGrid.loadSequence(playbackSeq);
    trackGrid.setTicksPerQuarter(ticksPerQuarterNote);

    if (mixer.isResetOnLoad) {
      mixer.resetMapping();
    }

    logPanel.log("! Loaded File: " + f.getFileName(), true);
    playlist.addFile(f.getFullPathName());
    grabKeyboardFocus();
  }
}

void MainComponent::sendPanic() {
  logPanel.log("! PANIC", true);
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
  // Clear engine scheduler too
  // engine.clearScheduler(); // If we add it, but for now panic just stops
  // stuff
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
void MainComponent::takeSnapshot() {}
void MainComponent::performUndo() { undoManager.undo(); }
void MainComponent::performRedo() { undoManager.redo(); }
void MainComponent::paint(juce::Graphics &g) {
  // Create a stylish background gradient from top to bottom
  juce::ColourGradient bgGrad(Theme::bgDark.brighter(0.03f), 0.0f, 0.0f,
                              Theme::bgDark.darker(0.1f), 0.0f,
                              (float)getHeight(), false);
  g.setGradientFill(bgGrad);
  g.fillAll();

  // Add a subtle "shimmer" or highlight to the very top header area
  g.setColour(juce::Colours::white.withAlpha(0.02f));
  g.fillRect(0, 0, getWidth(), 35);

  // Apply stylish panels to the main group sections if not in simple mode
  if (!isSimpleMode && currentView == AppView::Dashboard) {
    Theme::drawStylishPanel(g, grpNet.getBounds().toFloat(), Theme::bgPanel,
                            8.0f);
    Theme::drawStylishPanel(g, grpIo.getBounds().toFloat(), Theme::bgPanel,
                            8.0f);
    Theme::drawStylishPanel(g, grpArp.getBounds().toFloat(), Theme::bgPanel,
                            8.0f);

    // Also the sidebar areas
    Theme::drawStylishPanel(g, logPanel.getBounds().toFloat(), Theme::bgPanel,
                            6.0f);
    Theme::drawStylishPanel(g, playlist.getBounds().toFloat(), Theme::bgPanel,
                            6.0f);
  }
}
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

// NEW: Slider Drag Started for MIDI Learn
void MainComponent::sliderDragStarted(juce::Slider *s) {
  if (isMidiLearnMode) {
    juce::String paramID = s->getProperties()["paramID"].toString();
    if (paramID.isNotEmpty()) {
      mappingManager.setSelectedParameterForLearning(paramID);
      logPanel.log("! Learn Target: " + paramID, true);
    }
  }
}

void MainComponent::sliderDragEnded(juce::Slider *s) {
  if (s == &nudgeSlider) {
    s->setValue(0.0);
    logPanel.log("! Nudge End", false);
    if (auto *link = engine.getLink()) {
      auto state = link->captureAppSessionState();
      state.setTempo(baseBpm, link->clock().micros());
      link->commitAppSessionState(state);
    }
  }
}

void MainComponent::sliderValueChanged(juce::Slider *s) {}
bool MainComponent::isInterestedInFileDrag(const juce::StringArray &files) {
  for (auto &f : files)
    if (f.endsWithIgnoreCase(".mid") || juce::File(f).isDirectory())
      return true;
  return false;
}

void MainComponent::setParameterValue(juce::String paramID, float normValue) {
  juce::MessageManager::callAsync([this, paramID, normValue] {
    if (paramID == "bpm")
      tempoSlider.setValue(juce::jmap(normValue, 20.0f, 444.0f),
                           juce::sendNotification);
    else if (paramID == "vol1")
      vol1Simple.setValue(normValue * 127.0f, juce::sendNotification);
    else if (paramID == "vol2")
      vol2Simple.setValue(normValue * 127.0f, juce::sendNotification);
    else if (paramID == "arp_speed")
      sliderArpSpeed.setValue(normValue * 127.0f, juce::sendNotification);
    else if (paramID == "arp_vel")
      sliderArpVel.setValue(normValue * 127.0f, juce::sendNotification);
    else if (paramID == "arp_on")
      btnArp.setToggleState(normValue > 0.5f, juce::sendNotification);

    else if (paramID == "arp_latch")
      btnArpLatch.setToggleState(normValue > 0.5f, juce::sendNotification);
    else if (paramID == "arp_sync")
      btnArpSync.setToggleState(normValue > 0.5f, juce::sendNotification);

    if (paramID.startsWith("Mixer_")) {
      int ch = paramID.fromFirstOccurrenceOf("_", false, false).getIntValue();
      if (paramID.endsWith("_Vol"))
        mixer.setChannelVolume(ch, normValue * 127.0f);
      else if (paramID.endsWith("_On")) {
        // Toggle the active state of the strip
        for (auto *s : mixer.strips)
          if (s->channelIndex == ch - 1)
            s->btnActive.setToggleState(normValue > 0.5f,
                                        juce::sendNotification);
      }
    }
  });
}
void MainComponent::filesDropped(const juce::StringArray &files, int, int) {
  bool firstLoaded = false;
  for (auto &f : files) {
    juce::File file(f);

    if (file.hasFileExtension(".json") || file.hasFileExtension(".midimap")) {
      // Copy to Profile Directory
      auto targetDir = getProfileDirectory();
      auto targetFile = targetDir.getChildFile(file.getFileName());

      if (file.copyFileTo(targetFile)) {
        updateProfileComboBox();
        loadCustomProfile(targetFile);
        // FIX: Ensure targetFile is captured by value
        juce::MessageManager::callAsync([this, targetFile] {
          logPanel.log("! Imported Profile: " +
                           targetFile.getFileNameWithoutExtension(),
                       true);
        });
      }
      return;
    }

    if (!file.isDirectory() &&
        (file.hasFileExtension(".mid") || file.hasFileExtension(".midi"))) {
      playlist.addFile(file.getFullPathName());
      if (!firstLoaded) {
        loadMidiFile(file);
        firstLoaded = true;
      }
    }
  }
}

juce::File MainComponent::getProfileDirectory() {
  auto dir =
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("PatchworldBridge")
          .getChildFile("Profiles");

  if (!dir.exists())
    dir.createDirectory();

  return dir;
}

void MainComponent::saveFullProfile(juce::File file) {
  auto *root = new juce::DynamicObject();

  // 1. Save MIDI mappings (MIDI Learn)
  mappingManager.saveMappingsToJSON(root);

  // 2. Save Mixer names
  juce::Array<juce::var> mixerNames;
  for (int i = 0; i < 16; ++i) {
    mixerNames.add(mixer.strips[i]->nameLabel.getText());
  }
  root->setProperty("mixer_names", mixerNames);

  // 3. Save Last Received CCs
  juce::Array<juce::var> ccs;
  for (int i = 1; i <= 16; ++i)
    ccs.add(lastReceivedCC[i]);
  root->setProperty("last_received_ccs", ccs);

  // 4. GUI & Logic State
  root->setProperty("osc_scaling_127", isMidiScaling127);
  root->setProperty("midi_thru", btnMidiThru.getToggleState());
  root->setProperty("arp_speed", sliderArpSpeed.getValue());
  root->setProperty("arp_vel", sliderArpVel.getValue());
  root->setProperty("arp_pattern", cmbArpPattern.getSelectedId());
  root->setProperty("arp_latch", btnArpLatch.getToggleState());
  root->setProperty("prevent_bpm_override",
                    btnPreventBpmOverride.getToggleState());

  juce::String jsonStr = juce::JSON::toString(juce::var(root));
  file.replaceWithText(jsonStr);
  logPanel.log("! Profile Saved: " + file.getFileName(), true);
}

void MainComponent::loadFullProfile(juce::File file) {
  juce::var data = juce::JSON::parse(file);
  if (auto *obj = data.getDynamicObject()) {
    // 1. Load Mappings
    if (obj->hasProperty("mappings"))
      mappingManager.loadMappingsFromJSON(obj->getProperty("mappings"));

    // 2. Load Mixer names
    if (obj->hasProperty("mixer_names")) {
      auto *names = obj->getProperty("mixer_names").getArray();
      if (names) {
        for (int i = 0; i < std::min(16, names->size()); ++i) {
          mixer.strips[i]->setTrackName((*names)[i].toString());
        }
      }
    }

    // 3. Load State
    if (obj->hasProperty("last_received_ccs")) {
      auto *ccs = obj->getProperty("last_received_ccs").getArray();
      if (ccs) {
        for (int i = 0; i < std::min(16, ccs->size()); ++i)
          lastReceivedCC[i + 1] = (int)(*ccs)[i];
      }
    }

    if (obj->hasProperty("osc_scaling_127")) {
      isMidiScaling127 = (bool)obj->getProperty("osc_scaling_127");
      btnMidiScalingToggle.setToggleState(isMidiScaling127,
                                          juce::dontSendNotification);
    }
    if (obj->hasProperty("midi_thru"))
      btnMidiThru.setToggleState((bool)obj->getProperty("midi_thru"),
                                 juce::sendNotification);
    if (obj->hasProperty("arp_speed"))
      sliderArpSpeed.setValue((double)obj->getProperty("arp_speed"),
                              juce::sendNotification);
    if (obj->hasProperty("arp_vel"))
      sliderArpVel.setValue((double)obj->getProperty("arp_vel"),
                            juce::sendNotification);
    if (obj->hasProperty("arp_pattern"))
      cmbArpPattern.setSelectedId((int)obj->getProperty("arp_pattern"),
                                  juce::sendNotification);
    if (obj->hasProperty("arp_latch"))
      btnArpLatch.setToggleState((bool)obj->getProperty("arp_latch"),
                                 juce::sendNotification);
    if (obj->hasProperty("prevent_bpm_override"))
      btnPreventBpmOverride.setToggleState(
          (bool)obj->getProperty("prevent_bpm_override"),
          juce::sendNotification);
  }
  logPanel.log("! Profile Loaded: " + file.getFileName(), true);
}
void MainComponent::loadCustomProfile(juce::File f) {
  auto p = ControlProfile::fromFile(f);
  applyControlProfile(p);

  // Select it in definitions if possible, or just update logic
  // We need to match the ComboBox selection to this file if we want UI
  // consistency But triggering apply directly is fine for "Drop to Load"
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

void MainComponent::pausePlayback() {
  engine.stop();

  if (midiOutput) {
    midiOutput->sendMessageNow(
        juce::MidiMessage::allNotesOff(getSelectedChannel()));
    if (btnMidiClock.getToggleState())
      midiOutput->sendMessageNow(juce::MidiMessage(0xFC)); // Stop/Pause
  }
  if (isOscConnected)
    oscSender.send(oscConfig.eStop.getText(), 1.0f);

  logPanel.log("! Playback Paused", true);
  juce::MessageManager::callAsync([this] { btnPlay.setButtonText("Play"); });
}

void MainComponent::stopPlayback() {
  engine.stop();

  // All Notes Off
  if (midiOutput) {
    midiOutput->sendMessageNow(
        juce::MidiMessage::allNotesOff(getSelectedChannel()));
    if (btnMidiClock.getToggleState())
      midiOutput->sendMessageNow(juce::MidiMessage(0xFC)); // Stop
  }

  if (isOscConnected)
    oscSender.send(oscConfig.eStop.getText(), 1.0f);

  logPanel.log("! Playback Stopped", true);
  juce::MessageManager::callAsync([this] { btnPlay.setButtonText("Play"); });
}

void MainComponent::applyControlProfile(const ControlProfile &p) {
  currentProfile = p;
  logPanel.log("Loaded Profile. Cutoff CC: " + juce::String(p.ccCutoff), true);
  // Here we would propagate changes to components if they held their own
  // state. Currently, CCs might be generated dynamically. We need to ensure
  // that when we SEND CCs, we use 'currentProfile'.
}

void MainComponent::startPlayback() {
  engine.start();
  logPanel.log("! Playback Started", true);
  juce::MessageManager::callAsync([this] { btnPlay.setButtonText("Pause"); });
}

void MainComponent::clearPlaybackSequence() {
  playbackSeq.clear();
  trackGrid.loadSequence(playbackSeq);
  logPanel.log("! Sequence Cleared", true);
}

//==============================================================================
// PROFILE MANAGEMENT
//==============================================================================

void MainComponent::updateProfileComboBox() {
  cmbControlProfile.clear();

  // 1. Re-add Factory Defaults (IDs 1-99)
  cmbControlProfile.addItem("Default Mapping", 1);
  cmbControlProfile.addItem("Roland JD-Xi", 2);
  cmbControlProfile.addItem("Generic Keyboard", 3);
  cmbControlProfile.addItem("FL Studio", 4);
  cmbControlProfile.addItem("Ableton Live", 5);

  cmbControlProfile.addSeparator();

  // 2. Scan for User Profiles (IDs 1000+)
  auto dir = getProfileDirectory();
  auto files = dir.findChildFiles(juce::File::findFiles, false, "*.json");

  int customId = 1000;
  for (auto &f : files) {
    cmbControlProfile.addItem(f.getFileNameWithoutExtension(), customId++);
  }
}

void MainComponent::saveNewControllerProfile() {
  // Ableton-style prompt for profile name
  auto alert = std::make_shared<juce::AlertWindow>(
      "Save MIDI Mapping", "Enter a name for this controller profile:",
      juce::MessageBoxIconType::NoIcon);

  alert->addTextEditor("name", "My Controller", "Profile Name:");
  alert->addButton("Save", 1);
  alert->addButton("Cancel", 0);

  alert->enterModalState(
      true, juce::ModalCallbackFunction::create([this, alert](int result) {
        if (result == 1) {
          juce::String name = alert->getTextEditorContents("name");
          if (name.isEmpty())
            name = "New_Profile";

          juce::File file = getProfileDirectory().getChildFile(name + ".json");
          saveFullProfile(file);
          updateProfileComboBox(); // Refresh the list
          cmbControlProfile.setText(name, juce::dontSendNotification);
        }
      }));
}

void MainComponent::deleteSelectedProfile() {
  int id = cmbControlProfile.getSelectedId();

  // Ensure we only delete custom profiles (IDs 1000+)
  if (id >= 1000) {
    juce::String profileName = cmbControlProfile.getText();
    juce::File file = getProfileDirectory().getChildFile(profileName + ".json");

    if (file.existsAsFile()) {
      juce::AlertWindow::showOkCancelBox(
          juce::MessageBoxIconType::WarningIcon, "Delete Profile?",
          "Are you sure you want to permanently delete '" + profileName + "'?",
          "Delete", "Cancel", nullptr,
          juce::ModalCallbackFunction::create([this, file](int result) {
            if (result == 1) { // User clicked Delete
              if (file.deleteFile()) {
                logPanel.log("! Deleted Profile: " +
                                 file.getFileNameWithoutExtension(),
                             true);
                updateProfileComboBox();
                cmbControlProfile.setSelectedId(
                    1,
                    juce::sendNotification); // Reset
              }
            }
          }));
    }
  } else {
    logPanel.log("Cannot delete factory default profiles.", false);
  }
}

// ==============================================================================
// V5.2 Sync Logic Helper Methods
// ==============================================================================

void MainComponent::handleSequenceEnd(double masterBeat) {
  if (playlist.playMode == MidiPlaylist::LoopOne) {
    engine.resetTransportForLoop();
  } else if (playlist.playMode == MidiPlaylist::LoopAll) {
    stopPlayback();
    juce::MessageManager::callAsync([this] { btnSkip.onClick(); });
  } else {
    stopPlayback();
    logPanel.log("! Playback Finished", true);
  }
}
// ==============================================================================
// MISSING DEFINITIONS (Append to end of MainComponent.cpp)
// ==============================================================================

void MainComponent::handleIncomingMidiMessage(juce::MidiInput *source,
                                              const juce::MidiMessage &m) {
  // 1. Pass to Mapping Manager
  mappingManager.handleIncomingMidiMessage(source, m);
  midiInLight.activate();

  // 2. Process Clock
  if (m.isMidiClock()) {
    handleMidiClock(m);
  }

  // 3. Push to FIFO for UI thread
  bool isActiveSense = (m.getRawDataSize() == 1 && m.getRawData()[0] == 0xFE);
  if (!m.isMidiClock() && !isActiveSense) {
    int start1, size1, start2, size2;
    mainMidiFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 > 0)
      mainMidiBuffer[start1] = m;
    else if (size2 > 0)
      mainMidiBuffer[start2] = m;
    mainMidiFifo.finishedWrite(1);
    triggerAsyncUpdate();
  }
}

void MainComponent::handleAsyncUpdate() {
  bool learning = mappingManager.isLearnModeActive();
  isMidiLearnMode = learning;

  if (learning) {
    midiLearnOverlay.repaint();
  } else {
    int start1, size1, start2, size2;
    mainMidiFifo.prepareToRead(mainMidiFifo.getNumReady(), start1, size1,
                               start2, size2);
    if (size1 > 0)
      processMidiQueue(start1, size1);
    if (size2 > 0)
      processMidiQueue(start2, size2);
    mainMidiFifo.finishedRead(size1 + size2);
  }
}
void MainComponent::handleMidiClock(const juce::MidiMessage &m) {
  // 1. Thru Logic
  if (midiOutput && btnMidiThru.getToggleState() &&
      !btnBlockMidiOut.getToggleState()) {
    midiOutput->sendMessageNow(m);
  }

  // 2. Sync Logic (Only if Ext button is ON)
  if (!btnExtSync.getToggleState())
    return;

  double nowMs = juce::Time::getMillisecondCounterHiRes();
  clockInTimes.push_back(nowMs);
  clockPulseCounter++;

  if (clockPulseCounter >= 24) { // Every Quarter Note (24 pulses per beat)
    if (clockInTimes.size() >= 24) {
      double first = clockInTimes.front();
      double last = clockInTimes.back();
      double duration = last - first;

      if (duration > 50.0) {
        double bpm = 60000.0 / duration;
        bpm = juce::jlimit(20.0, 444.0, bpm);
        midiInBpm = bpm;

        // Apply to Link if enabled
        if (auto *link = engine.getLink()) {
          if (link->isEnabled()) {
            auto state = link->captureAppSessionState();
            state.setTempo(bpm, link->clock().micros());
            link->commitAppSessionState(state);
          }
        }

        // Update Slider (Thin out updates to avoid lag)
        if (!isPlaying && !pendingSyncStart) {
          juce::MessageManager::callAsync([this, bpm] {
            tempoSlider.setValue(bpm, juce::dontSendNotification);
          });
        }
      }
    }
    clockInTimes.clear();
    clockPulseCounter = 0;
  }
}
//==============================================================================
// MIDI EXPORT FUNCTIONS
//==============================================================================

void MainComponent::launchMidiExport() {
  // 1. Create a file chooser for saving .mid files
  fileChooser = std::make_unique<juce::FileChooser>(
      "Export Sequencer Pattern...",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
      "*.mid");

  auto flags = juce::FileBrowserComponent::saveMode |
               juce::FileBrowserComponent::canSelectFiles;

  // 2. Launch async
  fileChooser->launchAsync(flags, [this](const juce::FileChooser &fc) {
    auto result = fc.getResult();
    if (result != juce::File()) {
      // Enforce .mid extension
      if (!result.hasFileExtension(".mid"))
        result = result.withFileExtension(".mid");

      // 3. Perform the export
      exportSequencerToMidi(result);
    }
  });
}

void MainComponent::exportSequencerToMidi(juce::File targetFile) {
  juce::MidiMessageSequence sequence;

  // 1. Get Target Channel
  int targetMidiChannel = sequencer.cmbSeqOutCh.getSelectedId();
  if (targetMidiChannel == 0)
    targetMidiChannel = 1;

  // 2. Setup Timing (960 PPQN is standard)
  const int ticksPerQuarterNote = 960;
  const int ticksPerStep = ticksPerQuarterNote / 4; // 1/16th notes

  // 3. Iterate Sequencer Steps
  for (int i = 0; i < sequencer.numSteps; ++i) {
    if (sequencer.isStepActive(i)) {
      double startTick = i * ticksPerStep;
      double endTick = startTick + (ticksPerStep * 0.9); // 90% Gate length

      // Use root note from slider
      int note = (int)sequencer.noteSlider.getValue();

      // Create Note On/Off pair
      auto noteOn = juce::MidiMessage::noteOn(targetMidiChannel, note, 0.8f);
      auto noteOff = juce::MidiMessage::noteOff(targetMidiChannel, note);

      sequence.addEvent(noteOn, startTick);
      sequence.addEvent(noteOff, endTick);
    }
  }

  // 4. Create MIDI File Structure
  juce::MidiFile midiFile;
  midiFile.setTicksPerQuarterNote(ticksPerQuarterNote);
  midiFile.addTrack(sequence);

  // 5. Write to Disk
  if (auto outStream = targetFile.createOutputStream()) {
    midiFile.writeTo(*outStream);
    logPanel.log("! MIDI Exported: " + targetFile.getFileName(), true);

    // Auto-load into playlist for verification
    playlist.addFile(targetFile.getFullPathName());
  }
}
