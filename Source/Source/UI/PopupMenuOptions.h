/*
  ==============================================================================
    Source/UI/PopupMenuOptions.h
    Role: Helper so popup menus in modules are readable and draw over top-level
          (not clipped inside ModuleWindow). Use for all right-click menus.
  ==============================================================================
*/
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace PopupMenuOptions {
/** Standard item height for popup menus (readable, not tiny). */
constexpr int kStandardItemHeight = 36;

/** Returns Options with target for positioning, no parent (menu uses addToDesktop
 *  so it is not clipped by ModuleWindow or any parent). Larger item height. */
inline juce::PopupMenu::Options forComponent(juce::Component *target) {
  if (!target)
    return juce::PopupMenu::Options();
  return juce::PopupMenu::Options()
      .withTargetComponent(target)
      .withParentComponent(nullptr)
      .withStandardItemHeight(kStandardItemHeight);
}
} // namespace PopupMenuOptions
