/*
  ==============================================================================
    Source/UI/Widgets/MeterBarRenderer.h
    GPU meter bar rendering: instanced quads for mixer meters.
  ==============================================================================
*/
#pragma once

#include <juce_core/juce_core.h>
#include <juce_opengl/juce_opengl.h>
#include <vector>

/** Renders mixer meter bars on GPU (instanced quads). Call from renderOpenGL(). */
class MeterBarRenderer {
public:
  MeterBarRenderer() = default;

  /** Initialize GL resources (VBO, shader). Call when OpenGL context is active. */
  void init(juce::OpenGLContext &openGLContext);

  /** Update level data (normalized 0–1 per strip). Call from message thread. */
  void setLevels(const std::vector<float> &levels);

  /**
   * Draw instanced meter quads in the given rectangle (in main component coords).
   * viewHeight is used for OpenGL Y flip. Call from renderOpenGL() after CRT.
   */
  void render(juce::OpenGLContext &openGLContext, int viewWidth, int viewHeight,
              int meterX, int meterY, int meterW, int meterH);

  /** Release GL resources. Call when context is closing (context still active). */
  void release(juce::OpenGLContext &openGLContext);

  bool isInitialized() const { return shaderValid && quadVbo != 0; }

private:
  void compileShader(juce::OpenGLContext &openGLContext);

  std::unique_ptr<juce::OpenGLShaderProgram> shader;
  juce::uint32 quadVbo = 0;
  bool shaderValid = false;

  juce::CriticalSection levelsLock;
  std::vector<float> levelsCopy;
  static constexpr int maxChannels = 16;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterBarRenderer)
};
