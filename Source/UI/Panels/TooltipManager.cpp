/*
  ==============================================================================
    Source/Components/TooltipManager.cpp
    Status: FIXED (Added necessary panel includes for unique_ptr access)
  ==============================================================================
*/
#include "TooltipManager.h"
#include "../MainComponent.h"
#include "../RenderBackend.h"
#include "ConfigPanel.h"
#include "ConfigControls.h"
#include "PerformancePanel.h"
#include "ArpeggiatorPanel.h"
#include "TransportPanel.h"
#include "HeaderPanel.h"
#include "TrafficMonitor.h"
#include "MidiPlaylist.h"
#include "MixerPanel.h"
#include "SequencerPanel.h"
#include "LfoGeneratorPanel.h"

void TooltipManager::setupAllTooltips(MainComponent &main) {
  setupNavigationTooltips(main);
  setupTransportTooltips(main);
  setupHeaderTooltips(main);
  setupMidiIOTooltips(main);
  setupOscTooltips(main);
  setupLinkTooltips(main);
  setupRenderAndThreadingTooltips(main);
  setupPerformanceTooltips(main);
  setupSequencerTooltips(main);
  setupControlTooltips(main);
  setupMixerTooltips(main);
  setupArpTooltips(main);
  setupKeyboardTooltips(main);
  setupLfoGeneratorTooltips(main);
  setupTrafficMonitorTooltips(main);
  setupPlaylistTooltips(main);
}

void TooltipManager::setupNavigationTooltips(MainComponent &main) {
  main.btnDash.setTooltip("Switch to Config (network, MIDI, help) or back to Dashboard");
  main.btnPanic.setTooltip("Send All Notes Off and All Sound Off");
  main.btnMenu.setTooltip("Menu: Network, MIDI, Modules, Layout (Load Minimal/Full, Reset to default layout), Reset to defaults, Help");
  main.btnMidiLearn.setTooltip("MIDI Learn: map hardware controls to parameters");
  main.btnUndo.setTooltip("Undo last edit (Ctrl+Z).");
  main.btnRedo.setTooltip("Redo (Ctrl+Y).");
  main.getStatusBar().setTooltip("Status and UI zoom. Drag zoom slider or click value to type.");
}

void TooltipManager::setupTransportTooltips(MainComponent &main) {
  main.tempoSlider.setTooltip("Global tempo (BPM). Drag or double-click to type. Affects all synced output.");
  main.btnTap.setTooltip("Tap tempo: tap 4 times to set BPM from your rhythm.");
  main.linkIndicator.setTooltip("Ableton Link: shows sync status. Peers listed in OSC Log.");
  main.lblBpm.setTooltip("Current BPM. Shows EXT or LINK when synced to external clock.");
  if (main.transportPanel) {
    auto &t = *main.transportPanel;
    t.btnPlay.setTooltip("Play: start playback from current position. Pause keeps position.");
    t.btnStop.setTooltip("Stop: stop playback and return to start of track.");
    t.btnPrev.setTooltip("Previous: go to previous track in playlist.");
    t.btnSkip.setTooltip("Next: go to next track in playlist.");
    t.btnOctaveMinus.setTooltip("Octave down. Transposes keyboard, sequencer, and OSC/MIDI output by -12 semitones.");
    t.btnOctavePlus.setTooltip("Octave up. Transposes keyboard, sequencer, and OSC/MIDI output by +12 semitones.");
    t.btnQuantize.setTooltip("Quantize transport: start/stop on beat when Link is active.");
    t.btnReset.setTooltip("Reset: clear current track and grids (does not clear playlist).");
    t.btnBlock.setTooltip("Block: prevent MIDI output (no notes or CC sent).");
    t.btnSnapshot.setTooltip("SNAP: copy current MIDI-to-OSC mappings to clipboard.");
    t.btnSplit.setTooltip("Split: send notes 0–64 on one channel, 65–127 on another. Right-click for zone menu.");
    t.btnMetronome.setTooltip("Metronome: toggle click track. Right-click for options.");
    t.btnResetBpm.setTooltip("Reset BPM to file default or 120.");
  }
}

