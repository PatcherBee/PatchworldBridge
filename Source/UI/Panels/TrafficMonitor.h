// FILE: Source/UI/Panels/TrafficMonitor.h
#pragma once
#include "../../Core/TimerHub.h"
#include "../Audio/LockFreeRingBuffers.h"
#include "../Theme.h"
#include "../Fonts.h"
#include "../Widgets/SignalPathLegend.h"
#include <atomic>
#include <deque>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>

class TrafficMonitor : public juce::Component {
public:
  SignalPathLegend signalLegend;
  juce::TextEditor logDisplay;
  juce::Label lblStatus;
  juce::Label lblPeers;
  juce::Label lblLatency;
  juce::TextButton btnClear{"Clear"};
  juce::TextButton btnPause{"Pause"};
  std::deque<juce::String> messageBuffer;
  static constexpr size_t maxHistoryLines = 100;
  static constexpr size_t autoPauseThreshold = 200;
  static constexpr int scrollAtBottomThreshold = 30;  // chars from end = "at bottom"
  std::atomic<bool> isPaused{false};
  bool needsUpdate = false;
  bool autoPauseAtStartupDone = false;
  size_t messageCountSinceStartup = 0;
  bool userHasScrolledUp = false;

  TrafficMonitor() {
    // 1. Main Status (Connection)
    addAndMakeVisible(lblStatus);
    lblStatus.setFont(Fonts::bodyBold().withHeight(13.0f));
    lblStatus.setColour(juce::Label::textColourId, Theme::text.withAlpha(0.5f));
    lblStatus.setText("", juce::dontSendNotification);

    // 2. Link Peers Indicator (Moved from Transport)
    addAndMakeVisible(lblPeers);
    lblPeers.setFont(Fonts::bodyBold().withHeight(13.0f));
    lblPeers.setColour(juce::Label::textColourId, Theme::accent);
    lblPeers.setJustificationType(juce::Justification::centredLeft);
    lblPeers.setText("LINK: 0", juce::dontSendNotification);

    // 3. Latency Monitor (hidden - ms indicator removed from log UI)
    addChildComponent(lblLatency);
    lblLatency.setVisible(false);
    lblLatency.setFont(Fonts::body().withHeight(13.0f));
    lblLatency.setColour(juce::Label::textColourId, juce::Colours::lime);
    lblLatency.setJustificationType(juce::Justification::centredRight);
    lblLatency.setText("0ms", juce::dontSendNotification);

    addAndMakeVisible(btnPause);
    btnPause.setColour(juce::TextButton::buttonColourId,
                       juce::Colours::transparentBlack);
    btnPause.setClickingTogglesState(true);
    btnPause.onClick = [this] {
      isPaused.store(btnPause.getToggleState());
      btnPause.setButtonText(isPaused.load() ? "Resume" : "Pause");
    };

    addAndMakeVisible(btnClear);
    btnClear.setColour(juce::TextButton::buttonColourId,
                       juce::Colours::transparentBlack);
    btnClear.onClick = [this] {
      messageBuffer.clear();
      logDisplay.clear();
      needsUpdate = false;
    };

    logDisplay.setMultiLine(true);
    logDisplay.setReadOnly(true);
    logDisplay.setFont(Fonts::body());
    logDisplay.setColour(juce::TextEditor::backgroundColourId,
                         Theme::bgDark.withAlpha(0.5f));
    logDisplay.setColour(juce::TextEditor::outlineColourId,
                         juce::Colours::transparentBlack);
    logDisplay.setScrollbarsShown(true);
    addAndMakeVisible(logDisplay);

    addAndMakeVisible(signalLegend);
    hubId = "TrafficMonitor_" + std::to_string(reinterpret_cast<int64_t>(this));
    TimerHub::instance().subscribe(
        hubId,
        [this]() { flushLogToDisplay(); },
        TimerHub::Rate5Hz);
    signalLegend.setOpaque(false);
    signalLegend.setInterceptsMouseClicks(false, false);
  }

  ~TrafficMonitor() override {
    TimerHub::instance().unsubscribe(hubId);
  }

  void setLinkPeers(int count) {
    if (count != lastPeerCount) {
      lblPeers.setText("LINK: " + juce::String(count),
                       juce::dontSendNotification);
      lblPeers.setColour(
          juce::Label::textColourId,
          count > 0 ? juce::Colours::lime : juce::Colours::white.withAlpha(0.3f));
      lastPeerCount = count;
    }
  }

  void setLatency(double ms) {
    if (std::abs(ms - lastLatency) > 1.0) {
      lblLatency.setText(juce::String(ms, 1) + "ms",
                         juce::dontSendNotification);
      lastLatency = ms;
    }
    if (ms < 10)
      lblLatency.setColour(juce::Label::textColourId, juce::Colours::lime);
    else if (ms < 40)
      lblLatency.setColour(juce::Label::textColourId, juce::Colours::yellow);
    else
      lblLatency.setColour(juce::Label::textColourId, juce::Colours::red);
  }

  void setStatus(juce::String text) {
    lblStatus.setText(text, juce::dontSendNotification);
  }

