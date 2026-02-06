/*
  ==============================================================================
    Source/UI/Panels/StatusBar.h
    Role: Footer bar with Tooltips, CPU Stats, Audio Info, and UI Zoom.
  ==============================================================================
*/
#pragma once
#include "../ControlHelpers.h"
#include "../Fonts.h"
#include "../ScaledComponent.h"
#include "../Theme.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

class StatusBarComponent : public ScaledComponent, public juce::SettableTooltipClient {
public:
  std::function<void(float)> onScaleChanged;

  StatusBarComponent() {
    addAndMakeVisible(lblStatus);
    lblStatus.setFont(Fonts::header());
    lblStatus.setColour(juce::Label::textColourId, Theme::text.withAlpha(0.7f));
    lblStatus.setText("Ready", juce::dontSendNotification);

    addAndMakeVisible(lblCpu);
    lblCpu.setFont(Fonts::bodyBold());
    lblCpu.setColour(juce::Label::textColourId, Theme::accent);
    lblCpu.setTooltip("Audio Thread CPU Usage");

    addAndMakeVisible(lblAudioInfo);
    lblAudioInfo.setFont(Fonts::body());
    lblAudioInfo.setColour(juce::Label::textColourId, Theme::text.withAlpha(0.5f));

    addAndMakeVisible(lblBpmTransport);
    lblBpmTransport.setFont(Fonts::small());
    lblBpmTransport.setColour(juce::Label::textColourId, Theme::text.withAlpha(0.8f));
    lblBpmTransport.setText("— BPM · Stopped", juce::dontSendNotification);
    lblBpmTransport.setTooltip("Current BPM and transport state.");

    addAndMakeVisible(lblZoom);
    lblZoom.setText("Zoom:", juce::dontSendNotification);
    lblZoom.setFont(Fonts::small());
    lblZoom.setColour(juce::Label::textColourId, Theme::text.withAlpha(0.7f));

    addAndMakeVisible(sliderScale);
    sliderScale.setValue(1.0);
    sliderScale.setDefaultValue(1.0);
    sliderScale.setSliderStyle(juce::Slider::LinearHorizontal);
    sliderScale.setTextBoxStyle(juce::Slider::TextBoxRight, true, 50, 22);
    sliderScale.setTooltip("UI Zoom (50-200%). Click value to type, or drag (slow/precise).");
    sliderScale.setWantsKeyboardFocus(true);
    sliderScale.onValueChange = [this] {
      float s = (float)sliderScale.getValue();
      pendingScale = s;
      if (onScaleChanged)
        onScaleChanged(s); // Apply immediately - no debounce
    };
  }

  /** Called from TimerHub (master tick, ~10Hz). Handles stats update. */
  void tickFromMaster() {
    if (++statsTick >= 10) {
      statsTick = 0;
      updateStats();
    }
  }

  void setScale(float s) {
    sliderScale.setValue((double)s, juce::dontSendNotification);
  }

  /** Set scale and optionally notify (for shortcuts so zoom is applied). */
  void setScale(float s, bool notify) {
    sliderScale.setValue((double)s, juce::dontSendNotification);
    if (notify && onScaleChanged)
      onScaleChanged((float)sliderScale.getValue());
  }

  float getScale() const {
    return (float)sliderScale.getValue();
  }

  void setStatus(const juce::String &text) {
    lblStatus.setText(text, juce::dontSendNotification);
  }
  void setText(const juce::String &t, juce::NotificationType) {
    setStatus(t);
  }

  /** Update BPM and transport state (e.g. "120 BPM · Stopped"). */
  void setBpmAndTransport(double bpm, bool playing) {
    juce::String s = juce::String(juce::roundToInt(bpm)) + " BPM · "
        + (playing ? "Playing" : "Stopped");
    if (lblBpmTransport.getText() != s)
      lblBpmTransport.setText(s, juce::dontSendNotification);
  }

  void setDeviceManager(juce::AudioDeviceManager *dm) {
    deviceManager = dm;
    updateStats();
  }

  void paint(juce::Graphics &g) override {
    auto r = getLocalBounds();
    g.setColour(Theme::bgDark.darker(0.2f));
    g.fillRect(r);
    g.setColour(Theme::bgPanel.brighter(0.1f));
    g.drawHorizontalLine(0, 0.0f, (float)getWidth());
  }

  void resized() override {
    auto r = getLocalBounds().reduced(4, 2);
    auto zoomArea = r.removeFromRight(150);  // Space for label + slider + editable text box
    lblZoom.setBounds(zoomArea.removeFromLeft(42));
    sliderScale.setBounds(zoomArea.reduced(2));
    r.removeFromRight(8);
    lblAudioInfo.setBounds(r.removeFromRight(140));
    r.removeFromRight(10);
    lblCpu.setBounds(r.removeFromRight(52));
    lblBpmTransport.setBounds(r.removeFromRight(110));
    r.removeFromRight(6);
    lblStatus.setBounds(r);
  }

  void applyScale(float scale) override {
    ScaledComponent::applyScale(scale);
    float fontScale = juce::jlimit(0.8f, 2.0f, scale);
    lblStatus.setFont(Fonts::header().withHeight(Fonts::header().getHeight() * fontScale));
    lblCpu.setFont(Fonts::bodyBold().withHeight(Fonts::bodyBold().getHeight() * fontScale));
    lblZoom.setFont(Fonts::small().withHeight(Fonts::small().getHeight() * fontScale));
    lblAudioInfo.setFont(Fonts::body().withHeight(Fonts::body().getHeight() * fontScale));
    lblBpmTransport.setFont(Fonts::small().withHeight(Fonts::small().getHeight() * fontScale));
    repaint();
  }

private:
  float pendingScale = 1.0f;
  int statsTick = 0;
  juce::Label lblStatus;
  juce::Label lblBpmTransport;
  juce::Label lblZoom;
  juce::Label lblCpu;
  juce::Label lblAudioInfo;
  ControlHelpers::ZoomSlider sliderScale;
  juce::AudioDeviceManager *deviceManager = nullptr;

  void updateStats() {
    if (!deviceManager)
      return;
    double cpu = deviceManager->getCpuUsage();
    lblCpu.setText("CPU: " + juce::String(cpu * 100.0, 1) + "%",
                   juce::dontSendNotification);
    if (cpu > 0.8)
      lblCpu.setColour(juce::Label::textColourId, juce::Colours::red);
    else if (cpu > 0.5)
      lblCpu.setColour(juce::Label::textColourId, juce::Colours::orange);
    else
      lblCpu.setColour(juce::Label::textColourId, Theme::accent);

    if (auto *device = deviceManager->getCurrentAudioDevice()) {
      lblAudioInfo.setText(
          juce::String(device->getCurrentSampleRate(), 0) + " Hz / " +
              juce::String(device->getCurrentBufferSizeSamples()) + " spls",
          juce::dontSendNotification);
    } else {
      lblAudioInfo.setText("No Audio Device", juce::dontSendNotification);
    }
  }
};
