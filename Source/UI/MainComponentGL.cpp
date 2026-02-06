/*
  ==============================================================================
    Source/UI/MainComponentGL.cpp
    Role: OpenGL lifecycle and render mode switching (split from MainComponent).
  ==============================================================================
*/
#include "../Core/DebugLog.h"
#include "UI/MainComponent.h"
#include "UI/RenderBackend.h"
#include "UI/RenderConfig.h"
#include "UI/Theme.h"

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

// --- OpenGL lifecycle ---
void MainComponent::newOpenGLContextCreated() {
  try {
    DebugLog::debugLog("newOpenGLContextCreated() start");
    glContextManager.markReady();
    isGpuAvailable.store(true, std::memory_order_release);
    crtBackground.init(openGLContext);
    DebugLog::debugLog("newOpenGLContextCreated: crtBackground init OK");
    meterBarRenderer.init(openGLContext);
    DebugLog::debugLog("newOpenGLContextCreated: meterBarRenderer init OK");
    if (context && context->mixer)
      context->mixer->setGpuMetersActive(true);
    if (performancePanel) {
      performancePanel->trackGrid.initGL(openGLContext);
      DebugLog::debugLog("newOpenGLContextCreated: trackGrid initGL OK");
      performancePanel->spliceEditor.initGL(openGLContext);
      performancePanel->spliceEditor.setGpuNotesActive(true);
    }
    DebugLog::debugLog("newOpenGLContextCreated() done");
  } catch (const std::exception &e) {
    DebugLog::debugLog(juce::String("newOpenGLContextCreated exception: ") +
                       e.what());
    isGpuAvailable.store(false, std::memory_order_release);
    glContextManager.markLost();
  } catch (...) {
    DebugLog::debugLog("newOpenGLContextCreated exception: unknown");
    isGpuAvailable.store(false, std::memory_order_release);
    glContextManager.markLost();
  }
}

