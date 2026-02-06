/*
  ==============================================================================
    Source/UI/Panels/ChordGeneratorPanel.h
    Role: Interactive chord generator with scale-aware pads and voicing options
  ==============================================================================
*/
#pragma once
#include "../ControlHelpers.h"
#include "../Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include <algorithm>
#include <set>

class ChordGeneratorPanel : public juce::Component, private juce::Timer {
public:
    // Callback when a chord is triggered (root note, intervals from root, velocity)
    std::function<void(int root, const std::vector<int>& intervals, float velocity)> onChordTriggered;
    std::function<void(int root, const std::vector<int>& intervals)> onChordReleased;

    /** MIDI/OSC output channel for chord notes (1–16). */
    int getChordOutputChannel() const { return cmbChOut.getSelectedId(); }

    ChordGeneratorPanel() {
        // Root Note Selector
        addAndMakeVisible(cmbRoot);
        for (int i = 0; i < 12; ++i) {
            static const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            cmbRoot.addItem(noteNames[i], i + 1);
        }
        cmbRoot.setSelectedId(1);  // C
        cmbRoot.setTooltip("Root note of chord");

        // Ch Out (MIDI/OSC channel) — where Octave was
        addAndMakeVisible(cmbChOut);
        for (int ch = 1; ch <= 16; ++ch)
            cmbChOut.addItem("Ch " + juce::String(ch), ch);
        cmbChOut.setSelectedId(1);
        cmbChOut.setTooltip("MIDI/OSC output channel for chord notes (1–16)");

        // Chord Type Selector
        addAndMakeVisible(cmbChordType);
        cmbChordType.addItem("Major", 1);
        cmbChordType.addItem("Minor", 2);
        cmbChordType.addItem("Dim", 3);
        cmbChordType.addItem("Aug", 4);
        cmbChordType.addItem("Sus2", 5);
        cmbChordType.addItem("Sus4", 6);
        cmbChordType.addItem("7th", 7);
        cmbChordType.addItem("Maj7", 8);
        cmbChordType.addItem("Min7", 9);
        cmbChordType.addItem("Dim7", 10);
        cmbChordType.addItem("9th", 11);
        cmbChordType.addItem("Add9", 12);
        cmbChordType.setSelectedId(1);
        cmbChordType.setTooltip("Chord type (Major, Minor, 7th, etc.)");
        cmbChordType.onChange = [this] { updateChordPadLabels(); };
        cmbRoot.onChange = [this] { updateChordPadLabels(); };

        // Inversion Selector
        addAndMakeVisible(cmbInversion);
        cmbInversion.addItem("Root", 1);
        cmbInversion.addItem("1st Inv", 2);
        cmbInversion.addItem("2nd Inv", 3);
        cmbInversion.addItem("3rd Inv", 4);
        cmbInversion.setSelectedId(1);
        cmbInversion.setTooltip("Chord inversion (Root, 1st, 2nd, 3rd)");

        // Voicing Selector
        addAndMakeVisible(cmbVoicing);
        cmbVoicing.addItem("Close", 1);
        cmbVoicing.addItem("Open", 2);
        cmbVoicing.addItem("Drop 2", 3);
        cmbVoicing.addItem("Drop 3", 4);
        cmbVoicing.addItem("Spread", 5);
        cmbVoicing.addItem("Shell", 6);
        cmbVoicing.setSelectedId(1);
        cmbVoicing.setTooltip("Chord voicing style");

        // Trigger Mode (Instant / Strum)
        addAndMakeVisible(cmbTriggerMode);
        cmbTriggerMode.addItem("Instant", 1);
        cmbTriggerMode.addItem("Strum Down", 2);
        cmbTriggerMode.addItem("Strum Up", 3);
        cmbTriggerMode.setSelectedId(1);
        cmbTriggerMode.setTooltip("Trigger: Instant or Strum (Down/Up)");
        cmbTriggerMode.onChange = [this] {
            sliderStrumSpeed.setVisible(cmbTriggerMode.getSelectedId() > 1);
            resized();
        };
        addAndMakeVisible(sliderStrumSpeed);
        sliderStrumSpeed.setRange(10, 150, 1);
        sliderStrumSpeed.setValue(30);
        sliderStrumSpeed.setDefaultValue(30);
        sliderStrumSpeed.setSliderStyle(juce::Slider::LinearHorizontal);
        sliderStrumSpeed.setTextValueSuffix(" ms");
        sliderStrumSpeed.setTooltip("Delay between strum notes");
        sliderStrumSpeed.setVisible(false);

        // Trigger Button
        addAndMakeVisible(btnTrigger);
        btnTrigger.setButtonText("PLAY");
        btnTrigger.setTooltip("Play chord. Hold for sustain, release to stop. Strum mode: delay between notes.");
        btnTrigger.setColour(juce::TextButton::buttonColourId, Theme::accent);
        btnTrigger.addMouseListener(this, false);

        // Octave ± buttons (right of PLAY)
        addAndMakeVisible(btnOctaveMinus);
        btnOctaveMinus.setButtonText("-");
        btnOctaveMinus.setTooltip("Octave down (1–7)");
        btnOctaveMinus.onClick = [this] {
            int o = juce::jlimit(1, 7, chordOctave - 1);
            chordOctave = o;
            updateChordPadLabels();
        };
        addAndMakeVisible(btnOctavePlus);
        btnOctavePlus.setButtonText("+");
        btnOctavePlus.setTooltip("Octave up (1–7)");
        btnOctavePlus.onClick = [this] {
            int o = juce::jlimit(1, 7, chordOctave + 1);
            chordOctave = o;
            updateChordPadLabels();
        };

        // Velocity: all-in-one BPM-style bar (0–100%), no separate label
        addAndMakeVisible(sliderVelocity);
        sliderVelocity.setRange(0.0, 100.0, 1.0);
        sliderVelocity.setValue(80.0);
        sliderVelocity.setDefaultValue(80.0);
        sliderVelocity.setSliderStyle(juce::Slider::LinearBar);
        sliderVelocity.setTextBoxStyle(juce::Slider::TextBoxRight, true, 44, 20);
        sliderVelocity.setNumDecimalPlacesToDisplay(0);
        sliderVelocity.setTextValueSuffix("%");
        sliderVelocity.setDoubleClickReturnValue(true, 80.0);
        sliderVelocity.setTooltip("Chord velocity (0–100%). Click value to type or drag.");

        // Quick Chord Pads (I, ii, iii, IV, V, vi, vii) — labels updated from root/type
        static const char* degreeTooltips[] = {
            "I (tonic)", "ii (supertonic)", "iii (mediant)", "IV (subdominant)",
            "V (dominant)", "vi (submediant)", "vii\u00B0 (leading tone)"
        };
        for (int i = 0; i < 7; ++i) {
            auto* pad = new juce::TextButton(getRomanNumeral(i));
            pad->setColour(juce::TextButton::buttonColourId,
                isMinorDegree(i) ? Theme::bgPanel.darker() : Theme::bgPanel.brighter());
            pad->setTooltip(juce::String(degreeTooltips[i]) + ". Click to play scale degree chord.");
            pad->addMouseListener(this, false);
            pad->getProperties().set("degree", i);
            addAndMakeVisible(chordPads.add(pad));
        }
        updateChordPadLabels();
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10);

