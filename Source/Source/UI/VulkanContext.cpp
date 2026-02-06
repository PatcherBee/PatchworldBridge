/*
  ==============================================================================
    Source/UI/VulkanContext.cpp
    Vulkan context: instance, surface (Win32), device, swapchain, clear.
    Built when PATCHWORLD_VULKAN_SUPPORT is defined. Windows implemented first.
  ==============================================================================
*/
#if PATCHWORLD_VULKAN_SUPPORT

#include "UI/VulkanContext.h"
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <vulkan/vulkan_win32.h>
#include <windows.h>
#endif

#if JUCE_LINUX
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#endif

#if JUCE_MAC
#include <MoltenVK/mvk_vulkan.h>
#include <vulkan/vulkan_macos.h>
#endif

#include <algorithm>
#include <cstring>
#include <set>

namespace {

const std::vector<const char *> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"};

#if JUCE_WINDOWS
const std::vector<const char *> kInstanceExtensions = {
    VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
#elif JUCE_LINUX
const std::vector<const char *> kInstanceExtensions = {
    VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
#elif JUCE_MAC
const std::vector<const char *> kInstanceExtensions = {
    VK_KHR_SURFACE_EXTENSION_NAME, VK_MVK_MACOS_SURFACE_EXTENSION_NAME};
#else
const std::vector<const char *> kInstanceExtensions = {};
#endif

const std::vector<const char *> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME};

} // namespace

void VulkanContext::setError(const juce::String &msg) { lastError_ = msg; }

void VulkanContext::setClearColour(float r, float g, float b, float a) {
  clearR_ = r;
  clearG_ = g;
  clearB_ = b;
  clearA_ = a;
}

bool VulkanContext::attachTo(juce::Component &component) {
  if (attached_) {
    detach();
  }
  component_ = &component;

#if JUCE_WINDOWS
  {
    auto *peer = component.getPeer();
    if (!peer) {
      setError("Window not ready. Try again after the window is shown.");
      return false;
    }
    void *native = peer->getNativeHandle();
    if (!native) {
      setError("Could not get native window handle.");
      return false;
    }
  }
  width_ = static_cast<uint32_t>(std::max(1, component.getWidth()));
  height_ = static_cast<uint32_t>(std::max(1, component.getHeight()));
#elif JUCE_LINUX
  {
    auto *peer = component.getPeer();
    if (!peer) {
      setError("Window not ready. Try again after the window is shown.");
      return false;
    }
    if (!peer->getNativeHandle()) {
      setError("Could not get X11 window handle.");
      return false;
    }
  }
  width_ = static_cast<uint32_t>(std::max(1, component.getWidth()));
  height_ = static_cast<uint32_t>(std::max(1, component.getHeight()));
#elif JUCE_MAC
  {
    auto *peer = component.getPeer();
    if (!peer) {
      setError("Window not ready. Try again after the window is shown.");
      return false;
    }
    if (!peer->getNativeHandle()) {
      setError("Could not get native view handle.");
      return false;
    }
  }
  width_ = static_cast<uint32_t>(std::max(1, component.getWidth()));
  height_ = static_cast<uint32_t>(std::max(1, component.getHeight()));
#else
  (void)component;
  setError("Vulkan not supported on this platform.");
  return false;
#endif

  if (!createInstance())
    return false;
  if (!createSurface()) {
    shutdown();
    return false;
  }
  if (!pickPhysicalDevice()) {
    shutdown();
    return false;
  }
  if (!createDevice()) {
    shutdown();
    return false;
  }
  if (!createSwapchain()) {
    shutdown();
    return false;
  }
  if (!createRenderPass()) {
    shutdown();
    return false;
  }
  if (!createFramebuffers()) {
    shutdown();
    return false;
  }
  if (!createCommandPoolAndBuffers()) {
    shutdown();
    return false;
  }

  attached_ = true;
  lastError_.clear();
  return true;
}

void VulkanContext::detach() {
  shutdown();
  component_ = nullptr;
  attached_ = false;
}

bool VulkanContext::createInstance() {
  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Patchworld Bridge";
  appInfo.applicationVersion = VK_MAKE_VERSION(2, 0, 0);
  appInfo.pEngineName = "Patchworld";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(kInstanceExtensions.size());
  createInfo.ppEnabledExtensionNames = kInstanceExtensions.data();
  createInfo.enabledLayerCount = 0;

  VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
  if (result != VK_SUCCESS) {
    setError("vkCreateInstance failed: " +
             juce::String(static_cast<int>(result)));
    return false;
  }
  return true;
}

bool VulkanContext::createSurface() {
#if JUCE_WINDOWS
  auto *peer = component_->getPeer();
  if (!peer)
    return false;
  HWND hwnd = static_cast<HWND>(peer->getNativeHandle());
  HINSTANCE hinst = GetModuleHandle(nullptr);
  VkWin32SurfaceCreateInfoKHR surfInfo = {};
  surfInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surfInfo.hwnd = hwnd;
  surfInfo.hinstance = hinst;
  VkResult result =
      vkCreateWin32SurfaceKHR(instance_, &surfInfo, nullptr, &surface_);
  if (result != VK_SUCCESS) {
    setError(
        "Could not create Vulkan surface (Windows). Try Software or OpenGL.");
    return false;
  }
  return true;
#elif JUCE_LINUX
  auto *peer = component_->getPeer();
  if (!peer)
    return false;
  display_ = XOpenDisplay(nullptr);
  if (!display_) {
    setError("Could not open X11 display. Try Software or OpenGL.");
    return false;
  }
  Window win = reinterpret_cast<Window>(peer->getNativeHandle());
  VkXlibSurfaceCreateInfoKHR surfInfo = {};
  surfInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
  surfInfo.dpy = static_cast<Display *>(display_);
  surfInfo.window = win;
  VkResult result =
      vkCreateXlibSurfaceKHR(instance_, &surfInfo, nullptr, &surface_);
  if (result != VK_SUCCESS) {
    XCloseDisplay(static_cast<Display *>(display_));
    display_ = nullptr;
    setError(
        "Could not create Vulkan surface (Linux). Try Software or OpenGL.");
    return false;
  }
  return true;
#elif JUCE_MAC
  auto *peer = component_->getPeer();
  if (!peer)
    return false;
  void *view = peer->getNativeHandle();
  if (!view)
    return false;
  VkMacOSSurfaceCreateInfoMVK surfInfo = {};
  surfInfo.sType = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK;
  surfInfo.pView = view;
  VkResult result =
      vkCreateMacOSSurfaceMVK(instance_, &surfInfo, nullptr, &surface_);
  if (result != VK_SUCCESS) {
    setError(
        "Could not create Vulkan surface (macOS). Try Software or OpenGL.");
    return false;
  }
  return true;
#else
  (void)component_;
  setError("Vulkan surface not available on this platform.");
  return false;
#endif
}

bool VulkanContext::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
  if (deviceCount == 0) {
    setError("No Vulkan physical devices found.");
    return false;
  }
  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

