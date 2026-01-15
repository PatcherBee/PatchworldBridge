cmake_minimum_required(VERSION 3.15)
project(PatchworldBridge VERSION 0.0.1)

# 1. Link JUCE
add_subdirectory("C:/JUCE" JUCE)

# 2. Register the logo so 'BinaryData::logo_png' exists in your C++ code
juce_add_binary_data(BinaryAssets SOURCES "logo.png")

# 3. Create the app (Internal name has no spaces to ensure folder creation)
juce_add_gui_app(PatchworldBridge
    PRODUCT_NAME "Patchworld MIDI OSC Bridge" # This is the name of the .exe file
    ICON_BIG "logo.png" 
)

# 4. Source Files
target_sources(PatchworldBridge PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp)

# 5. Link Modules and Assets
target_link_libraries(PatchworldBridge PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_osc
    juce::juce_opengl
    BinaryAssets) # Links the logo variables

# 6. Ableton Link
set(LINK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/link")
target_include_directories(PatchworldBridge PRIVATE
    "${LINK_DIR}/include"
    "${LINK_DIR}/modules/asio-standalone/asio/include")

if(MSVC OR WIN32)
    target_compile_definitions(PatchworldBridge PRIVATE 
        LINK_PLATFORM_WINDOWS=1
        _WIN32_WINNT=0x0601) 
endif()

juce_generate_juce_header(PatchworldBridge)