void TooltipManager::setupMidiIOTooltips(MainComponent &main) {
  if (main.configPanel) {
    auto &c = *main.configPanel;
    // MIDI I/O
    c.btnMidiIn.setTooltip("Select MIDI Input Device or Virtual Keyboard");
    c.btnMidiOut.setTooltip("Select MIDI Output Device");
    c.cmbMidiCh.setTooltip("Select MIDI Channel for Output (1-16, or All)");
    c.btnThru.setTooltip("Enable MIDI Thru (Echo input directly to output)");
    c.btnClock.setTooltip("Send MIDI Clock (F8) pulses to MIDI Out");
    c.btnBlockMidiOut.setTooltip("Block MIDI output from playback");
    c.btnSplit.setTooltip("Split MIDI: Notes below C4 -> Ch2, C4+ -> Ch1");
    c.btnMidiScaling.setTooltip("Toggle MIDI value scaling: 0-127 or 0.0-1.0");

    // Theme & Profiles
    c.cmbTheme.setTooltip("Select visual theme for the application");
    c.cmbMidiMap.setTooltip("Select saved MIDI controller mapping");
    c.btnImportMap.setTooltip("Import MIDI mappings from a JSON file");
    c.btnExportMap.setTooltip("Export current MIDI mappings to a JSON file");
    c.btnResetMaps.setTooltip("Clear all MIDI learn mappings");
    c.cmbCtrlProfile.setTooltip("Select a saved controller profile");
    c.btnSaveProfile.setTooltip("Save current settings as a controller profile");
    c.btnLoadProfile.setTooltip("Load settings from selected profile");
    c.btnDeleteProfile.setTooltip("Delete the selected controller profile");

    c.btnDirectInput.setTooltip("Direct MIDI input path for lowest latency (recommended for live play).");
    c.btnPerformanceMode.setTooltip("Enable jitter filtering and 12-core performance mode for timing.");

    c.btnRtpDriver.setTooltip("Use OS RTP-MIDI driver (e.g. rtpMIDI on Windows).");
    c.btnRtpInternal.setTooltip("Use built-in RTP-MIDI server.");
    c.btnRtpOff.setTooltip("Disable network MIDI (RTP).");

    c.sliderLfoFreq.setTooltip("LFO rate (Hz). Modulates patched controls.");
    c.sliderLfoDepth.setTooltip("LFO depth (0–1).");
    c.cmbLfoWave.setTooltip("LFO shape: Sine, Triangle, Saw, Square, Random.");

    // Mixer controls
    c.btnResetMixerOnLoad.setTooltip(
        "Reset mixer channels when loading a MIDI file");
    c.btnResetMixer.setTooltip(
        "Reset all mixer channel names and values to default");
    c.btnForceGrid.setTooltip("Force notes to snap to grid timing");
    c.btnNoteQuantize.setTooltip("Quantize note timing to Link beat grid");
  }
}

void TooltipManager::setupArpTooltips(MainComponent &main) {
  if (main.arpPanel) {
    auto &a = *main.arpPanel;
    a.cmbArpPattern.setTooltip(
        "Arp pattern. Sends /arppattern. Up/Down/Random/Chord etc.");
    a.knobArpSpeed.setTooltip("Rate (1–32). Higher = faster arpeggiated notes.");
    a.knobArpVel.setTooltip("Velocity (0–127). Output note velocity.");
    a.knobArpGate.setTooltip("Gate (0.1–1). Note length per step. Shorter = staccato.");
    a.sliderArpOctave.setTooltip("Octave range (1–4). How many octaves to arpeggiate.");
    a.btnArpSync.setTooltip("Sync arp to Link tempo. Sends /arpsync.");
    a.btnArpLatch.setTooltip("Latch: hold notes after release. Sends /arplatch.");
    a.btnBlockBpm.setTooltip("Lock BPM: when synced, prevent arp from changing project tempo.");
  }
}

void TooltipManager::setupOscTooltips(MainComponent &main) {
  if (main.configPanel) {
    auto &c = *main.configPanel;
    c.edIp.setTooltip("OSC destination IP (e.g. 127.0.0.1). Check this if OSC won't connect.");
    c.edPOut.setTooltip("OSC output port. Must match the receiving app's port.");
    c.edPIn.setTooltip("OSC input port. Patchworld or other apps send to this port.");
    c.btnConnect.setTooltip("Connect or disconnect OSC. If connection fails, check IP and both ports.");
    c.btnZeroConfig.setTooltip(
        "Enable Zero Configuration networking (auto-discovery)");
    c.btnMulticast.setTooltip("Enable multicast for OSC broadcast");
    c.btnOscAdvanced.setTooltip("Show/hide advanced OSC address configuration");
    c.btnIPv6.setTooltip("Use IPv6 for OSC (if your network uses IPv6).");
    c.btnLowLatency.setTooltip("Bypass lookahead buffer for minimum latency (may increase jitter).");
  }
}

