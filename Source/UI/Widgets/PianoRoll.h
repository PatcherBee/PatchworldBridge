/*
  ==============================================================================
    Source/UI/Widgets/PianoRoll.h
    Role: Piano Roll and Custom Keyboard components
  ==============================================================================
*/
#pragma once
#include "../../Core/Common.h"
#include "../Theme.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>
#include <set>
#include <vector>

// --- CUSTOM KEYBOARD WRAPPER ---
// Inherits directly so PerformancePanel can pass (State, Orientation)
class CustomKeyboard : public juce::MidiKeyboardComponent {
public:
  CustomKeyboard(juce::MidiKeyboardState &state, Orientation orientation)
      : juce::MidiKeyboardComponent(state, orientation) {
    setInterceptsMouseClicks(true, true);
    setWantsKeyboardFocus(true);
    setKeyWidth(40.0f);
  }

  // Forward spacebar to parent (MainComponent) for transport control
  bool keyPressed(const juce::KeyPress &key) override {
    if (key == juce::KeyPress::spaceKey)
      return false; // Let parent handle it
    return juce::MidiKeyboardComponent::keyPressed(key);
  }

  // Callback for direct interaction
  std::function<void(int note, int vel, bool on)> onKeyClicked;

  bool mouseDownOnKey(int midiNoteNumber, const juce::MouseEvent &e) override {
    // Velocity from click Y: bottom of key = 127, top = 0 (standard piano
    // keyboard)
    auto keyBounds = getRectangleForKey(midiNoteNumber);
    float velNorm = 1.0f;
    if (keyBounds.getHeight() > 0) {
      float relY = (float)(e.getPosition().y - keyBounds.getY()) /
                   (float)keyBounds.getHeight();
      velNorm = juce::jlimit(0.0f, 1.0f, 1.0f - relY);
    }
    int vel = juce::jlimit(1, 127, (int)(velNorm * 127.0f));
    setVelocity(velNorm,
                false); // use our Y-derived velocity, not JUCE's mouse position
    if (onKeyClicked)
      onKeyClicked(midiNoteNumber, vel, true);
    return juce::MidiKeyboardComponent::mouseDownOnKey(midiNoteNumber, e);
  }

  void mouseUpOnKey(int midiNoteNumber, const juce::MouseEvent &e) override {
    if (onKeyClicked)
      onKeyClicked(midiNoteNumber, 0, false);
    juce::MidiKeyboardComponent::mouseUpOnKey(midiNoteNumber, e);
  }

  // Helper for visual offset if needed by external classes
  void setVisualOctaveShift(int shift) { visualOctaveShift = shift; }
  int visualOctaveShift = 0;
};

// --- COMPLEX PIANO ROLL ---
class ComplexPianoRoll : public juce::Component {
public:
  // FIX: Constructor - disable buffering for dynamic/scrolling (CPU perf)
  ComplexPianoRoll(juce::MidiKeyboardState &state) : keyboardState(state) {
    setBufferedToImage(false);
    setOpaque(true);
  }

  juce::MidiKeyboardState &keyboardState;
  juce::MidiMessageSequence ownedSequence;
  juce::CriticalSection dataLock;
  juce::MidiKeyboardComponent *keyboardComp = nullptr;

  float playbackCursor = 0.0f;
  double ticksPerQuarter = 960.0;
  bool showPlayhead = false;
  int visualOctaveShift = 0;
  std::set<int> activeVisualNotes;

  /** Optional: notify RepaintCoordinator when piano roll needs repaint (batch
   * with other dirty regions). */
  std::function<void()> onRequestRepaint;

  // Incremental rendering: only repaint playhead region when cursor moves
  float lastPaintedPlayhead = -1.0f;
  static constexpr int playheadRepaintWidth = 4;

  // --- OpenGL Instancing ---
  struct NoteInstance {
    float x, y, w, h;
    float r, g, b, a;
  };
  std::vector<NoteInstance> instanceData;
  std::unique_ptr<juce::OpenGLShaderProgram> shader;
  juce::uint32 vbo = 0;
  juce::uint32 quadVbo = 0;

