#pragma once
#include "../Core/Common.h"
#include "../UI/ControlHelpers.h"
#include "../UI/Fonts.h"
#include "../UI/PopupMenuOptions.h"
#include "../UI/Theme.h"
#include <atomic>
#include <functional>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// --- Level Meter Data (Lock-Free, peak & clip, optional peak hold 1.5s) ---
struct LevelMeterData {
  std::atomic<float> currentLevel{0.0f};
  std::atomic<float> peak{0.0f};
  std::atomic<int> clipCounter{0};
  std::atomic<uint32_t> peakHoldUntilMs{0};
  static constexpr uint32_t kPeakHoldMs = 1500;

  void update(float nextVal) noexcept {
    auto prevVal = currentLevel.load(std::memory_order_relaxed);
    while (nextVal > prevVal && !currentLevel.compare_exchange_weak(
                                    prevVal, nextVal, std::memory_order_release,
                                    std::memory_order_relaxed)) {
    }
    if (nextVal > peak.load(std::memory_order_relaxed)) {
      peak.store(nextVal, std::memory_order_relaxed);
      peakHoldUntilMs.store(
          (uint32_t)juce::Time::getMillisecondCounter() + kPeakHoldMs,
          std::memory_order_relaxed);
    }
    if (nextVal >= 1.0f)
      clipCounter.fetch_add(1, std::memory_order_relaxed);
  }

  float readAndReset() noexcept {
    return currentLevel.exchange(0.0f, std::memory_order_acquire);
  }

  float getPeak() const noexcept { return peak.load(std::memory_order_relaxed); }
  void decayPeak(float rate = 0.95f) noexcept {
    uint32_t now = (uint32_t)juce::Time::getMillisecondCounter();
    if (now < peakHoldUntilMs.load(std::memory_order_relaxed))
      return;
    float p = peak.load(std::memory_order_relaxed);
    peak.store(p * rate, std::memory_order_relaxed);
  }
  int consumeClip() noexcept { return clipCounter.exchange(0, std::memory_order_acquire); }
};

// --- Optimized Meter Component ---
struct MeterComponent : public juce::Component {
  LevelMeterData *source = nullptr;
  float currentLevel = 0.0f;
  int channelIndex = 0;
  /** When non-null and *skipMeterPaintPtr is true, skip painting (GPU draws). Set by MixerStrip. */
  bool *skipMeterPaintPtr = nullptr;

  // Cached graphics
  juce::ColourGradient meterGrad;

  void resized() override {
    auto bounds = getLocalBounds().toFloat();
    juce::Colour meterColor = Theme::getChannelColor(channelIndex + 1);

    meterGrad = juce::ColourGradient(meterColor.brighter(0.3f), bounds.getX(),
                                     bounds.getY(), meterColor.darker(0.2f),
                                     bounds.getX(), bounds.getBottom(), false);
  }

  void setLevel(float newLevel) {
    if (std::abs(newLevel - currentLevel) > 0.005f) {
      currentLevel = newLevel;
      if (isVisible())
        repaint();
    }
  }

  void paint(juce::Graphics &g) override {
    if (skipMeterPaintPtr && *skipMeterPaintPtr)
      return;

    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillRoundedRectangle(bounds, 3.0f);

    float meterH = bounds.getHeight() * currentLevel;
    if (meterH <= 0.0f)
      return;

    auto fillBounds = bounds.withTop(bounds.getBottom() - meterH);

    // Gradient Fill
    g.setGradientFill(meterGrad);
    g.fillRoundedRectangle(fillBounds, 3.0f);

    // Glow (Phantom meter)
    if (currentLevel > 0.5f) {
      g.setColour(Theme::getChannelColor(channelIndex + 1).withAlpha(0.25f));
      g.fillRoundedRectangle(fillBounds.expanded(2.0f), 4.0f);
    }

    // Overdrive flash (clipping)
    if (currentLevel > 0.95f) {
      g.setColour(juce::Colours::white.withAlpha(0.3f));
      g.fillRoundedRectangle(fillBounds, 3.0f);
    }
  }
};