void TooltipManager::setupLinkTooltips(MainComponent &main) {
  if (main.configPanel) {
    auto &c = *main.configPanel;
    c.sliderLatency.setTooltip("Latency Compensation (ms)");
    c.btnCalibrate.setTooltip(
        "Measure RTT via MIDI loopback. Connect MIDI Out to MIDI In, then click.");
    c.sliderClockOffset.setTooltip("MIDI Clock Offset (ms)");
    c.btnLockBpm.setTooltip("Prevent MIDI files from changing tempo");
    c.btnLinkEnable.setTooltip("Enable/Disable Ableton Link");
    c.cmbQuantum.setTooltip("Link Quantum (Beats per bar: 2, 4, 8, 16)");
  }
}

void TooltipManager::setupRenderAndThreadingTooltips(MainComponent &main) {
  if (!main.configPanel)
    return;
  auto &c = *main.configPanel;
  auto caps = RenderBackend::detectCapabilities();
  juce::String renderTip = "Eco: GPU 30fps. Pro: GPU 60fps+. Software: no GPU (all platforms).";
  if (caps.supportsVulkan || caps.supportsMetal)
    renderTip += " Vulkan/Metal detected for future use.";
  c.cmbRenderMode.setTooltip(renderTip);
  c.cmbThreadingMode.setTooltip(
      "Single: one worker thread. Multi-core: fixed workers. Adaptive: auto-detect cores.");
  c.lblRenderMode.setTooltip(renderTip);
  c.lblGpuBackend.setTooltip(
      "Preferred GPU API. OpenGL is used for rendering; Vulkan/Metal shown when available for future use.");
  c.cmbGpuBackend.setTooltip(
      "Preferred GPU backend. Current implementation uses OpenGL on all platforms.");
}

void TooltipManager::setupLfoGeneratorTooltips(MainComponent &main) {
  main.lfoGeneratorPanel.setupTooltips();
}

void TooltipManager::setupKeyboardTooltips(MainComponent &main) {
  if (main.performancePanel) {
    auto &p = *main.performancePanel;
    // Use SettableTooltipClient for components that support setTooltip
    if (auto *hk = dynamic_cast<juce::SettableTooltipClient *>(&p.horizontalKeyboard))
      hk->setTooltip(
          "Virtual keyboard. Sends /ch1note, /ch1noteoff. Use Oct +/- for shift.");
    if (auto *vk = dynamic_cast<juce::SettableTooltipClient *>(&p.verticalKeyboard))
      vk->setTooltip("Vertical keyboard. Sends /ch1note.");
    p.pitchWheel.setTooltip("Pitch bend. Sends /ch1pitch (0-16383).");
    p.modWheel.setTooltip("Mod wheel. Sends CC 1. Address: /ch1cc.");
  }
}

void TooltipManager::setupMixerTooltips(MainComponent &main) {
  auto *ctx = main.getContext();
  if (ctx && ctx->mixer) {
    auto &m = *ctx->mixer;
    // Reset CH: right-click top bar of mixer for "Reset CH (reset strip order to default)"
    for (auto *strip : m.strips) {
      if (strip) {
        strip->btnActive.setTooltip("Mute Channel");
        strip->btnSolo.setTooltip("Solo (Ctrl+Click = Exclusive)");
      }
    }
  }
}

void TooltipManager::setupPerformanceTooltips(MainComponent &main) {
  if (main.performancePanel) {
    auto &p = *main.performancePanel;
    p.btnViewMode.setTooltip(
        "Play: Falling notes view (keyboard bottom). Chord name shown from held notes.\n"
        "Edit: Piano roll editor (keyboard left)");
    p.spliceEditor.getVelocityLane().setTooltip(
        "Velocity lane. Drag vertically over notes to set velocity (0-1).");
    if (auto *tl = dynamic_cast<juce::SettableTooltipClient *>(&p.timeline))
      tl->setTooltip("Timeline. Drag to scrub during playback; with Link, seek applies at next beat/bar.");
    p.playView.setTooltip("Falling notes (Play mode). Scroll speed follows BPM. Ctrl+wheel to zoom (out = more keys and time visible, in = larger keys).");
  }
}

