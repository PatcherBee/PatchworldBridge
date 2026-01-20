/*
  ==============================================================================
    Source/Components/Tools.h
    Status: FIXED (Logging, Playlist, Pause Button)
  ==============================================================================
*/
#pragma once
#include "Common.h"
#include <JuceHeader.h>
#include <atomic>

// ... PingWorker Class remains same ...
class PingWorker : public juce::Thread {
public:
  std::atomic<int> lastPingMs{-1};
  PingWorker() : juce::Thread("PingThread") { startThread(); }
  ~PingWorker() { stopThread(2000); }
  void run() override {
    while (!threadShouldExit()) {
      lastPingMs = runPing();
      wait(5000);
    }
  }
  int runPing() {
    juce::ChildProcess proc;
    juce::String cmd;
#if JUCE_WINDOWS
    cmd = "ping -n 1 8.8.8.8";
#else
    cmd = "ping -c 1 8.8.8.8";
#endif
    if (proc.start(cmd)) {
      juce::String output = proc.readAllProcessOutput();
      int idx = output.indexOf("time=");
      if (idx > 0) {
        juce::String sub = output.substring(idx + 5);
        int msIdx = sub.indexOf("ms");
        if (msIdx > 0)
          return sub.substring(0, msIdx).getFloatValue();
      }
    }
    return -1;
  }
};

class TrafficMonitor : public juce::Component, public juce::Timer {
public:
  juce::TextEditor logDisplay;
  juce::Label statsLabel;
  juce::TextButton btnPause; // Changed to TextButton for custom color logic
  bool isPaused = false;
  juce::TextButton btnClear{"Clear"};
  juce::StringArray messageBuffer;
  int visibleLines = 0;
  PingWorker pingWorker;

  TrafficMonitor() {
    statsLabel.setFont(juce::FontOptions(12.0f));
    statsLabel.setColour(juce::Label::backgroundColourId,
                         Theme::bgPanel.brighter(0.1f));
    statsLabel.setJustificationType(juce::Justification::centredLeft);
    statsLabel.setText("Network: -- | Latency: --", juce::dontSendNotification);
    addAndMakeVisible(statsLabel);

    btnPause.setButtonText("Pause");
    btnPause.setClickingTogglesState(true);
    btnPause.setColour(juce::TextButton::buttonOnColourId,
                       juce::Colours::orange);
    btnPause.onClick = [this] {
      isPaused = btnPause.getToggleState();
      if (isPaused)
        btnPause.setButtonText("Paused");
      else
        btnPause.setButtonText("Pause");
    };
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
    if (isPaused && !alwaysShow)
      return;
    juce::ScopedLock sl(logLock);
    messageBuffer.add("! " + msg);
    if (messageBuffer.size() > 100)
      messageBuffer.remove(0);
    visibleLines++;
  }

  void updateStats(const juce::String &text) {
    juce::String sysLat = (getSystemLatency() >= 0)
                              ? (juce::String(getSystemLatency()) + "ms")
                              : "--";
    statsLabel.setText(text + " | " + sysLat, juce::dontSendNotification);
  }

  void resetStats() {
    juce::ScopedLock sl(logLock);
    messageBuffer.clear();
    logDisplay.clear();
    visibleLines = 0;
  }

  int getSystemLatency() { return pingWorker.lastPingMs; }

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

class MidiPlaylist : public juce::Component,
                     public juce::ListBoxModel,
                     public juce::DragAndDropContainer,
                     public juce::DragAndDropTarget {
public:
  juce::ListBox list;
  juce::StringArray files;
  int currentIndex = 0;
  enum PlayMode { Single, LoopOne, LoopAll };
  PlayMode playMode = Single;
  juce::TextButton btnLoopMode{"Single"};
  std::function<void(juce::String)> onLoopModeChanged;
  juce::TextButton btnClearPlaylist{"Clear"};
  juce::Label lblTitle{{}, "Playlist"};

  MidiPlaylist() {
    list.setModel(this);
    list.setRowHeight(24);
    list.setColour(juce::ListBox::backgroundColourId,
                   juce::Colours::transparentBlack);
    addAndMakeVisible(list);

    btnLoopMode.setColour(juce::TextButton::buttonColourId,
                          juce::Colours::grey.withAlpha(0.2f));
    btnLoopMode.setColour(juce::TextButton::textColourOffId,
                          juce::Colours::white);
    btnLoopMode.setButtonText("Loop Off");
    btnLoopMode.onClick = [this] {
      if (playMode == Single) {
        playMode = LoopOne;
        btnLoopMode.setButtonText("Loop One");
        btnLoopMode.setColour(juce::TextButton::buttonColourId,
                              juce::Colours::cyan.darker(0.3f));
        if (onLoopModeChanged)
          onLoopModeChanged("Loop One");
      } else if (playMode == LoopOne) {
        playMode = LoopAll;
        btnLoopMode.setButtonText("Loop All");
        btnLoopMode.setColour(juce::TextButton::buttonColourId,
                              juce::Colours::green.withAlpha(0.6f));
        if (onLoopModeChanged)
          onLoopModeChanged("Loop All");
      } else {
        playMode = Single;
        btnLoopMode.setButtonText("Loop Off");
        btnLoopMode.setColour(juce::TextButton::buttonColourId,
                              juce::Colours::grey.withAlpha(0.2f));
        if (onLoopModeChanged)
          onLoopModeChanged("Loop Off");
      }
    };
    addAndMakeVisible(btnLoopMode);

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
      g.drawText("Drag & Drop .mid", getLocalBounds().withTrimmedTop(20),
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

  juce::var
  getDragSourceDescription(const juce::SparseSet<int> &selectedRows) override {
    if (selectedRows.size() > 0)
      return "playlist_row_" + juce::String(selectedRows[0]);
    return {};
  }
  bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails
                                    &dragSourceDetails) override {
    return dragSourceDetails.description.toString().startsWith("playlist_row_");
  }
  void itemDropped(const juce::DragAndDropTarget::SourceDetails
                       &dragSourceDetails) override {
    int sourceIndex = dragSourceDetails.description.toString()
                          .fromLastOccurrenceOf("_", false, false)
                          .getIntValue();
    int targetIndex = list.getInsertionIndexForPosition(
        dragSourceDetails.localPosition.toInt().x,
        dragSourceDetails.localPosition.toInt().y);
    if (sourceIndex >= 0 && sourceIndex < files.size()) {
      if (targetIndex < 0)
        targetIndex = files.size() - 1;
      if (targetIndex > files.size())
        targetIndex = files.size();
      juce::String file = files[sourceIndex];
      files.remove(sourceIndex);
      if (targetIndex > sourceIndex)
        targetIndex--;
      files.insert(targetIndex, file);
      if (currentIndex == sourceIndex)
        currentIndex = targetIndex;
      else if (currentIndex > sourceIndex && currentIndex <= targetIndex)
        currentIndex--;
      else if (currentIndex < sourceIndex && currentIndex >= targetIndex)
        currentIndex++;
      list.updateContent();
      list.selectRow(currentIndex);
      list.repaint();
    }
  }

  void resized() override {
    auto r = getLocalBounds();
    auto topRow = r.removeFromTop(25);
    btnLoopMode.setBounds(topRow.removeFromLeft(60));
    btnClearPlaylist.setBounds(topRow.removeFromRight(50).reduced(2));
    lblTitle.setBounds(topRow);
    list.setBounds(r);
  }
};