#include "vulkan_instance.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <atlantis/assert.h>

#include "instance_api_version.h"
#include "validation.h"

namespace atlantis::vulkan_backend::detail {

namespace {

constexpr const char* kSurfaceExtension = "VK_KHR_surface";
constexpr const char* kWin32SurfaceExtension = "VK_KHR_win32_surface";
constexpr const char* kDebugUtilsExtension = "VK_EXT_debug_utils";
constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";
// Spec 0007 / ADR-0024 Section 8: queried and, if present, enabled at the
// instance level -- purely a query-mechanism prerequisite for the later
// per-physical-device dynamic-rendering capability query
// (vkGetPhysicalDeviceFeatures2KHR). Does not raise VkApplicationInfo's
// requested apiVersion (stays VK_API_VERSION_1_0, unchanged) and does not
// itself indicate anything about which dynamic-rendering path any given
// physical device supports.
constexpr const char* kGetPhysicalDeviceProperties2Extension = "VK_KHR_get_physical_device_properties2";

// Two-call idiom for vkEnumerateInstanceExtensionProperties (pLayerName ==
// nullptr: the implementation's own extensions, not a specific layer's).
// Returns std::nullopt on any checked VkResult failure, including
// VK_INCOMPLETE on the sized second call -- this function never treats
// partial data as a complete result; a size change between the two calls
// (astronomically unlikely with no concurrent Vulkan call in between, per
// the Phase 1 single-logical-frame-thread baseline, ADR-0004) is surfaced
// as a failure, not silently worked around with a retry loop. On success,
// the vector is resized to the second call's own returned count -- that
// count may be smaller than the first call's count, and the vector must
// never be returned with trailing, never-written default-constructed
// elements past it.
[[nodiscard]] std::optional<std::vector<VkExtensionProperties>> enumerateInstanceExtensions() {
  std::uint32_t count = 0;
  if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) {
    return std::nullopt;
  }
  if (count == 0) {
    return std::vector<VkExtensionProperties>{};
  }

  std::vector<VkExtensionProperties> extensions(count);
  const VkResult fillResult = vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
  if (fillResult != VK_SUCCESS) {
    return std::nullopt;
  }
  extensions.resize(count);
  return extensions;
}

// Two-call idiom for vkEnumerateInstanceLayerProperties. Same
// VK_INCOMPLETE/failure-handling and final-count-resize rationale as
// enumerateInstanceExtensions() above.
[[nodiscard]] std::optional<std::vector<VkLayerProperties>> enumerateInstanceLayers() {
  std::uint32_t count = 0;
  if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) {
    return std::nullopt;
  }
  if (count == 0) {
    return std::vector<VkLayerProperties>{};
  }

  std::vector<VkLayerProperties> layers(count);
  const VkResult fillResult = vkEnumerateInstanceLayerProperties(&count, layers.data());
  if (fillResult != VK_SUCCESS) {
    return std::nullopt;
  }
  layers.resize(count);
  return layers;
}

[[nodiscard]] bool containsExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
  for (const auto& extension : extensions) {
    if (std::strcmp(extension.extensionName, name) == 0) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool containsLayer(const std::vector<VkLayerProperties>& layers, const char* name) {
  for (const auto& layer : layers) {
    if (std::strcmp(layer.layerName, name) == 0) {
      return true;
    }
  }
  return false;
}

using ResultT = atlantis::Result<InstanceCreateResult, DeviceCreateError>;

}  // namespace

VkDebugUtilsMessengerCreateInfoEXT makeDebugMessengerCreateInfo() noexcept {
  VkDebugUtilsMessengerCreateInfoEXT createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugMessengerCallback;
  createInfo.pUserData = nullptr;
  return createInfo;
}

