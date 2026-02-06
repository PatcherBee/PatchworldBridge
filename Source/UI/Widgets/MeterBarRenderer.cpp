/*
  ==============================================================================
    Source/UI/Widgets/MeterBarRenderer.cpp
  ==============================================================================
*/
#include "MeterBarRenderer.h"
#include "../Theme.h"

void MeterBarRenderer::compileShader(juce::OpenGLContext &openGLContext) {
  if (shader)
    return;
  shader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
  const char *vshader =
      "attribute vec2 position;\n"
      "varying float vInstanceId;\n"
      "uniform float uLevels[16];\n"
      "void main() {\n"
      "  int id = gl_InstanceID;\n"
      "  vInstanceId = float(id);\n"
      "  float barW = 1.0 / 16.0;\n"
      "  float x = float(id) * barW;\n"
      "  float h = id < 16 ? uLevels[id] : 0.0;\n"
      "  vec2 pos = position;\n"
      "  pos.x = pos.x * barW + x;\n"
      "  pos.y = pos.y * h;\n"
      "  gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
      "}";
  const char *fshader =
      "varying float vInstanceId;\n"
      "uniform vec3 uColors[16];\n"
      "void main() {\n"
      "  int id = int(vInstanceId);\n"
      "  vec3 c = id >= 0 && id < 16 ? uColors[id] : vec3(0.5);\n"
      "  gl_FragColor = vec4(c, 0.9);\n"
      "}";
  if (shader->addVertexShader(vshader) && shader->addFragmentShader(fshader) &&
      shader->link()) {
    shaderValid = true;
  } else {
    shader.reset();
    shaderValid = false;
  }
}

void MeterBarRenderer::init(juce::OpenGLContext &openGLContext) {
  using namespace juce::gl;
  compileShader(openGLContext);
  if (quadVbo == 0) {
    GLfloat quad[] = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f
    };
    openGLContext.extensions.glGenBuffers(1, &quadVbo);
    openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
    openGLContext.extensions.glBufferData(
        GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(quad)), quad, GL_STATIC_DRAW);
    openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
}

void MeterBarRenderer::setLevels(const std::vector<float> &levels) {
  juce::CriticalSection::ScopedLockType lock(levelsLock);
  levelsCopy.resize(juce::jmin((int)levels.size(), maxChannels));
  for (size_t i = 0; i < levelsCopy.size(); ++i)
    levelsCopy[i] = juce::jlimit(0.0f, 1.0f, levels[i]);
  while (levelsCopy.size() < (size_t)maxChannels)
    levelsCopy.push_back(0.0f);
}

void MeterBarRenderer::render(juce::OpenGLContext &openGLContext,
                              int /* viewWidth */, int viewHeight,
                              int meterX, int meterY, int meterW, int meterH) {
  using namespace juce::gl;
  if (!shaderValid || !shader || shader->getProgramID() == 0 || quadVbo == 0)
    return;

  {
    juce::CriticalSection::ScopedLockType lock(levelsLock);
    if (levelsCopy.empty())
      return;
  }

  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glViewport(meterX, viewHeight - meterY - meterH, (GLsizei)meterW,
             (GLsizei)meterH);

  shader->use();

  GLint posLoc = (GLint)openGLContext.extensions.glGetAttribLocation(
      shader->getProgramID(), "position");
  GLint levelsLoc = (GLint)openGLContext.extensions.glGetUniformLocation(
      shader->getProgramID(), "uLevels");
  GLint colorsLoc = (GLint)openGLContext.extensions.glGetUniformLocation(
      shader->getProgramID(), "uColors");

  openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
  openGLContext.extensions.glVertexAttribPointer(
      (GLuint)posLoc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  openGLContext.extensions.glEnableVertexAttribArray((GLuint)posLoc);

  float levels[16];
  {
    juce::CriticalSection::ScopedLockType lock(levelsLock);
    for (int i = 0; i < maxChannels; ++i)
      levels[i] = i < (int)levelsCopy.size() ? levelsCopy[i] : 0.0f;
  }
  openGLContext.extensions.glUniform1fv(levelsLoc, 16, levels);

  float colors[16 * 3];
  for (int i = 0; i < maxChannels; ++i) {
    juce::Colour c = Theme::getChannelColor(i + 1);
    colors[i * 3 + 0] = c.getFloatRed();
    colors[i * 3 + 1] = c.getFloatGreen();
    colors[i * 3 + 2] = c.getFloatBlue();
  }
  juce::gl::glUniform3fv(colorsLoc, 16, colors);

  glDrawArraysInstanced(GL_TRIANGLES, 0, 6, maxChannels);

  openGLContext.extensions.glDisableVertexAttribArray((GLuint)posLoc);
  openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
  glDisable(GL_BLEND);

  glViewport(viewport[0], viewport[1], (GLsizei)viewport[2], (GLsizei)viewport[3]);
}

void MeterBarRenderer::release(juce::OpenGLContext &openGLContext) {
  shader.reset();
  shaderValid = false;
  if (quadVbo != 0) {
    openGLContext.extensions.glDeleteBuffers(1, &quadVbo);
    quadVbo = 0;
  }
}
