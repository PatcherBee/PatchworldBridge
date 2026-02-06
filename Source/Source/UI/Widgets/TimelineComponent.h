/*
  ==============================================================================
    Source/UI/Widgets/TimelineComponent.h
    Role: Visual timeline with seek functionality, loop selection, smooth
  scrubbing.
  ==============================================================================
*/

#pragma once
#include "../../Core/TimerHub.h"
#include "../Fonts.h"
#include "../Theme.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>

class TimelineComponent : public juce::Component {
public:
  std::function<void(double)> onSeek;
  std::function<void(double, double)> onLoopSelect;

  TimelineComponent() {
    hubId = "Timeline_" + std::to_string(reinterpret_cast<int64_t>(this));
    TimerHub::instance().subscribe(
        hubId,
        [this]() {
          if (!isVisible())
            return;
          double diff = targetBeat - displayBeat;
          if (std::abs(diff) > 0.001) {
            displayBeat += diff * 0.15f;
            repaint();
          }
        },
        TimerHub::Rate10Hz); // 10Hz is sufficient for timeline smoothing
  }

  ~TimelineComponent() override { TimerHub::instance().unsubscribe(hubId); }

  void setTotalLength(double beats) {
    totalBeats = beats;
    repaint();
  }

  void setPlayhead(double beat) {
    if (std::abs(beat - targetBeat) > 4.0)
      displayBeat = beat; // Jump on loop
    targetBeat = playheadBeat = beat;
    repaint();
  }

  void paint(juce::Graphics &g) override {
    auto area = getLocalBounds().toFloat();
    g.setColour(Theme::bgDark.darker(0.2f));
    g.fillRoundedRectangle(area, 4.0f);

    if (totalBeats <= 0)
      return;
    float pixelsPerBeat = area.getWidth() / (float)totalBeats;

    // Loop region
    if (loopStart >= 0 && loopEnd > loopStart) {
      float x1 = (float)loopStart * pixelsPerBeat;
      float x2 = (float)loopEnd * pixelsPerBeat;
      g.setColour(Theme::accent.withAlpha(0.2f));
      g.fillRect(x1, 0.0f, x2 - x1, area.getHeight());
      g.setColour(Theme::accent);
      g.fillRect(x1 - 1, 0.0f, 3.0f, area.getHeight());
      g.fillRect(x2 - 2, 0.0f, 3.0f, area.getHeight());
    }

    // Beat markers
    g.setFont(Fonts::monoSmall().withHeight(9.0f));
    for (int beat = 0; beat <= (int)totalBeats; ++beat) {
      float x = beat * pixelsPerBeat;
      bool isBar = (beat % 4 == 0);
      if (beat % 16 == 0) {
        g.setColour(Theme::text);
        g.drawVerticalLine((int)x, 0, area.getHeight());
        g.drawText(juce::String(beat / 4 + 1), (int)(x + 2), 1, 20, 12,
                   juce::Justification::left);
      } else if (isBar) {
        g.setColour(Theme::text.withAlpha(0.5f));
        g.drawVerticalLine((int)x, area.getHeight() * 0.5f, area.getHeight());
      } else {
        g.setColour(Theme::grid.withAlpha(0.3f));
        g.drawVerticalLine((int)x, area.getHeight() * 0.75f, area.getHeight());
      }
    }

    // Animated playhead
    float playX = (float)(displayBeat / totalBeats) * area.getWidth();
    for (float glow = 6.0f; glow > 0; glow -= 2.0f) {
      g.setColour(juce::Colours::yellow.withAlpha(0.08f));
      g.fillRect(playX - glow, 0.0f, glow * 2, area.getHeight());
    }
    g.setColour(juce::Colours::yellow);
    g.fillRect(playX - 1, 0.0f, 2.0f, area.getHeight());
    juce::Path triangle;
    triangle.addTriangle(playX - 6, 0.0f, playX + 6, 0.0f, playX, 8.0f);
    g.fillPath(triangle);
  }

  void mouseDown(const juce::MouseEvent &e) override {
    double beat = xToBeat(e.x);
    if (e.mods.isShiftDown()) {
      isSelectingLoop = true;
      loopStart = beat;
      loopEnd = beat;
    } else {
      isScrubbing = true;
      seekToPos(e.x);
    }
    repaint();
  }

  void mouseDrag(const juce::MouseEvent &e) override {
    double beat = xToBeat(e.x);
    if (isSelectingLoop) {
      if (beat < loopStart) {
        loopEnd = loopStart;
        loopStart = beat;
      } else {
        loopEnd = beat;
      }
      repaint();
    } else if (isScrubbing) {
      seekToPos(e.x);
    }
  }

  void mouseUp(const juce::MouseEvent &) override {
    if (isSelectingLoop && loopEnd > loopStart + 0.1 && onLoopSelect)
      onLoopSelect(loopStart, loopEnd);
    isSelectingLoop = false;
    isScrubbing = false;
  }

private:
  std::string hubId;
  double totalBeats = 16.0;
  double playheadBeat = 0.0;
  double targetBeat = 0.0;
  double displayBeat = 0.0;
  double loopStart = -1.0;
  double loopEnd = -1.0;
  bool isScrubbing = false;
  bool isSelectingLoop = false;

  double xToBeat(int x) const {
    return totalBeats <= 0
               ? 0.0
               : juce::jmax(0.0, (double)x / getWidth() * totalBeats);
  }

  void seekToPos(int x) {
    double b = xToBeat(x);
    playheadBeat = targetBeat = displayBeat = b;
    if (onSeek)
      onSeek(b);
    repaint();
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
};
