/*
  ==============================================================================
    Source/Theme.h
    Role: Global theme system with 11 presets + ModernGlowLF
    Performance: Animations at 30Hz to preserve MIDI/OSC priority
  ==============================================================================
*/
#pragma once
#include <algorithm>
#include <cmath>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// Global theme colors (static inline for C++17)
struct Theme {
  static inline juce::Colour bgDark{0xff0a0a0c};
  static inline juce::Colour bgGradEnd{0xff0a0a0c}; // Gradient End Color
  static inline juce::Colour bgPanel{0xff16161d};
  static inline juce::Colour bgMid{0xff1e1e1e};
  static inline juce::Colour bgMedium{0xff2d2d35};
  static inline juce::Colour accent{0xff00a3ff};
  static inline juce::Colour grid{0xff333333};
  static inline juce::Colour text{0xffffffff};
  static inline int currentThemeId = 1;

  // Semantic (roadmap 4.4)
  static inline juce::Colour success{juce::Colours::lime};
  static inline juce::Colour warning{juce::Colours::orange};
  static inline juce::Colour error{juce::Colours::red};

  static juce::Colour buttonHover(juce::Colour base) {
    return base.brighter(0.1f);
  }
  static juce::Colour buttonPressed(juce::Colour base) {
    return base.darker(0.1f);
  }

  // Helper for channel coloring (golden ratio hue distribution)
  static juce::Colour getChannelColor(int ch) {
    float hue = ((ch - 1) * 0.618f);
    hue = hue - (float)(int)hue; // Remove integer part (JUCE-safe modulo)
    return juce::Colour::fromHSV(hue, 0.7f, 0.95f, 1.0f);
  }

  // Stylish panel drawing helper - Enhanced Glassmorphism
  static void drawStylishPanel(juce::Graphics &g, juce::Rectangle<float> area,
                               juce::Colour baseColor, float cornerSize) {
    // 1. Shadow / Outer Depth
    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.fillRoundedRectangle(area.translated(0, 2), cornerSize);

    // 2. High-Quality Gradient (Glass Look)
    g.setGradientFill(juce::ColourGradient(
        baseColor.withAlpha(0.7f).brighter(0.1f), area.getX(), area.getY(),
        baseColor.withAlpha(0.85f).darker(0.05f), area.getX(), area.getBottom(),
        false));
    g.fillRoundedRectangle(area, cornerSize);

    // 3. Highlight Inner Rim
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawRoundedRectangle(area.reduced(0.5f), cornerSize, 1.0f);

    // 4. Glow Bottom Line
    g.setColour(accent.withAlpha(0.15f));
    g.drawHorizontalLine((int)area.getBottom() - 1, area.getX() + cornerSize,
                         area.getRight() - cornerSize);
  }

