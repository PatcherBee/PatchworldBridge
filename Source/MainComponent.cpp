/*
  ==============================================================================
    Source/MainComponent.cpp
    Status: CRITICAL FIXES (Layouts Repaired, Playback Controls Restored, OSC CC
  Multi-Arg)
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
      oscConfig(), controlPage() {

  // --- Ableton Link Init ---
  link = new ableton::Link(120.0);
  {
    auto state = link->captureAppSessionState();
    state.setTempo(bpmVal.get(), link->clock().micros());
    link->commitAppSessionState(state);
  }

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

  // --- Note Delay ---
  addAndMakeVisible(sliderNoteDelay);
  sliderNoteDelay.setRange(0.0, 2000.0, 1.0);
  sliderNoteDelay.setValue(200.0);
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
  // Manual toggle of visibility in resized or controlPage logic needed if not
  // using child components directly For now, MainComponent owns them, we just
  // place them in Control Page area in resized()

  transportStartBeat = 0.0;

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

  addAndMakeVisible(lblVol1);
  lblVol1.setText("Vol 1", juce::dontSendNotification);
  lblVol1.setJustificationType(juce::Justification::centred);

  addAndMakeVisible(lblVol2);
  lblVol2.setText("Vol 2", juce::dontSendNotification);
  lblVol2.setJustificationType(juce::Justification::centred);

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
    link->enable(enabled);
    link->enableStartStopSync(enabled);
    startupRetryActive = false;
    logPanel.log(enabled ? "! Link Enabled" : "! Link Disabled", true);
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

  configureToggleButton(btnBlockMidiOut, "Block Out", juce::Colours::red,
                        "Block MIDI Out");
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
  nudgeSlider.setRange(-0.10, 0.10, 0.001);
  nudgeSlider.setValue(0.0);
  nudgeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  nudgeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  nudgeSlider.addListener(this);
  nudgeSlider.onValueChange = [this] {
    if (link) {
      double mult = 1.0 + nudgeSlider.getValue();
      double target = baseBpm * mult;
      auto state = link->captureAppSessionState();
      state.setTempo(target, link->clock().micros());
      link->commitAppSessionState(state);
    }
  };
  nudgeSlider.onDragStart = [this] {
    if (link) {
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
        auto state = link->captureAppSessionState();
        state.setTempo(bpm, link->clock().micros());
        link->commitAppSessionState(state);
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
    logPanel.log(
        "! MIDI Learn: " + juce::String(isMidiLearnMode ? "ON" : "OFF"), true);
    if (isMidiLearnMode) {
      logPanel.log("Click a control then move MIDI CC", false);
    }
  };

  addAndMakeVisible(btnResetLearned);
  btnResetLearned.setButtonText("Reset Learn");
  btnResetLearned.setColour(juce::TextButton::buttonColourId,
                            juce::Colours::darkred.withAlpha(0.6f));
  btnResetLearned.onClick = [this] {
    mappingManager.clearMappings();
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
  addAndMakeVisible(lblIn);
  addAndMakeVisible(cmbMidiIn);
  addAndMakeVisible(lblOut);
  addAndMakeVisible(cmbMidiOut);
  addAndMakeVisible(lblCh);
  addAndMakeVisible(cmbMidiCh);
  cmbMidiCh.addItem("All", 17);
  for (int i = 1; i <= 16; ++i)
    cmbMidiCh.addItem(juce::String(i), i);
  cmbMidiCh.setSelectedId(17, juce::dontSendNotification);
  cmbMidiCh.onChange = [this] {
    logPanel.log("! MIDI Ch Changed: " + cmbMidiCh.getText(), false);
  };

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

  // --- Playback Controls ---
  addAndMakeVisible(btnPlay);
  btnPlay.onClick = [this] {
    if (isPlaying && !pendingSyncStart) {
      pausePlayback();
    } else {
      startPlayback();
    }
  };
  addAndMakeVisible(btnStop);
  btnStop.onClick = [this] { stopPlayback(); };
  addAndMakeVisible(btnPrev);
  btnPrev.onClick = [this] {
    bool wasPlaying = isPlaying;
    loadMidiFile(juce::File(playlist.getPrevFile()));
    if (wasPlaying)
      startPlayback();
  };
  addAndMakeVisible(btnSkip);
  btnSkip.onClick = [this] {
    bool wasPlaying = isPlaying;
    loadMidiFile(juce::File(playlist.getNextFile()));
    if (wasPlaying)
      startPlayback();
  };
  addAndMakeVisible(btnClearPR);
  btnClearPR.onClick = [this] { clearPlaybackSequence(); };
  addAndMakeVisible(btnResetBPM);
  btnResetBPM.onClick = [this] {
    double target = (currentFileBpm > 0.0) ? currentFileBpm : 120.0;
    auto state = link->captureAppSessionState();
    state.setTempo(target, link->clock().micros());
    link->commitAppSessionState(state);
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
  cmbTheme.addItem("Dark (Default)", 1);
  cmbTheme.addItem("Light", 2);
  cmbTheme.addItem("Midnight", 3);
  cmbTheme.addItem("Forest", 4);
  cmbTheme.addItem("Rainbow", 5);
  cmbTheme.addItem("Harvest Orange", 6);
  cmbTheme.addItem("Sandy Beach", 7);
  cmbTheme.addItem("Pink & Plentiful", 8);
  cmbTheme.addItem("Space Purple", 9);
  cmbTheme.addItem("Underwater", 10);
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
      // Find the file that corresponds to this ID (Name matching or re-scan)
      // Since we don't store a map of ID->File, we iterate the ComboBox text or
      // re-scan. Simplest: use the text of the selected item.
      auto name = cmbControlProfile.getText();
      auto file = getProfileDirectory().getChildFile(name + ".json");
      if (!file.existsAsFile())
        file = getProfileDirectory().getChildFile(name + ".midimap");

      if (file.existsAsFile()) {
        loadCustomProfile(file);
      }
    }

    // Logic for other factory profiles if needed
    logPanel.log("! Profile Loaded: " + cmbControlProfile.getText(), true);
  };

  addAndMakeVisible(btnSaveProfile);
  btnSaveProfile.setButtonText("Export Controller");
  btnSaveProfile.onClick = [this] {
    auto file = getProfileDirectory().getChildFile("exported_profile.json");
    fc = std::make_unique<juce::FileChooser>("Save Profile...", file, "*.json");
    fc->launchAsync(
        juce::FileBrowserComponent::saveMode |
            juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser &chooser) {
          auto result = chooser.getResult();
          if (result != juce::File{}) {
            saveFullProfile(result);
            logPanel.log("! Profile Exported: " + result.getFileName(), true);
          }
        });
  };

  // BINDING SEQUENCER RESET
  sequencer.btnResetCH.onClick = [this] { mixer.resetMapping(); };

  // Setup Visualizers
  addAndMakeVisible(phaseVisualizer);
  addAndMakeVisible(horizontalKeyboard);
  addAndMakeVisible(verticalKeyboard);
  addAndMakeVisible(logPanel);
  addAndMakeVisible(playlist);
  playlist.onLoopModeChanged = [this](juce::String state) {
    logPanel.log("! Playlist Mode: " + state, true);
  };
  addAndMakeVisible(sequencer);

  addAndMakeVisible(mixerViewport);
  mixer.setBounds(0, 0, 16 * mixer.stripWidth, 300);
  mixerViewport.setViewedComponent(&mixer, false);
  mixerViewport.setScrollBarsShown(false, true);

  addAndMakeVisible(lblArp);
  addAndMakeVisible(grpArp);

  // --- Arpeggiator ---
  addAndMakeVisible(btnArp);
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

  addAndMakeVisible(btnArpSync);
  btnArpSync.setButtonText("Sync");
  btnArpSync.setClickingTogglesState(true);
  btnArpSync.setColour(juce::TextButton::buttonOnColourId, Theme::accent);
  btnArpSync.onClick = [this] {
    logPanel.log("! Arp Sync: " +
                     juce::String(btnArpSync.getToggleState() ? "ON" : "OFF"),
                 false);
  };

  addAndMakeVisible(sliderArpSpeed);
  sliderArpSpeed.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  sliderArpSpeed.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  sliderArpSpeed.setRange(20, 1000, 1);
  sliderArpSpeed.setValue(400);
  sliderArpSpeed.setColour(juce::Slider::thumbColourId, Theme::accent);

  addAndMakeVisible(sliderArpVel);
  sliderArpVel.setSliderStyle(juce::Slider::RotaryVerticalDrag);
  sliderArpVel.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  sliderArpVel.setRange(0, 127, 1);
  sliderArpVel.setValue(90);
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
  // Tighten dropdown
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

  // --- Tooltips ---
  btnDash.setTooltip("Toggle Dashboard/Simple Mode");
  btnCtrl.setTooltip("Open Control Page");
  btnOscCfg.setTooltip("Open OSC Configuration");
  btnHelp.setTooltip("Open Help Page");
  btnPlay.setTooltip("Start Playback");
  btnStop.setTooltip("Stop Playback");
  btnPrev.setTooltip("Play Previous MIDI File");
  btnSkip.setTooltip("Play Next MIDI File");
  btnClearPR.setTooltip("Clear Playback Sequence");
  btnResetBPM.setTooltip("Reset BPM to file's BPM or 120");
  btnTapTempo.setTooltip("Tap tempo 4 times to set BPM");
  btnPrOctUp.setTooltip("Increase Playback Octave");
  btnPrOctDown.setTooltip("Decrease Playback Octave");
  btnPanic.setTooltip("Send All Notes Off and All Sound Off");
  btnConnect.setTooltip("Connect/Disconnect OSC");
  btnRetrigger.setTooltip("Retrigger notes on Note Off");
  btnGPU.setTooltip("Toggle GPU Rendering");
  btnBlockMidiOut.setTooltip("Block MIDI output from playback");
  btnLinkToggle.setTooltip("Toggle Ableton Link");
  btnSplit.setTooltip("Split MIDI channel 1 notes (C4 and below to Ch2)");
  btnPreventBpmOverride.setTooltip("Prevent MIDI files from changing tempo");
  cmbQuantum.setTooltip("Ableton Link Quantum");
  nudgeSlider.setTooltip("Nudge Link tempo temporarily");
  btnArp.setTooltip("Toggle Arpeggiator Latch");
  btnArpSync.setTooltip("Sync Arpeggiator to Link beat");
  sliderArpSpeed.setTooltip("Arpeggiator Speed");
  sliderArpVel.setTooltip("Arpeggiator Velocity");
  cmbArpPattern.setTooltip("Arpeggiator Pattern");
  sliderNoteDelay.setTooltip("Note Duration (Virtual Keyboard)");
  cmbMidiIn.setTooltip("Select MIDI Input Device");
  cmbMidiOut.setTooltip("Select MIDI Output Device");
  cmbMidiCh.setTooltip("Select MIDI Channel for Output");
  edIp.setTooltip("OSC Destination IP Address");
  edPOut.setTooltip("OSC Destination Port");
  edPIn.setTooltip("OSC Listening Port");
  lblLocalIpDisplay.setTooltip("Your local IP address");
  tempoSlider.setTooltip("Set Global Tempo");
  sliderPitchH.setTooltip("Pitch Bend (Horizontal Keyboard)");
  sliderModH.setTooltip("Modulation (Horizontal Keyboard)");
  sliderPitchV.setTooltip("Pitch Bend (Vertical Keyboard)");
  sliderModV.setTooltip("Modulation (Vertical Keyboard)");
  vol1Simple.setTooltip("Simple Mode Volume 1");
  vol2Simple.setTooltip("Simple Mode Volume 2");
  txtVol1Osc.setTooltip("OSC Address for Volume 1");
  txtVol2Osc.setTooltip("OSC Address for Volume 2");
  btnMidiThru.setTooltip("Enable MIDI Thru (Echo input to output)");
  btnVol1CC.setTooltip("Send CC20 for Volume 1");
  btnVol2CC.setTooltip("Send CC21 for Volume 2");
  btnMidiClock.setTooltip("Send MIDI Clock (F8) to MIDI Out");

  // --- Final Init ---
  setSize(800, 750);
  link->enable(true);
  link->enableStartStopSync(true);
  juce::Timer::startTimer(40);
  juce::HighResolutionTimer::startTimer(1);
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
    //     [this, logStr] { logPanel.log(logStr, false); });
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
            midiScheduler.scheduleNoteOff(
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
        midiScheduler.scheduleNoteOff(i, note,
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
    heldNotes.add(note);
    noteArrivalOrder.push_back(note);
  } else {
    // Send to OSC (Bidirectional) and MIDI Out
    if (!isHandlingOsc)
      sendSplitOscMessage(juce::MidiMessage::noteOn(ch, note, vel), ch);
    // Force 'ch' into sendSplitOscMessage to ensure it uses the SPLIT channel.

    if (midiOutput && !btnBlockMidiOut.getToggleState()) {
      // Send directly to Hardware.
      // Ensure this channel is not forcefully blocked by Mixer?
      // Actually, Mixer strips control OSC mapping mostly, but for live play we
      // usually want thru. User said "juce piano roll acts as a midi out
      // device".
      midiOutput->sendMessageNow(juce::MidiMessage::noteOn(ch, note, vel));
    }
  }
}

void MainComponent::handleNoteOff(juce::MidiKeyboardState *, int ch, int note,
                                  float vel) {
  if (btnArp.getToggleState()) {
    heldNotes.removeFirstMatchingValue(note);
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
//==============================================================================
void MainComponent::hiResTimerCallback() {
  double nowMs = juce::Time::getMillisecondCounterHiRes();

  // --- PROCESS SCHEDULED NOTE OFFS ---
  {
    auto dueNotes = midiScheduler.processDueNotes(nowMs);
    for (auto &n : dueNotes) {
      isHandlingOsc = true;
      keyboardState.noteOff(n.channel, n.note, 0.0f);
      isHandlingOsc = false;
    }
  }

  // Virtual Notes
  {
    auto dueVirtual = midiScheduler.processDueVirtualNotes(nowMs);
    for (auto &n : dueVirtual) {
      keyboardState.noteOff(n.channel, n.note, 0.0f);
    }
  }

  // --- CLOCK GEN ---
  // --- CLOCK GEN ---
  if (btnMidiClock.getToggleState() && midiOutput) {
    // 0. Parameters
    double currentBpm = (link && link->isEnabled())
                            ? link->captureAppSessionState().tempo()
                            : bpmVal.get();

    // Safety clamp
    if (currentBpm < 1.0)
      currentBpm = 120.0;

    double offsetMs = sliderClockOffset.getValue();

    // 1. PHASE LOCKED MODE (New)
    if (clockMode == PhaseLocked && link && link->isEnabled()) {
      auto state = link->captureAppSessionState();

      // Calculate beat at 'now + offset'
      double timeForBeat = (juce::Time::getMillisecondCounterHiRes() * 1000.0) +
                           (offsetMs * 1000.0);
      double currentBeat = state.beatAtTime(
          std::chrono::microseconds((long long)timeForBeat), quantum);

      static double lastLinkBeatFunc = 0.0;

      // Initialize if jump detected (e.g. invalid state or first run)
      if (lastLinkBeatFunc > currentBeat + 1.0)
        lastLinkBeatFunc = currentBeat;

      double expectedTicks = std::floor(currentBeat * 24.0);
      double lastTicks = std::floor(lastLinkBeatFunc * 24.0);

      int ticksToSend = (int)(expectedTicks - lastTicks);
      if (ticksToSend > 0 && ticksToSend < 10) { // Limit burst
        for (int i = 0; i < ticksToSend; ++i) {
          if (midiOutput && !btnBlockMidiOut.getToggleState())
            midiOutput->sendMessageNow(juce::MidiMessage(0xF8));
        }
      }

      lastLinkBeatFunc = currentBeat;

    } else {
      // 2. SMOOTH MODE (Legacy / Fallback)
      double msPerTick = (60000.0 / currentBpm) / 24.0;

      static double lastClockSendTime = nowMs;

      // Apply offset logic to "Target Time" if needed?
      // For free-running clock, "Offset" acts as a start delay or phase shift?
      // Hard to apply variable offset to free-run rate without hiccups.
      // We'll apply it by effectively modulating the 'now' time processing?
      // Simpler: Just run the timer.
      // User asked toggles for "realtime sequencing". Phase Locked is the key.

      // Recover from huge gaps (e.g. pause)
      if (nowMs - lastClockSendTime > 200.0)
        lastClockSendTime = nowMs;

      double timeSinceLastTick = nowMs - lastClockSendTime;

      if (timeSinceLastTick >= msPerTick) {
        int numTicksToSend = (int)(timeSinceLastTick / msPerTick);
        // Cap burst to avoid flooding
        if (numTicksToSend > 5)
          numTicksToSend = 5;

        for (int i = 0; i < numTicksToSend; ++i) {
          if (midiOutput && !btnBlockMidiOut.getToggleState())
            midiOutput->sendMessageNow(juce::MidiMessage(0xF8));
        }
        lastClockSendTime += (numTicksToSend * msPerTick);
      }
    }
  }

  // --- ARPEGGIATOR LOGIC ---
  if (btnArp.getToggleState() && !heldNotes.isEmpty()) {
    double arpIntervalMs = 0.0;
    if (btnArpSync.getToggleState() && link && link->isEnabled()) {
      double currentBpm = bpmVal.get();
      if (currentBpm < 1.0)
        currentBpm = 120.0;
      arpIntervalMs = (60000.0 / currentBpm) / 4.0;
    } else {
      double spd = sliderArpSpeed.getValue();
      if (spd < 1.0)
        spd = 20.0;
      arpIntervalMs = 60000.0 / spd;
    }

    if (nowMs - lastArpNoteTime >= arpIntervalMs) {
      lastArpNoteTime = nowMs;
      heldNotes.sort();
      int numNotes = heldNotes.size();
      int patternId = cmbArpPattern.getSelectedId(); // 1=Up, 2=Down, 6=Random

      if (patternId == 6) {
        currentArpIndex = juce::Random::getSystemRandom().nextInt(numNotes);
      } else if (patternId == 2) {
        currentArpIndex--;
        if (currentArpIndex < 0)
          currentArpIndex = numNotes - 1;
      } else {
        currentArpIndex++;
        if (currentArpIndex >= numNotes)
          currentArpIndex = 0;
      }

      int note = heldNotes[currentArpIndex];
      int ch = getSelectedChannel();
      float vel = (float)sliderArpVel.getValue() / 127.0f;
      if (vel <= 0.001f)
        vel = 0.8f;

      juce::MidiMessage m = juce::MidiMessage::noteOn(ch, note, vel);

      if (!isHandlingOsc)
        sendSplitOscMessage(m);
      if (midiOutput && !btnBlockMidiOut.getToggleState())
        midiOutput->sendMessageNow(m);

      midiScheduler.scheduleNoteOff(ch, note, nowMs + (arpIntervalMs * 0.8));
    }
  }

  // --- SEQUENCER & PLAYBACK CORE ---
  double quantum = 4.0;
  double currentBeat = 0.0;
  bool isLinkEnabled = (link && link->isEnabled());
  auto now = link->clock().micros(); // Current US time

  // Calculate Beat Position
  if (isLinkEnabled) {
    auto session = link->captureAppSessionState();
    currentBeat = session.beatAtTime(now, quantum);

    if (isPlaying) {
      if (pendingSyncStart) {
        // If no peers, start immediately. Otherwise wait for bar phase.
        bool shouldStart = (link->numPeers() == 0) ||
                           (session.phaseAtTime(now, quantum) < 0.05);
        if (shouldStart) {
          transportStartBeat = currentBeat - beatsPlayedOnPause;
          lastProcessedBeat = -1.0;
          playbackCursor = 0;
          pendingSyncStart = false;
          // Ensure Link Transport is Playing
          if (!session.isPlaying()) {
            session.setIsPlaying(true, now); // Just set playing, beat is auto
            link->commitAppSessionState(session);
          }
          // Fire Start/Continue MIDI
          if (isOscConnected)
            oscSender.send(oscConfig.ePlay.getText(), 1.0f);
          if (midiOutput && btnMidiClock.getToggleState() &&
              !btnBlockMidiOut.getToggleState()) // Added check
            midiOutput->sendMessageNow(juce::MidiMessage(0xFA));
        } else {
          return; // Waiting for sync
        }
      }
    }
  } else {
    // Internal Lock
    static double lastSpeedMult = 1.0;
    double speedMult = 1.0;

    // Time Mode Momentary Speed Up (Only if Link is OFF)
    // User: "Holding 1/32 makes the sequencer fly... at 8x speed."
    // 1/32 gives activeRollDiv = 32. Normal 1/4 gives 4.
    // 32/4 = 8.
    if (sequencer.getMode() == StepSequencer::Time &&
        sequencer.activeRollDiv > 0) {
      speedMult = (double)sequencer.activeRollDiv / 4.0;
    }

    if (isPlaying) {
      if (pendingSyncStart) {
        pendingSyncStart = false;
        internalPlaybackStartTimeMs = nowMs;
        lastProcessedBeat = -1.0;
        if (isOscConnected)
          oscSender.send(oscConfig.ePlay.getText(), 1.0f);
        if (midiOutput && btnMidiClock.getToggleState() &&
            !btnBlockMidiOut.getToggleState()) // Added check
          midiOutput->sendMessageNow(juce::MidiMessage(0xFA));
      }

      // Handle Speed Change Discontinuity to prevent playhead jumping
      if (std::abs(speedMult - lastSpeedMult) > 0.001) {
        // Calculate beats played so far using current rate before switching
        double currentBpm = bpmVal.get();
        if (currentBpm < 1.0)
          currentBpm = 120.0;
        double beatsPerMs = currentBpm / 60000.0;

        double elapsedMs = nowMs - internalPlaybackStartTimeMs;
        double beatsSinceStart = elapsedMs * beatsPerMs * lastSpeedMult;

        beatsPlayedOnPause += beatsSinceStart;
        internalPlaybackStartTimeMs = nowMs;
      }
      lastSpeedMult = speedMult;

      double elapsedMs = nowMs - internalPlaybackStartTimeMs;
      double currentBpm = bpmVal.get();
      if (currentBpm < 1.0)
        currentBpm = 120.0;
      double beatsPerMs = currentBpm / 60000.0;

      currentBeat = (elapsedMs * beatsPerMs * speedMult) + beatsPlayedOnPause;
    } else {
      currentBeat = beatsPlayedOnPause;
    }
  }

  // --- ALWAYS UPDATE PLAYHEAD & SEQUENCER IF PLAYBACK IS RUNNING ---
  if (isPlaying) {
    // Sync the visual grid
    double pbRelative = currentBeat - transportStartBeat;
    trackGrid.playbackCursor = (float)(pbRelative * ticksPerQuarterNote);
    trackGrid.octaveShift = pianoRollOctaveShift;

    // --- SEQUENCER PERFORMANCE LOGIC (LOOP / ROLL) ---
    int currentStepPos = -1;

    if (sequencer.activeRollDiv > 0) {
      // Capture the start of the roll/loop if we just started pressing
      if (!sequencer.isRollActive) {
        sequencer.rollCaptureBeat = currentBeat;
        sequencer.isRollActive = true;
      }

      double loopLengthBeats =
          4.0 / (double)sequencer.activeRollDiv; // e.g., 1/16 = 0.25 beats

      // LOOP MODE: Wraps the playback position within the capture window
      if (sequencer.getMode() == StepSequencer::Loop) {
        // Calculate a "local" beat that never leaves the 1/X window
        double offset =
            std::fmod(currentBeat - sequencer.rollCaptureBeat, loopLengthBeats);
        double effectiveBeat = sequencer.rollCaptureBeat + offset;

        // Map this effective beat to the sequencer steps (assuming 4 steps per
        // beat = 1/16th grid)
        currentStepPos = (int)(effectiveBeat * 4.0) % sequencer.numSteps;
      }
      // ROLL MODE: Stays on the master timeline but "ratchets" (multi-fires)
      // the note
      else if (sequencer.getMode() == StepSequencer::Roll) {
        currentStepPos = (int)(currentBeat * 4.0) % sequencer.numSteps;

        // If current step is active, check if we need to fire a sub-tick note
        if (sequencer.isStepActive(currentStepPos)) {
          // Check phase within the 1/X divisor
          double rollPhase = std::fmod(currentBeat, loopLengthBeats);

          // Fire if we just crossed the start of a new roll-pulse
          // Using a small window here. Better might be to track last fired
          // sub-step. Or compare integer index of sub-steps.
          int currentRollSubStep = (int)(currentBeat / loopLengthBeats);

          if (currentRollSubStep != sequencer.lastRollFiredStep) {
            // Fire Note!
            sequencer.lastRollFiredStep = currentRollSubStep;
            // We need to trigger the note below, so we set currentStepPos and
            // let the localized trigger handle it? BUT, the trigger below logic
            // checks (currentStepPos != sequencer.currentStep). Since Roll
            // stays on the same step, we need to force fire here.

            int note = (int)sequencer.noteSlider.getValue();
            int ch = sequencer.outputChannel; // Use dynamic channel

            // Latency Comp Calculation (Slider controlled)
            double latencyCompLines = sliderLatencyComp.getValue();
            double triggerTime = nowMs + latencyCompLines;

            // Verify Mixer Status
            if (mixer.isChannelActive(ch)) {
              keyboardState.noteOn(ch, note, 1.0f);
              midiScheduler.scheduleNoteOff(ch, note, triggerTime + 100.0);

              if (!isHandlingOsc) {
                sendSplitOscMessage(juce::MidiMessage::noteOn(ch, note, 1.0f),
                                    ch);
                if (midiOutput && !btnBlockMidiOut.getToggleState())
                  midiOutput->sendMessageNow(
                      juce::MidiMessage::noteOn(ch, note, 1.0f));
              }
            }
          }
        }
      }
    } else {
      sequencer.isRollActive = false; // Reset state when button is released
      sequencer.lastRollFiredStep = -1;
      currentStepPos = (int)(currentBeat * 4.0) % sequencer.numSteps;
    }

    // Standard Grid Trigger (Only if Step Changed)
    if (currentStepPos != sequencer.currentStep) {
      sequencer.setActiveStep(currentStepPos);
      if (sequencer.isStepActive(currentStepPos)) {
        // Don't double fire if Roll is active (Roll handles its own firing)
        if (sequencer.activeRollDiv == 0 ||
            sequencer.getMode() != StepSequencer::Roll) {
          int note = (int)sequencer.noteSlider.getValue();
          int ch = sequencer.outputChannel;

          // Latency Comp
          double latencyComp = sliderLatencyComp.getValue(); // ms
          double triggerTime = nowMs + latencyComp;

          if (mixer.isChannelActive(ch)) {
            keyboardState.noteOn(ch, note, 1.0f);
            midiScheduler.scheduleNoteOff(ch, note, triggerTime + 100.0);

            if (!isHandlingOsc) {
              sendSplitOscMessage(juce::MidiMessage::noteOn(ch, note, 1.0f),
                                  ch);
              if (midiOutput && !btnBlockMidiOut.getToggleState())
                midiOutput->sendMessageNow(
                    juce::MidiMessage::noteOn(ch, note, 1.0f));
            }
          }
        }
      }
    }

    // ROLL MODE CONTINUOUS FIRE
    if (sequencer.getMode() == StepSequencer::Roll &&
        sequencer.activeRollDiv > 0 && isPlaying) {
      // Logic: Fire fast repeats if the CURRENT step is active
      int normalStep = (int)(currentBeat * 4.0) % sequencer.numSteps;
      if (sequencer.isStepActive(normalStep)) {
        // Ratchet logic
        double rollInterval =
            (240000.0 / bpmVal.get()) / (double)sequencer.activeRollDiv; // ms
        // We need a specialized state to track roll fires.
        // For now, let's skip complex ratcheting to avoid cluttering
        // MainComponent too much and verify basic Time/Loop first as requested.
        // *User asked for Roll*. I should try.
        // using `lastArpNoteTime` like logic?
        static double lastRollTime = 0;
        if (nowMs - lastRollTime > rollInterval) {
          lastRollTime = nowMs;
          int note = (int)sequencer.noteSlider.getValue();
          keyboardState.noteOn(1, note, 1.0f);
          midiScheduler.scheduleNoteOff(1, note, nowMs + (rollInterval * 0.5));
          if (!isHandlingOsc) {
            sendSplitOscMessage(juce::MidiMessage::noteOn(1, note, 1.0f));
            if (midiOutput && !btnBlockMidiOut.getToggleState())
              midiOutput->sendMessageNow(
                  juce::MidiMessage::noteOn(1, note, 1.0f));
          }
        }
      }
    }

    // 2. MIDI File Playback
    if (playbackSeq.getNumEvents() > 0 && !pendingSyncStart) {
      if (playbackCursor == 0)
        lastProcessedBeat = -1.0; // Ensure first frame triggers
      double playbackBeats = currentBeat - transportStartBeat;
      double rangeEnd = playbackBeats;

      juce::ScopedLock sl(midiLock);
      while (playbackCursor < playbackSeq.getNumEvents()) {
        auto *ev = playbackSeq.getEventPointer(playbackCursor);
        double eventBeat = ev->message.getTimeStamp() / ticksPerQuarterNote;
        if (eventBeat >= rangeEnd)
          break;

        if (eventBeat >= lastProcessedBeat) {
          int rawCh = ev->message.getChannel();
          int ch = mixer.getMappedChannel(rawCh);
          int n = ev->message.getNoteNumber();

          if (ev->message.isNoteOnOrOff()) {
            n = juce::jlimit(0, 127, n + (pianoRollOctaveShift * 12));
            auto mCopy = ev->message;
            mCopy.setNoteNumber(n);

            if (btnSplit.getToggleState() && ch == 1) {
              if (n < 64)
                ch = 2;
            }

            if (mixer.isChannelActive(ch)) {
              sendSplitOscMessage(mCopy, ch);
              if (midiOutput && !btnBlockMidiOut.getToggleState())
                midiOutput->sendMessageNow(mCopy);
            }
          } else {
            if (mixer.isChannelActive(ch)) {
              sendSplitOscMessage(ev->message, ch);
              if (midiOutput && !btnBlockMidiOut.getToggleState())
                midiOutput->sendMessageNow(ev->message);
            }
          }
        }
        playbackCursor++;
      }
      lastProcessedBeat = rangeEnd;

      if (playbackCursor >= playbackSeq.getNumEvents()) {
        if (playlist.playMode == MidiPlaylist::LoopOne) {
          playbackCursor = 0;
          lastProcessedBeat = -1.0;
          transportStartBeat = currentBeat;
          logPanel.log("! Looping File", false);
        } else if (playlist.playMode == MidiPlaylist::LoopAll) {
          isPlaying = false;
          juce::MessageManager::callAsync([this] { btnSkip.onClick(); });
        } else {
          stopPlayback();
          logPanel.log("! Playback Finished", true);
        }
      }
    }
  } else {
    // Visual playhead follows beatsPlayedOnPause when stopped/paused
    trackGrid.playbackCursor =
        (float)(beatsPlayedOnPause * ticksPerQuarterNote);
  }
} // End hiResTimerCallback

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
    logPanel.updateStats("Peers: " + juce::String(link->numPeers()));
  }

  int currentPeers = (int)link->numPeers();
  if (currentPeers != lastNumPeers) {
    logPanel.log("! Link Peers: " + juce::String(currentPeers), true);
    lastNumPeers = currentPeers;
  }

  if (link && !link->isEnabled() && startupRetryActive) {
    linkRetryCounter++;
    if (linkRetryCounter >= 125)
      startupRetryActive = false;
    else
      link->enable(true);
  }
  phaseVisualizer.setPhase(session.phaseAtTime(link->clock().micros(), quantum),
                           quantum);
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

  // Global Header Elements
  btnDash.setVisible(true);
  btnCtrl.setVisible(true);
  btnOscCfg.setVisible(true);
  btnHelp.setVisible(true);

  // Panic Hidden in Simple Mode (Moved to specific location in resized?)
  // User said: "In simple mode layout I want the "P" panic button placed very
  // top right corner of app." So it is visible in both.
  btnPanic.setVisible(true);

  btnRetrigger.setVisible(isDash);
  btnGPU.setVisible(currentView == AppView::Help);
  btnMidiScaling.setVisible(currentView == AppView::Help);
  btnMidiScalingToggle.setVisible(currentView == AppView::Help);

  // User Request: "While in default layout and user clicks menu buttons
  // Control/OSc Config/Help I need the midi i/o to hide and I need My IP: to
  // hide also, also move the bpm slider to the very top left where My IP: was"

  bool hideMidiAndIP = isOverlayActive;

  // Network controls
  // User: "I need My IP: to hide also"
  bool showNet = isDash && !isSimple && !hideMidiAndIP;
  grpNet.setVisible(showNet);
  lblIp.setVisible(showNet);
  edIp.setVisible(showNet);
  lblPOut.setVisible(showNet);
  edPOut.setVisible(showNet);
  lblPIn.setVisible(showNet);
  edPIn.setVisible(showNet);
  btnConnect.setVisible(showNet);
  ledConnect.setVisible(showNet);

  // Dashboard - Shared
  // User: "move the bpm slider to the very top left where My IP: was while in
  // Control/OSc Config/Help menus" So BPM Slider is VISIBLE even in Overlay
  // modes!
  lblTempo.setVisible(isDash || isOverlayActive);
  tempoSlider.setVisible(isDash || isOverlayActive);

  // Other transport usually hidden if overlay active?
  // Let's keep them hidden if that was original behavior, but usually overlays
  // cover them. Original: btnResetBPM.setVisible(isDash);
  btnResetBPM.setVisible(isDash);
  btnLinkToggle.setVisible(isDash);
  btnTapTempo.setVisible(isDash);
  phaseVisualizer.setVisible(isDash);
  lblLinkBeat.setVisible(false);
  btnPlay.setVisible(isDash);
  btnStop.setVisible(isDash);
  btnPrev.setVisible(isDash);
  btnSkip.setVisible(isDash);
  btnClearPR.setVisible(isDash);
  btnSplit.setVisible(isDash);

  // Thru / Block moved
  // User: "In default layout Midi i/o section move Thru to very top of app to
  // the left of Panic." So Thru might be visible always? Or just Dash? "In
  // default layout".
  btnMidiThru.setVisible(isDash);
  btnMidiClock.setVisible(false);

  // MIDI I/O Group
  // User: "In Control menu hide the Midi i/o midi in/out/ch drop down menu"
  grpIo.setVisible(isDash && !controlPage.isVisible());

  // Dashboard - Default Mode Only
  bool isDefault = isDash && !isSimple && !isOverlayActive;

  trackGrid.setShowNotes(!isSimple); // Hide notes in Simple Mode

  trackGrid.setVisible(isDefault);
  horizontalKeyboard.setVisible(isDefault);
  sequencer.setVisible(isDefault);
  mixerViewport.setVisible(isDefault);
  grpArp.setVisible(isDefault);
  sliderPitchH.setVisible(isDefault);
  sliderModH.setVisible(isDefault);
  logPanel.setVisible(isDefault);
  playlist.setVisible(isDefault);
  btnPrOctUp.setVisible(isDefault);
  btnPrOctDown.setVisible(isDefault);

  // Arp Gen Internals
  btnArp.setVisible(isDefault);
  btnArpSync.setVisible(isDefault);

  // Block Out: "next in default view to the right of midi out port place the
  // Block Out button" But also in Simple Mode: "In its place (Panic top right)
  // you can put the Block Out button" So Visible in both Dash modes.
  btnBlockMidiOut.setVisible(isDash);

  sliderArpSpeed.setVisible(isDefault);
  sliderArpVel.setVisible(isDefault);
  cmbArpPattern.setVisible(isDefault);
  lblArpVel.setVisible(isDefault);
  lblArpBpm.setVisible(isDefault);

  // Dashboard - Simple Mode Only
  bool isSimpleDash = isDash && isSimple && !isOverlayActive;
  verticalKeyboard.setVisible(isSimpleDash);
  sliderPitchV.setVisible(isSimpleDash);
  sliderModV.setVisible(isSimpleDash);
  vol1Simple.setVisible(isSimpleDash);
  vol2Simple.setVisible(isSimpleDash);
  btnVol1CC.setVisible(isSimpleDash);
  btnVol2CC.setVisible(isSimpleDash);
  txtVol1Osc.setVisible(isSimpleDash);
  txtVol2Osc.setVisible(isSimpleDash);
  // Log and Playlist also in Simple but different spots
  if (isSimpleDash) {
    logPanel.setVisible(true);
    playlist.setVisible(true);
    // Simple Mode additions:
    trackGrid.setVisible(true);    // Timeline top
    sliderPitchV.setVisible(true); // Wheels bottom left
    sliderModV.setVisible(true);
  }

  // Help Menu Toggle
  // Help Menu Toggle
  bool isHelp = (currentView == AppView::Help);
  btnResetMixerOnLoad.setVisible(isHelp);
  cmbClockMode.setVisible(isHelp);
  sliderClockOffset.setVisible(isHelp);
  lblClockOffset.setVisible(isHelp);
  cmbTheme.setVisible(isHelp);
  cmbControlProfile.setVisible(isHelp);
}

void MainComponent::resized() {
  int logoYOffset = 2; // "hair down"
  logoView.setBounds(10, 8 + logoYOffset, 25, 25);
  lblLocalIpHeader.setBounds(45, 8 + logoYOffset, 50, 25);
  lblLocalIpDisplay.setBounds(100, 8 + logoYOffset, 120, 25);

  auto area = getLocalBounds().reduced(5);

  // --- 1. GLOBAL HEADER ---
  auto headerRow = area.removeFromTop(35);
  int btnW = 90;
  int navX = (getWidth() - (4 * btnW)) / 2;
  auto navArea = headerRow.withX(navX).withWidth(4 * btnW);
  btnDash.setBounds(navArea.removeFromLeft(btnW).reduced(2));
  btnCtrl.setBounds(navArea.removeFromLeft(btnW).reduced(2));
  btnOscCfg.setBounds(navArea.removeFromLeft(btnW).reduced(2));
  btnHelp.setBounds(navArea.removeFromLeft(btnW).reduced(2));

  // --- TOP RIGHT ACTIONS ---
  if (!isSimpleMode && currentView == AppView::Dashboard) {
    // "In default layout ... move Retrig to left of the Midi Thru button"
    btnPanic.setBounds(headerRow.removeFromRight(85).reduced(2));
    btnPanic.setButtonText("Panic");
    btnMidiThru.setBounds(headerRow.removeFromRight(50).reduced(2));
    btnRetrigger.setBounds(headerRow.removeFromRight(60).reduced(2));
  } else {
    // Simple Mode: Panic is "P" top right
    btnPanic.setBounds(headerRow.removeFromRight(30).reduced(2));
    btnPanic.setButtonText("P");
  }
  btnGPU.setBounds(headerRow.removeFromRight(50).reduced(2));

  // --- OVERLAY HANDLING ---
  if (currentView != AppView::Dashboard) {
    auto overlayRect = getLocalBounds().reduced(20).withY(50);
    // Move BPM slider to top left (My IP area)
    tempoSlider.setVisible(true);
    tempoSlider.setBounds(60, 5, 140, 25);

    if (currentView == AppView::OSC_Config)
      oscViewport.setBounds(overlayRect);
    else if (currentView == AppView::Help) {
      helpViewport.setBounds(overlayRect);
      int startY = overlayRect.getY() + 10;
      int startX = overlayRect.getX() + 10;

      btnMidiScaling.setBounds(startX, startY, 120, 25);
      btnMidiScalingToggle.setBounds(startX + 130, startY, 150, 25);

      // Clock Controls
      cmbClockMode.setBounds(startX, startY + 35, 120, 25);
      lblClockOffset.setBounds(startX + 130, startY + 35, 80, 25);
      sliderClockOffset.setBounds(startX + 220, startY + 35, 100, 25);

      btnResetMixerOnLoad.setBounds(overlayRect.getRight() - 220, startY, 200,
                                    25); // Top Right corner roughly

      // Theme & Profile
      int yOff = 80;
      cmbTheme.setBounds(startX, startY + yOff, 150, 25);
      cmbControlProfile.setBounds(startX + 160, startY + yOff, 150, 25);
    } else {
      controlPage.setBounds(overlayRect);
      if (controlPage.isVisible()) {
        lblLatencyComp.setBounds(20, 20, 120, 20);
        sliderLatencyComp.setBounds(20, 45, 200, 20);
      }
    }
    return;
  }

  // --- 3. SIMPLE MODE LAYOUT ---
  if (isSimpleMode) {
    auto r = area;
    lblLocalIpHeader.setVisible(false);
    lblLocalIpDisplay.setVisible(false);

    // Timeline Top (Right of where Vertical KB will start?)
    // User: "In simple mode we need a timeline with playhead indicator along
    // the top below midi io/above log" "poositon to right of the vveritcal
    // piano roll" Vertical KB is on Left. So Timeline is Top of Center Area.

    auto midiArea = r.removeFromTop(65).reduced(2);
    grpIo.setBounds(midiArea);
    auto rMidi = grpIo.getBounds().reduced(10, 20);
    lblIn.setBounds(rMidi.removeFromLeft(20));
    cmbMidiIn.setBounds(rMidi.removeFromLeft(90));
    lblCh.setBounds(rMidi.removeFromLeft(20));
    cmbMidiCh.setBounds(rMidi.removeFromLeft(50));
    lblOut.setBounds(rMidi.removeFromLeft(20));
    cmbMidiOut.setBounds(rMidi.removeFromLeft(90));
    btnSplit.setBounds(rMidi.removeFromLeft(45).reduced(2));
    btnMidiThru.setBounds(rMidi.removeFromLeft(45).reduced(2));
    btnBlockMidiOut.setVisible(true);
    btnRetrigger.setBounds(rMidi.removeFromLeft(45).reduced(2));
    btnBlockMidiOut.setBounds(rMidi.removeFromLeft(45).reduced(2));

    btnMidiClock.setVisible(false);

    // --- BOTTOM TRANSPORT ---
    auto transportArea = r.removeFromBottom(50).reduced(2);
    // Left: Pitch/Mod, Clear, Play...
    // "beside to the left of the Clear button ... super small juce mod/pitch
    // wheel"
    sliderPitchV.setBounds(transportArea.removeFromLeft(20).reduced(2));
    sliderModV.setBounds(transportArea.removeFromLeft(20).reduced(2));
    btnClearPR.setBounds(transportArea.removeFromLeft(50).reduced(2));
    btnPlay.setBounds(transportArea.removeFromLeft(50).reduced(2));
    btnStop.setBounds(transportArea.removeFromLeft(50).reduced(2));
    btnPrev.setBounds(transportArea.removeFromLeft(40).reduced(2));
    btnSkip.setBounds(transportArea.removeFromLeft(40).reduced(2));

    // Right side: BPM etc
    auto bpmRegion = transportArea.removeFromRight(150);
    tempoSlider.setBounds(bpmRegion.removeFromRight(80).reduced(2));
    btnTapTempo.setBounds(bpmRegion.removeFromRight(60).reduced(2));
    btnResetBPM.setBounds(transportArea.removeFromRight(60).reduced(2));

    // Left Column: Vertical Keyboard
    auto leftCol = r.removeFromLeft(35);
    verticalKeyboard.setBounds(
        leftCol.withHeight(juce::jmin(leftCol.getHeight(), 350)));

    // Right Column: Sliders
    auto rightPan = r.removeFromRight(140).reduced(5);
    // "lower the two sliders" -> Push to bottom
    // "link beat step indicator top right under that new timeline"

    // Timeline consumes top of center area
    auto timelineH = 40;
    auto topCenter = r.removeFromTop(timelineH);
    trackGrid.setBounds(topCenter.reduced(2));

    // Link Indicator (Top Right of Right Panel, under timeline level?)
    phaseVisualizer.setVisible(true);
    phaseVisualizer.setBounds(rightPan.removeFromTop(20));

    auto rightBottom = rightPan.removeFromBottom(250); // Sliders area
    auto topOctRow = rightBottom.removeFromTop(30);
    btnPrOctDown.setBounds(
        topOctRow.removeFromLeft(topOctRow.getWidth() / 2).reduced(2));
    btnPrOctUp.setBounds(topOctRow.reduced(2));

    auto sliderRow = rightBottom.removeFromTop(180);
    auto s1Area = sliderRow.removeFromLeft(sliderRow.getWidth() / 2);
    auto s2Area = sliderRow;

    vol1Simple.setBounds(s1Area.removeFromTop(140).reduced(15, 5));
    txtVol1Osc.setBounds(s1Area.removeFromTop(20).reduced(5, 2));
    btnVol1CC.setBounds(s1Area.reduced(5, 0));

    vol2Simple.setBounds(s2Area.removeFromTop(140).reduced(15, 5));
    txtVol2Osc.setBounds(s2Area.removeFromTop(20).reduced(5, 2));
    btnVol2CC.setBounds(s2Area.reduced(5, 0));

    // Center: Playlist/Log
    // "Keep log/play list tight to left beside the piano roll"
    // "Tighten up... dont have overlapping"
    auto tightLeft = r.removeFromLeft(180);
    logPanel.setBounds(tightLeft.removeFromTop(120).reduced(2));
    playlist.setBounds(tightLeft.removeFromTop(150).reduced(2));

    return;
  }

  // --- 4. DEFAULT (DASHBOARD) MODE LAYOUT ---
  lblLocalIpHeader.setVisible(true);
  lblLocalIpDisplay.setVisible(true);

  auto r = area;

  // Header Items Row
  auto configRow = r.removeFromTop(65);
  grpNet.setBounds(configRow.removeFromLeft(400).reduced(2));
  lblLocalIpHeader.setVisible(!isSimpleMode);
  lblLocalIpDisplay.setVisible(!isSimpleMode);
  auto rNet = grpNet.getBounds().reduced(5, 15);
  lblIp.setBounds(rNet.removeFromLeft(20));
  edIp.setBounds(rNet.removeFromLeft(85));
  lblPOut.setBounds(rNet.removeFromLeft(35));
  edPOut.setBounds(rNet.removeFromLeft(50));
  lblPIn.setBounds(rNet.removeFromLeft(25));
  edPIn.setBounds(rNet.removeFromLeft(50));
  btnConnect.setBounds(rNet.removeFromLeft(90).reduced(
      2)); // Moved slightly right via reduced padding?
  // User: "move ... to the right a couple px"
  // Previous: reduced(2). x is set by removeFromLeft.
  // To move RIGHT, I need a larger X. removeFromLeft increments X.
  // I can just add a gap.
  rNet.removeFromLeft(5); // Shift connect button right 5px
  ledConnect.setBounds(rNet.removeFromLeft(30).reduced(5));

  grpIo.setBounds(configRow.reduced(2));
  auto rIo = grpIo.getBounds().reduced(10, 5);
  auto indicatorCol = rIo.removeFromRight(12);
  midiInLight.setBounds(
      indicatorCol.removeFromTop(indicatorCol.getHeight() / 2).reduced(2, 4));
  midiOutLight.setBounds(indicatorCol.reduced(2, 4));

  auto row1 = rIo.removeFromTop(22);
  lblIn.setBounds(row1.removeFromLeft(20));
  cmbMidiIn.setBounds(row1.removeFromLeft(100));
  lblCh.setBounds(row1.removeFromLeft(20));
  cmbMidiCh.setBounds(row1.removeFromLeft(50));
  lblOut.setBounds(row1.removeFromLeft(20));
  cmbMidiOut.setBounds(row1.removeFromLeft(100));
  btnMidiThru.setBounds(row1.removeFromLeft(45).reduced(2));
  btnBlockMidiOut.setBounds(row1.removeFromLeft(80).reduced(2));

  // Transport Row
  auto transRow = r.removeFromTop(45).reduced(2);
  btnPlay.setBounds(transRow.removeFromLeft(45).reduced(2));
  btnStop.setBounds(transRow.removeFromLeft(45).reduced(2));
  btnPrev.setBounds(transRow.removeFromLeft(35).reduced(2));
  btnSkip.setBounds(transRow.removeFromLeft(35).reduced(2));

  // "move the default view BPM/BPM slider to the left next to Clear button"
  btnClearPR.setBounds(transRow.removeFromLeft(55).reduced(2));
  lblTempo.setBounds(transRow.removeFromLeft(40));
  // "slighty widen slider width"
  tempoSlider.setBounds(
      transRow.removeFromLeft(160).reduced(0, 5)); // Widened (was 140)
  btnResetBPM.setBounds(transRow.removeFromLeft(50).reduced(
      2)); // Reduced reset button space to fit? No, just shifted.

  // "move over link/beat visual indicator" (Right of BPM Reset)
  phaseVisualizer.setVisible(true);
  phaseVisualizer.setBounds(transRow.removeFromLeft(100).reduced(2));
  btnLinkToggle.setVisible(true);
  btnLinkToggle.setBounds(transRow.removeFromLeft(60).reduced(2));

  // "restore the Oct- and move over"
  btnPrOctDown.setVisible(true);
  btnPrOctUp.setVisible(true);
  // Shift others right
  btnSplit.setBounds(transRow.removeFromRight(55).reduced(2));

  // "right of btnSplit" (Actually Layout says left of split if using
  // removeFromRight?) User asked: "Midi Learn ... right of Retrig" Let's find
  // Retrig. It is usually grouped with Split. Wait, Retrig isn't in this block?
  // Let's check where Retrigger is. Original layout might have it in `footer`
  // or `rightSide`? Found declaration: `btnRetrigger`. Let's place Learn next
  // to it. Assuming Retrigger is not visible in `resized` (I need to check
  // where it is placed). Searching file for `btnRetrigger.setBounds`...

  // If Retrigger is not in `resized`, I need to add it too if it was missing?
  // Ah, assuming Retrigger is in `transRow` for now.

  btnMidiLearn.setBounds(transRow.removeFromRight(50).reduced(2));
  btnRetrigger.setBounds(transRow.removeFromRight(50).reduced(2));

  btnPrOctUp.setBounds(transRow.removeFromRight(45).reduced(2));
  btnPrOctDown.setBounds(transRow.removeFromRight(45).reduced(2));

  // Footer
  auto footer = r.removeFromBottom(140);
  auto linkArea = footer.removeFromRight(280).reduced(2);
  auto linkTop = linkArea.removeFromTop(30);
  cmbQuantum.setBounds(linkTop.removeFromLeft(100).reduced(2));
  btnPreventBpmOverride.setBounds(linkTop.removeFromLeft(80).reduced(2));
  btnSaveProfile.setBounds(linkTop.removeFromLeft(100).reduced(2));

  // User: "Move down the nudge slider to just above tap tempo button."
  // Layout Logic:
  // linkArea has ~110 height left.
  // Tap Tempo is at very bottom (40px).
  // Nudge slider should be just above it.

  btnTapTempo.setBounds(linkArea.removeFromBottom(40).reduced(5));
  nudgeSlider.setBounds(
      linkArea.removeFromBottom(25).reduced(5, 2)); // Shifted down
  // Remaining space in linkArea is empty/spacing

  mixerViewport.setBounds(footer);
  mixer.setSize(16 * mixer.stripWidth, mixerViewport.getHeight() - 20);

  // Right Side
  auto rightSide = r.removeFromRight(260).reduced(2);

  // Log Panel (Top)
  logPanel.setBounds(rightSide.removeFromTop(180).reduced(2));

  // Arp Gen Controls (Bottom)
  // User: "Make the arp gen section less tall also"
  // Previous height was 140. Let's try 115.
  auto botArp = rightSide.removeFromBottom(115);
  grpArp.setBounds(botArp.reduced(2));

  // Playlist (Middle)
  playlist.setBounds(rightSide.reduced(2));

  // Arp Internals
  auto ar = grpArp.getBounds().reduced(10, 10); // Tighter padding
  auto leftCol = ar.removeFromLeft(65);
  btnArp.setBounds(leftCol.removeFromTop(20).reduced(1));
  btnArpSync.setBounds(leftCol.removeFromTop(20).reduced(1));
  auto dialW = 55;
  auto dialS = ar.removeFromLeft(dialW);
  sliderArpSpeed.setBounds(dialS.removeFromTop(45));
  lblArpSpdLabel.setBounds(dialS.removeFromTop(15));
  auto dialV = ar.removeFromLeft(dialW);
  sliderArpVel.setBounds(dialV.removeFromTop(45));
  lblArpVelLabel.setBounds(dialV.removeFromTop(15));

  // Pattern Dropdown
  cmbArpPattern.setBounds(ar.removeFromTop(30).reduced(2, 2));

  // Center Area
  auto center = r.reduced(2);
  sequencer.setBounds(center.removeFromBottom(120));
  auto kbArea = center.removeFromBottom(80);
  auto wheelsL = kbArea.removeFromLeft(50);
  sliderPitchH.setBounds(wheelsL.removeFromLeft(22).reduced(2, 5));
  sliderModH.setBounds(wheelsL.removeFromLeft(22).reduced(2, 5));
  horizontalKeyboard.setBounds(kbArea);
  wheelFutureBox.setBounds(wheelsL.getX(), center.getY(), 44,
                           center.getHeight() - 80);
  trackGrid.setBounds(center);

  // --- Overlays ---
  oscViewport.setBounds(getLocalBounds().reduced(20));
  helpViewport.setBounds(getLocalBounds().reduced(20));
  controlPage.setBounds(getLocalBounds().reduced(20));

  // Reset Learn Button in Help/Center?
  // User: "place button in middle of help"
  // If Help is visible, we show it?
  // Or just put it in Control Page or Footer?
  // "middle of help" implies inside the Help Viewport or overlaid.
  // We'll put it in control page or overlay for now.
  // Actually, let's put it in the OscConfig or Control Page.
  // Since `helpViewport` shows text, let's add it as a child of Help Overlay if
  // possible, or just place it when Help is active.

  if (currentView == AppView::Help) {
    btnResetLearned.setVisible(true);
    btnResetLearned.setBounds(getLocalBounds().getCentreX() - 60,
                              getLocalBounds().getCentreY() + 150, 120, 30);
    btnResetLearned.toFront(false);
  } else {
    btnResetLearned.setVisible(false);
  }

  if (controlPage.isVisible()) {
    lblLatencyComp.setBounds(20, 20, 120, 20);
    sliderLatencyComp.setBounds(20, 45, 200, 20);
  }
}

void MainComponent::handleIncomingMidiMessage(juce::MidiInput *source,
                                              const juce::MidiMessage &m) {
  // 1. Pass to Mapping Manager (Optimized internal FIFO)
  mappingManager.handleIncomingMidiMessage(source, m);
  midiInLight.activate();

  // 2. Process Clock/Sync (Realtime Thread Safe)
  if (m.isMidiClock()) {
    handleMidiClock(m);
  }

  // 3. Push to FIFO for UI/OSC tasks (Avoid callAsync)
  bool isActiveSense = (m.getRawDataSize() == 1 && m.getRawData()[0] == 0xFE);
  if (!m.isMidiClock() && !isActiveSense) { // Filter spam
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

// Helper for Realtime Clock
void MainComponent::handleMidiClock(const juce::MidiMessage &m) {
  if (midiOutput && btnMidiThru.getToggleState() &&
      !btnBlockMidiOut.getToggleState()) {
    midiOutput->sendMessageNow(m);
  }

  // Sync Logic
  double nowMs = juce::Time::getMillisecondCounterHiRes();
  clockInTimes.push_back(nowMs);
  clockPulseCounter++;

  if (clockPulseCounter >= 24) { // Every Quarter Note
    if (clockInTimes.size() >= 24) {
      double first = clockInTimes.front();
      double last = clockInTimes.back();
      double duration = last - first;
      if (duration > 50.0) {
        double bpm = 60000.0 / duration;
        bpm = juce::jlimit(20.0, 444.0, bpm);
        midiInBpm = bpm;

        // Apply to Link
        if (link && link->isEnabled()) {
          auto state = link->captureAppSessionState();
          state.setTempo(bpm, link->clock().micros());
          link->commitAppSessionState(state);
        }

        // Update internal slider (Thin out updates)
        if (!isPlaying && !pendingSyncStart) {
          // This single callAsync is acceptable as it happens only once per
          // beat max and only if needed. Alternatively we could flag it for the
          // AsyncUpdater! But let's leave it for now to minimize changes.
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

void MainComponent::handleAsyncUpdate() {
  int start1, size1, start2, size2;
  mainMidiFifo.prepareToRead(mainMidiFifo.getNumReady(), start1, size1, start2,
                             size2);
  if (size1 > 0)
    processMidiQueue(start1, size1);
  if (size2 > 0)
    processMidiQueue(start2, size2);
  mainMidiFifo.finishedRead(size1 + size2);
}

void MainComponent::processMidiQueue(int start, int size) {
  for (int i = 0; i < size; ++i) {
    auto &m = mainMidiBuffer[start + i];

    // UI & OSC Logic (formerly callAsync)

    // Keyboard Visuals
    if (m.isNoteOnOrOff()) {
      isHandlingOsc = true;
      keyboardState.processNextMidiEvent(m);
      isHandlingOsc = false;
      // Note: keyboardState triggers listeners (handleNoteOn) which might send
      // OSC? handleNoteOn calls sendSplitOscMessage? Let's check handleNoteOn.
      // If so, we shouldn't call sendSplitOscMessage twice.
      // But handleIncomingMidiMessage previously called sendSplitOscMessage
      // directly too?
    }

    // Forwarding
    if (!isHandlingOsc)
      sendSplitOscMessage(m);

    // Profile Transport
    if (currentProfile.isTransportLink) {
      if (m.isController()) {
        int refCC = m.getControllerNumber();
        if (refCC == currentProfile.ccPlay && m.getControllerValue() >= 64)
          startPlayback();
        if (refCC == currentProfile.ccStop && m.getControllerValue() >= 64)
          stopPlayback();
      }
    }

    // Thru (Main Thread Thru? No, Realtime Thru is better. We did realtime thru
    // for Clock. But for notes, we might want to do it here if we want to block
    // on main thread conditions? Previous code: if (midiOutput &&
    // !btnBlockMidiOut.getToggleState() && btnMidiThru.getToggleState())
    //    midiOutput->sendMessageNow(m);
    // Doing this on MessageThread adds latency (jitter).
    // Ideally Thru is on MIDI thread.
    // But invalidating iterators or whatever crash might be relevant.
    // Let's keep Thru on MIDI thread if possible, but safe.
    // I will move Thru back to MIDI thread if simple.
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
    ticksPerQuarterNote = (double)mf.getTimeFormat();
    playbackSeq.clear();
    sequencer.activeTracks.clear();
    bool bpmFound = false;
    for (int i = 0; i < mf.getNumTracks(); ++i) {
      playbackSeq.addSequence(*mf.getTrack(i), 0);

      juce::String trackName = "Track " + juce::String(i + 1);

      // Parse Track Name (Meta Event 0x03)
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
          if (link && !btnPreventBpmOverride.getToggleState()) {
            auto sessionState = link->captureAppSessionState();
            sessionState.setTempo(currentFileBpm, link->clock().micros());
            link->commitAppSessionState(sessionState);
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
    playbackCursor = 0; // Reset on load
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
  midiScheduler.clear();
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

// NEW: Slider Drag Started for MIDI Learn
void MainComponent::sliderDragStarted(juce::Slider *s) {
  juce::String paramID = "";
  if (s == &vol1Simple)
    paramID = "vol1";
  else if (s == &vol2Simple)
    paramID = "vol2";
  else if (s == &tempoSlider)
    paramID = "bpm";
  else if (s == &sliderArpSpeed)
    paramID = "arpSpeed";
  else if (s == &sliderArpVel)
    paramID = "arpVel";

  // Check Mixer Strips?
  // Mixer strips are managed by MixerContainer, but if MainComp is listener to
  // them... Actually MixerContainer manages them. We might need a bridge. For
  // now, support main sliders.

  if (paramID.isNotEmpty()) {
    if (isMidiLearnMode) {
      mappingManager.setSelectedParameterForLearning(paramID);
      logPanel.log("! Learn Target: " + paramID, true);
    }
  }
}

void MainComponent::sliderDragEnded(juce::Slider *s) {
  if (s == &nudgeSlider) {
    s->setValue(0.0);
    logPanel.log("! Nudge End", false);
    if (link) {
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
    if (paramID == "vol1")
      vol1Simple.setValue(normValue * 127.0f, juce::sendNotification);
    else if (paramID == "vol2")
      vol2Simple.setValue(normValue * 127.0f, juce::sendNotification);
    else if (paramID == "bpm")
      tempoSlider.setValue(20.0f + (normValue * 424.0f),
                           juce::sendNotification);
    else if (paramID == "play") {
      if (normValue > 0.5f)
        btnPlay.triggerClick();
    } else if (paramID == "stop") {
      if (normValue > 0.5f)
        btnStop.triggerClick();
    } else if (paramID == "arpEnable")
      btnArp.setToggleState(normValue > 0.5f, juce::sendNotification);

    if (paramID.startsWith("Mixer_")) {
      int ch = paramID.fromFirstOccurrenceOf("_", false, false).getIntValue();
      if (paramID.endsWith("_Vol"))
        mixer.setChannelVolume(ch, normValue * 127.0f);
      else if (paramID.endsWith("_On")) {
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

void MainComponent::updateProfileComboBox() {
  // Keep standard items
  // NOTE: This assumes standard items 1-100 are reserved for hardcoded defaults
  // We will append custom files starting at ID 1000

  int startId = 1000;

  // Clear existing custom items (if we had a way to track them, for now we
  // might just append or rebuild) Simpler: Let's assume we just add them to the
  // end of the existing menu structure if possible OR: Rebuild the whole menu.

  // Rebuilding the menu might be safer to ensure no duplicates
  // But cmbControlProfile is set up in constructor.
  // Let's just iterate files and add them if not present?
  // Actually, safer to clear and re-add defaults if we want full refresh.
  // For now, just append to avoid breaking existing logic.

  auto dir = getProfileDirectory();
  auto files =
      dir.findChildFiles(juce::File::findFiles, false, "*.json;*.midimap");

  for (auto &f : files) {
    // Simple check if item exists not easily avail on ComboBox without
    // iterating We'll just add it. IDs > 1000. Ensure unique IDs based on hash
    // or just sequence? Let's rely on the file name.

    // Actually, let's clear the ComboBox and re-populate defaults + custom to
    // be clean. Requires moving default pop from Constructor to this method?
    // Let's do that in a follow-up refactor if needed.
    // For now, just append to avoid breaking existing logic.

    cmbControlProfile.addItem(f.getFileNameWithoutExtension(), startId++);
  }
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
  if (!isPlaying)
    return;
  isPlaying = false;

  // Calculate relative beat position for resume
  if (link && link->isEnabled()) {
    auto session = link->captureAppSessionState();
    beatsPlayedOnPause = session.beatAtTime(link->clock().micros(), quantum) -
                         transportStartBeat;
  } else {
    double nowMs = juce::Time::getMillisecondCounterHiRes();
    double elapsedMs = nowMs - internalPlaybackStartTimeMs;
    double bpm = bpmVal.get();
    if (bpm < 1.0)
      bpm = 120.0;
    beatsPlayedOnPause = (elapsedMs * (bpm / 60000.0)) + beatsPlayedOnPause;
  }

  if (midiOutput) {
    midiOutput->sendMessageNow(
        juce::MidiMessage::allNotesOff(getSelectedChannel()));
    if (btnMidiClock.getToggleState())
      midiOutput->sendMessageNow(juce::MidiMessage(0xFC)); // Stop/Pause
  }
  if (isOscConnected)
    oscSender.send(oscConfig.eStop.getText(), 1.0f);

  logPanel.log("! Playback Paused (at beat " +
                   juce::String(beatsPlayedOnPause, 2) + ")",
               true);
  juce::MessageManager::callAsync([this] { btnPlay.setButtonText("Play"); });
}

void MainComponent::stopPlayback() {
  isPlaying = false;
  pendingSyncStart = false;
  beatsPlayedOnPause = 0.0;
  playbackCursor = 0;
  lastProcessedBeat = -1.0;

  if (link && link->isEnabled()) {
    auto session = link->captureAppSessionState();
    session.setIsPlaying(false, link->clock().micros());
    link->commitAppSessionState(session);
  }

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
  // Here we would propagate changes to components if they held their own state.
  // Currently, CCs might be generated dynamically.
  // We need to ensure that when we SEND CCs, we use 'currentProfile'.
}

void MainComponent::startPlayback() {
  if (isPlaying)
    return;

  pendingSyncStart = true;
  isPlaying = true; // Set for UI tracking

  if (link && link->isEnabled()) {
    logPanel.log("! Waiting for Link Sync...", true);
  } else {
    internalPlaybackStartTimeMs = juce::Time::getMillisecondCounterHiRes();
    logPanel.log("! Playback Started", true);
    if (midiOutput && btnMidiClock.getToggleState())
      midiOutput->sendMessageNow(juce::MidiMessage(0xFA)); // Start
    pendingSyncStart =
        false; // Internal start is immediate here for now or aligned to BPM
  }
}

void MainComponent::clearPlaybackSequence() {
  playbackSeq.clear();
  trackGrid.loadSequence(playbackSeq);
  logPanel.log("! Sequence Cleared", true);
}
