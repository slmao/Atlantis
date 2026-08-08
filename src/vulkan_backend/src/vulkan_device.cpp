#include "vulkan_device.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <atlantis/assert.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include "validation.h"
#include "vulkan_instance.h"
#include "wsi/win32_surface.h"

namespace atlantis::vulkan_backend::detail {

namespace {

constexpr const char* kSwapchainExtension = "VK_KHR_swapchain";

// Two-call idiom for vkEnumeratePhysicalDevices. Same VK_INCOMPLETE/
// failure handling rationale as vulkan_instance.cpp's
// enumerateInstanceExtensions(): partial data is never treated as a
// complete result.
[[nodiscard]] std::optional<std::vector<VkPhysicalDevice>> enumeratePhysicalDevices(VkInstance instance) {
  std::uint32_t count = 0;
  if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS) {
    return std::nullopt;
  }
  if (count == 0) {
    return std::vector<VkPhysicalDevice>{};
  }

  std::vector<VkPhysicalDevice> devices(count);
  if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) != VK_SUCCESS) {
    return std::nullopt;
  }
  return devices;
}

// Two-call idiom for vkEnumerateDeviceExtensionProperties (pLayerName ==
// nullptr: the device's own extensions, not a specific layer's).
[[nodiscard]] std::optional<std::vector<VkExtensionProperties>> enumerateDeviceExtensions(
    VkPhysicalDevice physicalDevice) {
  std::uint32_t count = 0;
  if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr) != VK_SUCCESS) {
    return std::nullopt;
  }
  if (count == 0) {
    return std::vector<VkExtensionProperties>{};
  }

  std::vector<VkExtensionProperties> extensions(count);
  if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensions.data()) != VK_SUCCESS) {
    return std::nullopt;
  }
  return extensions;
}

[[nodiscard]] bool supportsSwapchainExtension(VkPhysicalDevice physicalDevice) {
  const std::optional<std::vector<VkExtensionProperties>> extensions = enumerateDeviceExtensions(physicalDevice);
  if (!extensions.has_value()) {
    return false;
  }
  for (const auto& extension : *extensions) {
    if (std::strcmp(extension.extensionName, kSwapchainExtension) == 0) {
      return true;
    }
  }
  return false;
}

// vkGetPhysicalDeviceQueueFamilyProperties returns void -- no VkResult
// exists for this function, so none is fabricated. Ordinary count-then-
// fill two-call form.
[[nodiscard]] std::vector<VkQueueFamilyProperties> queryQueueFamilies(VkPhysicalDevice physicalDevice) {
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
  if (count == 0) {
    return {};
  }
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, families.data());
  return families;
}

// The first queue family index on physicalDevice supporting both
// VK_QUEUE_GRAPHICS_BIT and Win32 generic presentation (Plan Section 7
// item 2) -- a combined graphics+present family. Separate-family fallback
// is not implemented, per the Plan's explicit Phase 1 disposition.
[[nodiscard]] std::optional<std::uint32_t> findCombinedGraphicsPresentQueueFamily(VkPhysicalDevice physicalDevice) {
  const std::vector<VkQueueFamilyProperties> families = queryQueueFamilies(physicalDevice);
  for (std::uint32_t index = 0; index < families.size(); ++index) {
    const bool hasGraphics = (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    if (hasGraphics && win32PresentationSupported(physicalDevice, index)) {
      return index;
    }
  }
  return std::nullopt;
}

struct PhysicalDeviceSelection {
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  std::uint32_t queueFamilyIndex = 0;
};

// Selects the first physical device, in vkEnumeratePhysicalDevices' own
// returned order, meeting Phase 1's minimum requirement: Vulkan API
// version >= 1.0 (Spec 0003 needs nothing higher -- this is a structural
// check, not a stricter version this Plan has no basis to invent),
// VK_KHR_swapchain support, and a combined graphics+present queue family
// (Plan Section 7 items 2-3). No scoring, no discrete/integrated
// preference, no caller-visible enumeration/selection API.
[[nodiscard]] std::optional<PhysicalDeviceSelection> selectPhysicalDevice(
    const std::vector<VkPhysicalDevice>& physicalDevices) {
  for (VkPhysicalDevice physicalDevice : physicalDevices) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_0) {
      continue;
    }
    if (!supportsSwapchainExtension(physicalDevice)) {
      continue;
    }
    const std::optional<std::uint32_t> queueFamilyIndex = findCombinedGraphicsPresentQueueFamily(physicalDevice);
    if (!queueFamilyIndex.has_value()) {
      continue;
    }
    return PhysicalDeviceSelection{physicalDevice, *queueFamilyIndex};
  }
  return std::nullopt;
}