  // Draw modern card-style panel with drop shadow
  static void drawCardPanel(juce::Graphics &g, juce::Rectangle<float> area,
                            juce::Colour baseColor, float cornerSize = 6.0f) {
    // 1. Multi-layer drop shadow
    for (int i = 3; i >= 1; --i) {
      g.setColour(juce::Colours::black.withAlpha(0.08f * i));
      g.fillRoundedRectangle(area.translated(0.0f, (float)i * 1.5f),
                             cornerSize);
    }

    // 2. Main card with subtle gradient
    juce::ColourGradient grad(baseColor.brighter(0.08f), area.getX(),
                              area.getY(), baseColor.darker(0.05f), area.getX(),
                              area.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(area, cornerSize);

    // 3. Top edge highlight
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawHorizontalLine((int)area.getY() + 1, area.getX() + cornerSize,
                         area.getRight() - cornerSize);

    // 4. Subtle border
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    g.drawRoundedRectangle(area, cornerSize, 1.0f);
  }

  /** Soft drop shadow for controls (steps, knobs, buttons). Draw before the control fill. */
  static void drawControlShadow(juce::Graphics &g, juce::Rectangle<float> rect,
                                float cornerRadius = 4.0f, float offsetY = 2.0f) {
    for (int i = 3; i >= 1; --i) {
      float o = offsetY * (float)i * 0.5f;
      float a = 0.12f - (float)i * 0.025f;
      g.setColour(juce::Colours::black.withAlpha(a));
      g.fillRoundedRectangle(rect.translated(0.0f, o), cornerRadius + (float)i * 0.5f);
    }
  }

  // Draw meter/level bar with glow
  static void drawGlowMeter(juce::Graphics &g, juce::Rectangle<float> area,
                            float level, juce::Colour color) {
    // Background track
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillRoundedRectangle(area, 2.0f);

    // Level fill
    auto fillArea = area;
    fillArea.setHeight(area.getHeight() * level);
    fillArea.setY(area.getBottom() - fillArea.getHeight());

    // Glow effect
    g.setColour(color.withAlpha(0.3f));
    g.fillRoundedRectangle(fillArea.expanded(2.0f), 3.0f);

    // Main gradient fill
    juce::ColourGradient levelGrad(
        color.brighter(0.3f), fillArea.getX(), fillArea.getY(),
        color.darker(0.2f), fillArea.getX(), fillArea.getBottom(), false);
    g.setGradientFill(levelGrad);
    g.fillRoundedRectangle(fillArea, 2.0f);

    // Top highlight
    if (fillArea.getHeight() > 4.0f) {
      g.setColour(juce::Colours::white.withAlpha(0.2f));
      g.fillRoundedRectangle(fillArea.withHeight(3.0f), 2.0f);
    }
  }
};

// Modern Glow Look & Feel for sliders with bloom effect
class ModernGlowLF : public juce::LookAndFeel_V4 {
public:
  void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, float rotaryStartAngle,
                        float rotaryEndAngle, juce::Slider &slider) override {
    juce::ignoreUnused(slider);
    auto fill = Theme::accent;
    auto bounds =
        juce::Rectangle<int>(x, y, width, height).toFloat().reduced(10);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto toAngle =
        rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto lineW = 3.5f;
    auto arcRadius = radius - lineW * 0.5f;

    // 1. Background Track
    juce::Path track;
    track.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), arcRadius,
                        arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle,
                        true);
    g.setColour(Theme::accent.withAlpha(0.1f));
    g.strokePath(track,
                 juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));

    // 2. The "Bloom" Glow Layer (3 passes for soft glow)
    juce::Path valueArc;
    valueArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), arcRadius,
                           arcRadius, 0.0f, rotaryStartAngle, toAngle, true);
    for (float i = 1.0f; i <= 3.0f; ++i) {
      g.setColour(fill.withAlpha(0.2f / i));
      g.strokePath(valueArc,
                   juce::PathStrokeType(lineW + (i * 3.0f),
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
    }

    // 3. Solid Core
    g.setColour(fill);
    g.strokePath(valueArc,
                 juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
  }

  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &backgroundColour,
                            bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override {
    juce::ignoreUnused(backgroundColour);
    auto cornerSize = 6.0f;
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto baseColour = Theme::bgPanel;

    if (shouldDrawButtonAsDown)
      baseColour = Theme::accent; // Flash accent
                                  // color on click
    else if (shouldDrawButtonAsHighlighted)
      baseColour = Theme::bgPanel.brighter(0.2f); // Glow on hover

    // Add button flash effect if property exists
    if (button.getProperties().contains("flashAlpha")) {
      float alpha = (float)button.getProperties()["flashAlpha"];
      if (alpha > 0.01f) {
        baseColour = baseColour.overlaidWith(Theme::accent.withAlpha(alpha));
      }
    }

    g.setColour(baseColour);
    g.fillRoundedRectangle(bounds, cornerSize);

    // Draw an accent border that glows
    g.setColour(shouldDrawButtonAsHighlighted ? Theme::accent
                                              : Theme::accent.withAlpha(0.3f));
    g.drawRoundedRectangle(bounds, cornerSize, 1.5f);

    // Add a subtle inner "bloom" if hovered
    if (shouldDrawButtonAsHighlighted) {
      g.setColour(Theme::accent.withAlpha(0.1f));
      g.fillRoundedRectangle(bounds.reduced(2.0f), cornerSize);
    }

    // Aurora Gradient Overlay (Simulated via
    // auroraStep property)
    if (button.getProperties().contains("auroraStep")) {
      float step = (float)button.getProperties()["auroraStep"];
      juce::ColourGradient grad(Theme::accent.withAlpha(0.1f),
                                bounds.getX() + (step * bounds.getWidth()),
                                bounds.getY(), Theme::grid.withAlpha(0.05f),
                                bounds.getRight(), bounds.getBottom(), false);
      g.setGradientFill(grad);
      g.fillRoundedRectangle(bounds, cornerSize);
    }
  }
};

// Theme Manager: 9 static (1–9) + 4 animated (10–13). CRT/particle effects only for 10–13.
class ThemeManager {
public:
  static int getNumThemes() { return 13; }
  static bool isAnimatedTheme(int themeId) {
    return themeId >= 10 && themeId <= 13;
  }
  static juce::String getThemeName(int id) {
    switch (id) {
    case 1:  return "Disco Dark";
    case 2:  return "Light Luminator";
    case 3:  return "Sandy Beach";
    case 4:  return "Great Pumpkin";
    case 5:  return "Rose";
    case 6:  return "Deepwater";
    case 7:  return "Dark Plum";
    case 8:  return "Forest";
    case 9:  return "Midnight";
    case 10: return "Vaporwave Neon";
    case 11: return "Plasma";
    case 12: return "Cybergridpunk";
    case 13: return "Matrix";
    default: return "Unknown";
    }
  }

  static void applyTheme(int themeId, juce::LookAndFeel &lf) {
    Theme::currentThemeId = juce::jlimit(1, 13, themeId);
    int id = Theme::currentThemeId;

    switch (id) {
    case 1: // Disco Dark (default)
      Theme::bgDark = juce::Colour(0xff0a0a0c);
      Theme::bgPanel = juce::Colour(0xff16161d);
      Theme::accent = juce::Colour(0xff00a3ff);
      Theme::grid = Theme::accent;
      Theme::bgGradEnd = Theme::bgDark;
      break;
    case 2: // Light Luminator
      Theme::bgDark = juce::Colour(0xffd1d1d1);
      Theme::bgPanel = juce::Colour(0xffe8e8e8);
      Theme::accent = juce::Colour(0xff333333);
      Theme::grid = juce::Colours::black;
      Theme::bgGradEnd = Theme::bgDark;
      break;
    case 3: // Sandy Beach
      Theme::bgDark = juce::Colour(0xff1c1917);
      Theme::bgPanel = juce::Colour(0xff2e2a27);
      Theme::accent = juce::Colour(0xfffde047);
      Theme::grid = Theme::accent;
      Theme::bgGradEnd = Theme::bgDark;
      break;
    case 4: // Great Pumpkin
      Theme::bgDark = juce::Colour(0xff1a0d00);
      Theme::bgPanel = juce::Colour(0xff2b1600);
      Theme::accent = juce::Colour(0xffff9500);
      Theme::grid = Theme::accent;
      Theme::bgGradEnd = Theme::bgDark;
      break;
    case 5: // Rose
      Theme::bgDark = juce::Colour(0xff1a0505);
      Theme::bgPanel = juce::Colour(0xff2d0a0a);
      Theme::accent = juce::Colour(0xfff1356d);
      Theme::grid = Theme::accent;
      Theme::bgGradEnd = Theme::bgDark;
      break;
    case 6: // Deepwater
      Theme::bgDark = juce::Colour(0xff01080e);
      Theme::bgPanel = juce::Colour(0xff051622);
      Theme::accent = juce::Colour(0xff17c3b2);
      Theme::grid = Theme::accent;
      Theme::bgGradEnd = Theme::bgDark;
      break;
    case 7: // Dark Plum
      Theme::bgDark = juce::Colour(0xff0d0014);
      Theme::bgPanel = juce::Colour(0xff1c0026);
      Theme::accent = juce::Colour(0xffd000ff);
      Theme::grid = Theme::accent;
      Theme::bgGradEnd = Theme::bgDark;
      break;
    case 8: // Forest
      Theme::bgDark = juce::Colour(0xff051005);
      Theme::bgPanel = juce::Colour(0xff0a1a0a);
      Theme::accent = juce::Colour(0xff20c040);
      Theme::grid = Theme::accent;
      Theme::bgGradEnd = Theme::bgDark;
      break;
    case 9: // Midnight
      Theme::bgDark = juce::Colour(0xff010103);
      Theme::bgPanel = juce::Colour(0xff050510);
      Theme::accent = juce::Colour(0xff5e5cff);
      Theme::grid = Theme::accent;
      Theme::bgGradEnd = Theme::bgDark;
      break;
    case 10: // Vaporwave Neon (animated)
      Theme::bgDark = juce::Colour(0xff12001a);
      Theme::bgGradEnd = juce::Colour(0xff2a0040);
      Theme::bgPanel = juce::Colour(0xff240033);
      Theme::accent = juce::Colour(0xff00fff2);
      Theme::grid = juce::Colour(0xffff0080);
      break;
    case 11: // Plasma (animated)
      Theme::bgDark = juce::Colour(0xff000000);
      Theme::bgGradEnd = juce::Colour(0xff0a0520);
      Theme::bgPanel = juce::Colour(0xff050510);
      Theme::accent = juce::Colour(0xff0080ff);
      Theme::grid = juce::Colours::white.withAlpha(0.1f);
      break;
    case 12: // Cybergridpunk (animated)
      Theme::bgDark = juce::Colour(0xff020202);
      Theme::bgGradEnd = juce::Colour(0xff0a0a12);
      Theme::bgPanel = juce::Colour(0xff0a0a0a);
      Theme::accent = juce::Colour(0xffff0080);
      Theme::grid = Theme::accent.withAlpha(0.2f);
      break;
    case 13: // Matrix (animated)
      Theme::bgDark = juce::Colour(0xff000400);
      Theme::bgGradEnd = juce::Colour(0xff000a00);
      Theme::bgPanel = juce::Colour(0xff001200);
      Theme::accent = juce::Colour(0xff00ff41);
      Theme::grid = juce::Colour(0xff00ff41).withAlpha(0.15f);
      break;
    default:
      Theme::bgGradEnd = Theme::bgDark;
      break;
    }

    // Push colors to JUCE Global LookAndFeel
    lf.setColour(juce::Slider::rotarySliderFillColourId, Theme::accent);
    lf.setColour(juce::Slider::thumbColourId, Theme::accent.brighter(0.4f));
    lf.setColour(juce::TextButton::buttonColourId, Theme::bgPanel);
    lf.setColour(juce::ComboBox::backgroundColourId, Theme::bgPanel);

    // Propagation to standard widgets (Fix 15)
    lf.setColour(juce::TextEditor::backgroundColourId,
                 Theme::bgPanel.darker(0.2f));
    lf.setColour(juce::TextEditor::textColourId, Theme::text);
    lf.setColour(juce::ListBox::backgroundColourId, Theme::bgPanel);
    lf.setColour(juce::Label::textColourId, Theme::text);
    lf.setColour(juce::TextButton::buttonColourId,
                 Theme::bgPanel.brighter(0.1f));
    lf.setColour(juce::TextButton::textColourOnId, Theme::accent);
  }

  // Animation update for animated themes 10–13 only (called at 30Hz max for performance)
  static void updateAnimation(int themeId, float &animStep,
                              juce::LookAndFeel &lf) {
    if (!isAnimatedTheme(themeId)) {
      breathAlpha = 0.0f;
      return;
    }
    if (themeId == 10) { // Vaporwave Neon - full rainbow
      animStep += 0.005f;
      if (animStep > 1.0f) animStep = 0.0f;
      Theme::accent = juce::Colour::fromHSV(animStep, 0.8f, 1.0f, 1.0f);
      lf.setColour(juce::Slider::rotarySliderFillColourId, Theme::accent);
      lf.setColour(juce::Slider::thumbColourId, Theme::accent.brighter(0.4f));
    } else if (themeId == 11) { // Plasma - hue shift
      animStep += 0.01f;
      float hue = 0.6f + 0.2f * std::sin(animStep * 0.5f);
      Theme::accent = juce::Colour::fromHSV(hue, 0.8f, 1.0f, 1.0f);
      lf.setColour(juce::Slider::rotarySliderFillColourId, Theme::accent);
    } else if (themeId == 12) { // Cybergridpunk - grid pulse
      animStep += 0.008f;
      float breath = 0.5f + 0.5f * std::sin(animStep * 5.0f);
      Theme::grid = Theme::accent.withAlpha(0.05f + (breath * 0.15f));
    } else if (themeId == 13) { // Matrix - green brightness breathing
      animStep += 0.006f;
      float brightness = 0.75f + 0.25f * std::sin(animStep * 4.0f);
      Theme::accent = juce::Colour::fromHSV(0.38f, 1.0f, brightness, 1.0f);
      lf.setColour(juce::Slider::rotarySliderFillColourId, Theme::accent);
    }
    breathAlpha = 0.03f + 0.04f * std::sin(animStep * 3.0f);
  }

  // Breathing alpha for animated button glow (read by BridgeLookAndFeel)
  static inline float breathAlpha = 0.0f;
};

// --- MODERN BRIDGE LOOK AND FEEL ---
class BridgeLookAndFeel : public juce::LookAndFeel_V4 {
public:
  BridgeLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, Theme::bgDark);
    setColour(juce::TextButton::buttonColourId, Theme::bgMid);
    setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    setColour(juce::ComboBox::backgroundColourId, Theme::bgMid);
    setColour(juce::Slider::backgroundColourId, Theme::bgMid);
    setColour(juce::Slider::thumbColourId, Theme::accent);
    setColour(juce::Slider::trackColourId, Theme::accent.withAlpha(0.5f));
  }

  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &backgroundColour,
                            bool isMouseOverButton,
                            bool isButtonDown) override {
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f, 0.5f);
    auto baseColour = backgroundColour;
    if (button.getToggleState())
      baseColour = button.findColour(juce::TextButton::buttonOnColourId);

    Theme::drawStylishPanel(g, bounds, baseColour, 4.0f);

    // Breathing glow overlay (animated themes)
    if (ThemeManager::breathAlpha > 0.01f) {
      g.setColour(Theme::accent.withAlpha(ThemeManager::breathAlpha));
      g.fillRoundedRectangle(bounds, 4.0f);
    }

    if (isMouseOverButton) {
      g.setColour(juce::Colours::white.withAlpha(0.1f));
      g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }

    if (isButtonDown) {
      g.setColour(juce::Colours::black.withAlpha(0.3f));
      g.fillRoundedRectangle(bounds, 4.0f);
    }
  }

  void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, float rotaryStartAngle,
                        float rotaryEndAngle, juce::Slider &slider) override {
    juce::ignoreUnused(slider);
    auto bounds =
        juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height)
            .reduced(10.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto centreX = bounds.getCentreX();
    auto centreY = bounds.getCentreY();
    auto angle =
        rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    g.setColour(Theme::bgMid);
    juce::Path track;
    track.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                        rotaryStartAngle, rotaryEndAngle, true);
    g.strokePath(track, juce::PathStrokeType(3.0f));

    juce::Path val;
    val.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle,
                      angle, true);

    // Glow passes
    for (float i = 1.0f; i <= 3.0f; ++i) {
      g.setColour(Theme::accent.withAlpha(0.15f / i));
      g.strokePath(val, juce::PathStrokeType(3.0f + (i * 2.5f),
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    }

    g.setColour(Theme::accent);
    g.strokePath(val, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));

    juce::Path p;
    p.addRectangle(-1.5f, -radius, 3.0f, radius * 0.7f);
    p.applyTransform(
        juce::AffineTransform::rotation(angle).translated(centreX, centreY));
    g.setColour(juce::Colours::white);
    g.fillPath(p);
  }

  void drawLinearSlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, float minSliderPos, float maxSliderPos,
                        const juce::Slider::SliderStyle style,
                        juce::Slider &slider) override {
    if (style == juce::Slider::LinearBar ||
        style == juce::Slider::LinearBarVertical) {
      auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width,
                                           (float)height);
      g.setColour(Theme::bgMid);
      g.fillRoundedRectangle(bounds, 3.0f);

      auto fill = bounds;
      if (style == juce::Slider::LinearBar)
        fill.setWidth(sliderPos - (float)x);
      else
        fill.setHeight(bounds.getBottom() - sliderPos);

      g.setGradientFill(
          juce::ColourGradient(Theme::accent.brighter(0.2f), fill.getX(),
                               fill.getY(), Theme::accent.darker(0.2f),
                               fill.getRight(), fill.getBottom(), false));
      g.fillRoundedRectangle(fill, 3.0f);
    } else {
      LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                       minSliderPos, maxSliderPos, style,
                                       slider);
    }
  }

  // Custom Scrollbar Style
  void drawScrollbar(juce::Graphics &g, juce::ScrollBar &scrollbar, int x,
                     int y, int width, int height, bool isScrollbarVertical,
                     int thumbStartPosition, int thumbSize, bool isMouseOver,
                     bool isMouseDown) override {
    juce::ignoreUnused(scrollbar, isMouseDown);
    g.fillAll(Theme::bgDark); // Background track

    // Draw the Thumb
    juce::Rectangle<int> thumbBounds;
    if (isScrollbarVertical)
      thumbBounds = {x + 2, thumbStartPosition, width - 4, thumbSize};
    else
      thumbBounds = {thumbStartPosition, y + 2, thumbSize, height - 4};

    g.setColour(isMouseOver ? Theme::accent : Theme::accent.withAlpha(0.5f));
    g.fillRoundedRectangle(thumbBounds.toFloat(), 3.0f);
  }
};

