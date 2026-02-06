/*
  ==============================================================================
    Source/UI/Widgets/VelocityLane.h
    Role: Velocity editing lane below piano roll. Click-drag to adjust note velocity.
  ==============================================================================
*/
#pragma once
#include "../Fonts.h"
#include "../PopupMenuOptions.h"
#include "../Theme.h"
#include "../../Audio/EditableNote.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <vector>

class VelocityLane : public juce::Component, public juce::SettableTooltipClient {
public:
  /** How velocity is drawn: Bars (default), Line (connect points), Curve (smooth), Ramp (linear ramp per note). */
  enum class DrawMode { Bars, Line, Curve, Ramp };
  void setDrawMode(DrawMode m) { drawMode_ = m; repaint(); }
  DrawMode getDrawMode() const { return drawMode_; }

  VelocityLane() {
    setOpaque(false);
    setInterceptsMouseClicks(true, true);
  }

  void setNotes(std::vector<EditableNote> *notesRef) { notesRef_ = notesRef; }

  void setCoordinateHelpers(std::function<float(double)> beatToX,
                            std::function<double(float)> xToBeat) {
    beatToX_ = std::move(beatToX);
    xToBeat_ = std::move(xToBeat);
  }

  void setScrollX(float scrollX) { scrollX_ = scrollX; }
  void setPixelsPerBeat(float ppb) { pixelsPerBeat_ = ppb; }
  void setPianoKeysWidth(float w) { pianoKeysWidth_ = w; }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgDark.darker(0.2f));

    if (!notesRef_ || notesRef_->empty()) {
      g.setColour(Theme::text.withAlpha(0.3f));
      g.setFont(Fonts::small().withHeight(11.0f));
      g.drawText("Velocity - drag to edit", getLocalBounds().reduced(4),
                 juce::Justification::centredLeft);
      return;
    }

    auto r = getLocalBounds().toFloat().reduced(2);
    float h = r.getHeight();
    const float bottom = r.getBottom();

    switch (drawMode_) {
    case DrawMode::Line: {
      juce::Path path;
      bool first = true;
      for (size_t i = 0; i < notesRef_->size(); ++i) {
        const auto &n = (*notesRef_)[i];
        float x = beatToX_ ? beatToX_(n.startBeat) : 0.0f;
        float xEnd = x + (float)(n.durationBeats * pixelsPerBeat_);
        if (xEnd < pianoKeysWidth_ || x > r.getRight()) continue;
        float y = bottom - n.velocity * h;
        if (first) {
          path.startNewSubPath(x, y);
          first = false;
        } else
          path.lineTo(x, y);
        path.lineTo(juce::jmin(xEnd, r.getRight()), y);
      }
      if (!path.isEmpty()) {
        g.setColour(juce::Colour::fromHSV(0.25f, 0.7f, 0.9f, 0.9f));
        g.strokePath(path, juce::PathStrokeType(1.5f));
      }
      break;
    }
    case DrawMode::Curve: {
      // Build control points (note start positions)
      std::vector<juce::Point<float>> pts;
      for (size_t i = 0; i < notesRef_->size(); ++i) {
        const auto &n = (*notesRef_)[i];
        float x = beatToX_ ? beatToX_(n.startBeat) : 0.0f;
        float xEnd = x + (float)(n.durationBeats * pixelsPerBeat_);
        if (xEnd < pianoKeysWidth_ || x > r.getRight()) continue;
        float y = bottom - n.velocity * h;
        pts.push_back({ x, y });
      }
      if (pts.size() < 2) {
        if (pts.size() == 1) {
          juce::Path path;
          path.startNewSubPath(pts[0].x, pts[0].y);
          g.setColour(juce::Colour::fromHSV(0.25f, 0.7f, 0.9f, 0.9f));
          g.strokePath(path, juce::PathStrokeType(1.5f));
        }
        break;
      }
      // Catmull-Rom spline through pts -> cubic Bezier segments
      juce::Path path;
      path.startNewSubPath(pts[0].x, pts[0].y);
      for (size_t i = 0; i + 1 < pts.size(); ++i) {
        juce::Point<float> p0 = (i == 0) ? pts[0] + (pts[0] - pts[1]) : pts[i - 1];
        juce::Point<float> p1 = pts[i];
        juce::Point<float> p2 = pts[i + 1];
        juce::Point<float> p3 = (i + 2 >= pts.size()) ? pts[i + 1] + (pts[i + 1] - pts[i]) : pts[i + 2];
        float cx1 = p1.x + (p2.x - p0.x) / 6.0f;
        float cy1 = p1.y + (p2.y - p0.y) / 6.0f;
        float cx2 = p2.x - (p3.x - p1.x) / 6.0f;
        float cy2 = p2.y - (p3.y - p1.y) / 6.0f;
        path.cubicTo(cx1, cy1, cx2, cy2, p2.x, p2.y);
      }
      g.setColour(juce::Colour::fromHSV(0.25f, 0.7f, 0.9f, 0.9f));
      g.strokePath(path, juce::PathStrokeType(1.5f));
      break;
    }
    case DrawMode::Ramp: {
      for (size_t i = 0; i < notesRef_->size(); ++i) {
        const auto &n = (*notesRef_)[i];
        float x = beatToX_ ? beatToX_(n.startBeat) : 0.0f;
        float w = (float)(n.durationBeats * pixelsPerBeat_);
        w = juce::jmax(4.0f, w);
        if (x + w < pianoKeysWidth_ || x > r.getRight()) continue;
        float y0 = bottom - n.velocity * h;
        auto col = juce::Colour::fromHSV(0.3f - n.velocity * 0.3f, 0.8f, 0.9f, 0.85f);
        g.setColour(col);
        g.fillRect(x, y0, w, bottom - y0);
      }
      break;
    }
    case DrawMode::Bars:
    default: {
      for (size_t i = 0; i < notesRef_->size(); ++i) {
        const auto &n = (*notesRef_)[i];
        float x = beatToX_ ? beatToX_(n.startBeat) : 0.0f;
        float w = (float)(n.durationBeats * pixelsPerBeat_);
        w = juce::jmax(4.0f, w);
        float barH = n.velocity * h;
        if (x + w < pianoKeysWidth_ || x > r.getRight()) continue;
        float y0 = bottom - barH;
        auto col = juce::Colour::fromHSV(0.3f - n.velocity * 0.3f, 0.8f, 0.9f, 1.0f);
        g.setColour(col);
        g.fillRect(x, y0, w, barH);
        g.setColour(col.brighter(0.3f).withAlpha(0.5f));
        g.drawRect(x, y0, w, barH, 1.0f);
      }
      break;
    }
    }
  }

  void mouseDown(const juce::MouseEvent &e) override {
    if (e.mods.isRightButtonDown()) {
      juce::PopupMenu m;
      m.addItem("Bars", true, drawMode_ == DrawMode::Bars, [this] { setDrawMode(DrawMode::Bars); });
      m.addItem("Line", true, drawMode_ == DrawMode::Line, [this] { setDrawMode(DrawMode::Line); });
      m.addItem("Curve", true, drawMode_ == DrawMode::Curve, [this] { setDrawMode(DrawMode::Curve); });
      m.addItem("Ramp", true, drawMode_ == DrawMode::Ramp, [this] { setDrawMode(DrawMode::Ramp); });
      m.showMenuAsync(PopupMenuOptions::forComponent(this));
      return;
    }
    lastDragX_ = e.x;
    lastDragY_ = e.y;
    handleDrag(e);
  }
  void mouseDrag(const juce::MouseEvent &e) override {
    if (e.mods.isRightButtonDown() || !xToBeat_ || !notesRef_ || notesRef_->empty())
      return;
    // Curve drawing: sample points along drag path so fast drags still paint
    int x0 = lastDragX_;
    int y0 = lastDragY_;
    int x1 = e.x;
    int y1 = e.y;
    int steps = juce::jmax(1, std::abs(x1 - x0) / 4);
    for (int i = 0; i <= steps; ++i) {
      float t = (float)i / (float)steps;
      int x = juce::roundToInt(x0 + t * (x1 - x0));
      int y = juce::roundToInt(y0 + t * (y1 - y0));
      double beat = xToBeat_((float)x);
      float vel = 1.0f - (float)y / (float)getHeight();
      vel = juce::jlimit(0.0f, 1.0f, vel);
      for (auto &n : *notesRef_) {
        if (beat >= n.startBeat && beat < n.getEndBeat()) {
          n.velocity = vel;
          break;
        }
      }
    }
    lastDragX_ = x1;
    lastDragY_ = y1;
    if (onVelocityChanged) onVelocityChanged();
    repaint();
  }

private:
  int lastDragX_ = 0;
  int lastDragY_ = 0;
  DrawMode drawMode_ = DrawMode::Bars;
  std::vector<EditableNote> *notesRef_ = nullptr;
  std::function<float(double)> beatToX_;
  std::function<double(float)> xToBeat_;
  float scrollX_ = 0.0f;
  float pixelsPerBeat_ = 60.0f;
  float pianoKeysWidth_ = 48.0f;

  void handleDrag(const juce::MouseEvent &e) {
    if (!notesRef_ || notesRef_->empty() || !xToBeat_)
      return;
    double beat = xToBeat_((float)e.x);
    float newVel = 1.0f - (float)e.y / (float)getHeight();
    newVel = juce::jlimit(0.0f, 1.0f, newVel);
    for (auto &n : *notesRef_) {
      if (beat >= n.startBeat && beat < n.getEndBeat()) {
        n.velocity = newVel;
        if (onVelocityChanged) onVelocityChanged();
        break;
      }
    }
    repaint();
  }

public:
  std::function<void()> onVelocityChanged;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VelocityLane)
};