// Move-only RAII guards for partial-construction cleanup. Declared and
// used in createDevice() strictly in creation order, so C++'s automatic
// reverse-destruction-order tears them down device -> messenger ->
// instance on any early-return failure -- exactly the order Section 6's
// destruction-boundary table requires. release() transfers ownership out
// on success, so nothing already handed to VulkanDevice is destroyed a
// second time.

class InstanceGuard {
 public:
  explicit InstanceGuard(VkInstance instance) : instance_(instance) {}
  ~InstanceGuard() {
    if (instance_ != VK_NULL_HANDLE) {
      vkDestroyInstance(instance_, nullptr);
    }
  }
  InstanceGuard(const InstanceGuard&) = delete;
  InstanceGuard& operator=(const InstanceGuard&) = delete;

  [[nodiscard]] VkInstance get() const noexcept { return instance_; }
  [[nodiscard]] VkInstance release() noexcept {
    VkInstance released = instance_;
    instance_ = VK_NULL_HANDLE;
    return released;
  }

 private:
  VkInstance instance_;
};

class MessengerGuard {
 public:
  MessengerGuard(VkInstance instance, VkDebugUtilsMessengerEXT messenger,
                 PFN_vkDestroyDebugUtilsMessengerEXT destroyFn)
      : instance_(instance), messenger_(messenger), destroyFn_(destroyFn) {}
  ~MessengerGuard() {
    if (messenger_ != VK_NULL_HANDLE) {
      ATLANTIS_CHECK(destroyFn_ != nullptr);
      destroyFn_(instance_, messenger_, nullptr);
    }
  }
  MessengerGuard(const MessengerGuard&) = delete;
  MessengerGuard& operator=(const MessengerGuard&) = delete;

  [[nodiscard]] VkDebugUtilsMessengerEXT release() noexcept {
    VkDebugUtilsMessengerEXT released = messenger_;
    messenger_ = VK_NULL_HANDLE;
    return released;
  }

 private:
  VkInstance instance_;
  VkDebugUtilsMessengerEXT messenger_;
  PFN_vkDestroyDebugUtilsMessengerEXT destroyFn_;
};

class DeviceGuard {
 public:
  explicit DeviceGuard(VkDevice device) : device_(device) {}
  ~DeviceGuard() {
    if (device_ != VK_NULL_HANDLE) {
      vkDestroyDevice(device_, nullptr);
    }
  }
  DeviceGuard(const DeviceGuard&) = delete;
  DeviceGuard& operator=(const DeviceGuard&) = delete;

  [[nodiscard]] VkDevice release() noexcept {
    VkDevice released = device_;
    device_ = VK_NULL_HANDLE;
    return released;
  }

 private:
  VkDevice device_;
};

}  // namespace

VulkanDevice::VulkanDevice(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue,
                            std::uint32_t queueFamilyIndex, VkDebugUtilsMessengerEXT explicitMessenger,
                            PFN_vkDestroyDebugUtilsMessengerEXT destroyMessengerFn)
    : instance_(instance),
      physicalDevice_(physicalDevice),
      device_(device),
      queue_(queue),
      queueFamilyIndex_(queueFamilyIndex),
      explicitMessenger_(explicitMessenger),
      destroyMessengerFn_(destroyMessengerFn) {}

VulkanDevice::~VulkanDevice() {
  // Destruction order: VkDevice, then the explicit VkDebugUtilsMessengerEXT
  // (if any), then VkInstance -- matches Section 6's destruction-boundary
  // table and this file's guard-based creation-order pattern.
  vkDestroyDevice(device_, nullptr);
  if (explicitMessenger_ != VK_NULL_HANDLE) {
    ATLANTIS_CHECK(destroyMessengerFn_ != nullptr);
    destroyMessengerFn_(instance_, explicitMessenger_, nullptr);
  }
  vkDestroyInstance(instance_, nullptr);
}

}  // namespace atlantis::vulkan_backend::detail

