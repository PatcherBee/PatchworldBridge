/*
  ==============================================================================
    MainComponent.h
    Status: COMPLETE
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <juce_opengl/juce_opengl.h>
#include "SubComponents.h"
#include <ableton/Link.hpp>
#include <set>
#include <map>
#include <vector>

// Inherit from HighResolutionTimer for 1ms background timing
class MainComponent : public juce::Component,
    public juce::Timer,
    public juce::HighResolutionTimer,
    public juce::MidiInputCallback,
    public juce::FileDragAndDropTarget,
    public juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
    public juce::MidiKeyboardStateListener,
    public juce::KeyListener,
    public juce::ValueTree::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;               // For UI Updates (25Hz)
    void hiResTimerCallback() override;          // For MIDI/OSC Playback (1000Hz)
    void mouseDown(const juce::MouseEvent& e) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    bool keyPressed(const juce::KeyPress& key, Component* originatingComponent) override;

    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void oscMessageReceived(const juce::OSCMessage& message) override;
    void handleNoteOn(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;

private:
    juce::ImageComponent logoView;
    juce::ValueTree parameters{ "Params" };
    juce::UndoManager undoManager;
    juce::CachedValue<double> bpmVal;

    ableton::Link link;
    double quantum = 4.0;
    int lastNumPeers = 0;

    juce::ComboBox cmbQuantum;
    juce::ToggleButton btnLinkToggle{ "Link" };
    juce::Slider latencySlider;
    juce::Label lblLatency{ {}, "Lat(ms)" };
    PhaseVisualizer phaseVisualizer;
    juce::TextButton btnTapTempo{ "Tap Tempo" };
    std::vector<double> tapTimes;
    int tapCounter = 0;

    juce::OpenGLContext openGLContext;
    juce::MidiKeyboardState keyboardState;

    // SEPARATE KEYBOARDS
    juce::MidiKeyboardComponent horizontalKeyboard{ keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::MidiKeyboardComponent verticalKeyboard{ keyboardState, juce::MidiKeyboardComponent::verticalKeyboardFacingRight };

    std::unique_ptr<juce::MidiInput> midiInput;
    std::unique_ptr<juce::MidiOutput> midiOutput;

    juce::CriticalSection midiLock;
    std::atomic<bool> isPlaying{ false };
    std::atomic<double> currentTransportTime{ 0.0 };
    std::atomic<int> playbackCursor{ 0 };
    std::atomic<int> stepSeqIndex{ -1 };
    std::atomic<int> virtualOctaveShift{ 0 };
    int pianoRollOctaveShift = 0;

    juce::OSCSender oscSender; juce::OSCReceiver oscReceiver; bool isOscConnected = false;
    void sendSplitOscMessage(const juce::MidiMessage& m, int overrideChannel = -1);
    int matchOscChannel(const juce::String& pattern, const juce::String& incoming);

    enum class AppView { Dashboard, OSC_Config, Help };
    AppView currentView = AppView::Dashboard;
    bool isSimpleMode = false;
    void setView(AppView v); void updateVisibility();

    juce::TextButton btnDash{ "Dashboard" }, btnOscCfg{ "OSC Config" }, btnHelp{ "Help" };
    juce::GroupComponent grpNet{ "net", "Network" };
    juce::Label lblIp{ {}, "IP:" }, lblPIn{ {}, "PIn:" }, lblPOut{ {}, "POut:" };
    juce::TextEditor edIp, edPIn, edPOut;
    juce::TextButton btnConnect{ "Connect" };
    juce::ToggleButton btnRetrigger{ "Retrigger" };
    juce::ToggleButton btnGPU{ "GPU" };
    ConnectionLight ledConnect;
    juce::GroupComponent grpIo{ "io", "MIDI I/O" };
    juce::ComboBox cmbMidiIn, cmbMidiOut, cmbMidiCh;
    juce::Label lblIn{ {}, "In" }, lblOut{ {}, "Out" }, lblCh{ {}, "CH:" };
    std::set<int> activeChannels;
    void toggleChannel(int ch, bool active);
    int getSelectedChannel() const;
    juce::TextButton btnPlay{ "Play" }, btnStop{ "Stop" }, btnPrev{ "<" }, btnSkip{ ">" }, btnResetFile{ "Reset" }, btnClearPR{ "Clear" }, btnResetBPM{ "Reset BPM" };
    juce::ToggleButton btnLoopPlaylist{ "Loop" };
    juce::Slider tempoSlider; juce::Label lblTempo;
    juce::TextButton btnPrOctUp{ "Oct +" }, btnPrOctDown{ "Oct -" };
    juce::Label lblArp{ {}, "Arp Gen:" }, lblArpBpm{ {}, "Speed" }, lblArpVel{ {}, "Vel" };
    juce::GroupComponent grpArp{ "arp", "Arp Generator" };
    juce::ToggleButton btnArp{ "Latch" }, btnArpSync{ "Sync" };
    juce::Slider sliderArpSpeed, sliderArpVel;
    juce::ComboBox cmbArpPattern;
    juce::SortedSet<int> heldNotes;
    std::vector<int> noteArrivalOrder;
    int arpNoteIndex = 0;

    ComplexPianoRoll trackGrid{ keyboardState };
    TrafficMonitor logPanel;
    MidiPlaylist playlist;
    OscAddressConfig oscConfig;
    StepSequencer sequencer;
    MixerContainer mixer;
    juce::Viewport mixerViewport;
    juce::TextEditor helpText;

    juce::MidiMessageSequence playbackSeq;
    double sequenceLength = 0.0;
    double currentFileBpm = 0.0;

    void loadMidiFile(juce::File f);
    void stopPlayback();
    void takeSnapshot(); void performUndo(); void performRedo();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};