/*
  ==============================================================================
    Source/UI/RenderBackend.h
    Role: Platform-specific GPU renderer selection (OpenGL/Metal/Vulkan).
    Supports: Software (all platforms), OpenGL (all), Vulkan (Win/Linux when
    loader present), Metal (macOS). Current GPU implementation uses OpenGL;
    Vulkan/Metal are detected for future use and preferred in Auto on supported
    platforms.
  ==============================================================================
*/
#pragma once
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

class RenderBackend {
public:
  enum class Type {
    Software,
    OpenGL,
    Metal,
    Vulkan,
    Auto // Platform-specific best: OpenGL or Vulkan/Metal when implemented
  };

  /** True if we actually use this backend for drawing. */
  static bool isBackendImplemented(Type type) {
    if (type == Type::Software || type == Type::OpenGL)
      return true;
#if PATCHWORLD_VULKAN_SUPPORT
    if (type == Type::Vulkan)
      return true;
#endif
    return false;
  }

  struct Capabilities {
    bool supportsOpenGL = false;
    bool supportsMetal = false;
    bool supportsVulkan = false;
    juce::String openGLVersion;
    juce::String metalVersion;
    juce::String vulkanVersion;
  };

  static Capabilities detectCapabilities() {
    if (hasCachedCapabilities)
      return cachedCapabilities;

    Capabilities caps;

#if JUCE_OPENGL
    juce::OpenGLContext testContext;
    juce::Component dummyComponent;
    testContext.setRenderer(nullptr);
    testContext.setComponentPaintingEnabled(true);
    testContext.attachTo(dummyComponent);
    caps.supportsOpenGL = true;
    caps.openGLVersion = "OpenGL";
    testContext.detach();
#endif

#if JUCE_MAC
    caps.supportsMetal = true;
    caps.metalVersion = "Metal 2.0+ (native)";
    // MoltenVK: Vulkan on Metal (Vulkan SDK for Mac or standalone MoltenVK)
    juce::DynamicLibrary vulkanLib;
    if (vulkanLib.open("libvulkan.1.dylib") ||
        vulkanLib.open("libMoltenVK.dylib") ||
        vulkanLib.open("@executable_path/../Frameworks/libMoltenVK.dylib") ||
        vulkanLib.open("@rpath/libMoltenVK.dylib")) {
      if (vulkanLib.getFunction("vkGetInstanceProcAddr") != nullptr) {
        caps.supportsVulkan = true;
        caps.vulkanVersion = "MoltenVK (Vulkan on Metal)";
      }
    }
#endif

#if JUCE_WINDOWS || JUCE_LINUX
    juce::DynamicLibrary vulkanLib;
#if JUCE_WINDOWS
    if (vulkanLib.open("vulkan-1.dll")) {
#else
    if (vulkanLib.open("libvulkan.so.1") || vulkanLib.open("libvulkan.so")) {
#endif
      if (vulkanLib.getFunction("vkGetInstanceProcAddr") != nullptr) {
        caps.supportsVulkan = true;
        caps.vulkanVersion = "Vulkan 1.x (loader present)";
      }
    }
#endif

    cachedCapabilities = caps;
    hasCachedCapabilities = true;
    return caps;
  }

  static void refreshCapabilities() { hasCachedCapabilities = false; }

  static Type getDefaultBackend() {
#if JUCE_MAC
    return Type::OpenGL; // Metal preferred by OS; we use OpenGL for
                         // compatibility
#elif JUCE_IOS
    return Type::OpenGL;
#elif JUCE_ANDROID
    return Type::OpenGL;
#elif JUCE_WINDOWS || JUCE_LINUX
    return Type::OpenGL; // Vulkan optional when implemented
#else
    return Type::Software;
#endif
  }

  /** Resolve preferred to an implemented backend. Falls back to OpenGL then
   * Software. */
  static Type selectBestAvailable(Type preferred) {
    auto caps = detectCapabilities();
    switch (preferred) {
    case Type::Software:
      return Type::Software;
    case Type::OpenGL:
      return caps.supportsOpenGL ? Type::OpenGL : Type::Software;
    case Type::Metal:
      if (caps.supportsMetal && isBackendImplemented(Type::Metal))
        return Type::Metal;
      break;
    case Type::Vulkan:
      if (caps.supportsVulkan && isBackendImplemented(Type::Vulkan))
        return Type::Vulkan;
      break;
    case Type::Auto:
      break;
    default:
      break;
    }
    // Fallback: use implemented GPU path (OpenGL) or Software
    if (caps.supportsOpenGL)
      return Type::OpenGL;
    return Type::Software;
  }

  static juce::String getBackendName(Type type) {
    switch (type) {
    case Type::Software:
      return "Software";
    case Type::OpenGL:
      return "OpenGL";
    case Type::Metal:
      return "Metal";
    case Type::Vulkan:
      return "Vulkan";
    case Type::Auto:
      return "Auto";
    default:
      return "Unknown";
    }
  }

  static juce::String getBackendDescription(Type type) {
    switch (type) {
    case Type::Software:
      return "CPU-only, works on all platforms. No GPU required.";
    case Type::OpenGL:
      return "GPU-accelerated (OpenGL). Supported on Windows, Linux, macOS.";
    case Type::Metal:
      return "GPU (Metal, macOS native). Detected for future use.";
    case Type::Vulkan:
      return "GPU (Vulkan). Win/Linux: loader. macOS: MoltenVK (Vulkan on "
             "Metal). Future use.";
    case Type::Auto:
      return "Use best available: OpenGL or Software.";
    default:
      return "";
    }
  }

  static juce::StringArray getAvailableBackends() {
    juce::StringArray backends;
    backends.add("Software");
    auto caps = detectCapabilities();
    if (caps.supportsOpenGL)
      backends.add("OpenGL");
    if (caps.supportsMetal)
      backends.add("Metal");
    if (caps.supportsVulkan)
      backends.add("Vulkan");
    backends.add("Auto");
    return backends;
  }

  static Type getCurrentBackend() { return currentBackend; }
  static void setCurrentBackend(Type type) {
    currentBackend = selectBestAvailable(type);
  }

private:
  static inline Type currentBackend = Type::Auto;
  static inline bool hasCachedCapabilities = false;
  static inline Capabilities cachedCapabilities;
};
