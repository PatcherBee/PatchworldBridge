/*
  ==============================================================================
    Source/Components/Mixer.h
  ==============================================================================
*/
#pragma once
#include "../Core/Common.h"
#include "../Core/TimerHub.h"
#include "../UI/Theme.h"
#include "MixerStrip.h"
#include "../PopupMenuOptions.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>
#include <memory>
#include <string>
#include <vector>

// --- Smart Viewport: Ignores scroll drags on Sliders ---
class SmartScrollViewport : public juce::Viewport {
public:
  SmartScrollViewport() {
    setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  }

  void mouseDrag(const juce::MouseEvent &e) override {
    if (auto *viewed = getViewedComponent()) {
      // Convert Viewport coords to viewed component coords (accounts for scroll)
      auto pt = viewed->getLocalPoint(this, e.getPosition());
      if (auto *c = viewed->getComponentAt(pt.x, pt.y)) {
        for (auto *walk = c; walk != nullptr && walk != viewed;
             walk = walk->getParentComponent()) {
          if (walk->getProperties().getWithDefault("blockScroll", false))
            return;
        }
      }
    }
    if (e.mods.isLeftButtonDown()) {
      auto delta = e.getOffsetFromDragStart();
      setViewPosition(startPos.x - delta.x, startPos.y - delta.y);
    }
  }

  void mouseDown(const juce::MouseEvent &) override {
    startPos = getViewPosition();
  }

private:
  juce::Point<int> startPos;
};

