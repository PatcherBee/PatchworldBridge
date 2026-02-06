/*
  ==============================================================================
    Source/UI/CRTBackground.h
    Role: Renders a high-performance Vaporwave/Cyberpunk CRT background
  ==============================================================================
*/

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

class CRTBackground {
public:
  CRTBackground() {}

  void init(juce::OpenGLContext &openGLContext) {
    // Compile the shader when context is created
    compileOpenGLShaders(openGLContext);
  }

  void releaseResources() {
    shader.reset();
    shaderValid = false;
    matrixShader.reset();
    matrixShaderValid = false;
  }

  // Modulation Setters
  void setVignette(float v) { vignetteVal = v; }
  void setAberration(float v) { aberrationVal = v; }
  void setScanline(float v) { scanlineVal = v; }
  void setAurora(float v) { auroraVal = v; }

  bool isShaderValid() const { return shaderValid || matrixShaderValid; }

  void render(juce::OpenGLContext &openGLContext, int w, int h, float time,
              int themeId = 0) {
    using namespace juce::gl;
    bool useMatrix = (themeId == 13 && matrixShader != nullptr && matrixShaderValid);
    if (useMatrix) {
      renderMatrix(openGLContext, w, h, time);
      return;
    }
    if (shader == nullptr || !shaderValid) {
      glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      return;
    }

    shader->use();

    // High-DPI fix: framebuffer is larger than component size on Retina/scaled displays
    float scale = (float)openGLContext.getRenderingScale();
    float resX = (float)w * scale, resY = (float)h * scale;
    if (uniformResolution && (resX != lastResolutionX || resY != lastResolutionY)) {
      lastResolutionX = resX;
      lastResolutionY = resY;
      uniformResolution->set(resX, resY);
    }
    if (uniformTime)
      uniformTime->set(time);

    // Only update style uniforms when values change (reduces GL state churn)
    if (uniformVignette && vignetteVal != lastVignette) {
      lastVignette = vignetteVal;
      uniformVignette->set(vignetteVal);
    }
    if (uniformAberration && aberrationVal != lastAberration) {
      lastAberration = aberrationVal;
      uniformAberration->set(aberrationVal);
    }
    if (uniformScanline && scanlineVal != lastScanline) {
      lastScanline = scanlineVal;
      uniformScanline->set(scanlineVal);
    }
    if (uniformAurora && auroraVal != lastAurora) {
      lastAurora = auroraVal;
      uniformAurora->set(auroraVal);
    }

    // Draw Full Screen Quad covering the window
    // The shader calculates the pixel color for every pixel on screen
    GLfloat vertices[] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};

    GLuint positionAttributeID =
        (GLuint)openGLContext.extensions.glGetAttribLocation(
            shader->getProgramID(), "position");

    openGLContext.extensions.glVertexAttribPointer(
        positionAttributeID, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    openGLContext.extensions.glEnableVertexAttribArray(positionAttributeID);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    openGLContext.extensions.glDisableVertexAttribArray(positionAttributeID);
  }

private:
  std::unique_ptr<juce::OpenGLShaderProgram> shader;
  std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uniformResolution,
      uniformTime;
  std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uniformVignette,
      uniformAberration, uniformScanline, uniformAurora;

  std::unique_ptr<juce::OpenGLShaderProgram> matrixShader;
  std::unique_ptr<juce::OpenGLShaderProgram::Uniform> matrixUniformResolution,
      matrixUniformTime;
  bool matrixShaderValid = false;
  float matrixLastResX = -1.0f, matrixLastResY = -1.0f;

  bool shaderValid = false;
  float vignetteVal = 1.0f;
  float aberrationVal = 0.003f;
  float scanlineVal = 0.05f;
  float auroraVal = 0.0f;
  float lastResolutionX = -1.0f, lastResolutionY = -1.0f;
  float lastVignette = -1.0f, lastAberration = -1.0f, lastScanline = -1.0f, lastAurora = -1.0f;

