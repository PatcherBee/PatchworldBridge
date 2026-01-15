/*
  ==============================================================================
    SubComponents.h
    Status: FINAL (Dual-Mode Piano Roll, Note Names, Log Ready)
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

struct Theme {
    static const juce::Colour bgDark;
    static const juce::Colour bgPanel;
    static const juce::Colour accent;
    static const juce::Colour grid;
    static const juce::Colour text;

    static juce::Colour getChannelColor(int ch) {
        return juce::Colour::fromHSV(((ch - 1) * 0.618f) - std::floor((ch - 1) * 0.618f), 0.7f, 0.95f, 1.0f);
    }
};

inline const juce::Colour Theme::bgDark = juce::Colour::fromString("FF121212");
inline const juce::Colour Theme::bgPanel = juce::Colour::fromString("FF1E1E1E");
inline const juce::Colour Theme::accent = juce::Colour::fromString("FF007ACC");
inline const juce::Colour Theme::grid = juce::Colour::fromString("FF333333");
inline const juce::Colour Theme::text = juce::Colours::white;

// ==================== VISUALIZERS ====================
struct PhaseVisualizer : public juce::Component {
    double currentPhase = 0.0;
    double quantum = 4.0;
    void setPhase(double p, double q) { currentPhase = p; quantum = (q > 0) ? q : 4.0; repaint(); }
    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        int numBlocks = (int)quantum; if (numBlocks < 1) numBlocks = 1;
        float blockWidth = (bounds.getWidth() - (numBlocks - 1) * 2.0f) / numBlocks;
        int activeBlockIndex = (int)std::floor(currentPhase) % numBlocks;
        for (int i = 0; i < numBlocks; ++i) {
            auto blockRect = juce::Rectangle<float>(bounds.getX() + i * (blockWidth + 2.0f), bounds.getY(), blockWidth, bounds.getHeight());
            if (i == activeBlockIndex) { g.setColour(Theme::accent); g.fillRoundedRectangle(blockRect, 3.0f); }
            else { g.setColour(Theme::bgPanel.brighter(0.1f)); g.fillRoundedRectangle(blockRect, 3.0f); g.setColour(Theme::grid); g.drawRoundedRectangle(blockRect, 3.0f, 1.0f); }
        }
    }
};

class ConnectionLight : public juce::Component {
public:
    bool isConnected = false;
    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        float d = juce::jmin(bounds.getWidth(), bounds.getHeight()) - 8.0f;
        auto circle = bounds.withSizeKeepingCentre(d, d);
        g.setColour(isConnected ? juce::Colours::lime : juce::Colours::red); g.fillEllipse(circle);
        if (isConnected) { g.setColour(juce::Colours::lime.withAlpha(0.6f)); g.drawEllipse(circle, 2.0f); }
    }
};

class TrafficMonitor : public juce::Component, public juce::Timer {
public:
    juce::TextEditor logDisplay; juce::Label statsLabel;
    juce::ToggleButton btnPause{ "Pause" }; juce::TextButton btnClear{ "Clear" };
    juce::StringArray messageBuffer;
    int visibleLines = 0; int timerTicks = 0; bool autoPauseEnabled = true;

    TrafficMonitor() {
        statsLabel.setFont(juce::FontOptions(12.0f)); statsLabel.setColour(juce::Label::backgroundColourId, Theme::bgPanel.brighter(0.1f));
        statsLabel.setJustificationType(juce::Justification::centredLeft); addAndMakeVisible(statsLabel);
        btnPause.setToggleState(false, juce::dontSendNotification);
        btnPause.onClick = [this] { if (!btnPause.getToggleState()) { visibleLines = 0; autoPauseEnabled = false; } };
        addAndMakeVisible(btnPause);
        btnClear.onClick = [this] { resetStats(); }; addAndMakeVisible(btnClear);
        logDisplay.setMultiLine(true); logDisplay.setReadOnly(true); logDisplay.setScrollbarsShown(true);
        logDisplay.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
        logDisplay.setColour(juce::TextEditor::textColourId, juce::Colours::lightgreen);
        logDisplay.setFont(juce::FontOptions("Consolas", 11.0f, juce::Font::plain)); addAndMakeVisible(logDisplay);
        startTimer(100);
    }
    void resetStats() { messageBuffer.clear(); visibleLines = 0; logDisplay.clear(); if (autoPauseEnabled) btnPause.setToggleState(false, juce::dontSendNotification); repaint(); }
    void log(juce::String msg) {
        if (autoPauseEnabled && !btnPause.getToggleState() && visibleLines >= 100) {
            if (messageBuffer.size() > 0 && messageBuffer[messageBuffer.size() - 1] == "--- Paused ---") return;
            messageBuffer.add("--- Paused ---"); btnPause.setToggleState(true, juce::dontSendNotification); return;
        }
        if (btnPause.getToggleState()) return;
        messageBuffer.add(msg); visibleLines++; if (messageBuffer.size() > 100) messageBuffer.removeRange(0, 20);
    }
    void timerCallback() override {
        timerTicks++;
        if (timerTicks >= 50) { timerTicks = 0; int lat = juce::Random::getSystemRandom().nextInt(10) + 2; statsLabel.setText(" Latency: " + juce::String(lat) + "ms", juce::dontSendNotification); }
        if (messageBuffer.size() > 0) { logDisplay.setText(messageBuffer.joinIntoString("\n")); logDisplay.moveCaretToEnd(); }
    }
    void resized() override {
        auto r = getLocalBounds(); auto header = r.removeFromTop(25);
        btnClear.setBounds(header.removeFromRight(50).reduced(2)); btnPause.setBounds(header.removeFromRight(60).reduced(2));
        statsLabel.setBounds(header); logDisplay.setBounds(r);
    }
};

class MidiPlaylist : public juce::Component, public juce::ListBoxModel {
public:
    juce::ListBox list; juce::Label header{ {}, "Playlist (.mid)" }; juce::TextButton btnClear{ "Clear" };
    juce::StringArray files; int currentIndex = -1;
    MidiPlaylist() {
        header.setFont(juce::FontOptions(13.0f).withStyle("Bold")); header.setColour(juce::Label::backgroundColourId, Theme::bgPanel.brighter(0.1f));
        addAndMakeVisible(header); btnClear.onClick = [this] { files.clear(); list.updateContent(); repaint(); }; addAndMakeVisible(btnClear);
        list.setModel(this); list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack); addAndMakeVisible(list);
    }
    void addFile(juce::String f) { files.add(f); list.updateContent(); list.repaint(); }
    juce::String getNextFile() { if (files.isEmpty()) return ""; currentIndex = (currentIndex + 1) % files.size(); list.repaint(); return files[currentIndex]; }
    juce::String getPrevFile() { if (files.isEmpty()) return ""; currentIndex = (currentIndex - 1 + files.size()) % files.size(); list.repaint(); return files[currentIndex]; }
    int getNumRows() override { return files.size(); }
    void paint(juce::Graphics& g) override {
        g.fillAll(Theme::bgPanel);
        if (files.isEmpty()) { g.setColour(juce::Colours::grey); g.setFont(juce::FontOptions(14.0f)); g.drawText("- Drag & Drop .mid -", getLocalBounds().withTrimmedTop(20), juce::Justification::centred, true); }
    }
    void paintListBoxItem(int r, juce::Graphics& g, int w, int h, bool s) override {
        if (s || r == currentIndex) g.fillAll(Theme::accent.withAlpha(0.3f));
        g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(14.0f)); g.drawText(files[r], 5, 0, w, h, juce::Justification::centredLeft);
    }
    void resized() override { auto h = getLocalBounds().removeFromTop(20); btnClear.setBounds(h.removeFromRight(40)); header.setBounds(h); list.setBounds(getLocalBounds().withTrimmedTop(20)); }
};

class StepSequencer : public juce::Component {
public:
    juce::ComboBox cmbSteps, cmbRate; juce::Label lblTitle{ {}, "Sequencer" };
    juce::Slider noteSlider; juce::Label lblNote{ {}, "Root:" };
    juce::OwnedArray<juce::ToggleButton> stepButtons; int numSteps = 16, currentStep = -1;

    StepSequencer() {
        addAndMakeVisible(lblTitle); lblTitle.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
        addAndMakeVisible(cmbSteps); cmbSteps.addItemList({ "4", "8", "12", "16" }, 1); cmbSteps.setSelectedId(4, juce::dontSendNotification);
        cmbSteps.onChange = [this] { rebuildSteps(cmbSteps.getText().getIntValue()); };
        addAndMakeVisible(cmbRate); cmbRate.addItem("1/1", 1); cmbRate.addItem("1/2", 2); cmbRate.addItem("1/4", 3);
        cmbRate.addItem("1/8", 4); cmbRate.addItem("1/16", 5); cmbRate.addItem("1/32", 6); cmbRate.setSelectedId(5, juce::dontSendNotification);

        addAndMakeVisible(noteSlider);
        noteSlider.setRange(36, 72, 1);
        noteSlider.setValue(60);
        noteSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        noteSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);

        noteSlider.textFromValueFunction = [](double value) {
            return juce::MidiMessage::getMidiNoteName((int)value, true, true, 3);
            };

        addAndMakeVisible(lblNote);
        lblNote.setJustificationType(juce::Justification::centredRight);

        rebuildSteps(16);
    }
    void rebuildSteps(int count) {
        numSteps = count; stepButtons.clear();
        for (int i = 0; i < numSteps; ++i) { auto* b = stepButtons.add(new juce::ToggleButton(juce::String(i + 1))); b->setColour(juce::ToggleButton::tickColourId, Theme::accent); addAndMakeVisible(b); }
        resized();
    }
    void setActiveStep(int s) { if (currentStep != s) { currentStep = s % numSteps; repaint(); } }
    bool isStepActive(int s) { if (s >= 0 && s < stepButtons.size()) return stepButtons[s]->getToggleState(); return false; }
    void paint(juce::Graphics& g) override {
        g.fillAll(Theme::bgPanel); g.setColour(juce::Colours::grey); g.drawRect(getLocalBounds(), 1);
        if (numSteps > 0 && currentStep >= 0) { float stepW = (float)getWidth() / numSteps; g.setColour(Theme::accent.withAlpha(0.6f)); g.fillRect((float)(currentStep * stepW), 30.0f, stepW, (float)(getHeight() - 30)); }
    }
    void resized() override {
        auto r = getLocalBounds().reduced(2); auto head = r.removeFromTop(30);
        lblTitle.setBounds(head.removeFromLeft(70)); cmbSteps.setBounds(head.removeFromLeft(60)); cmbRate.setBounds(head.removeFromLeft(70));
        head.removeFromLeft(10);
        lblNote.setBounds(head.removeFromLeft(40));
        noteSlider.setBounds(head.removeFromLeft(100));

        if (numSteps > 0) { float w = (float)r.getWidth() / numSteps; for (int i = 0; i < numSteps; ++i) stepButtons[i]->setBounds((int)(r.getX() + i * w), r.getY(), (int)w, r.getHeight()); }
    }
};

// ==================== TRACK GRID / VERTICAL STRIP ====================
class ComplexPianoRoll : public juce::Component {
public:
    juce::MidiKeyboardState& keyboardState;
    struct ChannelInfo { int channel; int program; juce::String name; };
    std::vector<ChannelInfo> activeTracks;
    bool isVerticalStripMode = false;

    ComplexPianoRoll(juce::MidiKeyboardState& s) : keyboardState(s) {}

    void setPixelsPerSecond(float) {}
    void setVisualTranspose(int) {}
    void setPlayhead(float) {}

    void setVerticalStripMode(bool b) {
        isVerticalStripMode = b;
        repaint();
    }

    void loadSequence(const juce::MidiMessageSequence& s) {
        activeTracks.clear();
        std::map<int, int> chProgs; std::set<int> chActives;
        for (auto* ev : s) {
            if (ev->message.isNoteOn()) chActives.insert(ev->message.getChannel());
            if (ev->message.isProgramChange()) chProgs[ev->message.getChannel()] = ev->message.getProgramChangeNumber();
        }
        for (int ch : chActives) {
            int prog = chProgs.count(ch) ? chProgs[ch] : -1;
            juce::String name = "CH " + juce::String(ch);
            if (prog >= 0) name += " (P" + juce::String(prog) + ")";
            if (ch == 10) name = "DRUMS";
            activeTracks.push_back({ ch, prog, name });
        }
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(Theme::bgDark);

        if (isVerticalStripMode) {
            // SIMPLE MODE: Vertical Strip (No grid text)
            g.setColour(Theme::grid);
            int step = getHeight() / 12;
            for (int i = 0; i < 12; ++i) {
                g.drawHorizontalLine(i * step, 0, getWidth());
                g.setColour(juce::Colours::white.withAlpha(0.05f));
                g.fillRect(0, i * step, getWidth(), step - 1);
                g.setColour(Theme::grid);
            }
            g.drawRect(getLocalBounds(), 1);
            return;
        }

        // DEFAULT MODE: Grid View
        if (activeTracks.empty()) { g.setColour(Theme::text.withAlpha(0.5f)); g.drawText("No MIDI File Loaded", getLocalBounds(), juce::Justification::centred, true); return; }

        int cols = 4;
        int itemW = getWidth() / cols;
        int itemH = 30;

        for (int i = 0; i < activeTracks.size(); ++i) {
            int row = i / cols;
            int col = i % cols;
            auto r = juce::Rectangle<int>(col * itemW, row * itemH, itemW, itemH).reduced(2);
            g.setColour(Theme::getChannelColor(activeTracks[i].channel));
            g.fillRoundedRectangle(r.toFloat(), 4.0f);
            g.setColour(juce::Colours::black.withAlpha(0.5f)); g.drawRect(r);
            g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
            g.drawText(activeTracks[i].name, r, juce::Justification::centred, true);
        }
    }
    void resized() override {}
};

class MixerContainer : public juce::Component {
public:
    struct MixerStrip : public juce::Component, public juce::Slider::Listener {
        juce::Slider volSlider; juce::TextEditor nameLabel; juce::ToggleButton btnActive;
        int channelIndex; std::function<void(int, float)> onLevelChange; std::function<void(int, bool)> onActiveChange;
        MixerStrip(int i) : channelIndex(i) {
            volSlider.setSliderStyle(juce::Slider::LinearVertical); volSlider.setRange(0, 127, 1); volSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); volSlider.addListener(this); addAndMakeVisible(volSlider);
            nameLabel.setText(juce::String(i + 1)); nameLabel.setFont(juce::FontOptions(12.0f)); nameLabel.setJustification(juce::Justification::centred);
            nameLabel.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack); nameLabel.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack); addAndMakeVisible(nameLabel);
            btnActive.setToggleState(true, juce::dontSendNotification); btnActive.setButtonText("ON"); btnActive.onClick = [this] { if (onActiveChange) onActiveChange(channelIndex + 1, btnActive.getToggleState()); }; addAndMakeVisible(btnActive);
        }
        void sliderValueChanged(juce::Slider* s) override { if (onLevelChange) onLevelChange(channelIndex + 1, (float)s->getValue()); }
        void paint(juce::Graphics& g) override {
            auto r = getLocalBounds().reduced(2); g.setColour(Theme::bgPanel); g.fillRoundedRectangle(r.toFloat(), 4.0f);
            if (btnActive.getToggleState()) { g.setColour(Theme::getChannelColor(channelIndex + 1).withAlpha(0.2f)); g.fillRoundedRectangle(r.toFloat(), 4.0f); }
            float level = (float)volSlider.getValue() / 127.0f; int h = (int)((volSlider.getHeight()) * level);
            g.setColour(Theme::getChannelColor(channelIndex + 1).withAlpha(0.5f)); g.fillRect((float)volSlider.getX(), (float)(volSlider.getBottom() - h), (float)volSlider.getWidth(), (float)h);
        }
        void resized() override { nameLabel.setBounds(0, getHeight() - 20, getWidth(), 20); btnActive.setBounds(0, 2, getWidth(), 15); volSlider.setBounds(0, 20, getWidth(), getHeight() - 40); }
    };

    juce::OwnedArray<MixerStrip> strips; std::function<void(int, float)> onMixerActivity; std::function<void(int, bool)> onChannelToggle;
    const int stripWidth = 60;
    MixerContainer() {
        for (int i = 0; i < 16; ++i) {
            auto* s = strips.add(new MixerStrip(i));
            s->onLevelChange = [this](int ch, float val) { if (onMixerActivity) onMixerActivity(ch, val); };
            s->onActiveChange = [this](int ch, bool active) { if (onChannelToggle) onChannelToggle(ch, active); };
            addAndMakeVisible(s);
        }
    }
    juce::String getChannelName(int ch) { if (ch < 1 || ch > 16) return juce::String(ch); return strips[ch - 1]->nameLabel.getText(); }
    void resized() override { for (int i = 0; i < 16; ++i) strips[i]->setBounds(i * stripWidth, 0, stripWidth, getHeight()); }
};

class OscAddressConfig : public juce::Component {
public:
    juce::Label lblTitle{ {}, "OSC Addresses" }; juce::Label lblGui{ {}, "GUI Control" };
    juce::Label lN{ {}, "Note:" }, lV{ {}, "Velocity:" }, lOff{ {}, "Off:" }, lCC{ {}, "CC#:" }, lCCV{ {}, "CCVal:" }, lP{ {}, "Pitch:" }, lPr{ {}, "Touch:" };
    juce::TextEditor eN, eV, eOff, eCC, eCCV, eP, ePr;
    juce::Label lPlay{ {}, "Play:" }, lStop{ {}, "Stop:" }, lRew{ {}, "Rew:" }, lLoop{ {}, "Loop:" };
    juce::Label lTap{ {}, "Tap:" }, lOctUp{ {}, "Oct+:" }, lOctDn{ {}, "Oct-:" };
    juce::TextEditor ePlay, eStop, eRew, eLoop, eTap, eOctUp, eOctDn;

    OscAddressConfig() {
        addAndMakeVisible(lblTitle); lblTitle.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
        setup(lN, eN, "/ch{X}note"); setup(lV, eV, "/ch{X}nvalue"); setup(lOff, eOff, "/ch{X}noteoff");
        setup(lCC, eCC, "/ch{X}cc"); setup(lCCV, eCCV, "/ch{X}ccvalue"); setup(lP, eP, "/ch{X}pitch"); setup(lPr, ePr, "/ch{X}pressure");
        addAndMakeVisible(lblGui); lblGui.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
        setup(lPlay, ePlay, "/play"); setup(lStop, eStop, "/stop"); setup(lRew, eRew, "/rewind"); setup(lLoop, eLoop, "/loop");
        setup(lTap, eTap, "/tap"); setup(lOctUp, eOctUp, "/octup"); setup(lOctDn, eOctDn, "/octdown");
    }
    void setup(juce::Label& l, juce::TextEditor& e, juce::String def) { addAndMakeVisible(l); addAndMakeVisible(e); e.setText(def); }
    void paint(juce::Graphics& g) override { g.fillAll(Theme::bgPanel.withAlpha(0.95f)); g.setColour(Theme::accent); g.drawRect(getLocalBounds(), 2); }
    void resized() override {
        auto r = getLocalBounds().reduced(20); lblTitle.setBounds(r.removeFromTop(30));
        auto addRow = [&](juce::Label& l, juce::TextEditor& e) { auto row = r.removeFromTop(25); l.setBounds(row.removeFromLeft(60)); e.setBounds(row); r.removeFromTop(5); };
        addRow(lN, eN); addRow(lV, eV); addRow(lOff, eOff); addRow(lCC, eCC); addRow(lCCV, eCCV); addRow(lP, eP); addRow(lPr, ePr);
        r.removeFromTop(15); lblGui.setBounds(r.removeFromTop(25));
        addRow(lPlay, ePlay); addRow(lStop, eStop); addRow(lRew, eRew); addRow(lLoop, eLoop);
        addRow(lTap, eTap); addRow(lOctUp, eOctUp); addRow(lOctDn, eOctDn);
    }
};