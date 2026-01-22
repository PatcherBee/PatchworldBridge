/*
  ==============================================================================
    Source/Components/MidiLearnOverlay.h
    Status: CRASH FIXED (Removed infinite recursion in hitTest)
  ==============================================================================
*/
#pragma once
#include "MidiMappingManager.h"
#include <JuceHeader.h>

class MidiLearnOverlay : public juce::Component {
public:
  MidiLearnOverlay(MidiMappingManager &managerRef,
                   juce::Component &rootContentComp)
      : manager(managerRef), rootContent(rootContentComp) {
    setInterceptsMouseClicks(true, true);
    setVisible(false); // Start hidden
  }

  void setOverlayActive(bool active) {
    setVisible(active);
    repaint();
  }

  // CRITICAL FIX: Custom hit detection that ignores 'this' to prevent recursion
  bool hitTest(int x, int y) override {
    juce::Point<int> pt(x, y);

    // Find the component under the mouse, ignoring the overlay itself
    auto *target = findComponentUnderMouse(pt);

    if (target == nullptr)
      return false;

    // Bubble up to find if any parent (or the target) has a paramID
    auto *c = target;
    while (c != nullptr && c != &rootContent) {
      if (c->getProperties().contains("paramID"))
        return true; // We want to intercept clicks on mappable controls
      c = c->getParentComponent();
    }
    return false; // Allow clicks to pass through if not mapping
  }

  void paint(juce::Graphics &g) override {
    // Highlight the component we are hovering over
    if (hoveredComponent != nullptr) {
      // Get bounds relative to the overlay
      auto bounds =
          getLocalArea(hoveredComponent, hoveredComponent->getLocalBounds());

      g.setColour(juce::Colours::yellow.withAlpha(0.6f));
      g.drawRect(bounds, 3.0f);
      g.setColour(juce::Colours::yellow.withAlpha(0.2f));
      g.fillRect(bounds);
    }

    // Display "LEARNING: [ID]" Box
    juce::String waiting = manager.getSelectedParameter();
    if (waiting.isNotEmpty()) {
      auto r = getLocalBounds().removeFromTop(80).reduced(20).translated(0, 40);
      g.setColour(juce::Colours::black.withAlpha(0.8f));
      g.fillRect(r);
      g.setColour(juce::Colours::yellow);
      g.drawRect(r, 2.0f);
      g.setFont(20.0f);
      g.drawFittedText("LEARNING: " + waiting + "\nMove a HW Control...", r,
                       juce::Justification::centred, 2);
    }
  }

  void mouseMove(const juce::MouseEvent &e) override {
    if (isVisible())
      updateHoveredComponent(e);
  }

  void mouseDown(const juce::MouseEvent &e) override {
    if (hoveredComponent != nullptr) {
      auto paramID = hoveredComponent->getProperties()["paramID"].toString();
      if (paramID.isNotEmpty()) {
        selectedComponent = hoveredComponent; // HOLD the visual box
        manager.setSelectedParameterForLearning(paramID);
        repaint();
      }
    }
  }

private:
  MidiMappingManager &manager;
  juce::Component &rootContent;
  juce::Component *hoveredComponent = nullptr;
  juce::Component *selectedComponent =
      nullptr; // NEW: Holds the click selection

  // Helper to safely find components below the overlay
  juce::Component *findComponentUnderMouse(juce::Point<int> pt) {
    // We iterate the rootContent's children in reverse Z-order (top to bottom)
    // We skip 'this' (the overlay)
    for (int i = rootContent.getNumChildComponents() - 1; i >= 0; --i) {
      auto *child = rootContent.getChildComponent(i);

      if (child == this || !child->isVisible())
        continue;

      if (child->getBounds().contains(pt)) {
        // Convert point to child's local space and drill down
        auto local = pt - child->getPosition();
        if (child->contains(local)) {
          return child->getComponentAt(local);
        }
      }
    }
    return nullptr;
  }

  void updateHoveredComponent(const juce::MouseEvent &e) {
    juce::Point<int> rootPos = rootContent.getLocalPoint(this, e.getPosition());

    // Use our safe finder instead of rootContent.getComponentAt
    juce::Component *target = findComponentUnderMouse(rootPos);

    // Bubble up to find one with paramID
    auto *scan = target;
    while (scan != nullptr && scan != &rootContent) {
      if (scan->getProperties().contains("paramID")) {
        target = scan; // Focus on the mappable parent (e.g. Slider, not
                       // Slider's TextBox)
        break;
      }
      scan = scan->getParentComponent();
    }

    if (target != hoveredComponent) {
      hoveredComponent = target;
      repaint();
    }
  }
};