void MainComponent::renderOpenGL() {
  static bool firstRender = true;
  if (firstRender) {
    firstRender = false;
    DebugLog::debugLog("renderOpenGL() first call");
  }
  using namespace juce::gl;
  // Always clear first to avoid ghosting from stale buffer (no early return
  // before clear)
  juce::Colour c = Theme::bgDark;
  glClearColor(c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue(), 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  if (!context || !sysController)
    return;
  if (getWidth() < 50 || getHeight() < 50)
    return;

  try {
    if (currentRenderMode == 1) {
      glDisable(GL_BLEND);
      return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // CRT and background animation only for animated themes (10–13)
    if (ThemeManager::isAnimatedTheme(Theme::currentThemeId) &&
        crtBackground.isShaderValid()) {
      float time = (float)juce::Time::getMillisecondCounter() / 1000.0f;
      crtBackground.render(openGLContext, getWidth(), getHeight(), time,
                           Theme::currentThemeId);
    }

    int viewW = getWidth();
    int viewH = getHeight();

    // GUARD: Only render mixer meters if mixer is visible AND effectively
    // joined (not in a separate window) In multi-window mode, winMixer wraps
    // the context->mixer. If winMixer is visible, it means the content is
    // hosted there. We should only draw meters here if this MainComponent is
    // responsible for them. However, MainComponent IS the background. If mixer
    // is floating, it draws itself. If we are in single window mode, mixer
    // might be a child of MainComponent.
    if (context && context->mixer && context->mixer->isVisible()) {
      bool shouldDrawMeters = false;
      if (auto *p = context->mixer->getParentComponent()) {
        // If mixer is directly on us, or on a viewport/container owned by us
        if (p == this || p->getParentComponent() == this)
          shouldDrawMeters = true;
      }

      if (shouldDrawMeters) {
        auto levels = context->mixer->getMeterLevels();
        if (!levels.empty()) {
          meterBarRenderer.setLevels(levels);
          juce::Rectangle<int> meterBounds =
              context->mixer->getMeterAreaBounds();
          if (!meterBounds.isEmpty()) {
            juce::Point<int> topLeft =
                getLocalPoint(context->mixer.get(), meterBounds.getTopLeft());
            meterBarRenderer.render(openGLContext, viewW, viewH, topLeft.x,
                                    topLeft.y, meterBounds.getWidth(),
                                    meterBounds.getHeight());
          }
        }
      }
    }

    // GUARD: TrackGrid / SpliceEditor
    if (performancePanel && performancePanel->isVisible()) {
      bool shouldDrawPerf = false;
      if (auto *p = performancePanel->getParentComponent()) {
        if (p == this ||
            p->getParentComponent() ==
                this) // Simplify: only if docked in Main or direct child
          shouldDrawPerf = true;
      }

      if (shouldDrawPerf) {
        if (performancePanel->trackGrid.isVisible() &&
            performancePanel->trackGrid.hasGLContent()) {
          performancePanel->trackGrid.renderGL(openGLContext);
        }

        if (performancePanel->getViewMode() ==
                PerformancePanel::ViewMode::Edit &&
            performancePanel->spliceEditor.isVisible() &&
            performancePanel->spliceEditor.hasGLContent()) {
          juce::Point<int> tl = getLocalPoint(&performancePanel->spliceEditor,
                                              juce::Point<int>(0, 0));
          int vw = performancePanel->spliceEditor.getWidth();
          int vh = performancePanel->spliceEditor.getHeight();
          if (vw > 0 && vh > 0) {
            performancePanel->spliceEditor.renderGL(openGLContext, viewW, viewH,
                                                    tl.x, tl.y, vw, vh);
          }
        }
      }
    }

    glDisable(GL_BLEND);
  } catch (const std::exception &e) {
    DebugLog::debugLog(juce::String("renderOpenGL exception: ") + e.what());
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  } catch (...) {
    DebugLog::debugLog("renderOpenGL exception: unknown");
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}

void MainComponent::openGLContextClosing() {
  glContextManager.markLost();
  crtBackground.releaseResources();
  meterBarRenderer.release(openGLContext);
  if (context && context->mixer)
    context->mixer->setGpuMetersActive(false);
  if (performancePanel) {
    performancePanel->spliceEditor.setGpuNotesActive(false);
    performancePanel->spliceEditor.releaseGL(openGLContext);
    performancePanel->trackGrid.releaseGL(openGLContext);
  }
  isGpuAvailable.store(false, std::memory_order_release);
  juce::Component::SafePointer<MainComponent> safeThis(this);
  juce::MessageManager::callAsync([safeThis]() {
    if (!safeThis)
      return;
    safeThis->showGpuUnavailableMessage.store(true, std::memory_order_release);
    if (safeThis->context) {
      safeThis->context->log("GPU context lost; using software rendering.",
                             true);
      safeThis->context->appState.setRenderMode(3);
    }
    safeThis->handleRenderModeChange(
        3); // Always switch to software so UI remains usable
    if (safeThis->configPanel)
      safeThis->configPanel->syncRenderModeTo(3);
  });
}

void MainComponent::handleRenderModeChange(int mode) {
  if (mode < 1 || mode > 4)
    return;

  RenderConfig::RenderMode rmode = RenderConfig::OpenGL_Eco;
  int effectiveMode = mode;
  if (mode == 4) {
    rmode = RenderConfig::resolveAuto();
    effectiveMode = (rmode == RenderConfig::Software) ? 3 : 1;
  } else if (mode == 2) {
    rmode = RenderConfig::OpenGL_Perf;
  } else if (mode == 3) {
    rmode = RenderConfig::Software;
  }

  vBlankAttachment.reset();
  auto &animator = juce::Desktop::getInstance().getAnimator();
  animator.cancelAllAnimations(true);

  struct ModuleState {
    ModuleWindow *window = nullptr;
    juce::Rectangle<int> bounds;
    bool visible = false;
    bool folded = false;
    int unfoldedH = 200;
    bool hadFocus = false;
  };

  std::array<ModuleState, 10> moduleStates;
  std::array<ModuleWindow *, 10> modules = {
      winEditor.get(), winMixer.get(),  winSequencer.get(), winPlaylist.get(),
      winLog.get(),    winArp.get(),    winMacros.get(),    winChords.get(),
      winLfoGen.get(), winControl.get()};

  for (size_t i = 0; i < modules.size(); ++i) {
    if (auto *w = modules[i]) {
      moduleStates[i].window = w;
      moduleStates[i].bounds = w->getBounds();
      moduleStates[i].visible = w->isVisible();
      moduleStates[i].folded = w->isFolded;
      moduleStates[i].unfoldedH = w->unfoldedHeight;
      moduleStates[i].hadFocus = w->hasKeyboardFocus(true);
      w->setVisible(false);
      w->setBufferedToImage(false);
    }
  }

  // Only one GPU backend may be attached at a time. Tear down both, then attach
  // the chosen one.
  if (openGLContext.isAttached()) {
    openGLContext.executeOnGLThread(
        [](juce::OpenGLContext &ctx) {
          juce::ignoreUnused(ctx);
          juce::gl::glFinish();
          juce::gl::glFlush();
        },
        true);
    openGLContext.setContinuousRepainting(false);
    openGLContext.detach();
    openGLContext.detach();
    // sleep(50) removed: detach() is blocking and sufficient.
  }
#if PATCHWORLD_VULKAN_SUPPORT
  if (vulkanContext && vulkanContext->isAttached()) {
    vulkanContext->detach();
    isGpuAvailable.store(false, std::memory_order_release);
  }
#endif

  const bool useVulkan =
#if PATCHWORLD_VULKAN_SUPPORT
      (rmode != RenderConfig::Software) &&
      (RenderBackend::getCurrentBackend() == RenderBackend::Type::Vulkan) &&
      RenderBackend::isBackendImplemented(RenderBackend::Type::Vulkan);
#else
      false;
#endif

  // GUARD: Prevent overlapping switches?
  // We rely on MainComponent::handleRenderModeChange being called on the
  // Message Thread.

  if (rmode == RenderConfig::Software || !useVulkan) {
    if (vBlankAttachment)
      vBlankAttachment.reset(); // Stop vblank before config change
    RenderConfig::setMode(*this, openGLContext, rmode);
    bool nowSoftware = (rmode == RenderConfig::Software);
    isGpuAvailable.store(!nowSoftware, std::memory_order_release);
    if (!nowSoftware)
      showGpuUnavailableMessage.store(false, std::memory_order_release);
    currentRenderMode = (effectiveMode == 2) ? 0 : 1;
    if (nowSoftware) {
      showGpuUnavailableMessage.store(false, std::memory_order_release);
      repaint();
      if (auto *p = getParentComponent())
        p->repaint();
    }

    // Notify Listeners (e.g. ConfigPanel) to sync their UI
    if (onRenderModeChangedInternal)
      onRenderModeChangedInternal(effectiveMode);

  } else {
#if PATCHWORLD_VULKAN_SUPPORT
    setBufferedToImage(false);
    if (!vulkanContext)
      vulkanContext = std::make_unique<VulkanContext>();
    if (vulkanContext) {
      vulkanContext->setOnDeviceLost([this]() {
        if (context) {
          context->log("Vulkan device lost; using software rendering.", true);
          context->appState.setRenderMode(3);
        }
        handleRenderModeChange(3);
        if (configPanel)
          configPanel->syncRenderModeTo(3);
      });
    }
    if (!vulkanContext->attachTo(*this)) {
      if (context) {
        context->log("Vulkan: " + vulkanContext->getLastError(), true);
        context->appState.setGpuBackend("OpenGL");
      }
      RenderBackend::setCurrentBackend(RenderBackend::Type::OpenGL);
      RenderConfig::setMode(*this, openGLContext, rmode);
      isGpuAvailable.store(rmode != RenderConfig::Software,
                           std::memory_order_release);
      if (configPanel)
        configPanel->syncGpuBackendTo("OpenGL");
    } else {
      isGpuAvailable.store(true, std::memory_order_release);
      showGpuUnavailableMessage.store(false, std::memory_order_release);
    }
    currentRenderMode = (effectiveMode == 2) ? 0 : 1;
#endif
  }

  for (const auto &state : moduleStates) {
    if (auto *w = state.window) {
      w->setBounds(state.bounds);
      w->unfoldedHeight = state.unfoldedH;
      if (state.folded != w->isFolded)
        w->toggleFold();
      w->setVisible(state.visible);
      w->setBufferedToImage(false);
      w->repaint();
    }
  }
  setupComponentCaching();
  int appliedComboId =
      (rmode == RenderConfig::Software) ? 3 : (effectiveMode == 2 ? 2 : 1);
  if (configPanel)
    configPanel->syncRenderModeTo(appliedComboId);

  // Force first frame to draw and keep switching consistent: mark dashboard
  // dirty and flush so vblank doesn't skip; then trigger the appropriate
  // repaint for the new mode.
  if (context) {
    context->repaintCoordinator.markDirty(RepaintCoordinator::Dashboard);
    context->repaintCoordinator.flush(
        [this](uint32_t dirtyBits) { repaintDirtyRegions(dirtyBits); });
  }
  repaint();
  if (openGLContext.isAttached())
    openGLContext.triggerRepaint();
  else if (getParentComponent())
    getParentComponent()->repaint();

  if (context)
    context->isHighPerformanceMode.store(currentRenderMode == 0);

  juce::Component::SafePointer<MainComponent> safeThis(this);
  juce::Timer::callAfterDelay(150, [safeThis]() {
    if (!safeThis)
      return;
    auto *self = safeThis.getComponent();

    std::function<void()> vblankCallback = [self]() {
      if (!self)
        return;
      self->flushPendingResize();

      bool isPlaying = self->context && self->context->engine &&
                       self->context->engine->getIsPlaying();
      bool hasVisuals = self->dynamicBg.hasActiveParticles();
      auto *mouseSrc = juce::Desktop::getInstance().getMouseSource(0);
      bool mouseActive = mouseSrc && mouseSrc->isDragging();

      // MIDI clock is unaffected by throttling (ClockWorker runs on its own
      // thread). Throttle repaints when idle (enter low-power after 30 frames
      // focused, 10 when backgrounded)
      bool windowFocused = true;
      if (auto *rw = self->findParentComponentOfClass<juce::ResizableWindow>())
        windowFocused = rw->hasKeyboardFocus(true) || rw->isActiveWindow();
      static int idleFrames = 0;
      if (!isPlaying && !hasVisuals && !mouseActive) {
        int idleThreshold = windowFocused ? 30 : 10;
        if (++idleFrames < idleThreshold) {
          if (self->vBlankWasAnimating) {
            self->repaint();
            self->vBlankWasAnimating = false;
          }
          return;
        }
        idleFrames = 0;
        // Truly idle: skip processUpdates/TimerHub/repaint to avoid ~13% CPU
        // from 60Hz tick
        float newScale = 1.0f;
        if (auto *disp =
                juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
          newScale = static_cast<float>(disp->scale);
        if (std::abs(newScale - self->cachedDisplayScale) > 0.01f) {
          self->cachedDisplayScale = newScale;
          self->repaint();
        }
        return;
      }
      idleFrames = 0;
      self->vBlankWasAnimating = true;

      double now = juce::Time::getMillisecondCounterHiRes();
      float dt = (self->lastFrameTime > 0.0)
                     ? static_cast<float>((now - self->lastFrameTime) / 1000.0)
                     : 0.016f;
      self->lastFrameTime = now;

      // 1.1 Reduced UI: when not playing and not dragging, run full
      // processUpdates at ~4 Hz (every 15th frame)
      bool reducedMode = !isPlaying && !mouseActive;
      static int reducedFrameGl = 0;
      static int runtimeFrameGl = 0;
      if (reducedMode && self->context) {
        reducedFrameGl = (reducedFrameGl + 1) % 15;
        if (reducedFrameGl == 0) {
          self->context->repaintCoordinator.flush([self](uint32_t dirtyBits) {
            if (self)
              self->repaintDirtyRegions(dirtyBits);
          });
        } else {
          return; // No flush, no processUpdates, no GL repaint this frame
        }
        if (self->sysController)
          self->sysController->processUpdates(true);
      } else {
        reducedFrameGl = 0;
        // Runtime (playing or dragging): light tick every frame, full ~15 Hz to
        // cut CPU
        runtimeFrameGl = (runtimeFrameGl + 1) % 4;
        if (self->sysController)
          self->sysController->processUpdates(runtimeFrameGl == 0);
      }

      // 1.2/1.3 Only run background animation for animated themes; never call
      // when !hasVisuals
      if (hasVisuals)
        self->dynamicBg.updateAnimation(dt);

      // Skip GL/Vulkan repaint when no region was dirty (saves CPU). Never skip
      // while dragging. In reduced (idle) mode still redraw on our flush frame
      // so the display doesn't freeze.
      if (self->context && !mouseActive &&
          !self->context->repaintCoordinator.hadDirtyLastFlush()) {
        if (!reducedMode || reducedFrameGl != 0)
          return;
      }
      // While dragging/resizing, force Dashboard dirty so GL and compositor
      // always redraw (reduces Pro mode ghosting)
      if (mouseActive && self->context)
        self->context->repaintCoordinator.markDirty(
            RepaintCoordinator::Dashboard);
      // Only one GPU backend is attached at a time (enforced in
      // handleRenderModeChange).
#if PATCHWORLD_VULKAN_SUPPORT
      if (self->vulkanContext && self->vulkanContext->isAttached()) {
        self->vulkanContext->render();
      } else
#endif
          if (!self->openGLContext.isAttached()) {
        self->repaint();
      } else {
        self->openGLContext.triggerRepaint();
      }
    };

    self->vBlankAttachment = std::make_unique<juce::VBlankAttachment>(
        self, std::move(vblankCallback));
  });
}
