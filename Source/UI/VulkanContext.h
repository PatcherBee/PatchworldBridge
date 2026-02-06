/*
  ==============================================================================
    Source/UI/VulkanContext.h
    Role: Vulkan rendering context (instance, device, swapchain, clear).
    Built only when PATCHWORLD_VULKAN_SUPPORT is defined (CMake option).
  ==============================================================================
*/
#pragma once

#if PATCHWORLD_VULKAN_SUPPORT

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vulkan/vulkan.h>

#include <vector>
#include <functional>

class VulkanContext {
public:
  VulkanContext() = default;
  ~VulkanContext() { shutdown(); }

  /** Attach to a top-level component and create surface from its native window. */
  bool attachTo(juce::Component& component);
  void detach();

  /** One frame: acquire image, clear, present. Call from message/rendering thread. */
  void render();

  bool isAttached() const { return attached_; }
  juce::String getLastError() const { return lastError_; }

  /** Optional: clear colour (default dark). */
  void setClearColour(float r, float g, float b, float a);

  /** Called on message thread when device is lost; app should switch to software. */
  void setOnDeviceLost(std::function<void()> cb) { onDeviceLost_ = std::move(cb); }

private:
  bool recreateSwapchain();
  void destroySwapchainOnly();
  bool createInstance();
  bool createSurface();
  bool pickPhysicalDevice();
  bool createDevice();
  bool createSwapchain();
  bool createRenderPass();
  bool createFramebuffers();
  bool createSyncObjects();
  bool createCommandPoolAndBuffers();
  bool createCommandPoolAndBuffersOnly();
  void shutdown();
  void setError(const juce::String& msg);

  std::function<void()> onDeviceLost_;

  juce::Component* component_ = nullptr;
  juce::String lastError_;
  bool attached_ = false;

  float clearR_ = 0.08f, clearG_ = 0.08f, clearB_ = 0.12f, clearA_ = 1.0f;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
  VkQueue presentQueue_ = VK_NULL_HANDLE;
  uint32_t graphicsQueueFamily_ = 0;
  uint32_t presentQueueFamily_ = 0;

  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  std::vector<VkImage> swapchainImages_;
  std::vector<VkImageView> swapchainImageViews_;
  VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
  VkExtent2D swapchainExtent_ = { 0, 0 };

  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers_;

  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> commandBuffers_;

  std::vector<VkSemaphore> imageAvailableSemaphores_;
  std::vector<VkSemaphore> renderFinishedSemaphores_;
  std::vector<VkFence> inFlightFences_;
  size_t currentFrame_ = 0;
  static constexpr size_t maxFramesInFlight_ = 2;

  uint32_t width_ = 0;
  uint32_t height_ = 0;

#if JUCE_LINUX
  void* display_ = nullptr;  // X11 Display* for surface creation; closed in shutdown
#endif
};

#endif // PATCHWORLD_VULKAN_SUPPORT