namespace atlantis::vulkan_backend {

atlantis::Result<std::unique_ptr<atlantis::rhi::Device>, DeviceCreateError> createDevice(
    const DeviceCreateParams& params) {
  using ResultT = atlantis::Result<std::unique_ptr<atlantis::rhi::Device>, DeviceCreateError>;

  // Computed exactly once; every later decision in this function uses
  // this value, never params.enableValidationLayers directly (validation.h).
  const bool validationEnabled =
      detail::effectiveValidationLayersEnabled(detail::IsDebugBuild, params.enableValidationLayers);

  atlantis::Result<detail::InstanceCreateResult, DeviceCreateError> instanceResult =
      detail::createInstance(params, validationEnabled);
  if (instanceResult.isErr()) {
    return ResultT::Err(instanceResult.error());
  }
  detail::InstanceGuard instanceGuard(instanceResult.value().instance);

  VkDebugUtilsMessengerEXT explicitMessenger = VK_NULL_HANDLE;
  PFN_vkDestroyDebugUtilsMessengerEXT destroyMessengerFn = nullptr;
  if (validationEnabled) {
    auto createMessengerFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instanceGuard.get(), "vkCreateDebugUtilsMessengerEXT"));
    destroyMessengerFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instanceGuard.get(), "vkDestroyDebugUtilsMessengerEXT"));
    if (createMessengerFn == nullptr || destroyMessengerFn == nullptr) {
      return ResultT::Err(DeviceCreateError::InstanceCreationFailed);
    }

    const VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo = detail::makeDebugMessengerCreateInfo();
    if (createMessengerFn(instanceGuard.get(), &messengerCreateInfo, nullptr, &explicitMessenger) != VK_SUCCESS) {
      return ResultT::Err(DeviceCreateError::InstanceCreationFailed);
    }
  }
  detail::MessengerGuard messengerGuard(instanceGuard.get(), explicitMessenger, destroyMessengerFn);

  const std::optional<std::vector<VkPhysicalDevice>> physicalDevices =
      detail::enumeratePhysicalDevices(instanceGuard.get());
  if (!physicalDevices.has_value()) {
    return ResultT::Err(DeviceCreateError::NoSuitablePhysicalDevice);
  }
  const std::optional<detail::PhysicalDeviceSelection> selection = detail::selectPhysicalDevice(*physicalDevices);
  if (!selection.has_value()) {
    return ResultT::Err(DeviceCreateError::NoSuitablePhysicalDevice);
  }

  constexpr float kQueuePriority = 1.0f;
  VkDeviceQueueCreateInfo queueCreateInfo{};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = selection->queueFamilyIndex;
  queueCreateInfo.queueCount = 1;
  queueCreateInfo.pQueuePriorities = &kQueuePriority;

  const char* deviceExtension = "VK_KHR_swapchain";
  VkDeviceCreateInfo deviceCreateInfo{};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.queueCreateInfoCount = 1;
  deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
  deviceCreateInfo.enabledExtensionCount = 1;
  deviceCreateInfo.ppEnabledExtensionNames = &deviceExtension;

  VkDevice device = VK_NULL_HANDLE;
  if (vkCreateDevice(selection->physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS) {
    return ResultT::Err(DeviceCreateError::DeviceCreationFailed);
  }
  detail::DeviceGuard deviceGuard(device);

  // vkGetDeviceQueue has no VkResult -- none is fabricated. device just
  // succeeded above and selection->queueFamilyIndex came from real
  // enumeration/selection, so both inputs are already known valid.
  VkQueue queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(device, selection->queueFamilyIndex, 0, &queue);

  std::unique_ptr<atlantis::rhi::Device> vulkanDevice = std::make_unique<detail::VulkanDevice>(
      instanceGuard.release(), selection->physicalDevice, deviceGuard.release(), queue, selection->queueFamilyIndex,
      messengerGuard.release(), destroyMessengerFn);

  return ResultT::Ok(std::move(vulkanDevice));
}

}  // namespace atlantis::vulkan_backend
