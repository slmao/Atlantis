#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/result.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

// Private VkInstance creation. Nothing here creates a VkPhysicalDevice,
// VkDevice, VkSurfaceKHR, or VkSwapchainKHR -- see vulkan_device.h and
// wsi/win32_surface.h for those.
namespace atlantis::vulkan_backend::detail {

// Builds the VkDebugUtilsMessengerCreateInfoEXT this module uses at both
// installation sites (see plans/0003-rhi-vulkan-windowed-foundation.md
// Section 6): the VkInstanceCreateInfo::pNext chain createInstance()
// builds below, and the separate, explicit VkDebugUtilsMessengerEXT
// VulkanDevice installs immediately after instance creation succeeds.
// Built in exactly one place so the two installations cannot drift apart.
// Severity is WARNING+ERROR only; message types are
// general+validation+performance. pUserData is always nullptr --
// detail::debugMessengerCallback() needs no per-instance state.
[[nodiscard]] VkDebugUtilsMessengerCreateInfoEXT makeDebugMessengerCreateInfo() noexcept;

struct InstanceCreateResult {
  VkInstance instance = VK_NULL_HANDLE;

  // Spec 0007 / ADR-0024 Section 8's instance-level prerequisite for the
  // dynamic-rendering capability query: true iff
  // VK_KHR_get_physical_device_properties2 was present in
  // vkEnumerateInstanceExtensionProperties(), was successfully requested
  // in VkInstanceCreateInfo, AND vkGetPhysicalDeviceFeatures2KHR resolved
  // to a non-null function pointer via vkGetInstanceProcAddr() immediately
  // afterward. An instance-wide fact, computed exactly once here, never
  // re-queried per physical-device candidate.
  bool physicalDeviceProperties2ExtensionAvailable = false;

  // Non-null only when physicalDeviceProperties2ExtensionAvailable is
  // true -- never called if this resolution step was skipped or returned
  // nullptr (Section 8's "never assumed to be directly linkable"
  // discipline, mirroring the device-level vkCmdBeginRenderingKHR/
  // vkCmdEndRenderingKHR resolution).
  PFN_vkGetPhysicalDeviceFeatures2KHR getPhysicalDeviceFeatures2KHR = nullptr;

  // ADR-0024 "Accepted Amendment -- 2026-08-13", Section 3: true iff this
  // instance actually requested VkApplicationInfo::apiVersion >=
  // VK_API_VERSION_1_3 (queried via the loader-version check in
  // createInstance() below, before vkCreateInstance()). An instance-wide
  // fact, computed exactly once here, passed unchanged into
  // decideDynamicRenderingPath() (dynamic_rendering.h) for every
  // physical-device candidate -- never re-derived per candidate.
  bool instanceRequestedApiVersionAtLeast1_3 = false;
};

// Creates a VkInstance for params.applicationName, requesting
// VK_KHR_surface and VK_KHR_win32_surface unconditionally, plus --
// only when validationEnabled is true -- VK_EXT_debug_utils and the
// VK_LAYER_KHRONOS_validation layer, with the pNext-chained debug
// messenger (via makeDebugMessengerCreateInfo() above) so
// vkCreateInstance/vkDestroyInstance themselves are validation-covered.
// No headless, Android, portability/MoltenVK, or other backend
// extension is ever requested -- Windows-only, Phase 1 scope.
//
// ADR-0024 "Accepted Amendment -- 2026-08-13", Section 3: before ever
// calling vkCreateInstance(), queries the Vulkan loader's own maximum
// supported version (vkEnumerateInstanceVersion, resolved via
// vkGetInstanceProcAddr(nullptr, ...) -- a global command, callable
// without an instance) and requests
// VkApplicationInfo::apiVersion = VK_API_VERSION_1_3 if and only if the
// loader itself reports at least that version; otherwise requests
// VK_API_VERSION_1_0, unchanged from this repository's original,
// shipped behavior. A loader reporting below 1.3 (including a genuine
// Vulkan-1.0-only loader with no vkEnumerateInstanceVersion at all) is
// not an error condition and never fails this function by itself -- see
// instance_api_version.h's decideRequestedInstanceApiVersion() for the
// pure decision logic this step delegates to.
//
// validationEnabled must already be the caller's single, already-computed
// effectiveValidationLayersEnabled(IsDebugBuild, params.enableValidationLayers)
// result (validation.h) -- this function does not compute, re-derive, or
// read params.enableValidationLayers itself.
//
// Does not install the explicit VkDebugUtilsMessengerEXT -- that is
// VulkanDevice's own responsibility (Section 6), covering every Vulkan
// call made after vkCreateInstance returns.
//
// On Err(DeviceCreateError::ValidationLayerUnavailable): validationEnabled
// was true and VK_LAYER_KHRONOS_validation is not present on this
// machine -- does not silently continue without validation.
// On Err(DeviceCreateError::InstanceCreationFailed): any other
// instance-creation precondition (a required extension missing) or a
// failing vkCreateInstance VkResult. No VkInstance is ever returned on
// Err -- nothing is leaked and nothing partially-constructed escapes.
[[nodiscard]] atlantis::Result<InstanceCreateResult, DeviceCreateError> createInstance(
    const DeviceCreateParams& params, bool validationEnabled);

}  // namespace atlantis::vulkan_backend::detail
