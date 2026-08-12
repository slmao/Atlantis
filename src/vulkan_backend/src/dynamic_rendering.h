#pragma once

// Pure, GPU-independent dynamic-rendering capability decision (Spec 0007
// / ADR-0024). decideDynamicRenderingPath() makes no Vulkan call --
// unit-testable with literal booleans (tests/vulkan_backend/
// dynamic_rendering_tests.cpp). Mirrors this codebase's existing
// decideRecreateAction()/decideAcquireAction() extraction pattern.
namespace atlantis::vulkan_backend::detail {

enum class DynamicRenderingPath { Core, Extension, Unavailable };

// Two distinct layers -- do not conflate them. (1)
// physicalDeviceProperties2InstanceExtensionAvailable is a single,
// instance-wide query-mechanism fact (whether
// VK_KHR_get_physical_device_properties2 was enabled at instance
// creation, and vkGetPhysicalDeviceFeatures2KHR successfully resolved),
// computed once, never per-candidate-device. If false, none of the four
// arguments below could have been meaningfully queried for any physical
// device, and this function returns Unavailable unconditionally,
// regardless of their values. (2) The remaining four arguments are the
// actual per-candidate capability being detected, using the query
// mechanism (1) makes available.
[[nodiscard]] DynamicRenderingPath decideDynamicRenderingPath(
    bool physicalDeviceProperties2InstanceExtensionAvailable, bool apiVersionAtLeast1_3, bool coreFeatureSupported,
    bool extensionAdvertised, bool extensionFeatureSupported);

}  // namespace atlantis::vulkan_backend::detail