atlantis::Result<InstanceCreateResult, DeviceCreateError> createInstance(const DeviceCreateParams& params,
                                                                          bool validationEnabled) {
  const std::optional<std::vector<VkExtensionProperties>> availableExtensions = enumerateInstanceExtensions();
  if (!availableExtensions.has_value()) {
    return ResultT::Err(DeviceCreateError::InstanceCreationFailed);
  }
  if (!containsExtension(*availableExtensions, kSurfaceExtension) ||
      !containsExtension(*availableExtensions, kWin32SurfaceExtension)) {
    return ResultT::Err(DeviceCreateError::InstanceCreationFailed);
  }
  if (validationEnabled && !containsExtension(*availableExtensions, kDebugUtilsExtension)) {
    return ResultT::Err(DeviceCreateError::InstanceCreationFailed);
  }

  // Spec 0007 / ADR-0024 Section 8, step 1: computed exactly once, before
  // the instance exists at all -- an instance-wide fact, never re-queried
  // per physical-device candidate below.
  const bool physicalDeviceProperties2Available =
      containsExtension(*availableExtensions, kGetPhysicalDeviceProperties2Extension);

  if (validationEnabled) {
    const std::optional<std::vector<VkLayerProperties>> availableLayers = enumerateInstanceLayers();
    if (!availableLayers.has_value()) {
      return ResultT::Err(DeviceCreateError::InstanceCreationFailed);
    }
    if (!containsLayer(*availableLayers, kValidationLayerName)) {
      return ResultT::Err(DeviceCreateError::ValidationLayerUnavailable);
    }
  }

  std::vector<const char*> enabledExtensions{kSurfaceExtension, kWin32SurfaceExtension};
  std::vector<const char*> enabledLayers;
  if (validationEnabled) {
    enabledExtensions.push_back(kDebugUtilsExtension);
    enabledLayers.push_back(kValidationLayerName);
  }
  // Step 2: if available, requested alongside whatever this repository's
  // existing instance creation already enables. If unavailable, simply
  // not requested -- vkCreateInstance() itself is entirely unaffected
  // either way; only the later capability query is gated by this fact.
  if (physicalDeviceProperties2Available) {
    enabledExtensions.push_back(kGetPhysicalDeviceProperties2Extension);
  }

  // Copied into a local std::string so the c_str() pointer VkApplicationInfo
  // uses stays valid for the duration of this call only -- no borrowed
  // reference into params is retained beyond this function.
  const std::string applicationName = params.applicationName;

  // ADR-0024 "Accepted Amendment -- 2026-08-13", Section 3, point 1:
  // resolved via vkGetInstanceProcAddr(nullptr, ...) -- a global command,
  // valid to call with no VkInstance in existence yet. A nullptr result
  // is itself the Vulkan specification's own documented way to detect a
  // genuine Vulkan-1.0-only loader; loaderVersion is never read in that
  // case (decideRequestedInstanceApiVersion() ignores it whenever
  // loaderVersionQueryAvailable is false).
  const auto enumerateInstanceVersionFn =
      reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
  std::uint32_t loaderVersion = 0;
  const bool loaderVersionQueryAvailable = enumerateInstanceVersionFn != nullptr;
  if (loaderVersionQueryAvailable) {
    // vkEnumerateInstanceVersion's own VkResult is always VK_SUCCESS per
    // the Vulkan specification (it has no documented failure code) --
    // still assigned to a named local rather than discarded, matching
    // this codebase's "every VkResult is checked" discipline, but not
    // separately branched on since there is no defined failure outcome
    // to check against.
    const VkResult versionResult = enumerateInstanceVersionFn(&loaderVersion);
    ATLANTIS_CHECK(versionResult == VK_SUCCESS);
  }
  const std::uint32_t requestedApiVersion =
      decideRequestedInstanceApiVersion(loaderVersionQueryAvailable, loaderVersion);

  VkApplicationInfo applicationInfo{};
  applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  applicationInfo.pApplicationName = applicationName.c_str();
  applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
  applicationInfo.pEngineName = "Atlantis";
  applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
  applicationInfo.apiVersion = requestedApiVersion;

  const VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = makeDebugMessengerCreateInfo();

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &applicationInfo;
  createInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size());
  createInfo.ppEnabledExtensionNames = enabledExtensions.data();
  createInfo.enabledLayerCount = static_cast<std::uint32_t>(enabledLayers.size());
  createInfo.ppEnabledLayerNames = enabledLayers.data();
  if (validationEnabled) {
    // Input structure consumed synchronously by vkCreateInstance; not
    // retained by the loader beyond this call (see vulkan_instance.h /
    // Section 6's "Precisely What the pNext-Chained Messenger Covers").
    createInfo.pNext = &debugCreateInfo;
  }

  VkInstance instance = VK_NULL_HANDLE;
  if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
    return ResultT::Err(DeviceCreateError::InstanceCreationFailed);
  }

  // Step 3: immediately after vkCreateInstance() succeeds, if the
  // extension was enabled, resolve vkGetPhysicalDeviceFeatures2KHR's
  // function pointer explicitly via vkGetInstanceProcAddr -- never
  // assumed to be directly linkable. If resolution fails (returns
  // nullptr), the feature is treated as unavailable for this instance's
  // whole lifetime, same as if the extension had never been enabled.
  PFN_vkGetPhysicalDeviceFeatures2KHR getPhysicalDeviceFeatures2KHR = nullptr;
  bool physicalDeviceProperties2Resolved = false;
  if (physicalDeviceProperties2Available) {
    getPhysicalDeviceFeatures2KHR = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2KHR>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2KHR"));
    physicalDeviceProperties2Resolved = getPhysicalDeviceFeatures2KHR != nullptr;
  }

  InstanceCreateResult result{instance};
  result.physicalDeviceProperties2ExtensionAvailable = physicalDeviceProperties2Resolved;
  result.getPhysicalDeviceFeatures2KHR = physicalDeviceProperties2Resolved ? getPhysicalDeviceFeatures2KHR : nullptr;
  result.instanceRequestedApiVersionAtLeast1_3 = requestedApiVersion >= VK_API_VERSION_1_3;
  return ResultT::Ok(result);
}

}  // namespace atlantis::vulkan_backend::detail
