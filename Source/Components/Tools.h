/*
  ==============================================================================
    Source/Components/Tools.h
    Updated: Playlist Label, "!" Log, Stats Logic
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
  juce::TextButton btnClear{"Clear Log"};
  juce::StringArray messageBuffer;
  int visibleLines = 0;

  TrafficMonitor() {
    statsLabel.setFont(juce::FontOptions(12.0f));
    statsLabel.setColour(juce::Label::backgroundColourId,
                         Theme::bgPanel.brighter(0.1f));
    statsLabel.setJustificationType(juce::Justification::centredLeft);
    statsLabel.setText("Network: -- | Latency: --", juce::dontSendNotification);
    addAndMakeVisible(statsLabel);

    btnPause.setToggleState(false, juce::dontSendNotification);
    addAndMakeVisible(btnPause);

    btnClear.onClick = [this] { resetStats(); };
    addAndMakeVisible(btnClear);

    logDisplay.setMultiLine(true);
    logDisplay.setReadOnly(true);
    logDisplay.setFont(juce::FontOptions(13.0f));
    logDisplay.setColour(juce::TextEditor::backgroundColourId, Theme::bgDark);
    logDisplay.setColour(juce::TextEditor::outlineColourId, Theme::grid);
    addAndMakeVisible(logDisplay);

    startTimer(100);
  }

  void log(const juce::String &msg, bool alwaysShow = false) {
    if (btnPause.getToggleState() && !alwaysShow)
      return;
    juce::ScopedLock sl(logLock);
    // CHANGED: Use "!" instead of time
    messageBuffer.add("! " + msg);
    if (messageBuffer.size() > 100)
      messageBuffer.remove(0);
    visibleLines++;
  }

  void updateStats(const juce::String &text) {
    statsLabel.setText(text, juce::dontSendNotification);
  }

  void resetStats() {
    juce::ScopedLock sl(logLock);
    messageBuffer.clear();
    logDisplay.clear();
    visibleLines = 0;
  }

  void timerCallback() override {
    if (visibleLines > 0) {
      juce::ScopedLock sl(logLock);
      juce::String text;
      for (auto &m : messageBuffer)
        text += m + "\n";
      logDisplay.setText(text);
      logDisplay.moveCaretToEnd();
      visibleLines = 0;
    }
  }

  void resized() override {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(25);
    statsLabel.setBounds(top.removeFromLeft(top.getWidth() - 120));
    btnPause.setBounds(top.removeFromLeft(60).reduced(2));
    btnClear.setBounds(top.removeFromLeft(60).reduced(2));
    logDisplay.setBounds(r);
  }

private:
  juce::CriticalSection logLock;
};

class MidiPlaylist : public juce::Component, public juce::ListBoxModel {
public:
  juce::ListBox list;
  juce::StringArray files;
  int currentIndex = 0;
  juce::ToggleButton btnLoop{"Loop"};
  juce::TextButton btnClearPlaylist{"Clear"};
  juce::Label lblTitle{{}, "Playlist"}; // NEW LABEL

  MidiPlaylist() {
    list.setModel(this);
    list.setRowHeight(24);
    list.setColour(juce::ListBox::backgroundColourId, Theme::bgPanel);
    addAndMakeVisible(list);

    btnLoop.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(btnLoop);

    // Setup Header Label
    lblTitle.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    lblTitle.setJustificationType(juce::Justification::centred);
    lblTitle.setColour(juce::Label::textColourId, Theme::accent);
    addAndMakeVisible(lblTitle);

    addAndMakeVisible(btnClearPlaylist);
    btnClearPlaylist.onClick = [this] {
      files.clear();
      list.updateContent();
      list.repaint();
    };
  }

  void addFile(const juce::String &path) {
    if (!files.contains(path)) {
      files.add(path);
      list.updateContent();
      list.repaint();
    }
  }

  juce::String getNextFile() {
    if (files.isEmpty())
      return "";
    currentIndex = (currentIndex + 1) % files.size();
    list.selectRow(currentIndex);
    return files[currentIndex];
  }

  juce::String getPrevFile() {
    if (files.isEmpty())
      return "";
    currentIndex = (currentIndex - 1 + files.size()) % files.size();
    list.selectRow(currentIndex);
    return files[currentIndex];
  }

  int getNumRows() override { return files.size(); }

  void paint(juce::Graphics &g) override {
    g.fillAll(Theme::bgPanel);
    if (files.isEmpty()) {
      g.setColour(juce::Colours::grey);
      g.setFont(juce::FontOptions(14.0f));
      g.drawText("- Drag Folder or .mid -", getLocalBounds().withTrimmedTop(20),
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
    g.drawText(f.getFileNameWithoutExtension(), 5, 0, w - 5, h,
               juce::Justification::centredLeft, true);
  }

  void resized() override {
    auto r = getLocalBounds();
    auto topRow = r.removeFromTop(25);

    // Layout: Loop | Playlist | Clear
    btnLoop.setBounds(topRow.removeFromLeft(50));
    btnClearPlaylist.setBounds(topRow.removeFromRight(50).reduced(2));
    lblTitle.setBounds(topRow); // Centered in remaining space

    list.setBounds(r);
  }
};