        // Top row: Root, Type, Inversion, Voicing (Ch Out is in play row)
        auto topRow = r.removeFromTop(30);
        cmbRoot.setBounds(topRow.removeFromLeft(55).reduced(2));
        cmbChordType.setBounds(topRow.removeFromLeft(70).reduced(2));
        cmbInversion.setBounds(topRow.removeFromLeft(65).reduced(2));
        cmbVoicing.setBounds(topRow.removeFromLeft(70).reduced(2));

        r.removeFromTop(5);

        // Trigger mode row: Instant/Strum menu + optional strum speed + velocity (BPM-style, all-in-one)
        auto modeRow = r.removeFromTop(24);
        cmbTriggerMode.setBounds(modeRow.removeFromLeft(100).reduced(2));
        if (sliderStrumSpeed.isVisible())
            sliderStrumSpeed.setBounds(modeRow.removeFromLeft(80).reduced(2));
        sliderVelocity.setBounds(modeRow.reduced(2));

        r.removeFromTop(10);

        // Chord pads row
        auto padRow = r.removeFromTop(50);
        int padWidth = padRow.getWidth() / 7;
        for (auto* pad : chordPads) {
            pad->setBounds(padRow.removeFromLeft(padWidth).reduced(2));
        }

        r.removeFromTop(10);

