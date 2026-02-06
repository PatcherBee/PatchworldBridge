/*
  ==============================================================================
    Source/UI/RenderConfig.h
    Role: Central control for GPU vs Software, Eco/Pro, and platform fallback.
    Supports: Software (no GPU), OpenGL Eco (30fps), OpenGL Perf (60fps+).
    Auto resolves to best available via RenderBackend (OpenGL or Software).
  ==============================================================================
*/
#pragma once
#include "RenderBackend.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

class RenderConfig {
public:
  enum RenderMode {
    Software = 3,
    OpenGL_Eco = 1,   // 30 FPS, minimal shaders
    OpenGL_Perf = 2,  // 60+ FPS, full effects
    Auto = 0          // Resolve to OpenGL_Eco or Software via RenderBackend
  };

  /** Resolve Auto to OpenGL_Eco if GPU available, else Software. */
  static RenderMode resolveAuto() {
    auto backend = RenderBackend::selectBestAvailable(RenderBackend::getDefaultBackend());
    return (backend == RenderBackend::Type::OpenGL || backend == RenderBackend::Type::Metal ||
            backend == RenderBackend::Type::Vulkan)
               ? OpenGL_Eco
               : Software;
  }

  static void setMode(juce::Component &topLevelComp,
                      juce::OpenGLContext &openGLContext, RenderMode mode) {
    if (mode == Auto)
      mode = resolveAuto();
    if (mode == Software) {
      if (openGLContext.isAttached()) {
        openGLContext.setContinuousRepainting(false);
        openGLContext.detach();
      }
      topLevelComp.setBufferedToImage(true);
    } else {
      topLevelComp.setBufferedToImage(false);
      if (!openGLContext.isAttached()) {
        openGLContext.attachTo(topLevelComp);
      }
      openGLContext.setContinuousRepainting(mode == OpenGL_Perf);
    }
  }

  static void setCached(juce::Component &comp, bool shouldCache) {
    comp.setBufferedToImage(shouldCache);
  }

  static bool isGpuMode(RenderMode mode) {
    if (mode == Auto)
      return RenderBackend::selectBestAvailable(RenderBackend::getDefaultBackend()) !=
             RenderBackend::Type::Software;
    return mode != Software;
  }
};
