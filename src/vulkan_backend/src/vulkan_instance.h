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
