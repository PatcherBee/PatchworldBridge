cmake_minimum_required(VERSION 3.22)

project(PatchworldBridge VERSION 1.0.0 LANGUAGES C CXX)

# Ensure C++17 for JUCE and Ableton Link compatibility
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Add JUCE (Expects a 'JUCE' folder with its own CMakeLists.txt)
add_subdirectory(JUCE)

# 2. Setup Ableton Link & Asio (Resolves 'IoContext' and 'Clock' errors)
add_library(AbletonLink INTERFACE)
target_include_directories(AbletonLink INTERFACE 
    "link/include"
    "link/modules/asio-standalone/asio/include"
)
if(WIN32)
    target_compile_definitions(AbletonLink INTERFACE 
        ASIO_STANDALONE 
        LINK_PLATFORM_WINDOWS
        _WIN32_WINNT=0x0A00
    )
elseif(APPLE)
    target_compile_definitions(AbletonLink INTERFACE 
        ASIO_STANDALONE 
        LINK_PLATFORM_MACOSX
    )
endif()

# 3. Create the App
juce_add_gui_app(PatchworldBridge
    PRODUCT_NAME "Patchworld MIDI OSC Bridge"
    VERSION "1.0.0"
    BUNDLE_ID "com.patchworld.bridge")

# 4. Source Files 
target_sources(PatchworldBridge PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/SubComponents.h
    Source/Components/Common.h
    Source/Components/Tools.h
    Source/Components/Sequencer.h
    Source/Components/Mixer.h
    Source/Components/Controls.h
    Source/Components/MidiScheduler.h)

# 5. Header Search Paths (Fixes IntelliSense and "File Not Found" errors)
target_include_directories(PatchworldBridge PRIVATE 
    Source
    Source/Components
    JuceLibraryCode
)

# 6. Link Libraries
target_link_libraries(PatchworldBridge PRIVATE
    AbletonLink
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_core
    juce::juce_data_structures
    juce::juce_osc
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_opengl)

# 7. Binary Data (Matches BinaryData::logo_png in MainComponent.cpp)
if(WIN32)
    target_link_libraries(PatchworldBridge PRIVATE ws2_32 iphlpapi)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/logo.png")
    juce_add_binary_data(BinaryData SOURCES logo.png)
    target_link_libraries(PatchworldBridge PRIVATE BinaryData)
endif()

# 8. Generate Header (Must be at the end)
juce_generate_juce_header(PatchworldBridge)