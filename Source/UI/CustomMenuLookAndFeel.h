/*
  ==============================================================================
    Source/UI/CustomMenuLookAndFeel.h
    Role: Cyberpunk/Pro style for Popup Menus (Glassmorphism + Glow)
  ==============================================================================
*/
#pragma once
#include "Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

class CustomMenuLookAndFeel : public juce::LookAndFeel_V4 {
public:
  CustomMenuLookAndFeel() {
    setColour(juce::PopupMenu::backgroundColourId,
              Theme::bgPanel.withAlpha(0.95f));
    setColour(juce::PopupMenu::textColourId, Theme::text);
    setColour(juce::PopupMenu::headerTextColourId,
              Theme::accent.brighter(0.2f));
    setColour(juce::PopupMenu::highlightedBackgroundColourId,
              Theme::accent.withAlpha(0.2f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
  }

  void drawPopupMenuBackground(juce::Graphics &g, int width,
                              int height) override {
    auto r = juce::Rectangle<float>(0, 0, (float)width, (float)height);
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(r.translated(4, 4), 6.0f);
    g.setColour(Theme::bgDark.withAlpha(0.96f));
    g.fillRoundedRectangle(r, 6.0f);
    g.setGradientFill(juce::ColourGradient(
        Theme::bgPanel.withAlpha(0.3f), 0, 0,
        juce::Colours::transparentBlack, 0, (float)height, false));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(Theme::accent.withAlpha(0.3f));
    g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
  }

  void drawPopupMenuItem(juce::Graphics &g,
                         const juce::Rectangle<int> &area, bool isSeparator,
                         bool isActive, bool isHighlighted, bool isTicked,
                         bool hasSubMenu, const juce::String &text,
                         const juce::String &shortcutKeyText,
                         const juce::Drawable *icon,
                         const juce::Colour *textColour) override {
    juce::ignoreUnused(icon, hasSubMenu);
    auto r = area.toFloat();
    if (isSeparator) {
      g.setColour(Theme::text.withAlpha(0.1f));
      g.drawHorizontalLine((int)r.getCentreY(), r.getX() + 10.0f,
                           r.getRight() - 10.0f);
      return;
    }
    if (isHighlighted && isActive) {
      g.setColour(Theme::accent.withAlpha(0.2f));
      g.fillRoundedRectangle(r.reduced(4.0f, 1.0f), 4.0f);
      g.setColour(Theme::accent);
      g.fillRect(r.getX() + 2.0f, r.getY() + 4.0f, 3.0f, r.getHeight() - 8.0f);
    }
    g.setColour(isActive ? (textColour ? *textColour : Theme::text)
                         : Theme::text.withAlpha(0.4f));
    g.setFont(isHighlighted ? juce::FontOptions(16.0f).withStyle("Bold")
                            : juce::FontOptions(16.0f));
    auto textRect = r.reduced(30, 0);
    g.drawFittedText(text, textRect.toNearestInt(),
                     juce::Justification::centredLeft, 1);
    if (shortcutKeyText.isNotEmpty()) {
      g.setColour(Theme::text.withAlpha(0.5f));
      g.setFont(12.0f);
      g.drawText(shortcutKeyText, r.reduced(24, 0),
                 juce::Justification::centredRight);
    }
    // Right-pointing arrow on every menu item (submenu indicator style)
    g.setColour(Theme::text.withAlpha(0.6f));
    auto arrowR = r.removeFromRight(20).reduced(6).toFloat();
    juce::Path p;
    p.addTriangle(arrowR.getX(), arrowR.getY(), arrowR.getRight(),
                  arrowR.getCentreY(), arrowR.getX(), arrowR.getBottom());
    g.strokePath(p, juce::PathStrokeType(1.5f));
    if (isTicked) {
      g.setColour(Theme::accent);
      auto tickR = r.removeFromLeft(25).reduced(8).toFloat();
      juce::Path tickPath;
      tickPath.startNewSubPath(tickR.getX(), tickR.getCentreY());
      tickPath.lineTo(tickR.getCentreX() - 2, tickR.getBottom() - 2);
      tickPath.lineTo(tickR.getRight(), tickR.getY());
      g.strokePath(tickPath,
                   juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
      g.setColour(Theme::accent.withAlpha(0.3f));
      g.fillEllipse(tickR.expanded(4.0f));
    }
  }

  void drawPopupMenuSectionHeader(juce::Graphics &g,
                                  const juce::Rectangle<int> &area,
                                  const juce::String &sectionName) override {
    g.setColour(Theme::accent.brighter(0.2f));
    g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    g.drawText(sectionName.toUpperCase(), area.reduced(10, 0),
               juce::Justification::centredLeft, true);
    g.setColour(Theme::accent.withAlpha(0.2f));
    g.drawHorizontalLine(area.getBottom() - 2, (float)area.getX() + 10,
                         (float)area.getRight() - 60);
  }

  juce::Font getPopupMenuFont() override {
    return juce::FontOptions("Verdana", 16.0f, juce::Font::plain);
  }

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomMenuLookAndFeel)
};