  void initGL(juce::OpenGLContext &openGLContext) {
    if (shader)
      return;

    const char *vshader =
        "attribute vec2 position;\n"
        "attribute vec4 instanceData1;\n"
        "attribute vec4 instanceData2;\n"
        "varying vec4 vColor;\n"
        "void main() {\n"
        "    vColor = instanceData2;\n"
        "    vec2 pos = position * instanceData1.zw + instanceData1.xy;\n"
        "    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
        "}";

    const char *fshader = "varying vec4 vColor;\n"
                          "void main() {\n"
                          "    gl_FragColor = vColor;\n"
                          "}";

    shader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    if (shader->addVertexShader(vshader) &&
        shader->addFragmentShader(fshader)) {
      shader->link();
    }
    openGLContext.extensions.glGenBuffers(1, (unsigned int *)&vbo);
    if (quadVbo == 0) {
      GLfloat quad[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                        0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f};
      openGLContext.extensions.glGenBuffers(1, (unsigned int *)&quadVbo);
      openGLContext.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, quadVbo);
      openGLContext.extensions.glBufferData(juce::gl::GL_ARRAY_BUFFER,
                                            (GLsizeiptr)sizeof(quad), quad,
                                            juce::gl::GL_STATIC_DRAW);
      openGLContext.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, 0);
    }
  }

  void releaseGL(juce::OpenGLContext &openGLContext) {
    shader.reset();
    if (vbo != 0) {
      openGLContext.extensions.glDeleteBuffers(1, &vbo);
      vbo = 0;
    }
    if (quadVbo != 0) {
      openGLContext.extensions.glDeleteBuffers(1, &quadVbo);
      quadVbo = 0;
    }
  }

  bool hasGLContent() const {
    return shader != nullptr && shader->getProgramID() != 0 &&
           !instanceData.empty();
  }

  void renderGL(juce::OpenGLContext &openGLContext) {
    using namespace juce::gl;
    if (!shader || shader->getProgramID() == 0 || instanceData.empty() ||
        vbo == 0 || quadVbo == 0)
      return;

    juce::ScopedLock sl(dataLock);
    if (instanceData.empty())
      return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader->use();
    GLuint prog = (GLuint)shader->getProgramID();
    GLint posLoc =
        (GLint)openGLContext.extensions.glGetAttribLocation(prog, "position");
    GLint i1Loc = (GLint)openGLContext.extensions.glGetAttribLocation(
        prog, "instanceData1");
    GLint i2Loc = (GLint)openGLContext.extensions.glGetAttribLocation(
        prog, "instanceData2");

    openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, vbo);
    openGLContext.extensions.glBufferData(
        GL_ARRAY_BUFFER,
        (GLsizeiptr)(instanceData.size() * sizeof(NoteInstance)),
        instanceData.data(), GL_STREAM_DRAW);
    openGLContext.extensions.glVertexAttribPointer(
        (GLuint)i1Loc, 4, GL_FLOAT, GL_FALSE, sizeof(NoteInstance), (void *)0);
    openGLContext.extensions.glVertexAttribPointer(
        (GLuint)i2Loc, 4, GL_FLOAT, GL_FALSE, sizeof(NoteInstance),
        (void *)(4 * sizeof(float)));
    openGLContext.extensions.glEnableVertexAttribArray((GLuint)i1Loc);
    openGLContext.extensions.glEnableVertexAttribArray((GLuint)i2Loc);
    glVertexAttribDivisor((GLuint)i1Loc, 1);
    glVertexAttribDivisor((GLuint)i2Loc, 1);

    openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
    openGLContext.extensions.glVertexAttribPointer((GLuint)posLoc, 2, GL_FLOAT,
                                                   GL_FALSE, 0, nullptr);
    openGLContext.extensions.glEnableVertexAttribArray((GLuint)posLoc);
    glVertexAttribDivisor((GLuint)posLoc, 0);

    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)instanceData.size());

    glVertexAttribDivisor((GLuint)posLoc, 0);
    glVertexAttribDivisor((GLuint)i1Loc, 0);
    glVertexAttribDivisor((GLuint)i2Loc, 0);
    openGLContext.extensions.glDisableVertexAttribArray((GLuint)posLoc);
    openGLContext.extensions.glDisableVertexAttribArray((GLuint)i1Loc);
    openGLContext.extensions.glDisableVertexAttribArray((GLuint)i2Loc);
    openGLContext.extensions.glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
  }

  // FIX: Methods called by SystemController (Message) / paint (OpenGL) - need
  // lock
  void visualNoteOn(int note, int ch) {
    juce::ignoreUnused(ch);
    const juce::ScopedLock sl(dataLock);
    activeVisualNotes.insert(note);
  }

  void visualNoteOff(int note, int ch) {
    juce::ignoreUnused(ch);
    const juce::ScopedLock sl(dataLock);
    activeVisualNotes.erase(note);
  }

  // FIX: Method called by PerformancePanel to sync playhead (dirty-region
  // repaint)
  void setPlaybackPosition(double currentBeat, double ppq) {
    if (ppq > 0)
      ticksPerQuarter = ppq;
    float newCursor = (float)(currentBeat * ticksPerQuarter);
    if (!showPlayhead) {
      playbackCursor = newCursor;
      return;
    }
    playbackCursor = newCursor;
    int keyLineY = (int)(getHeight() * 0.85f);
    repaint(0, keyLineY - playheadRepaintWidth, getWidth(),
            playheadRepaintWidth * 2);
    if (onRequestRepaint)
      onRequestRepaint();
  }

  void setVisualOctaveShift(int shift) {
    visualOctaveShift = shift;
    repaint();
    if (onRequestRepaint)
      onRequestRepaint();
  }

  void setSequence(const juce::MidiMessageSequence &seq) {
    const juce::ScopedLock sl(dataLock);
    ownedSequence = seq;
    ownedSequence.updateMatchedPairs();
    lastPaintedPlayhead = -1.0f;
    repaint();
    if (onRequestRepaint)
      onRequestRepaint();
  }

  void invalidateNotesCache() {
    lastPaintedPlayhead = -1.0f;
    repaint();
    if (onRequestRepaint)
      onRequestRepaint();
  }

  void paint(juce::Graphics &g) override {
    // 1. Solid Background (fastest)
    g.fillAll(juce::Colour(0xff050505));

    // 2. Grid (subtle vertical lines - pitch divisions)
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    const float keyWidth = getWidth() / 128.0f;
    for (int n = 0; n < 128; n += 12) {
      float x = (float)n * keyWidth;
      if (x < (float)getWidth())
        g.drawVerticalLine((int)x, 0.0f, (float)getHeight());
    }
    // 3. Playhead Line (Keybed / "now" line) - yellow when playing
    int keyLineY = (int)(getHeight() * 0.85f);
    g.setColour(showPlayhead ? juce::Colours::yellow.withAlpha(0.5f)
                             : juce::Colours::white.withAlpha(0.1f));
    g.drawHorizontalLine(keyLineY, 0.0f, (float)getWidth());

    const juce::ScopedLock sl(dataLock);

    const float h = (float)getHeight();
    const float pixelsPerBeatY = 40.0f;
    const double currentBeat = playbackCursor / ticksPerQuarter;

    if (ownedSequence.getNumEvents() > 0) {
      for (int i = 0; i < ownedSequence.getNumEvents(); ++i) {
        auto *ev = ownedSequence.getEventPointer(i);
        if (ev->message.isNoteOn()) {

          double noteStartBeat = ev->message.getTimeStamp() / ticksPerQuarter;
          double distFromNow = noteStartBeat - currentBeat;
          if (distFromNow < -2.0 || distFromNow > 16.0)
            continue;

          auto *noteOff = ownedSequence.getEventPointer(
              ownedSequence.getIndexOfMatchingKeyUp(i));
          double duration =
              noteOff ? (noteOff->message.getTimeStamp() / ticksPerQuarter) -
                            noteStartBeat
                      : 1.0;

          float noteHeight = (float)(duration * pixelsPerBeatY);
          if (noteHeight < 1.0f)
            noteHeight = 1.0f;

          float noteBottomY =
              (float)keyLineY - (float)(distFromNow * pixelsPerBeatY);
          float noteTopY = noteBottomY - noteHeight;

          float x = 0.0f;
          float w = 0.0f;
          int noteNum = ev->message.getNoteNumber();

          if (keyboardComp) {
            auto r = keyboardComp->getRectangleForKey(noteNum);
            x = (float)r.getX();
            w = (float)r.getWidth();
          } else {
            x = std::floor(noteNum * keyWidth);
            w = std::ceil(keyWidth);
          }

          if (noteTopY > h || noteBottomY < 0)
            continue;

          juce::Colour baseC;
          switch (ev->message.getChannel() % 4) {
          case 0:
            baseC = juce::Colour(0xff00f0ff);
            break;
          case 1:
            baseC = juce::Colour(0xffbd00ff);
            break;
          case 2:
            baseC = juce::Colour(0xff00ff9d);
            break;
          case 3:
            baseC = juce::Colour(0xffff9000);
            break;
          default:
            baseC = juce::Colours::white;
            break;
          }

          // Solid fill (10x faster than gradients)
          g.setColour(baseC.withAlpha(0.8f));
          g.fillRect(juce::Rectangle<float>(x, noteTopY, w, noteHeight));
        }
      }
    }

    // Live Note Flashes
    if (!activeVisualNotes.empty()) {
      g.setColour(juce::Colours::white.withAlpha(0.5f));
      for (int note : activeVisualNotes) {
        float x = 0.0f;
        float w = 0.0f;
        if (keyboardComp) {
          auto r = keyboardComp->getRectangleForKey(note);
          x = (float)r.getX();
          w = (float)r.getWidth();
        } else {
          x = (float)(note * keyWidth);
          w = std::ceil(keyWidth);
        }
        g.fillRect(x, (float)keyLineY, w, 5.0f);
      }
    }
  }

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ComplexPianoRoll)
};