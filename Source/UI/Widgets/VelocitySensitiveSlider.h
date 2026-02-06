/*
  ==============================================================================
    Source/UI/Widgets/VelocitySensitiveSlider.h
    Role: Slider with velocity-sensitive dragging (slower drag = finer control)
  ==============================================================================
*/
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class VelocitySensitiveSlider : public juce::Slider {
public:
  VelocitySensitiveSlider() {
    setVelocityBasedMode(true);
    setVelocityModeParameters(1.0, 1, 0.0, false);
    setMouseDragSensitivity(200); // Base sensitivity
  }
  
  explicit VelocitySensitiveSlider(SliderStyle style) : juce::Slider(style, NoTextBox) {
    setVelocityBasedMode(true);
    setVelocityModeParameters(1.0, 1, 0.0, false);
    setMouseDragSensitivity(200);
  }
  
  void mouseDrag(const juce::MouseEvent& e) override {
    // Calculate drag velocity (pixels per second)
    auto now = juce::Time::getMillisecondCounterHiRes();
    double dt = (now - lastDragTime) / 1000.0;
    if (dt > 0.001) {
      auto delta = e.position - lastDragPos;
      double velocity = delta.getDistanceFromOrigin() / dt;
      
      // Adaptive sensitivity: slower drag = finer control
      // velocity range: ~10 (very slow) to ~2000 (very fast) pixels/sec
      double sensitivity = 200.0; // Base
      if (velocity < 50.0) {
        // Very slow: fine control (10x finer)
        sensitivity = 2000.0;
      } else if (velocity < 200.0) {
        // Slow: medium-fine control (3x finer)
        sensitivity = 600.0;
      } else if (velocity > 800.0) {
        // Fast: coarse control (2x coarser)
        sensitivity = 100.0;
      }
      
      setMouseDragSensitivity((int)sensitivity);
      lastDragTime = now;
      lastDragPos = e.position;
    }
    
    juce::Slider::mouseDrag(e);
  }
  
  void mouseDown(const juce::MouseEvent& e) override {
    lastDragTime = juce::Time::getMillisecondCounterHiRes();
    lastDragPos = e.position;
    juce::Slider::mouseDown(e);
  }
  
private:
  double lastDragTime = 0.0;
  juce::Point<float> lastDragPos;
  
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VelocitySensitiveSlider)
};