void TooltipManager::setupSequencerTooltips(MainComponent &main) {
  auto *ctx = main.getContext();
  if (ctx && ctx->sequencer) {
    auto &s = *ctx->sequencer;
    s.stepGrid.setTooltip("Step grid. Click to toggle note; drag up/down for velocity; Shift+click to set note; right-click for menu (note, probability, clear).");
    s.cmbTimeSig.setTooltip("Time signature (4/4, 3/4, 5/4).");
    s.cmbSteps.setTooltip("Number of steps (4–64).");
    s.cmbSeqOutCh.setTooltip("MIDI output channel for sequencer (1–16).");
    s.cmbMode.setTooltip("Mode: Roll (step div), Time (signature), Loop (loop length), Chord.");
    s.cmbChordType.setTooltip("Chord preset when Mode is Chord.");
    s.cmbPattern.setTooltip("Pattern bank A–H. Each bank stores separate step data.");
    for (int i = 0; i < s.patternButtons.size(); ++i) {
      auto *b = s.patternButtons.getUnchecked(i);
      b->setTooltip("Pattern " + juce::String(static_cast<char>('A' + i)) + ". Switch bank to edit.");
    }
    for (int i = 0; i < s.rollButtons.size(); ++i) {
      auto *b = s.rollButtons.getUnchecked(i);
      int div = (i == 0) ? 4 : (i == 1) ? 8 : (i == 2) ? 16 : 32;
      b->setTooltip("1/" + juce::String(div) + " – Roll: step division; Time: denominator; Loop: steps.");
    }
    s.probSlider.setTooltip("Default step probability (0–100%). Right-click a step for per-step probability.");
    s.btnRec.setTooltip("Record: capture notes from keyboard to steps.");
    s.btnForceGrid.setTooltip("Force strict grid (no swing) when recording.");
    s.btnExport.setTooltip("Export sequence as .mid file.");
    s.btnClearAll.setTooltip("Clear all steps (notes and velocity). Asks for confirmation.");
    s.btnRandom.setTooltip("Randomize current page (scale-based notes, ~30% fill).");
    s.btnEuclid.setTooltip("Euclidean rhythm: fill steps by pattern (pulses, steps, rotation). Opens dialog.");
    s.btnCopy.setTooltip("Copy current page (16 steps) to clipboard.");
    s.btnPaste.setTooltip("Paste from clipboard into current page.");
    s.swingSlider.setTooltip("Swing amount (0–100%). Delays every other step for shuffle feel.");
    s.btnPage.setTooltip("Next page of steps (when 16+ steps). Shows current page number.");
  }
}

void TooltipManager::setupControlTooltips(MainComponent &main) {
  if (main.controlPage) {
    auto &c = *main.controlPage;
    if (auto *xy = dynamic_cast<juce::SettableTooltipClient *>(&c.xyPad))
      xy->setTooltip(
          "X/Y pad. X=CC 74 (Filter), Y=CC 1 (Mod). Sends /ch1cc.");
    c.morphSlider.setTooltip("Morph slider. Sends /morph (0-1).");
    for (int i = 0; i < c.controls.size(); ++i) {
      auto *ctrl = c.controls[i];
      juce::String addr = ctrl->addrBox.getText();
      if (auto *tc = dynamic_cast<juce::SettableTooltipClient *>(ctrl))
        tc->setTooltip("Control " + juce::String(i + 1) + ". Address: " +
                       addr + ". Sends float 0-1.");
    }
  }
}

void TooltipManager::setupHeaderTooltips(MainComponent &main) {
  if (main.headerPanel) {
    main.headerPanel->btnModules.setTooltip("Toggle module windows.");
  }
  if (main.networkConfigPanel) {
    main.networkConfigPanel->edIp.setTooltip("OSC target IP (e.g. 127.0.0.1).");
    main.networkConfigPanel->edPortOut.setTooltip("OSC output port.");
    main.networkConfigPanel->edPortIn.setTooltip("OSC input port.");
    main.networkConfigPanel->btnConnect.setTooltip("Connect/disconnect OSC.");
  }
}

void TooltipManager::setupTrafficMonitorTooltips(MainComponent &main) {
  if (main.logPanel) {
    auto &l = *main.logPanel;
    // signalLegend: no setTooltip (juce::Component has no setTooltip in this JUCE build)
    l.logDisplay.setTooltip("OSC and MIDI message log.");
    l.btnPause.setTooltip("Pause/Resume log scrolling.");
    l.btnClear.setTooltip("Clear log.");
    l.lblPeers.setTooltip("Link peers count (Ableton Link).");
    l.lblLatency.setTooltip("Round-trip latency (ms).");
  }
}

void TooltipManager::setupPlaylistTooltips(MainComponent &main) {
  if (main.playlist) {
    auto &p = *main.playlist;
    p.btnLoopMode.setTooltip(
        "Loop: Off / One / All. Controls .mid playback.");
    p.btnRandom.setTooltip("Random shuffle (no repeats).");
    p.btnClearPlaylist.setTooltip("Clear playlist.");
    p.tree.setTooltip("Click to select, double-click to load .mid. Right-click for folder menu.");
  }
}