  for (VkPhysicalDevice dev : devices) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount,
                                             queueFamilies.data());

    uint32_t graphicsIdx = UINT32_MAX, presentIdx = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
      if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        graphicsIdx = i;
      VkBool32 presentSupport = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &presentSupport);
      if (presentSupport)
        presentIdx = i;
    }
    if (graphicsIdx != UINT32_MAX && presentIdx != UINT32_MAX) {
      uint32_t extCount = 0;
      vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
      std::vector<VkExtensionProperties> exts(extCount);
      vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount,
                                           exts.data());
      std::set<juce::String> extNames;
      for (const auto &e : exts)
        extNames.insert(e.extensionName);
      bool hasSwapchain = extNames.count(VK_KHR_SWAPCHAIN_EXTENSION_NAME) > 0;
      if (hasSwapchain) {
        physicalDevice_ = dev;
        graphicsQueueFamily_ = graphicsIdx;
        presentQueueFamily_ = presentIdx;
        return true;
      }
    }
  }
  setError(
      "No suitable Vulkan physical device (graphics + present + swapchain).");
  return false;
}

bool VulkanContext::createDevice() {
  std::set<uint32_t> uniqueFamilies = {graphicsQueueFamily_,
                                       presentQueueFamily_};
  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  float queuePriority = 1.0f;
  for (uint32_t family : uniqueFamilies) {
    VkDeviceQueueCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    info.queueFamilyIndex = family;
    info.queueCount = 1;
    info.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(info);
  }

  VkPhysicalDeviceFeatures features = {};
  VkDeviceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();
  createInfo.pEnabledFeatures = &features;
  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(kDeviceExtensions.size());
  createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();

  VkResult result =
      vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_);
  if (result != VK_SUCCESS) {
    setError("vkCreateDevice failed: " +
             juce::String(static_cast<int>(result)));
    return false;
  }
  vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
  vkGetDeviceQueue(device_, presentQueueFamily_, 0, &presentQueue_);
  return true;
}

