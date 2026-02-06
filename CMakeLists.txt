cmake_minimum_required(VERSION 3.15)

project(PatchworldBridge VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compile optimization: fewer translation units = faster incremental builds
option(PATCHWORLD_UNITY_BUILD "Use unity build (faster full, slower incremental)" OFF)

# Optional Vulkan rendering backend (Windows/Linux; macOS via MoltenVK)
option(PATCHWORLD_VULKAN_SUPPORT "Build Vulkan rendering backend" OFF)
# If ON: require Vulkan SDK when Vulkan support is requested (configure fails if not found).
# If OFF: build without Vulkan when SDK missing (graceful fallback).
option(PATCHWORLD_VULKAN_REQUIRED "Fail configure if Vulkan requested but SDK not found" OFF)

if(MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT PatchworldBridge)
endif()

include(FetchContent)

# --- JUCE SETUP ---
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0
)
FetchContent_MakeAvailable(juce)

# --- ASSETS ---
juce_add_binary_data(PatchworldAssets SOURCES Source/UI/Assets/logo.png)

# --- MAIN TARGET ---
juce_add_gui_app(PatchworldBridge
    PRODUCT_NAME "PatchworldBridge"
    VERSION "2.0.0"
    BUNDLE_ID "com.patchworld.bridge"
    COMPANY_NAME "Patchworld"
    ICON_BIG "Source/UI/Assets/logo.png"
    MODULES 
        juce_audio_basics juce_audio_devices juce_audio_formats
        juce_audio_processors juce_audio_utils juce_core
        juce_data_structures juce_events juce_graphics
        juce_gui_basics juce_gui_extra juce_opengl juce_osc
)

target_sources(PatchworldBridge PRIVATE
    Source/Main.cpp
    Source/UI/MainComponent.cpp
    Source/UI/MainComponentCore.cpp
    Source/UI/MainComponentEvents.cpp
    Source/UI/MainComponentGL.cpp
    Source/Core/BridgeContext.cpp
    Source/Core/CommandDispatcher.cpp
    Source/Core/TransportViewModel.cpp
    Source/Core/MixerViewModel.cpp
    Source/Core/SequencerViewModel.cpp
    Source/Core/MenuBuilder.cpp
    Source/Core/MidiHardwareController.cpp
    Source/Core/SystemController.cpp

    Source/Audio/AudioEngine.cpp
    Source/Audio/MidiRouter.cpp
    Source/Audio/MidiScheduler.cpp
    Source/Audio/ClockWorker.h
    Source/Audio/ClockSmoother.cpp
    Source/Audio/PlaybackController.cpp
    Source/Network/NetworkWorker.cpp
    Source/Network/OscManager.cpp
    Source/Services/MidiDeviceService.cpp
    Source/Services/MidiMappingService.cpp
    Source/Services/ProfileService.cpp
    Source/Services/GamepadService.cpp

    Source/UI/Panels/SpliceEditor.cpp
    Source/UI/Panels/SpliceEditorMouse.cpp
    Source/UI/Panels/SpliceEditorRendering.cpp
    Source/UI/Panels/PerformancePanel.cpp
    Source/UI/Panels/TooltipManager.cpp
    Source/UI/Panels/TransportPanel.cpp
    Source/UI/Widgets/MeterBarRenderer.cpp
)

# Vulkan: enable if SDK is found; optional fallback or strict require
if(PATCHWORLD_VULKAN_SUPPORT)
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        if(PATCHWORLD_VULKAN_REQUIRED)
            message(FATAL_ERROR "Vulkan support was requested (-DPATCHWORLD_VULKAN_SUPPORT=ON) but Vulkan SDK was not found. Install from https://vulkan.lunarg.com/ or set PATCHWORLD_VULKAN_SUPPORT=OFF.")
        else()
            set(PATCHWORLD_VULKAN_SUPPORT OFF CACHE BOOL "Build Vulkan rendering backend" FORCE)
            message(STATUS "Vulkan SDK not found; building without Vulkan backend. Install from https://vulkan.lunarg.com/ to enable.")
        endif()
    endif()
endif()
if(PATCHWORLD_VULKAN_SUPPORT)
    target_sources(PatchworldBridge PRIVATE Source/UI/VulkanContext.cpp)
    target_link_libraries(PatchworldBridge PRIVATE Vulkan::Vulkan)
    target_compile_definitions(PatchworldBridge PRIVATE PATCHWORLD_VULKAN_SUPPORT=1)
endif()

target_include_directories(PatchworldBridge PRIVATE Source)

# --- ABLETON LINK ---
add_library(AbletonLink INTERFACE)
target_include_directories(AbletonLink INTERFACE 
    "${CMAKE_CURRENT_SOURCE_DIR}/link/include" 
    "${CMAKE_CURRENT_SOURCE_DIR}/link/modules/asio-standalone/asio/include"
)

# Ableton Link: platform and ASIO standalone (required for Win/Linux/macOS)
if(WIN32)
    target_compile_definitions(AbletonLink INTERFACE ASIO_STANDALONE LINK_PLATFORM_WINDOWS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(AbletonLink INTERFACE ASIO_STANDALONE LINK_PLATFORM_LINUX)
elseif(APPLE)
    target_compile_definitions(AbletonLink INTERFACE ASIO_STANDALONE LINK_PLATFORM_MACOSX)
endif()

# --- LINKING ---
target_link_libraries(PatchworldBridge PRIVATE 
    AbletonLink
    PatchworldAssets
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils 
    juce::juce_osc 
    juce::juce_opengl 
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    juce::juce_gui_extra
    juce::juce_core
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_events
    juce::juce_data_structures
)

if(MSVC)
    # /MP = parallel compile
    target_compile_options(PatchworldBridge PRIVATE /EHsc /bigobj /MP /arch:AVX2 /utf-8)
    
    # Link system libraries (avrt for MMCSS Pro Audio)
    target_link_libraries(PatchworldBridge PRIVATE 
        ws2_32 
        iphlpapi 
        winmm 
        opengl32 
        shlwapi 
        runtimeobject
        avrt
    )
    
    target_compile_definitions(PatchworldBridge PRIVATE 
        JUCE_WIN_PERFORMANCE_COUNTER=1 
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX 
)
endif()

# --- LINUX SPECIFIC ---
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Link X11 and friends
    target_link_libraries(PatchworldBridge PRIVATE
        dl
        pthread
        rt
        GL
    )
endif()

# --- macOS / Apple Silicon (ARM) ---
if(APPLE)
    # Prefer native arch: arm64 (Apple Silicon) or x86_64 (Intel). JUCE may set this; override only if unset.
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Apple Silicon; set to x86_64 for Intel-only")
    endif()
    # Universal binary: set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "macOS min (10.14 Mojave; Metal/MoltenVK 10.11+)")
    endif()
    # Optional: link MoltenVK at build time (Vulkan on Metal). Runtime detection in RenderBackend works without this.
    option(PATCHWORLD_LINK_MOLTENVK "Link MoltenVK on macOS (Vulkan SDK or Homebrew molten-vk)" OFF)
    if(PATCHWORLD_LINK_MOLTENVK)
        find_library(MOLTENVK_LIB MoltenVK PATHS "$ENV{VULKAN_SDK}/lib" /usr/local/lib /opt/homebrew/lib)
        if(MOLTENVK_LIB)
            target_link_libraries(PatchworldBridge PRIVATE "${MOLTENVK_LIB}")
            target_compile_definitions(PatchworldBridge PRIVATE JUCE_MOLTENVK_LINKED=1)
        endif()
    endif()
endif()

juce_generate_juce_header(PatchworldBridge)