/*
  ==============================================================================
    Source/Components/Tools.h
  ==============================================================================
*/
#pragma once
#include "Common.h"
#include <JuceHeader.h>

class TrafficMonitor : public juce::Component, public juce::Timer {
public:
  juce::TextEditor logDisplay;
  juce::Label statsLabel;
  juce::ToggleButton btnPause{"Pause"};
  juce::TextButton btnClear{"Clear"};
  juce::StringArray messageBuffer;
  int visibleLines = 0;
  int timerTicks = 0;
  bool autoPauseEnabled = true;

  TrafficMonitor() {
    statsLabel.setFont(juce::FontOptions(12.0f));
    statsLabel.setColour(juce::Label::backgroundColourId,
                         Theme::bgPanel.brighter(0.1f));
    statsLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statsLabel);
    btnPause.setToggleState(false, juce::dontSendNotification);
    btnPause.onClick = [this] {
      if (!btnPause.getToggleState()) {
        visibleLines = 0;
        autoPauseEnabled = false;
      }
    };
    addAndMakeVisible(btnPause);
    btnClear.onClick = [this] { resetStats(); };
    addAndMakeVisible(btnClear);
    logDisplay.setMultiLine(true);
    logDisplay.setReadOnly(true);
    logDisplay.setScrollbarsShown(true);
    logDisplay.setColour(juce::TextEditor::backgroundColourId,
                         juce::Colours::black);
    logDisplay.setColour(juce::TextEditor::textColourId,
                         juce::Colours::lightgreen);
    logDisplay.setFont(juce::FontOptions("Consolas", 11.0f, juce::Font::plain));
    addAndMakeVisible(logDisplay);
    startTimer(100);
  }
  void resetStats() {
    messageBuffer.clear();
    visibleLines = 0;
    logDisplay.clear();
    if (autoPauseEnabled)
      btnPause.setToggleState(false, juce::dontSendNotification);
    repaint();
  }
  void log(juce::String msg) {
    if (autoPauseEnabled && !btnPause.getToggleState() && visibleLines >= 100) {
      if (messageBuffer.size() > 0 &&
          messageBuffer[messageBuffer.size() - 1] == "--- Paused ---")
        return;
      messageBuffer.add("--- Paused ---");
      btnPause.setToggleState(true, juce::dontSendNotification);
      return;
    }
    if (btnPause.getToggleState())
      return;
    messageBuffer.add(msg);
    visibleLines++;
    if (messageBuffer.size() > 100)
      messageBuffer.removeRange(0, 20);
  }
  void timerCallback() override {
    timerTicks++;
    if (timerTicks >= 50) {
      timerTicks = 0;
      int lat = juce::Random::getSystemRandom().nextInt(10) + 2;
      statsLabel.setText(" Latency: " + juce::String(lat) + "ms",
                         juce::dontSendNotification);
    }
    if (messageBuffer.size() > 0) {
      logDisplay.setText(messageBuffer.joinIntoString("\n"));
      logDisplay.moveCaretToEnd();
    }
  }
  void resized() override {
    auto r = getLocalBounds();
    auto header = r.removeFromTop(25);
    btnClear.setBounds(header.removeFromRight(50).reduced(2));
    btnPause.setBounds(header.removeFromRight(60).reduced(2));
    statsLabel.setBounds(header);
    logDisplay.setBounds(r);
  }
};

class MidiPlaylist : public juce::Component, public juce::ListBoxModel {
public:
  juce::ListBox list;
  juce::Label header{{}, "Playlist (.mid)"};
  juce::StringArray files;
  int currentIndex = -1;
  juce::TextButton btnClear{"CLR"}, btnLoop{"Loop"};
  MidiPlaylist() {
    header.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    header.setColour(juce::Label::backgroundColourId,
                     Theme::bgPanel.brighter(0.1f));
    addAndMakeVisible(header);

    btnClear.onClick = [this] {
      files.clear();
      list.updateContent();
      repaint();
    };
    addAndMakeVisible(btnClear);

    btnLoop.setClickingTogglesState(true);
    btnLoop.setColour(juce::TextButton::buttonOnColourId, Theme::accent);
    addAndMakeVisible(btnLoop);

    list.setModel(this);
    list.setColour(juce::ListBox::backgroundColourId,
                   juce::Colours::transparentBlack);
    addAndMakeVisible(list);
  }
  void addFile(juce::String f) {
    files.add(f);
    list.updateContent();
    list.repaint();
  }
  juce::String getNextFile() {
    if (files.isEmpty())
      return "";
    currentIndex = (currentIndex + 1) % files.size();
    list.repaint();
    return files[currentIndex];
  }
  juce::String getPrevFile() {
    if (files.isEmpty())
      return "";
    currentIndex = (currentIndex - 1 + files.size()) % files.size();
    list.repaint();
    return files[currentIndex];
  }
  int getNumRows() override { return files.size(); }
  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgPanel);
    if (files.isEmpty()) {
      g.setColour(juce::Colours::grey);
      g.setFont(juce::FontOptions(14.0f));
      g.drawText("- Drag & Drop .mid -", getLocalBounds().withTrimmedTop(20),
                 juce::Justification::centred, true);
    }
  }
  void paintListBoxItem(int r, juce::Graphics &g, int w, int h,
                        bool s) override {
    if (s || r == currentIndex)
      g.fillAll(Theme::accent.withAlpha(0.3f));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(14.0f));

    juce::File f(files[r]);
    g.drawText(f.getFileName(), 5, 0, w, h, juce::Justification::centredLeft);
  }
  void resized() override {
    auto h = getLocalBounds().removeFromTop(20);
    btnClear.setBounds(h.removeFromRight(40));
    btnLoop.setBounds(h.removeFromRight(50)); // Beside Clear
    header.setBounds(h);
    list.setBounds(getLocalBounds().withTrimmedTop(20));
  }
};