        // Centred row: CH out dropdown, PLAY, Octave -, +
        auto playRow = r.removeFromTop(36).reduced(8, 5);
        int totalW = 55 + 70 + 28 + 28 + 24;  // CH + PLAY + - + + + spacing
        int startX = (playRow.getWidth() - totalW) / 2;
        if (startX < 0) startX = 0;
        auto playArea = playRow.withX(playRow.getX() + startX);
        cmbChOut.setBounds(playArea.removeFromLeft(55).reduced(2));
        playArea.removeFromLeft(12);
        btnTrigger.setBounds(playArea.removeFromLeft(70).reduced(0, 4));
        playArea.removeFromLeft(8);
        btnOctaveMinus.setBounds(playArea.removeFromLeft(28).reduced(2));
        btnOctavePlus.setBounds(playArea.removeFromLeft(28).reduced(2));

        r.removeFromTop(5);
        // Chord visualization (mini piano)
        chordVisualArea = r.removeFromBottom(40).reduced(10, 4).toFloat();
    }

    void paint(juce::Graphics& g) override {
        Theme::drawStylishPanel(g, getLocalBounds().toFloat(), Theme::bgPanel, 8.0f);
        drawChordVisualization(g, chordVisualArea);
        auto now = (juce::uint32)juce::Time::getMillisecondCounter();
        if (lastTriggerTime_ != 0 && now - lastTriggerTime_ < 200) {
            float alpha = 0.35f * (1.0f - (float)(now - lastTriggerTime_) / 200.0f);
            g.setColour(juce::Colours::white.withAlpha(alpha));
            g.fillRoundedRectangle(btnTrigger.getBounds().toFloat().expanded(4), 6.0f);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        // Check if it's the main trigger button
        if (e.originalComponent == &btnTrigger) {
            triggerCurrentChord();
            lastTriggerTime_ = (juce::uint32)juce::Time::getMillisecondCounter();
            startTimer(40);
        }
        // Check chord pads
        else if (auto* btn = dynamic_cast<juce::TextButton*>(e.originalComponent)) {
            if (btn->getProperties().contains("degree")) {
                int degree = (int)btn->getProperties()["degree"];
                triggerScaleDegree(degree);
                lastTriggerTime_ = (juce::uint32)juce::Time::getMillisecondCounter();
                startTimer(40);
            }
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (e.originalComponent == &btnTrigger ||
            dynamic_cast<juce::TextButton*>(e.originalComponent)) {
            releaseChord();
        }
    }

private:
    void timerCallback() override {
        repaint();
        if (lastTriggerTime_ != 0 &&
            (juce::uint32)juce::Time::getMillisecondCounter() - lastTriggerTime_ > 200)
            stopTimer();
    }

    juce::ComboBox cmbRoot, cmbChOut, cmbChordType, cmbInversion, cmbVoicing, cmbTriggerMode;
    juce::TextButton btnTrigger, btnOctaveMinus, btnOctavePlus;
    ControlHelpers::ResponsiveSlider sliderVelocity, sliderStrumSpeed;
    int chordOctave = 4;
    juce::OwnedArray<juce::TextButton> chordPads;
    juce::Rectangle<float> chordVisualArea;
    juce::uint32 lastTriggerTime_ = 0;

    std::vector<int> lastTriggeredNotes;

    std::vector<int> applyVoicing(std::vector<int> intervals, int voicingId) {
        if (intervals.size() < 3) return intervals;
        std::sort(intervals.begin(), intervals.end());
        switch (voicingId) {
            case 2: // Open
                for (size_t i = 1; i < intervals.size(); i += 2)
                    intervals[i] += 12;
                std::sort(intervals.begin(), intervals.end());
                break;
            case 3: // Drop2
                if (intervals.size() >= 3) {
                    intervals[intervals.size() - 2] -= 12;
                    std::sort(intervals.begin(), intervals.end());
                }
                break;
            case 4: // Drop3
                if (intervals.size() >= 4) {
                    intervals[intervals.size() - 3] -= 12;
                    std::sort(intervals.begin(), intervals.end());
                }
                break;
            case 5: // Spread
                for (size_t i = 0; i < intervals.size(); ++i)
                    intervals[i] += (int)(i * 12 / intervals.size());
                break;
            case 6: { // Shell
                std::vector<int> shell = {0};
                for (int i : intervals) {
                    if ((i == 3 || i == 4) && (int)shell.size() < 2) shell.push_back(i);
                    if ((i == 10 || i == 11) && (int)shell.size() < 3) shell.push_back(i);
                }
                return shell;
            }
        }
        return intervals;
    }

    void drawChordVisualization(juce::Graphics& g, juce::Rectangle<float> area) {
        if (area.getHeight() < 8 || area.getWidth() < 50) return;
        int rootNote = (cmbRoot.getSelectedId() - 1);
        int octave = chordOctave;
        int midiRoot = rootNote + (octave * 12);
        auto intervals = getChordIntervals(cmbChordType.getSelectedId());
        intervals = applyInversion(intervals, cmbInversion.getSelectedId());
        intervals = applyVoicing(intervals, cmbVoicing.getSelectedId());

        std::set<int> activeNotes;
        for (int i : intervals)
            activeNotes.insert((midiRoot + i) % 12);

        float keyWidth = area.getWidth() / 7.0f;
        static const int whiteKeys[] = {0, 2, 4, 5, 7, 9, 11};
        static const int blackKeys[] = {1, 3, -1, 6, 8, 10, -1};
        for (int i = 0; i < 7; ++i) {
            int pc = whiteKeys[i];
            float x = area.getX() + i * keyWidth;
            auto keyRect = juce::Rectangle<float>(x + 1, area.getY(), keyWidth - 2, area.getHeight());
            bool isActive = activeNotes.count(pc) > 0;
            bool isRoot = (pc == rootNote);
            g.setColour(isRoot ? Theme::accent : (isActive ? Theme::accent.withAlpha(0.6f) : juce::Colours::white.withAlpha(0.9f)));
            g.fillRoundedRectangle(keyRect, 3.0f);
            g.setColour(juce::Colours::black.withAlpha(0.3f));
            g.drawRoundedRectangle(keyRect, 3.0f, 1.0f);
        }
        for (int i = 0; i < 7; ++i) {
            int pc = blackKeys[i];
            if (pc < 0) continue;
            float x = area.getX() + (i + 0.7f) * keyWidth;
            auto keyRect = juce::Rectangle<float>(x, area.getY(), keyWidth * 0.6f, area.getHeight() * 0.6f);
            bool isActive = activeNotes.count(pc) > 0;
            bool isRoot = (pc == rootNote);
            g.setColour(isRoot ? Theme::accent.darker(0.3f) : (isActive ? Theme::accent.darker(0.5f) : juce::Colours::black));
            g.fillRoundedRectangle(keyRect, 2.0f);
        }
    }

    std::vector<int> getChordIntervals(int typeId) {
        switch (typeId) {
            case 1:  return {0, 4, 7};           // Major
            case 2:  return {0, 3, 7};           // Minor
            case 3:  return {0, 3, 6};           // Dim
            case 4:  return {0, 4, 8};           // Aug
            case 5:  return {0, 2, 7};           // Sus2
            case 6:  return {0, 5, 7};           // Sus4
            case 7:  return {0, 4, 7, 10};       // 7th (Dom7)
            case 8:  return {0, 4, 7, 11};       // Maj7
            case 9:  return {0, 3, 7, 10};       // Min7
            case 10: return {0, 3, 6, 9};        // Dim7
            case 11: return {0, 4, 7, 10, 14};   // 9th
            case 12: return {0, 4, 7, 14};       // Add9
            default: return {0, 4, 7};
        }
    }

    std::vector<int> applyInversion(std::vector<int> intervals, int inversion) {
        if (inversion <= 1 || intervals.empty())
            return intervals;

        for (int inv = 1; inv < inversion && inv < (int)intervals.size(); ++inv) {
            // Move lowest note up an octave
            if (!intervals.empty()) {
                intervals[0] += 12;
                std::sort(intervals.begin(), intervals.end());
            }
        }
        return intervals;
    }

    void triggerCurrentChord() {
        int rootNote = (cmbRoot.getSelectedId() - 1);
        int octave = chordOctave;
        int midiRoot = rootNote + (octave * 12);

        auto intervals = getChordIntervals(cmbChordType.getSelectedId());
        intervals = applyInversion(intervals, cmbInversion.getSelectedId());
        intervals = applyVoicing(intervals, cmbVoicing.getSelectedId());

        float velocity = (float)(sliderVelocity.getValue() / 100.0);

        lastTriggeredNotes.clear();
        for (int i : intervals)
            lastTriggeredNotes.push_back(juce::jlimit(0, 127, midiRoot + i));

        int mode = cmbTriggerMode.getSelectedId();
        if (mode == 1) {
            if (onChordTriggered)
                onChordTriggered(midiRoot, intervals, velocity);
        } else {
            int delayMs = (int)sliderStrumSpeed.getValue();
            if (mode == 3) std::reverse(lastTriggeredNotes.begin(), lastTriggeredNotes.end());
            for (size_t i = 0; i < lastTriggeredNotes.size(); ++i) {
                int note = lastTriggeredNotes[i];
                int d = (int)i * delayMs;
                if (d == 0) {
                    if (onChordTriggered)
                        onChordTriggered(note, {0}, velocity);
                } else {
                    juce::Component::SafePointer<ChordGeneratorPanel> safe(this);
                    juce::Timer::callAfterDelay(d, [safe, note, velocity] {
                        if (auto* p = safe.getComponent())
                            if (p->onChordTriggered)
                                p->onChordTriggered(note, {0}, velocity);
                    });
                }
            }
        }
    }

    void triggerScaleDegree(int degree) {
        // Major scale degrees: I, ii, iii, IV, V, vi, vii
        static const int majorScaleOffsets[] = {0, 2, 4, 5, 7, 9, 11};

        int rootNote = (cmbRoot.getSelectedId() - 1);
        int octave = chordOctave;
        int degreeRoot = rootNote + majorScaleOffsets[degree] + (octave * 12);

        // Use chord type from dropdown (Major/Minor/Dim/Aug/etc) so dropdown actually affects output
        auto intervals = getChordIntervals(cmbChordType.getSelectedId());
        float velocity = (float)(sliderVelocity.getValue() / 100.0);

        if (onChordTriggered)
            onChordTriggered(degreeRoot, intervals, velocity);

        lastTriggeredNotes.clear();
        for (int i : intervals)
            lastTriggeredNotes.push_back(degreeRoot + i);
    }

    void releaseChord() {
        if (onChordReleased && !lastTriggeredNotes.empty()) {
            // Send note-offs with empty intervals to signal release
            onChordReleased(lastTriggeredNotes[0], {});
        }
        lastTriggeredNotes.clear();
    }

    juce::String getRomanNumeral(int degree) {
        static const char* numerals[] = {"I", "ii", "iii", "IV", "V", "vi", "vii"};
        return numerals[degree];
    }

    juce::String getChordTypeSuffix(int typeId) {
        switch (typeId) {
            case 1:  return "";
            case 2:  return "m";
            case 3:  return "dim";
            case 4:  return "aug";
            case 5:  return "sus2";
            case 6:  return "sus4";
            case 7:  return "7";
            case 8:  return "Maj7";
            case 9:  return "m7";
            case 10: return "dim7";
            case 11: return "9";
            case 12: return "add9";
            default: return "";
        }
    }

    void updateChordPadLabels() {
        static const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        static const int majorOffsets[] = {0, 2, 4, 5, 7, 9, 11};
        int root = cmbRoot.getSelectedId() - 1;
        if (root < 0 || root > 11) root = 0;
        juce::String suffix = getChordTypeSuffix(cmbChordType.getSelectedId());
        for (int i = 0; i < 7 && i < chordPads.size(); ++i) {
            int note = (root + majorOffsets[i]) % 12;
            chordPads[i]->setButtonText(juce::String(noteNames[note]) + suffix);
        }
    }

    bool isMinorDegree(int degree) {
        return degree == 1 || degree == 2 || degree == 5;  // ii, iii, vi
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordGeneratorPanel)
};