  void flushLogToDisplay() {
    if (!isVisible())
      return;
    int textLen = logDisplay.getText().length();
    if (!needsUpdate) {
      // No new messages: detect if user is at bottom (resume auto-scroll) or scrolled up
      if (textLen > 0) {
        int caretPos = logDisplay.getCaretPosition();
        userHasScrolledUp = (caretPos < textLen - scrollAtBottomThreshold);
      }
      return;
    }
    juce::String fullText;
    if (!messageBuffer.empty()) {
      juce::StringArray lines;
      // Limit lines sent to display to reduce allocation and paint cost (keep last N)
      static constexpr size_t kMaxDisplayLines = 80;
      size_t start = messageBuffer.size() > kMaxDisplayLines
                         ? messageBuffer.size() - kMaxDisplayLines
                         : 0;
      for (size_t i = start; i < messageBuffer.size(); ++i)
        lines.add(messageBuffer[i]);
      fullText = lines.joinIntoString("\n") + "\n";
    }
    int visibleH = juce::jmax(20, logDisplay.getHeight() - 10);

    if (userHasScrolledUp) {
      // User scrolled up: update text but preserve scroll position so they can keep reading
      int saveCaret = logDisplay.getCaretPosition();
      logDisplay.setText(fullText);
      int newLen = fullText.length();
      int restoreCaret = (newLen > 0) ? juce::jmin(saveCaret, newLen - 1) : 0;
      logDisplay.setCaretPosition(restoreCaret);
      logDisplay.scrollEditorToPositionCaret(0, visibleH / 2);
    } else {
      // At bottom: follow latest message
      logDisplay.setText(fullText);
      logDisplay.moveCaretToEnd();
      logDisplay.scrollEditorToPositionCaret(0, visibleH);
      userHasScrolledUp = false;
    }
    needsUpdate = false;
  }

  void log(const juce::String &msg, bool isSystemMessage) {
    messageCountSinceStartup++;
    if (!autoPauseAtStartupDone && messageCountSinceStartup >= autoPauseThreshold) {
      autoPauseAtStartupDone = true;
      isPaused.store(true);
      btnPause.setToggleState(true, juce::dontSendNotification);
      btnPause.setButtonText("Resume");
      messageBuffer.push_back("  -Paused for Performance-");
      if (messageBuffer.size() > maxHistoryLines)
        messageBuffer.pop_front();
      needsUpdate = true;
    }
    if (isPaused.load(std::memory_order_acquire))
      return;
    messageBuffer.push_back((isSystemMessage ? "! " : "  ") + msg);
    if (messageBuffer.size() > maxHistoryLines)
      messageBuffer.pop_front();
    needsUpdate = true;
  }

  void appendLog(const juce::String &msg, bool u) { log(msg, u); }

  void logEntry(const LogEntry &entry) {
    // When paused, do not add OSC/MIDI traffic (only system messages like Transport/Link)
    bool isTraffic = (entry.code == LogEntry::Code::MidiInput ||
                      entry.code == LogEntry::Code::MidiOutput ||
                      entry.code == LogEntry::Code::OscIn ||
                      entry.code == LogEntry::Code::OscOut);
    if (isPaused.load(std::memory_order_acquire) && isTraffic)
      return;

    juce::String text;
    bool isSystemMsg = true;

    switch (entry.code) {
    case LogEntry::Code::MidiInput:
      text = "MIDI IN: " + juce::String(entry.val1);
      isSystemMsg = false;
      break;
    case LogEntry::Code::MidiOutput: {
      int ch = entry.val1 / 256;
      int noteOrCC = entry.val1 % 256;
      text = "MIDI OUT Ch" + juce::String(ch) + " " +
             juce::String(noteOrCC) + " " + juce::String((int)(entry.val2 * 127.0f));
      isSystemMsg = false;
      break;
    }
    case LogEntry::Code::OscIn:
      text = "OSC IN: /" + juce::String(entry.val1);
      isSystemMsg = false;
      break;
    case LogEntry::Code::OscOut:
      text = "OSC OUT: /" + juce::String(entry.val1);
      isSystemMsg = false;
      break;
    case LogEntry::Code::TransportPlay:
      text = "Transport: PLAY";
      break;
    case LogEntry::Code::TransportStop:
      text = "Transport: STOP";
      break;
    case LogEntry::Code::LinkEnabled:
      text = "Link: Enabled";
      break;
    case LogEntry::Code::LinkDisabled:
      text = "Link: Disabled";
      break;
    case LogEntry::Code::Error:
      text = "ERROR: " + juce::String(entry.val1);
      break;
    case LogEntry::Code::Custom:
      text = "Event: " + juce::String(entry.val1);
      break;
    default:
      text = "Event: " + juce::String((int)entry.code);
      break;
    }

    log(text, isSystemMsg);
  }

  void resized() override {
    auto r = getLocalBounds().reduced(4);
    signalLegend.setBounds(r.removeFromTop(22));
    r.removeFromTop(2);
    auto topRow = r.removeFromTop(24);

    btnClear.setBounds(topRow.removeFromRight(50));
    btnPause.setBounds(topRow.removeFromRight(55));
    lblPeers.setBounds(topRow.removeFromLeft(80));
    if (lblStatus.getText().isEmpty())
      lblStatus.setBounds(0, 0, 0, 0);
    else
      lblStatus.setBounds(topRow);

    r.removeFromTop(2);
    logDisplay.setBounds(r);
  }

  void paint(juce::Graphics &g) override {
    Theme::drawStylishPanel(g, getLocalBounds().toFloat(), Theme::bgPanel,
                            6.0f);
  }

private:
  std::string hubId;
  int lastPeerCount = -1;
  double lastLatency = -1.0;
};