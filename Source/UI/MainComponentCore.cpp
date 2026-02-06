/*
  ==============================================================================
    Source/UI/MainComponentCore.cpp
    Role: Layout, resize, and audio lifecycle (split from MainComponent).
  ==============================================================================
*/
#include "UI/MainComponent.h"
#include "../Core/DebugLog.h"
#include "../Audio/AudioEngine.h"
#include "../Audio/CountInManager.h"
#include "../Audio/MidiRouter.h"
#include "../Audio/Metronome.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

// --- AUDIO LIFECYCLE ---
void MainComponent::prepareToPlay(int samplesPerBlockExpected,
                                  double sampleRate) {
  static bool firstPrepare = true;
  if (firstPrepare) {
    firstPrepare = false;
    DebugLog::debugLog("prepareToPlay() first call");
  }
  if (!context)
    return;
  if (context->engine)
    context->engine->prepareToPlay(sampleRate, samplesPerBlockExpected);
  if (context->midiRouter)
    context->midiRouter->prepareToPlay(sampleRate,
                                             samplesPerBlockExpected);
  if (context->metronome)
    context->metronome->prepare((float)sampleRate);
}

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  // Guard against shutdown or race: never dereference context from audio thread if null.
  if (!context) {
    bufferToFill.clearActiveBufferRegion();
    return;
  }
  if (context->audioWatchdog)
    context->audioWatchdog->pet();

  if (context->engine) {
    context->engine->driveAudioCallback(
        bufferToFill.numSamples,
        deviceManager.getAudioDeviceSetup().sampleRate);
  }

  if (context->midiRouter) {
    context->midiRouter->processAudioThreadEvents();
  }

  bufferToFill.clearActiveBufferRegion();

  double beat = 0.0, bpm = 120.0;
  if (context->engine) {
    beat = context->engine->getCurrentBeat();
    bpm = context->engine->getBpm();
  }
  if (context->metronome && context->metronome->isEnabled() &&
      context->engine) {
    auto *buf = bufferToFill.buffer;
    if (buf && bufferToFill.numSamples > 0)
      context->metronome->processBlock(*buf, bufferToFill.startSample,
                                       bufferToFill.numSamples, beat, bpm);
  }
  if (context->countInManager && context->engine)
    context->countInManager->process(beat, bpm);
}

void MainComponent::releaseResources() {}

bool MainComponent::isPlaying() const {
  return context && context->engine && context->engine->getIsPlaying();
}

// --- LAYOUT ---
void MainComponent::resized() {
  if (!headerPanel || !transportPanel)
    return;

  juce::Rectangle<int> area = getLocalBounds();
  // Always layout menu/transport/footer so they stay visible and attached (no early return for small window)
  area.setWidth(juce::jmax(1, area.getWidth()));
  area.setHeight(juce::jmax(1, area.getHeight()));

  if (isPlaying()) {
    const juce::ScopedLock sl(resizeLock);
    pendingResizeBounds = area;
    resizePending.store(true, std::memory_order_release);
    return;
  }

  isResizing = true;
  applyLayout(area);
  isResizing = false;
}