class MixerPanel : public juce::Component,
                   public juce::DragAndDropContainer,
                   public juce::FileDragAndDropTarget {
public:
  bool isResizing = false;

  MixerPanel() {
    // Initialize atomic cache to true (active)
    for (auto &a : channelActiveCache)
      a.store(true);
    preSoloMuteState.fill(true);

    // btnClearSolo removed

    for (int i = 0; i < 17; ++i)
      meterLevels.emplace_back(std::make_unique<LevelMeterData>());

    for (int i = 0; i < 16; ++i) {
      channelMapping[i] = i;
      visualToLogicalMap[i] = i;
    }

    for (int i = 0; i < 16; ++i) {
      auto *s = strips.add(new MixerStrip(i));
      s->meter.source = meterLevels[i].get();
      s->controlCC = 20 + i;

      // Callbacks
      s->onLevelChange = [this, s](int ch, float v) {
        juce::ignoreUnused(ch);
        if (onMixerActivity)
          onMixerActivity(s->visualIndex, v, s->channelIndex + 1);
      };

      s->onActiveChange = [this](int ch, bool a) {
        updateChannelCache(ch, a);
        if (onChannelToggle)
          onChannelToggle(ch, a);
      };

      s->onSoloClicked = [this](int chIdx, bool exclusive) {
        handleSoloClick(chIdx, exclusive);
      };

      s->onSendChanged = [this](int ch, int cc, float val) {
        if (onSendChanged)
          onSendChanged(ch, cc, val);
      };

      s->onControlClicked = [this](juce::String id) {
        if (onControlClicked)
          onControlClicked(id);
      };

      s->onFileDropped = [this](juce::String p, int ch) {
        if (onFileDropped)
          onFileDropped(p, ch);
      };

      s->onStatusUpdate = [this](juce::String txt) {
        if (onStatusUpdate)
          onStatusUpdate(txt);
      };

      s->onSwapStrips = [this](int src, int dst) { swapStrips(src, dst); };

      s->onRoutingRefreshNeeded = [this] { refreshRouting(); };

      s->onLearnRequested = [this](juce::String paramID) {
        if (onLearnRequested)
          onLearnRequested(paramID);
      };

      stripContainer.addAndMakeVisible(s);
    }
    stripViewport.setViewedComponent(&stripContainer, false);
    stripViewport.setScrollBarsShown(false, true);
    stripViewport.setScrollBarThickness(10);
    addAndMakeVisible(stripViewport);
  }

  ~MixerPanel() override {
    if (!vuHubId.empty())
      TimerHub::instance().unsubscribe(vuHubId);
  }

  // --- Called from MainComponent master timer (no per-panel timers) ---
  static constexpr int METER_UPDATE_INTERVAL_MS = 50;
  double lastMeterUpdate = 0.0;

  void updateVisuals() {
    double now = juce::Time::getMillisecondCounterHiRes();
    bool doMeters = (now - lastMeterUpdate >= METER_UPDATE_INTERVAL_MS);
    if (doMeters)
      lastMeterUpdate = now;
    for (auto *s : strips) {
      if (s)
        s->updateVisuals(doMeters);
    }
    if (doMeters && stripViewport.isVisible()) {
      stripViewport.repaint();
      if (onRequestRepaint)
        onRequestRepaint();
    }
  }

  // --- THREAD-SAFE AUDIO QUERY ---
  // AudioEngine calls this. reads from atomic cache.
  bool isChannelActive(int ch) {
    if (ch >= 0 && ch < 17)
      return channelActiveCache[ch].load(std::memory_order_relaxed);
    return true;
  }

  void updateChannelCache(int ch, bool active) {
    if (ch >= 0 && ch < 17)
      channelActiveCache[ch].store(active, std::memory_order_relaxed);
  }

  void updateMeterLevel(int channelIndex, float level) {
    if (channelIndex >= 0 && channelIndex < (int)meterLevels.size())
      meterLevels[channelIndex]->update(level);
  }

  /** Current levels (0–1) for GPU meter renderer. Call from message thread. */
  std::vector<float> getMeterLevels() const {
    std::vector<float> out;
    for (size_t i = 0; i < meterLevels.size(); ++i)
      out.push_back(meterLevels[i]->currentLevel.load(std::memory_order_relaxed));
    return out;
  }

  /** Union of all strip meter bounds in panel local coords (for GPU viewport). */
  juce::Rectangle<int> getMeterAreaBounds() {
    juce::Rectangle<int> r;
    for (auto *s : strips) {
      if (!s)
        continue;
      juce::Rectangle<int> meterInContainer =
          s->meter.getBounds() + s->getBounds().getPosition();
      juce::Point<int> tl = getLocalPoint(&stripContainer,
                                          meterInContainer.getTopLeft());
      juce::Point<int> br = getLocalPoint(&stripContainer,
                                          meterInContainer.getBottomRight());
      juce::Rectangle<int> meterInPanel(tl, br);
      r = r.isEmpty() ? meterInPanel : r.getUnion(meterInPanel);
    }
    return r;
  }

  /** When true, meter components skip paint (GPU draws meters). */
  bool skipMeterPaint = false;
  void setGpuMetersActive(bool active) {
    skipMeterPaint = active;
    for (auto *s : strips)
      if (s)
        s->skipMeterPaint = active;
  }

  // Alias for updateVisuals (centralized master timer)
  void updateMeters() { updateVisuals(); }

  void updateHardwarePosition(const juce::String &paramID, float level) {
    isUpdatingFromNetwork = true;
    for (auto *s : strips) {
      if (paramID == "MixerStrip_" + juce::String(s->visualIndex) + "_Vol")
        s->updateHardwarePosition(level);
      else if (paramID == "MixerStrip_" + juce::String(s->visualIndex) + "_Pan")
        s->panSlider.setValue(level, juce::dontSendNotification);
      else if (paramID ==
               "MixerStrip_" + juce::String(s->visualIndex) + "_Send")
        s->sendKnob.setValue(level * 127.0f, juce::dontSendNotification);
      else if (paramID == "MixerStrip_" + juce::String(s->visualIndex) + "_On")
        s->setActive(level > 0.5f);
      else if (paramID ==
               "MixerStrip_" + juce::String(s->visualIndex) + "_Solo")
        s->setSolo(level > 0.5f);
    }
    isUpdatingFromNetwork = false;
  }

  void updateSmoothers() {
    // Handled by timerCallback -> s->updateVisuals()
  }

  void removeAllStrips() {
    strips.clear();
    for (int i = 0; i < 16; ++i)
      addAndMakeVisible(strips.add(createStrip(i)));
    resized();
  }

  void resetMapping(bool clearNames) {
    // 1. Restore order: sort strips by channel index (undo drag-and-drop swap)
    // FIX: Use std::sort with begin/end iterators for MSVC compliance
    std::sort(strips.begin(), strips.end(),
              [](const MixerStrip *a, const MixerStrip *b) {
                return a->channelIndex < b->channelIndex;
              });

    // 2. Reset state
    for (int i = 0; i < 16; ++i) {
      channelMapping[i] = i;
      visualToLogicalMap[i] = i;

      if (i >= strips.size())
        continue;
      auto *s = strips[i];
      if (!s)
        continue;
      s->visualIndex = i;

      // 3. Names: preserve file-loaded names unless clearNames
      if (clearNames) {
        s->setTrackName(juce::String(i + 1));
      } else if (!s->isLoadedFromFile) {
        s->setTrackName(juce::String(i + 1));
      }

      updateChannelCache(i + 1, true);
    }
    resized();
    repaint();
  }

  void handleSoloClick(int channelIndex, bool isExclusive) {
    if (isExclusive) {
      for (auto *s : strips)
        if (s->channelIndex != channelIndex)
          s->isSolo = false;
    }
    for (auto *s : strips) {
      if (s->channelIndex == channelIndex && onSoloStateChanged)
        onSoloStateChanged(s->channelIndex + 1, s->isSolo);
    }
    updateSoloStates();
  }

  void updateSoloStates() {
    bool anySolo = false;
    for (auto *s : strips)
      if (s->isSolo)
        anySolo = true;

    if (anySolo && !wasSoloActive) {
      // Transitioning into solo: snapshot current mute state for restore on exit
      wasSoloActive = true;
      for (auto *s : strips)
        preSoloMuteState[(size_t)s->channelIndex] = s->isActive;
    } else if (!anySolo) {
      wasSoloActive = false;
    }

    for (auto *s : strips) {
      bool shouldBeActive;
      if (anySolo) {
        shouldBeActive = s->isSolo;
      } else {
        // Exiting solo: restore pre-solo mute state
        shouldBeActive = preSoloMuteState[(size_t)s->channelIndex];
      }
      s->setActive(shouldBeActive);
      updateChannelCache(s->channelIndex + 1, shouldBeActive);
    }
    repaint();
  }

  int getMappedChannel(int src) {
    if (src >= 0 && src < 16)
      return channelMapping[src];
    return src;
  }

  void swapStrips(int indexA, int indexB) {
    if (indexA == indexB)
      return;
    if (indexA < 0 || indexA >= strips.size() ||
        indexB < 0 || indexB >= strips.size())
      return;

    strips.swap(indexA, indexB);

    for (int i = 0; i < strips.size(); ++i) {
      strips[i]->visualIndex = i;
    }

    resized();
    repaint();
    if (onMappingChanged)
      onMappingChanged();
  }

  juce::String getChannelName(int ch) {
    for (auto *s : strips)
      if (s->channelIndex == ch)
        return s->nameLabel.getText();
    return juce::String(ch + 1);
  }

  void setChannelVolume(int ch, float val) {
    for (auto *s : strips)
      if (s->channelIndex == ch)
        s->volSlider.setValue(val, juce::dontSendNotification);
  }

  void setChannelName(int ch, juce::String n) {
    for (auto *s : strips)
      if (s->channelIndex == ch)
        s->setTrackName(n);
  }

  void triggerFlash(int ch) {
    for (auto *s : strips)
      if (s->channelIndex == ch)
        s->triggerFlash();
  }

  void updateFlashes() {
    // Handled by tickResizeSettle / VU update
  }

  void setActive(int ch, bool active) {
    for (auto *s : strips)
      if (s->channelIndex == ch) {
        s->setActive(active);
        updateChannelCache(ch, active);
      }
  }

  void tickResizeSettle() {
    double now = juce::Time::getMillisecondCounterHiRes();
    if (isResizing && (now - resizeStartTimeMs) >= 120.0)
      onResizeSettle();
  }

  void onResizeSettle() {
    isResizing = false;
    if (!vuHubId.empty()) {
      TimerHub::instance().unsubscribe(vuHubId);
      vuHubId.clear();
    }
    stripContainer.setBufferedToImage(false);
  }

  void resized() override {
    isResizing = true;
    stripContainer.setBufferedToImage(true);
    resizeStartTimeMs = juce::Time::getMillisecondCounterHiRes();
    if (vuHubId.empty()) {
      vuHubId = "MixerPanel_resize_" + juce::Uuid().toDashedString().toStdString();
      TimerHub::instance().subscribe(vuHubId, [this] { tickResizeSettle(); },
                                     TimerHub::Rate10Hz);
    }

    auto r = getLocalBounds();
    // Minimal top bar for right-click "Reset CH" menu (no button)
    topBarHeight_ = 14;
    auto topBar = r.removeFromTop(topBarHeight_);

    stripViewport.setBounds(r);
    int totalW = stripWidth * strips.size();
    int stripH = r.getHeight();
    stripContainer.setSize(juce::jmax(totalW, r.getWidth()), juce::jmax(stripH, 1));
    int x = 0;
    for (auto *s : strips) {
      s->setBounds(x, 0, stripWidth, stripContainer.getHeight());
      x += stripWidth;
    }
  }

  void paint(juce::Graphics &g) override { juce::ignoreUnused(g); }

  void mouseDown(const juce::MouseEvent &e) override {
    if (e.mods.isRightButtonDown() && e.getPosition().y < topBarHeight_) {
      juce::PopupMenu m;
      m.addItem("Reset CH (reset strip order to default)", [this] {
        resetMapping(false);
      });
      m.showMenuAsync(PopupMenuOptions::forComponent(this));
    }
  }

  bool isInterestedInFileDrag(const juce::StringArray &files) override {
    for (const auto &f : files)
      if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
        return true;
    return false;
  }

  void filesDropped(const juce::StringArray &files, int x, int y) override {
    auto pt = stripContainer.getLocalPoint(this, juce::Point<int>(x, y));
    for (auto *s : strips)
      if (s->getBounds().contains(pt)) {
        s->filesDropped(files, pt.x - s->getX(), pt.y - s->getY());
        return;
      }
  }

  void refreshRouting() {
    if (onRoutingChanged)
      onRoutingChanged();
  }

  void refreshVolumeCCLabels() {
    if (!getCCForParamCallback)
      return;
    for (auto *s : strips) {
      if (!s)
        continue;
      juce::String paramID = "MixerStrip_" + juce::String(s->visualIndex) + "_Vol";
      int cc = getCCForParamCallback(paramID);
      s->setVolumeCCDisplay(cc >= 0 ? cc : -1);
    }
  }

  /** Returns the strip's output channel (1–16) from its channel dropdown, or 1 if out of range. */
  int getOutputChannelForStrip(int visualIndex) const {
    if (visualIndex >= 0 && visualIndex < strips.size() && strips[visualIndex])
      return strips[visualIndex]->channelIndex + 1;
    return 1;
  }

  juce::TextButton btnClearSolo{"SOLO"};
  SmartScrollViewport stripViewport;
  juce::Component stripContainer;
  juce::OwnedArray<MixerStrip> strips;

  std::function<void()> onRoutingChanged;
  /** visualIdx = strip index (for paramID); outputCh = 1–16 from strip's channel dropdown (for OSC/MIDI out). */
  std::function<void(int visualIdx, float val, int outputCh)> onMixerActivity;
  std::function<void(juce::String)> onStatusUpdate;
  std::function<void(juce::String)> onLearnRequested;
  std::function<void(int, juce::String)> onNameChanged;
  std::function<void(int, int, float)> onSendChanged;
  std::function<void(int, bool)> onChannelToggle;
  /** Called when a strip's solo state changes (ch 1-based). Used for MIDI override. */
  std::function<void(int, bool)> onSoloStateChanged;
  std::function<void(juce::String)> onControlClicked;
  std::function<void(juce::String, int)> onFileDropped;
  std::function<void()> onResetRequested;
  std::function<void()> onMappingChanged;
  /** Optional: notify RepaintCoordinator when mixer needs repaint (batch with other dirty regions). */
  std::function<void()> onRequestRepaint;
  /** Called to refresh volume CC labels on strips (e.g. when MIDI mappings change). */
  std::function<int(juce::String paramID)> getCCForParamCallback;

private:
  int topBarHeight_ = 14;

  MixerStrip *createStrip(int i) {
    auto *s = new MixerStrip(i);
    s->meter.source = meterLevels[i].get();
    s->controlCC = 20 + i;
    // Re-bind callbacks (same as constructor). removeAllStrips() uses this so
    // strips are recreated with correct callbacks; no longer broken.

    s->onLevelChange = [this, s](int ch, float v) {
      juce::ignoreUnused(ch);
      if (onMixerActivity)
        onMixerActivity(s->visualIndex, v, s->channelIndex + 1);
    };
    s->onActiveChange = [this](int ch, bool a) {
      updateChannelCache(ch, a);
      if (onChannelToggle)
        onChannelToggle(ch, a);
    };
    s->onSoloClicked = [this](int chIdx, bool exclusive) {
      handleSoloClick(chIdx, exclusive);
    };
    s->onSendChanged = [this](int ch, int cc, float val) {
      if (onSendChanged)
        onSendChanged(ch, cc, val);
    };
    s->onControlClicked = [this](juce::String id) {
      if (onControlClicked)
        onControlClicked(id);
    };
    s->onFileDropped = [this](juce::String p, int ch) {
      if (onFileDropped)
        onFileDropped(p, ch);
    };
    s->onStatusUpdate = [this](juce::String txt) {
      if (onStatusUpdate)
        onStatusUpdate(txt);
    };
    s->onSwapStrips = [this](int src, int dst) { swapStrips(src, dst); };
    s->onRoutingRefreshNeeded = [this] { refreshRouting(); };
    s->onLearnRequested = [this](juce::String paramID) {
      if (onLearnRequested)
        onLearnRequested(paramID);
    };

    return s;
  }

  std::vector<std::unique_ptr<LevelMeterData>> meterLevels;
  static constexpr int stripWidth = 52;
  int channelMapping[16];
  int visualToLogicalMap[16];
  bool isUpdatingFromNetwork = false;

  // Race Condition Fix: Atomic cache
  std::array<std::atomic<bool>, 17> channelActiveCache;
  // Restore pre-solo mute state when exiting solo (default: all unmuted)
  std::array<bool, 16> preSoloMuteState;
  bool wasSoloActive = false;

  std::string vuHubId;
  double resizeStartTimeMs = 0.0;
};