bool VulkanContext::createSwapchain() {
  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

  uint32_t formatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount,
                                       nullptr);
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount,
                                       formats.data());

  swapchainFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
  for (const auto &f : formats) {
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      swapchainFormat_ = f.format;
      break;
    }
  }

  VkExtent2D extent;
  if (caps.currentExtent.width != UINT32_MAX) {
    extent = caps.currentExtent;
  } else {
    extent.width = std::clamp(width_, caps.minImageExtent.width,
                              caps.maxImageExtent.width);
    extent.height = std::clamp(height_, caps.minImageExtent.height,
                               caps.maxImageExtent.height);
  }
  swapchainExtent_ = extent;

  uint32_t imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
    imageCount = caps.maxImageCount;

  VkSwapchainCreateInfoKHR createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = surface_;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = swapchainFormat_;
  createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  uint32_t queueFamilies[] = {graphicsQueueFamily_, presentQueueFamily_};
  if (graphicsQueueFamily_ != presentQueueFamily_) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilies;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }
  createInfo.preTransform = caps.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  createInfo.clipped = VK_TRUE;

  VkResult result =
      vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_);
  if (result != VK_SUCCESS) {
    setError("vkCreateSwapchainKHR failed: " +
             juce::String(static_cast<int>(result)));
    return false;
  }

  vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
  swapchainImages_.resize(imageCount);
  vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount,
                          swapchainImages_.data());

  swapchainImageViews_.resize(swapchainImages_.size());
  for (size_t i = 0; i < swapchainImages_.size(); ++i) {
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = swapchainImages_[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = swapchainFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &viewInfo, nullptr,
                          &swapchainImageViews_[i]) != VK_SUCCESS) {
      setError("vkCreateImageView failed for swapchain image.");
      return false;
    }
  }
  return true;
}

bool VulkanContext::createRenderPass() {
  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = swapchainFormat_;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorRef = {};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;

  VkRenderPassCreateInfo rpInfo = {};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpInfo.attachmentCount = 1;
  rpInfo.pAttachments = &colorAttachment;
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;

  if (vkCreateRenderPass(device_, &rpInfo, nullptr, &renderPass_) !=
      VK_SUCCESS) {
    setError("vkCreateRenderPass failed.");
    return false;
  }
  return true;
}

bool VulkanContext::createFramebuffers() {
  framebuffers_.resize(swapchainImageViews_.size());
  for (size_t i = 0; i < swapchainImageViews_.size(); ++i) {
    VkFramebufferCreateInfo fbInfo = {};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &swapchainImageViews_[i];
    fbInfo.width = swapchainExtent_.width;
    fbInfo.height = swapchainExtent_.height;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffers_[i]) !=
        VK_SUCCESS) {
      setError("vkCreateFramebuffer failed.");
      return false;
    }
  }
  return true;
}

bool VulkanContext::createSyncObjects() {
  imageAvailableSemaphores_.resize(maxFramesInFlight_);
  renderFinishedSemaphores_.resize(maxFramesInFlight_);
  inFlightFences_.resize(maxFramesInFlight_);
  VkSemaphoreCreateInfo semInfo = {};
  semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (size_t i = 0; i < maxFramesInFlight_; ++i) {
    if (vkCreateSemaphore(device_, &semInfo, nullptr,
                          &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
        vkCreateSemaphore(device_, &semInfo, nullptr,
                          &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
        vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) !=
            VK_SUCCESS) {
      setError("Failed to create sync objects.");
      return false;
    }
  }
  return true;
}

bool VulkanContext::createCommandPoolAndBuffersOnly() {
  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = graphicsQueueFamily_;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) !=
      VK_SUCCESS) {
    setError("vkCreateCommandPool failed.");
    return false;
  }
  VkCommandBufferAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool_;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = static_cast<uint32_t>(framebuffers_.size());
  commandBuffers_.resize(framebuffers_.size());
  if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) !=
      VK_SUCCESS) {
    setError("vkAllocateCommandBuffers failed.");
    return false;
  }
  return true;
}

bool VulkanContext::createCommandPoolAndBuffers() {
  return createSyncObjects() && createCommandPoolAndBuffersOnly();
}