  void renderMatrix(juce::OpenGLContext &openGLContext, int w, int h, float time) {
    using namespace juce::gl;
    if (matrixShader == nullptr || !matrixShaderValid) return;
    matrixShader->use();
    float scale = (float)openGLContext.getRenderingScale();
    float resX = (float)w * scale, resY = (float)h * scale;
    if (matrixUniformResolution && (resX != matrixLastResX || resY != matrixLastResY)) {
      matrixLastResX = resX; matrixLastResY = resY;
      matrixUniformResolution->set(resX, resY);
    }
    if (matrixUniformTime) matrixUniformTime->set(time);
    GLfloat vertices[] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    GLuint posId = (GLuint)openGLContext.extensions.glGetAttribLocation(
        matrixShader->getProgramID(), "position");
    openGLContext.extensions.glVertexAttribPointer(posId, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    openGLContext.extensions.glEnableVertexAttribArray(posId);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    openGLContext.extensions.glDisableVertexAttribArray(posId);
  }

  void compileOpenGLShaders(juce::OpenGLContext &openGLContext) {
    if (shader)
      return;

    // Simple vertex shader (passes coordinates through)
    juce::String vertexCode = "attribute vec2 position;\n"
                              "varying vec2 vTexCoord;\n"
                              "void main() {\n"
                              "    vTexCoord = position * 0.5 + 0.5;\n"
                              "    gl_Position = vec4(position, 0.0, 1.0);\n"
                              "}\n";

    // The Magic Fragment Shader (Phase 9: Extreme Aesthetic Edition)
    juce::String fragmentCode =
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#else\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec2 vTexCoord;\n"
        "uniform vec2 resolution;\n"
        "uniform float time;\n"
        "uniform float vignetteAmt;\n"
        "uniform float aberrationAmt;\n"
        "uniform float scanlineAmt;\n"
        "uniform float uAuroraStep;\n"
        "\n"
        "void main() {\n"
        "    vec2 uv = vTexCoord;\n"
        "    \n"
        "    // 1. Sine-Wave CRT Scanlines\n"
        "    float scanline = sin(uv.y * 800.0 + time * 10.0) * (scanlineAmt * "
        "0.5);\n"
        "    \n"
        "    // 2. Chromatic Aberration (RGB Distort)\n"
        "    vec2 dist = uv - 0.5;\n"
        "    float r = 0.05, g = 0.05, b = 0.08; // Dark Base\n"
        "    \n"
        "    // 3. Vaporwave Pulsing & Aurora\n"
        "    r += abs(sin(time + uv.x)) * 0.1;\n"
        "    b += uAuroraStep * 0.4;\n"
        "    \n"
        "    // 4. Cyber Grid\n"
        "    float grid = step(0.99, fract(uv.x * 30.0 + time * 0.02)) * "
        "0.05;\n"
        "    grid += step(0.99, fract(uv.y * 15.0 - time * 0.01)) * 0.05;\n"
        "    vec3 color = vec3(r, g, b) + (vec3(0.0, 0.8, 1.0) * grid);\n"
        "    \n"
        "    // 5. Vignette\n"
        "    float vig = (vignetteAmt * 16.0 * uv.x * uv.y * (1.0 - uv.x) * "
        "(1.0 - uv.y));\n"
        "    vig = clamp(vig, 0.0, 1.0);\n"
        "    \n"
        "    gl_FragColor = vec4(color * vig - scanline, 1.0);\n"
        "}\n";

    shader.reset(new juce::OpenGLShaderProgram(openGLContext));

    if (shader->addVertexShader(vertexCode) &&
        shader->addFragmentShader(fragmentCode) && shader->link()) {
      uniformResolution.reset(
          new juce::OpenGLShaderProgram::Uniform(*shader, "resolution"));
      uniformTime.reset(
          new juce::OpenGLShaderProgram::Uniform(*shader, "time"));
      uniformVignette.reset(
          new juce::OpenGLShaderProgram::Uniform(*shader, "vignetteAmt"));
      uniformAberration.reset(
          new juce::OpenGLShaderProgram::Uniform(*shader, "aberrationAmt"));
      uniformScanline.reset(
          new juce::OpenGLShaderProgram::Uniform(*shader, "scanlineAmt"));
      uniformAurora.reset(
          new juce::OpenGLShaderProgram::Uniform(*shader, "uAuroraStep"));
      shaderValid = true;
    } else {
      shader.reset();
      shaderValid = false;
    }
    compileMatrixShaders(openGLContext);
  }

  void compileMatrixShaders(juce::OpenGLContext &openGLContext) {
    if (matrixShader) return;
    juce::String vertexCode = "attribute vec2 position;\n"
                              "varying vec2 vTexCoord;\n"
                              "void main() {\n"
                              "    vTexCoord = position * 0.5 + 0.5;\n"
                              "    gl_Position = vec4(position, 0.0, 1.0);\n"
                              "}\n";
    // Falling green "Matrix rain" columns: vertical streaks with bright head and fading trail
    juce::String matrixFrag =
        "#ifdef GL_ES\n precision mediump float;\n #endif\n"
        "varying vec2 vTexCoord;\n"
        "uniform vec2 resolution;\n"
        "uniform float time;\n"
        "float hash(float n) { return fract(sin(n) * 43758.5453); }\n"
        "void main() {\n"
        "    vec2 uv = vTexCoord;\n"
        "    float col = floor(uv.x * 80.0);\n"
        "    float speed = 0.3 + hash(col) * 0.4;\n"
        "    float head = fract(time * speed + hash(col * 1.7));\n"
        "    float trail = 0.0;\n"
        "    float y = 1.0 - uv.y;\n"
        "    float dist = head - y;\n"
        "    if (dist > 0.0 && dist < 0.15) trail = 1.0 - dist / 0.15;\n"
        "    else if (dist < 0.0 && dist > -0.5) trail = 0.3 * (1.0 + dist / 0.5);\n"
        "    float scan = sin(uv.y * 600.0 + time * 8.0) * 0.03;\n"
        "    vec3 green = vec3(0.0, 0.4, 0.0) + vec3(0.0, 0.6, 0.0) * trail;\n"
        "    vec3 base = vec3(0.0, 0.02, 0.0);\n"
        "    vec3 color = base + green - scan;\n"
        "    gl_FragColor = vec4(color, 1.0);\n"
        "}\n";
    matrixShader.reset(new juce::OpenGLShaderProgram(openGLContext));
    if (matrixShader->addVertexShader(vertexCode) &&
        matrixShader->addFragmentShader(matrixFrag) && matrixShader->link()) {
      matrixUniformResolution.reset(
          new juce::OpenGLShaderProgram::Uniform(*matrixShader, "resolution"));
      matrixUniformTime.reset(
          new juce::OpenGLShaderProgram::Uniform(*matrixShader, "time"));
      matrixShaderValid = true;
    } else {
      matrixShader.reset();
      matrixShaderValid = false;
    }
  }
};
