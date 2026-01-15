/*
  ==============================================================================
    Main.cpp
  ==============================================================================
*/

#include <JuceHeader.h>
#include "MainComponent.h"

class StandaloneOSCApplication : public juce::JUCEApplication {
public:
    StandaloneOSCApplication() {}
    const juce::String getApplicationName() override { return "Standalone OSC"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    void initialise(const juce::String&) override { mainWindow.reset(new MainWindow(getApplicationName())); }
    void shutdown() override { mainWindow = nullptr; }

    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow(juce::String name) : DocumentWindow(name, juce::Colours::black, allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);

            // --- ADDED WINDOW ICON LOGIC ---
            auto icon = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);
            setIcon(icon);
            // -------------------------------

            setResizable(true, true);
            centreWithSize(805, 630);
            setVisible(true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };
private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(StandaloneOSCApplication)