void VulkanContext::render() {
  if (!attached_ || !device_ || swapchain_ == VK_NULL_HANDLE || !component_)
    return;

  // Recreate swapchain if main window was resized (e.g. after
  // flushPendingResize)
  uint32_t currentW =
      static_cast<uint32_t>(std::max(1, component_->getWidth()));
  uint32_t currentH =
      static_cast<uint32_t>(std::max(1, component_->getHeight()));
  if (currentW != swapchainExtent_.width ||
      currentH != swapchainExtent_.height) {
    if (!recreateSwapchain())
      return;
  }

  vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE,
                  UINT64_MAX);

  uint32_t imageIndex;
  VkResult result = vkAcquireNextImageKHR(
      device_, swapchain_, UINT64_MAX, imageAvailableSemaphores_[currentFrame_],
      VK_NULL_HANDLE, &imageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapchain();
    return;
  }
  if (result == VK_ERROR_DEVICE_LOST) {
    if (onDeviceLost_)
      juce::MessageManager::callAsync(onDeviceLost_);
    return;
  }
  if (result != VK_SUCCESS)
    return;
  /* VK_SUBOPTIMAL_KHR: continue and present */

  vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);

  VkCommandBuffer cmd = commandBuffers_[imageIndex];
  vkResetCommandBuffer(cmd, 0);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(cmd, &beginInfo);

  VkClearValue clearValue = {};
  clearValue.color.float32[0] = clearR_;
  clearValue.color.float32[1] = clearG_;
  clearValue.color.float32[2] = clearB_;
  clearValue.color.float32[3] = clearA_;

  VkRenderPassBeginInfo rpBegin = {};
  rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rpBegin.renderPass = renderPass_;
  rpBegin.framebuffer = framebuffers_[imageIndex];
  rpBegin.renderArea.offset = {0, 0};
  rpBegin.renderArea.extent = swapchainExtent_;
  rpBegin.clearValueCount = 1;
  rpBegin.pClearValues = &clearValue;
  vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  VkSemaphore waitSems[] = {imageAvailableSemaphores_[currentFrame_]};
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSems;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  VkSemaphore signalSems[] = {renderFinishedSemaphores_[currentFrame_]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSems;
  vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]);

  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &renderFinishedSemaphores_[currentFrame_];
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapchain_;
  presentInfo.pImageIndices = &imageIndex;
  VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
  if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
      presentResult == VK_SUBOPTIMAL_KHR)
    recreateSwapchain();
  else if (presentResult == VK_ERROR_DEVICE_LOST && onDeviceLost_)
    juce::MessageManager::callAsync(onDeviceLost_);

  currentFrame_ = (currentFrame_ + 1) % maxFramesInFlight_;
}

void VulkanContext::destroySwapchainOnly() {
  if (!device_)
    return;
  vkDeviceWaitIdle(device_);
  if (commandPool_ != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device_, commandPool_, nullptr);
    commandPool_ = VK_NULL_HANDLE;
  }
  commandBuffers_.clear();
  for (auto fb : framebuffers_)
    if (fb != VK_NULL_HANDLE)
      vkDestroyFramebuffer(device_, fb, nullptr);
  framebuffers_.clear();
  for (auto iv : swapchainImageViews_)
    if (iv != VK_NULL_HANDLE)
      vkDestroyImageView(device_, iv, nullptr);
  swapchainImageViews_.clear();
  swapchainImages_.clear();
  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

bool VulkanContext::recreateSwapchain() {
  if (!component_ || !device_ || surface_ == VK_NULL_HANDLE ||
      renderPass_ == VK_NULL_HANDLE)
    return false;

  // FIX Issue #9: Prevent swapchain recreation when minimized or zero size
  if (component_->getWidth() <= 0 || component_->getHeight() <= 0)
    return false;

  width_ = static_cast<uint32_t>(std::max(1, component_->getWidth()));
  height_ = static_cast<uint32_t>(std::max(1, component_->getHeight()));
  destroySwapchainOnly();
  if (!createSwapchain() || !createFramebuffers() ||
      !createCommandPoolAndBuffersOnly())
    return false;
  currentFrame_ = 0;
  return true;
}

void VulkanContext::shutdown() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
    for (size_t i = 0; i < maxFramesInFlight_; ++i) {
      if (imageAvailableSemaphores_[i] != VK_NULL_HANDLE)
        vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
      if (renderFinishedSemaphores_[i] != VK_NULL_HANDLE)
        vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
      if (inFlightFences_[i] != VK_NULL_HANDLE)
        vkDestroyFence(device_, inFlightFences_[i], nullptr);
    }
    imageAvailableSemaphores_.clear();
    renderFinishedSemaphores_.clear();
    inFlightFences_.clear();
    if (commandPool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device_, commandPool_, nullptr);
      commandPool_ = VK_NULL_HANDLE;
    }
    commandBuffers_.clear();
    for (auto fb : framebuffers_)
      if (fb != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();
    if (renderPass_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, renderPass_, nullptr);
      renderPass_ = VK_NULL_HANDLE;
    }
    for (auto iv : swapchainImageViews_)
      if (iv != VK_NULL_HANDLE)
        vkDestroyImageView(device_, iv, nullptr);
    swapchainImageViews_.clear();
    swapchainImages_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(device_, swapchain_, nullptr);
      swapchain_ = VK_NULL_HANDLE;
    }
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  graphicsQueue_ = VK_NULL_HANDLE;
  presentQueue_ = VK_NULL_HANDLE;
  physicalDevice_ = VK_NULL_HANDLE;
  if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }
#if JUCE_LINUX
  if (display_) {
    XCloseDisplay(static_cast<Display *>(display_));
    display_ = nullptr;
  }
#endif
  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

#endif // PATCHWORLD_VULKAN_SUPPORT