// --- BIG FANCY DIAL LOOK AND FEEL ---
class FancyDialLF : public ModernGlowLF {
public:
  void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, float rotaryStartAngle,
                        float rotaryEndAngle, juce::Slider &slider) override {
    auto outline = Theme::accent;
    auto fill = Theme::accent.withAlpha(0.6f);

    bool chunky = (width >= 48 && height >= 48);  // Macro-style big chunky knob
    auto bounds =
        juce::Rectangle<int>(x, y, width, height).toFloat().reduced(chunky ? 4.0f : 6.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto toAngle =
        rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto lineW = chunky ? 10.0f : 6.0f;
    auto arcRadius = radius - lineW * 1.5f;

    // 1. Background Orb
    g.setGradientFill(juce::ColourGradient(
        Theme::bgPanel.brighter(0.05f), bounds.getCentreX(),
        bounds.getCentreY(), Theme::bgDark, bounds.getCentreX(),
        bounds.getBottom(), true));
    g.fillEllipse(bounds.reduced(lineW));

    // 2. Outer Glow Track
    juce::Path track;
    track.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), arcRadius,
                        arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle,
                        true);
    g.setColour(Theme::accent.withAlpha(0.1f));
    g.strokePath(track,
                 juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));

    // 3. The Value Arc (with Bloom)
    juce::Path valueArc;
    valueArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), arcRadius,
                           arcRadius, 0.0f, rotaryStartAngle, toAngle, true);

    for (float i = 1.0f; i <= 3.0f; ++i) {
      g.setColour(outline.withAlpha(0.2f / i));
      g.strokePath(valueArc,
                   juce::PathStrokeType(lineW + (i * 4.0f),
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));
    }

    g.setColour(outline);
    g.strokePath(valueArc,
                 juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));

    // 4. Dot Indicator (chunky knob: bigger dot)
    float dotR = chunky ? 6.0f : 4.0f;
    juce::Point<float> dotPos(
        bounds.getCentreX() +
            (arcRadius)*std::cos(toAngle - juce::MathConstants<float>::halfPi),
        bounds.getCentreY() +
            (arcRadius)*std::sin(toAngle - juce::MathConstants<float>::halfPi));

    g.setColour(juce::Colours::white);
    g.fillEllipse(dotPos.getX() - dotR, dotPos.getY() - dotR, dotR * 2.0f,
                  dotR * 2.0f);

    g.setColour(Theme::accent);
    g.drawEllipse(dotPos.getX() - dotR, dotPos.getY() - dotR, dotR * 2.0f,
                  dotR * 2.0f, 1.5f);

    // Hover: glow ring
    if (slider.isMouseOverOrDragging()) {
      g.setColour(Theme::accent.withAlpha(0.3f));
      g.drawEllipse(bounds.reduced(4.0f), 2.0f);
    }

    // Dragging: value popup
    if (slider.isMouseButtonDown()) {
      g.setColour(juce::Colours::white);
      g.setFont(juce::FontOptions(12.0f));
      g.drawText(juce::String(slider.getValue(), 1), bounds.toNearestInt(),
                 juce::Justification::centred);
    }
  }
};
