/*
  ==============================================================================
    Source/Components/MidiLearnOverlay.h
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
    setInterceptsMouseClicks(false, false);
    setAlwaysOnTop(true);
  }

  void setOverlayActive(bool active) {
    setVisible(active);
    setInterceptsMouseClicks(active, active);
    repaint();
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(juce::Colours::cyan.withAlpha(0.2f));

    if (hoveredComponent != nullptr) {
      auto bounds = hoveredComponent->getScreenBounds().translated(
          -getScreenX(), -getScreenY());
      g.setColour(juce::Colours::yellow);
      g.drawRect(bounds, 3.0f);
    }

    juce::String waiting = manager.getSelectedParameter();
    if (waiting.isNotEmpty()) {
      g.setColour(juce::Colours::white);
      g.setFont(24.0f);
      g.drawText("LEARNING: " + waiting + "\nMove a MIDI Control...",
                 getLocalBounds(), juce::Justification::centred, true);
    }
  }

  void mouseMove(const juce::MouseEvent &e) override {
    updateHoveredComponent(e);
  }

  void mouseDown(const juce::MouseEvent &e) override {
    if (hoveredComponent != nullptr) {
      juce::String paramID = hoveredComponent->getProperties()
                                 .getWithDefault("paramID", "")
                                 .toString();
      if (paramID.isNotEmpty()) {
        manager.setSelectedParameterForLearning(paramID);
        repaint();
      }
    }
  }

private:
  MidiMappingManager &manager;
  juce::Component &rootContent;
  juce::Component *hoveredComponent = nullptr;

  void updateHoveredComponent(const juce::MouseEvent &e) {
    juce::Point<int> rootPos = rootContent.getLocalPoint(this, e.getPosition());
    juce::Component *target = rootContent.getComponentAt(rootPos);

    // Drill down
    while (target != nullptr) {
      auto local = rootContent.getLocalPoint(target, rootPos);
      auto *child = target->getComponentAt(local);
      if (child == nullptr)
        break;
      target = child;
    }

    // Bubble up to find one with paramID
    while (target != nullptr) {
      if (target->getProperties().contains("paramID"))
        break;
      target = target->getParentComponent();
    }

    if (target != hoveredComponent) {
      hoveredComponent = target;
      repaint();
    }
  }
};