void MainComponent::applyLayout(juce::Rectangle<int> area) {

  // ==========================================
  // 0. Full-Screen Backgrounds
  // ==========================================
  dynamicBg.setBounds(area);

  // Layout menu bar (Dashboard: Undo/Redo/Link/indicator move to transport row; Reset BPM right of Tap)
  auto menuBarArea = area.withHeight(30).reduced(2);
  auto menuBar = menuBarArea;
  logoView.setBounds(menuBar.removeFromLeft(30));
  menuBar.removeFromLeft(4);
  btnMenu.setBounds(menuBar.removeFromLeft(108).reduced(1));
  menuBar.removeFromLeft(4);
  btnDash.setBounds(menuBar.removeFromLeft(90).reduced(1));
  menuBar.removeFromLeft(4);

  juce::Rectangle<int> resetBpmSlot;
  const bool isDashboard = (currentView == AppView::Dashboard);

  if (isDashboard) {
    // Dashboard: no Undo/Redo in menu bar; tempo, Tap, then BPM Reset (right of Tap)
    tempoSlider.setBounds(menuBar.removeFromLeft(100).reduced(1));
    btnTap.setBounds(menuBar.removeFromLeft(36).reduced(1));
    resetBpmSlot = menuBar.removeFromLeft(40).reduced(1);
    btnResetBpm.setBounds(resetBpmSlot);
    btnResetBpm.setVisible(true);
  } else {
    btnUndo.setBounds(menuBar.removeFromLeft(48).reduced(1));
    menuBar.removeFromLeft(2);
    btnRedo.setBounds(menuBar.removeFromLeft(48).reduced(1));
    menuBar.removeFromLeft(4);
    tempoSlider.setBounds(menuBar.removeFromLeft(100).reduced(1));
    btnTap.setBounds(menuBar.removeFromLeft(36).reduced(1));
    resetBpmSlot = menuBar.removeFromLeft(40).reduced(1);
    btnResetBpm.setBounds(resetBpmSlot);
    btnResetBpm.setVisible(true);
  }

  btnMidiLearn.setBounds(menuBar.removeFromRight(80).reduced(1));
  btnPanic.setBounds(menuBar.removeFromRight(60).reduced(1));
  btnExtSyncMenu.setBounds(menuBar.removeFromRight(52).reduced(1));
  btnThru.setBounds(menuBar.removeFromRight(48).reduced(1));
  if (!isDashboard) {
    linkIndicator.setBounds(menuBar.removeFromRight(88).reduced(2));
    btnLink.setBounds(menuBar.removeFromRight(40).reduced(1));
  }
  headerPanel->setVisible(false);
  headerPanel->setBounds(0, 0, 0, 0);

  area.removeFromTop(30);

  // Overlays (full-area so holes work; holes need menu bar + learn button laid out)
  if (midiLearnOverlay) {
    auto overlayArea = getLocalBounds(); // Cover full area so learn button hole works
    midiLearnOverlay->setBounds(overlayArea);
    if (midiLearnOverlay->isVisible()) {
      auto logBounds =
          winLog ? winLog->getScreenBounds().translated(
                       -getScreenPosition().getX(), -getScreenPosition().getY())
                 : juce::Rectangle<int>();
      midiLearnOverlay->updateHoles(logBounds, btnMidiLearn.getBounds());
    }
  }

  if (setupWizard.isVisible())
    setupWizard.setBounds(getLocalBounds());
  if (layoutChoiceWizard.isVisible())
    layoutChoiceWizard.setBounds(getLocalBounds());

  // ==========================================
  // 3. VIEW SWITCHING (Non-Dashboard overlays)
  // ==========================================
  if (currentView != AppView::Dashboard) {
    // Config/Control replace the whole window; viewport covers full area so nothing shows behind
    juce::Rectangle<int> fullBounds = getLocalBounds();
    configViewport.setVisible(true);
    configViewport.setBounds(fullBounds);
    configViewport.toFront(true);  // Repaint parent so Config is visibly on top of module windows

    if (configPanel)
      configPanel->setSize(fullBounds.getWidth() - 20, 1200);

    transportPanel->setVisible(false);
    // Keep status bar at bottom so footer stays attached
    const int statusH = 24;
    statusBar.setBounds(0, fullBounds.getHeight() - statusH, fullBounds.getWidth(), statusH);
    statusBar.toFront(false);
    // Keep menu bar and nav buttons on top so user can click Dashboard to return
    logoView.toFront(false);
    btnMenu.toFront(false);
    btnUndo.toFront(false);
    btnRedo.toFront(false);
    tempoSlider.toFront(false);
    btnTap.toFront(false);
    btnResetBpm.toFront(false);
    btnLink.toFront(false);
    linkIndicator.toFront(false);
    btnDash.toFront(false);
    btnThru.toFront(false);
    btnExtSyncMenu.toFront(false);
    btnPanic.toFront(false);
    btnMidiLearn.toFront(false);
    return;
  }

  // ==========================================
  // DASHBOARD LAYOUT
  // ==========================================
  configViewport.setVisible(false);
  transportPanel->setVisible(true);

  // Do not force module visibility — Full/Minimal/saved layout already set which windows are visible; switching views must not change layout

  // ==========================================
  // 4. TRANSPORT (Compact: 35px). Undo/Redo/Link/indicator in this row; Reset BPM in menu bar.
  // ==========================================
  transportPanel->setExternalTransportRefs(&btnUndo, &btnRedo, &btnLink, &linkIndicator);
  transportPanel->setBounds(area.removeFromTop(35).reduced(0, 2));
  transportPanel->btnResetBpm.setVisible(false);  // BPM Reset is in menu bar (btnResetBpm)
  btnUndo.toFront(false);
  btnRedo.toFront(false);
  btnLink.toFront(false);
  linkIndicator.toFront(false);

  // ==========================================
  // 5. STATUS BAR (Bottom 20px)
  // ==========================================
  statusBar.setBounds(area.removeFromBottom(24));

  // ==========================================
  // 6. MODULE WINDOWS — Positions managed by user dragging.
  //    No forced layout here. Windows keep their user-set positions.
  //    Only constrain if a window is completely off-screen.
  // ==========================================

  // ==========================================
  // 7. DIAGNOSTIC OVERLAY
  // ==========================================
  if (diagOverlay && diagOverlay->isVisible()) {
    auto editorBounds = winEditor ? winEditor->getBounds() : getLocalBounds();
    diagOverlay->setBounds(editorBounds.getRight() - 160,
                           editorBounds.getY() + 10, 150, 80);
  }

  // Ensure overlays stay on top
  setupWizard.toFront(false);
  layoutChoiceWizard.toFront(false);
  if (midiLearnOverlay)
    midiLearnOverlay->toFront(false);

  isResizing = false;
}
