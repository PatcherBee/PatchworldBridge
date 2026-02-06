/*
  ==============================================================================
    Source/UI/LayoutHelpers.h
    Role: Reusable layout utilities for resized() (vertical/horizontal stacks).
  ==============================================================================
*/
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <initializer_list>
#include <vector>

namespace LayoutHelpers {

/** Lay out components in a row (left to right) with equal width and gap. */
inline void layoutHorizontally(juce::Rectangle<int> area, int gap,
                               std::vector<juce::Component *> components) {
  if (components.empty())
    return;
  const int n = (int)components.size();
  const int totalGap = (n - 1) * gap;
  const int w = (area.getWidth() - totalGap) / n;
  int x = area.getX();
  for (auto *c : components) {
    if (c)
      c->setBounds(x, area.getY(), w, area.getHeight());
    x += w + gap;
  }
}

/** Convenience: lay out from initializer_list. */
inline void layoutHorizontally(juce::Rectangle<int> area, int gap,
                               std::initializer_list<juce::Component *> components) {
  layoutHorizontally(area, gap, std::vector<juce::Component *>(components));
}

/** Lay out components in a column (top to bottom) with equal height and gap. */
inline void layoutVertically(juce::Rectangle<int> area, int gap,
                             std::vector<juce::Component *> components) {
  if (components.empty())
    return;
  const int n = (int)components.size();
  const int totalGap = (n - 1) * gap;
  const int h = (area.getHeight() - totalGap) / n;
  int y = area.getY();
  for (auto *c : components) {
    if (c)
      c->setBounds(area.getX(), y, area.getWidth(), h);
    y += h + gap;
  }
}

/** Convenience: lay out from initializer_list. */
inline void layoutVertically(juce::Rectangle<int> area, int gap,
                             std::initializer_list<juce::Component *> components) {
  layoutVertically(area, gap, std::vector<juce::Component *>(components));
}

} // namespace LayoutHelpers