// --- Mixer Strip (No Timer) ---
struct MixerStrip : public juce::Component,
                    public juce::Slider::Listener,
                    public juce::DragAndDropTarget,
                    public juce::FileDragAndDropTarget {
  ControlHelpers::ResponsiveSlider volSlider, sendKnob, panSlider;
  juce::TextEditor nameLabel;
  juce::TextButton btnActive, btnSolo;
  juce::ComboBox chSelect;
  juce::Label trackLabel;
  juce::Label ccAddressLabel;
  MeterComponent meter;
  /** CC number for volume (0-127). -1 means use default 7. */
  int volumeCCDisplay = -1;

  bool isActive = true;
  bool isSolo = false;
  int channelIndex = 0;
  int visualIndex = 0;
  int controlCC = -1;
  bool isHooked = false;
  /** When true, meter does not paint (GPU draws meters). Set by MixerPanel. */
  bool skipMeterPaint = false;
  std::atomic<bool> isDirty{false}; // Dirty flag for repaint optimization
  std::atomic<float> hardwareLevel{-1.0f};
  ParameterSmoother faderSmoother;
  std::atomic<float> smoothedTarget{-1.0f};
  float flashAlpha = 0.0f;
  float lastPaintedLevel = 0.0f;
  bool isLoadedFromFile = false;
  juce::String customOscIn, customOscOut;

  // Callbacks
  std::function<void(int, float)> onLevelChange;
  std::function<void(int, bool)> onActiveChange;
  std::function<void(int, bool)> onSoloClicked;
  std::function<void(int, juce::String)> onNameChanged;
  std::function<void(juce::String)> onControlClicked;
  std::function<void(juce::String, int)> onFileDropped;
  std::function<void(int, int, float)> onSendChanged;
  std::function<void(int, juce::String)> onAddressChanged;
  std::function<void(juce::String)> onStatusUpdate;

  // New Decoupling Callbacks
  std::function<void(int, int)> onSwapStrips;
  std::function<void()> onRoutingRefreshNeeded;
  std::function<void(juce::String)> onLearnRequested;

  MixerStrip(int i) : channelIndex(i), visualIndex(i) {
    const juce::String stripID = "MixerStrip_" + juce::String(i);
    setBufferedToImage(false);
    setOpaque(false);
    meter.skipMeterPaintPtr = &skipMeterPaint;
    getProperties().set("blockScroll", true);  // Let strip drag work; viewport won't scroll over strips

    // Volume
    volSlider.setSliderStyle(juce::Slider::LinearVertical);
    volSlider.setRange(0, 127, 1);
    volSlider.setDoubleClickReturnValue(true, 100.0);
    volSlider.setPopupDisplayEnabled(true, false, this);
    volSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volSlider.setValue(100.0, juce::dontSendNotification);
    volSlider.setDefaultValue(100.0);
    volSlider.getProperties().set("paramID", stripID + "_Vol");
    volSlider.getProperties().set("blockScroll", true);
    updateOscTooltips();
    volSlider.onValueChange = [this] {
      if (volSlider.isMouseButtonDown()) {
        if (onLevelChange)
          onLevelChange(channelIndex + 1, (float)volSlider.getValue());
        updateStatus("Ch " + nameLabel.getText() + " Vol",
                     (float)volSlider.getValue());
      }
    };
    addAndMakeVisible(volSlider);

    // Active (Mute)
    btnActive.getProperties().set("paramID", stripID + "_On");
    btnActive.getProperties().set("blockScroll", true);
    btnActive.setButtonText("M");
    btnActive.setTooltip("Mute Channel");
    btnActive.setClickingTogglesState(true);
    updateActiveButtonColor();
    btnActive.onClick = [this] {
      isActive = !btnActive.getToggleState();
      updateActiveButtonColor();
      if (onActiveChange)
        onActiveChange(channelIndex + 1, isActive);
      repaint();
    };
    addAndMakeVisible(btnActive);

    // Solo
    btnSolo.getProperties().set("paramID", stripID + "_Solo");
    btnSolo.getProperties().set("blockScroll", true);
    btnSolo.setButtonText("S");
    btnSolo.setTooltip("Solo this channel. Cmd+click for exclusive solo.");
    updateSoloButtonColor();
    btnSolo.onClick = [this] {
      bool isExclusive = juce::ModifierKeys::currentModifiers.isCommandDown();
      isSolo = !isSolo;
      updateSoloButtonColor();
      if (onSoloClicked)
        onSoloClicked(channelIndex, isExclusive);
      repaint();
    };
    addAndMakeVisible(btnSolo);

    // Pan
    panSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.setValue(0.0, juce::dontSendNotification);
    panSlider.setDefaultValue(0.0);
    panSlider.setDoubleClickReturnValue(true, 0.0);
    panSlider.setPopupDisplayEnabled(true, false, this);
    panSlider.getProperties().set("paramID", stripID + "_Pan");
    panSlider.getProperties().set("blockScroll", true);
    panSlider.onValueChange = [this] {
      if (panSlider.isMouseButtonDown())
        updateStatus("Ch " + nameLabel.getText() + " Pan",
                     (float)panSlider.getValue());
    };
    addAndMakeVisible(panSlider);

    // Channel Select
    addAndMakeVisible(chSelect);
    for (int c = 1; c <= 16; ++c)
      chSelect.addItem(juce::String(c), c);
    chSelect.setSelectedId(channelIndex + 1, juce::dontSendNotification);
    chSelect.setJustificationType(juce::Justification::centred);
    chSelect.getProperties().set("blockScroll", true);
    chSelect.onChange = [this] {
      channelIndex = chSelect.getSelectedId() - 1;
      updateCcAddressText();
      updateOscTooltips();
    };

    // Send
    sendKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    sendKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sendKnob.setRange(0.0, 127.0, 1.0);
    sendKnob.setValue(0.0, juce::dontSendNotification);
    sendKnob.setDefaultValue(0.0);
    sendKnob.setDoubleClickReturnValue(true, 0.0);
    sendKnob.setPopupDisplayEnabled(true, false, this);
    sendKnob.getProperties().set("paramID", stripID + "_Send");
    sendKnob.getProperties().set("blockScroll", true);
    sendKnob.onValueChange = [this] {
      if (sendKnob.isMouseButtonDown()) {
        if (onSendChanged)
          onSendChanged(channelIndex + 1, 12, (float)sendKnob.getValue());
        updateStatus("Ch " + nameLabel.getText() + " Send",
                     (float)sendKnob.getValue());
      }
    };
    addAndMakeVisible(sendKnob);

    // Labels — pass clicks to strip so drag can start from header/name area
    nameLabel.setText(juce::String(i + 1));
    nameLabel.setFont(Fonts::body());
    nameLabel.setJustification(juce::Justification::centred);
    nameLabel.setTooltip("Strip name (e.g. Kick, Pad). Right-click to rename.");
    nameLabel.setColour(juce::TextEditor::backgroundColourId,
                        juce::Colours::transparentBlack);
    nameLabel.setReadOnly(true);
    nameLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel);

    trackLabel.setFont(Fonts::small());
    trackLabel.setJustificationType(juce::Justification::centred);
    trackLabel.setColour(juce::Label::backgroundColourId,
                         juce::Colours::black.withAlpha(0.3f));
    trackLabel.setColour(juce::Label::textColourId,
                         Theme::accent.brighter(0.3f));
    trackLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(trackLabel);

    ccAddressLabel.setFont(Fonts::monoSmall().withHeight(9.0f));
    ccAddressLabel.setJustificationType(juce::Justification::centred);
    ccAddressLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    ccAddressLabel.setColour(juce::Label::textColourId, Theme::accent.withAlpha(0.8f));
    ccAddressLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(ccAddressLabel);
    updateCcAddressText();

    volSlider.addListener(this);
  }

  /** Update tooltips with current OSC addresses (e.g. /ch3cc 7 0-1). */
  void updateOscTooltips() {
    int ch = channelIndex + 1;
    juce::String ccAddr = "/ch" + juce::String(ch) + "cc";
    int volCC = volumeCCDisplay >= 0 ? volumeCCDisplay : 7;
    volSlider.setTooltip("Volume. OSC: " + ccAddr + " " + juce::String(volCC) + " 0-1");
    panSlider.setTooltip("Pan. OSC: " + ccAddr + " 10 -1 to 1");
    sendKnob.setTooltip("Send. OSC: " + ccAddr + " " + juce::String(controlCC > 0 ? controlCC : 12) + " 0-1");
    chSelect.setTooltip("MIDI/OSC channel (1-16). Outgoing: " + ccAddr);
  }

  /** Set the CC number shown for volume (0-127). Use -1 for default (7). */
  void setVolumeCCDisplay(int cc) {
    if (volumeCCDisplay != cc) {
      volumeCCDisplay = cc;
      updateCcAddressText();
      updateOscTooltips();
    }
  }

  /** Refresh the bottom label text from volumeCCDisplay (e.g. "/ch3cc 7 0-1" or "CC 12"). */
  void updateCcAddressText() {
    int ch = channelIndex + 1;
    int cc = volumeCCDisplay >= 0 ? volumeCCDisplay : 7;
    juce::String text = "/ch" + juce::String(ch) + "cc " + juce::String(cc) + " 0-1";
    if (ccAddressLabel.getText() != text)
      ccAddressLabel.setText(text, juce::dontSendNotification);
  }

  // --- Centralized Update Logic (meter update throttled by MixerPanel) ---
  void updateVisuals(bool updateMeters = true) {
    updateSmoothing();
    updateFlash();

    if (updateMeters && meter.source) {
      float raw = meter.source->currentLevel.load(std::memory_order_relaxed);
      float target = raw;
      float current = meter.currentLevel;
      float alpha = (target > current) ? 0.3f : 0.15f;
      float smoothed = current + (target - current) * alpha;
      if (std::abs(smoothed - lastPaintedLevel) > 0.01f) {
        meter.setLevel(smoothed);
        volSlider.getProperties().set("meterLevel", smoothed);
        lastPaintedLevel = smoothed;
        volSlider.repaint();
      }
      if (meter.source->getPeak() > 0.01f)
        meter.source->decayPeak(0.98f);
    }
  }

  void updateSmoothing() {
    float hLevel = hardwareLevel.load(std::memory_order_relaxed);
    if (!isHooked) {
      float uiLevel = (float)volSlider.getValue() / 127.0f;
      if (std::abs(hLevel - uiLevel) < 0.05f)
        isHooked = true;
    }
    if (isHooked)
      smoothedTarget.store(hLevel, std::memory_order_relaxed);

    float target = smoothedTarget.load(std::memory_order_relaxed);
    if (target >= 0.0f) {
      float current = (float)volSlider.getValue() / 127.0f;
      // Hardcoded interval ~16ms for 60Hz
      float coeff = 0.85f;
      float next = current + (target - current) * (1.0f - coeff);

      if (std::abs(next - current) > 0.001f)
        volSlider.setValue(next * 127.0f, juce::dontSendNotification);
      else if (std::abs(next - target) < 0.0001f)
        smoothedTarget.store(-1.0f, std::memory_order_relaxed);
    }
  }

  void updateFlash() {
    if (flashAlpha > 0.01f) {
      flashAlpha *= 0.85f;
      repaint();
    }
  }

  void triggerFlash() {
    flashAlpha = 1.0f;
    meter.repaint();
  }

  void updateHardwarePosition(float level) {
    hardwareLevel.store(level, std::memory_order_relaxed);
  }

  void updateStatus(const juce::String &label, float value) {
    if (onStatusUpdate)
      onStatusUpdate(label + ": " + juce::String(value, 1));
  }

  void updateActiveButtonColor() {
    if (!isActive) {
      btnActive.setColour(juce::TextButton::buttonOnColourId,
                          juce::Colours::red);
      btnActive.setColour(juce::TextButton::textColourOnId,
                          juce::Colours::white);
    } else {
      btnActive.setColour(juce::TextButton::buttonColourId,
                          juce::Colours::grey.darker(0.3f));
      btnActive.setColour(juce::TextButton::textColourOffId,
                          juce::Colours::white.withAlpha(0.5f));
    }
  }

  void updateSoloButtonColor() {
    if (isSolo) {
      btnSolo.setColour(juce::TextButton::buttonColourId,
                        juce::Colours::yellow.darker(0.2f));
      btnSolo.setColour(juce::TextButton::textColourOffId,
                        juce::Colours::black);
    } else {
      btnSolo.setColour(juce::TextButton::buttonColourId,
                        juce::Colours::grey.darker(0.3f));
      btnSolo.setColour(juce::TextButton::textColourOffId,
                        juce::Colours::white.withAlpha(0.6f));
    }
  }

  void setActive(bool active) {
    if (isActive != active) {
      isActive = active;
      btnActive.setToggleState(!isActive, juce::dontSendNotification);
      updateActiveButtonColor();
      markDirty();
    }
  }

  void setSolo(bool solo) {
    if (isSolo != solo) {
      isSolo = solo;
      updateSoloButtonColor();
      markDirty();
    }
  }

  void markDirty() { isDirty.store(true); }

  void setTrackName(juce::String n) {
    if (n.isEmpty())
      n = juce::String(channelIndex + 1);
    trackLabel.setText(n, juce::dontSendNotification);
    nameLabel.setText(n);
    isLoadedFromFile = !n.equalsIgnoreCase(juce::String(channelIndex + 1));
  }

  void setCustomOscAddress(const juce::String &addr) {
    customOscIn = addr;
    if (customOscIn.isNotEmpty() && !customOscIn.startsWith("/"))
      customOscIn = "/" + customOscIn;
    updateStatus("Ch " + nameLabel.getText() + " OSC", 0.0f);
    if (onAddressChanged)
      onAddressChanged(channelIndex, customOscIn);

    if (onRoutingRefreshNeeded)
      onRoutingRefreshNeeded();
  }

  void paint(juce::Graphics &g) override {
    auto r = getLocalBounds().reduced(2).toFloat();

    // Optimization: Simplified Paint
    juce::Colour bg = isActive ? Theme::bgPanel.brighter(0.05f)
                               : juce::Colours::black.withAlpha(0.4f);
    if (flashAlpha > 0.01f) {
      bg = bg.interpolatedWith(juce::Colours::white, flashAlpha * 0.3f);
    }
    g.setColour(bg);
    g.fillRoundedRectangle(r, 5.0f);

    g.setColour(Theme::accent.withAlpha(0.1f));
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(2);
    trackLabel.setBounds(r.removeFromTop(12));
    r.removeFromTop(1);
    chSelect.setBounds(r.removeFromTop(16).reduced(2, 0));
    r.removeFromTop(2);
    auto dials = r.removeFromTop(36);
    int halfW = dials.getWidth() / 2;
    panSlider.setBounds(dials.removeFromLeft(halfW).reduced(1));
    sendKnob.setBounds(dials.reduced(1));
    r.removeFromTop(2);
    auto btns = r.removeFromTop(20);
    btnActive.setBounds(btns.removeFromLeft(halfW).reduced(1));
    btnSolo.setBounds(btns.reduced(1));
    ccAddressLabel.setBounds(r.removeFromBottom(10));
    nameLabel.setBounds(r.removeFromBottom(14));
    volSlider.setBounds(r);
    meter.setBounds(r.reduced(4, 0));
  }

  void mouseDown(const juce::MouseEvent &e) override {
    if (e.mods.isRightButtonDown()) {
      juce::PopupMenu m;
      m.addSectionHeader("Channel " + juce::String(channelIndex + 1));
      m.addItem("Set Custom OSC Address...", [this] {
        auto *aw = new juce::AlertWindow(
            "OSC Routing", "Enter custom OSC address for this strip:",
            juce::MessageBoxIconType::QuestionIcon);
        aw->addTextEditor("addr", customOscIn,
                          "Address (e.g. /my/custom/fader):");
        aw->addButton("OK", static_cast<int>(1), juce::KeyPress(juce::KeyPress::returnKey));
        aw->addButton("Clear", static_cast<int>(2));
        aw->addButton("Cancel", static_cast<int>(0), juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<MixerStrip> safe(this);
        aw->enterModalState(
            true, juce::ModalCallbackFunction::create([safe, aw](int result) {
              if (result == 1 && safe != nullptr)
                safe->setCustomOscAddress(aw->getTextEditorContents("addr").trim());
              else if (result == 2 && safe != nullptr)
                safe->setCustomOscAddress("");
              delete aw;
            }),
            false);
      });
      juce::String addrToCopy = customOscIn.isNotEmpty() ? customOscIn : ccAddressLabel.getText();
      if (addrToCopy.isNotEmpty())
        m.addItem("Copy address", [this, addrToCopy] { juce::SystemClipboard::copyTextToClipboard(addrToCopy); });
      m.addSeparator();
      m.addItem("MIDI Learn Fader", [this] {
        if (onLearnRequested)
          onLearnRequested("MixerStrip_" + juce::String(visualIndex) + "_Vol");
      });
      m.addItem("Reset Volume",
                [this] { volSlider.setValue(100.0, juce::sendNotification); });
      m.addItem("Rename strip...", [this] {
        auto *aw = new juce::AlertWindow(
            "Rename strip", "Channel " + juce::String(channelIndex + 1),
            juce::MessageBoxIconType::QuestionIcon);
        aw->addTextEditor("name", nameLabel.getText(), "Strip name:");
        aw->addButton("OK", static_cast<int>(1), juce::KeyPress(juce::KeyPress::returnKey));
        aw->addButton("Cancel", static_cast<int>(0), juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<MixerStrip> safe(this);
        aw->enterModalState(
            true, juce::ModalCallbackFunction::create([safe, aw](int result) {
              if (result == 1 && safe != nullptr) {
                juce::String n = aw->getTextEditorContents("name").trim();
                safe->setTrackName(n.isEmpty() ? juce::String(safe->channelIndex + 1) : n);
                if (safe->onNameChanged)
                  safe->onNameChanged(safe->channelIndex, safe->nameLabel.getText());
              }
              delete aw;
            }),
            false);
      });

      m.showMenuAsync(PopupMenuOptions::forComponent(this));
      return;
    }

    // Drag from strip header (track label) or name area — strip now receives
    // these clicks because both labels use setInterceptsMouseClicks(false, false)
    if (e.mods.isLeftButtonDown()) {
      auto pos = e.getPosition();
      bool inDragArea =
          trackLabel.getBounds().contains(pos) || nameLabel.getBounds().contains(pos);
      if (inDragArea) {
        if (auto *dc =
                juce::DragAndDropContainer::findParentDragContainerFor(this))
          dc->startDragging("mixer_strip_" + juce::String(visualIndex), this);
      }
    }
  }

  void sliderValueChanged(juce::Slider *s) override {
    if (s == &volSlider && onLevelChange)
      onLevelChange(channelIndex + 1, (float)s->getValue());
  }

  bool isInterestedInDragSource(
      const juce::DragAndDropTarget::SourceDetails &d) override {
    return d.description.toString().startsWith("mixer_strip_");
  }

  void itemDropped(const juce::DragAndDropTarget::SourceDetails &d) override {
    int src = d.description.toString()
                  .fromLastOccurrenceOf("_", false, false)
                  .getIntValue();
    if (onSwapStrips)
      onSwapStrips(src, visualIndex);
  }

  bool isInterestedInFileDrag(const juce::StringArray &files) override {
    for (auto &f : files)
      if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
        return true;
    return false;
  }

  void filesDropped(const juce::StringArray &files, int, int) override {
    for (auto &f : files) {
      if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi")) {
        if (onFileDropped)
          onFileDropped(f, channelIndex + 1);
      }
    }
  }
};
