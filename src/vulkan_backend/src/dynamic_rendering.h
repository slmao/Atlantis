#pragma once

// Pure, GPU-independent dynamic-rendering capability decision (Spec 0007
// / ADR-0024). decideDynamicRenderingPath() makes no Vulkan call --
// unit-testable with literal booleans (tests/vulkan_backend/
// dynamic_rendering_tests.cpp). Mirrors this codebase's existing
// decideRecreateAction()/decideAcquireAction() extraction pattern.
namespace atlantis::vulkan_backend::detail {

enum class DynamicRenderingPath { Core, Extension, Unavailable };

// Two distinct layers -- do not conflate them. (1)
// physicalDeviceProperties2InstanceExtensionAvailable and
// instanceRequestedApiVersionAtLeast1_3 are both single, instance-wide
// query-mechanism/prerequisite facts -- computed once, never
// per-candidate-device. If physicalDeviceProperties2InstanceExtensionAvailable
// is false, none of the four remaining arguments below could have been
// meaningfully queried for any physical device, and this function
// returns Unavailable unconditionally, regardless of their values. (2)
// The remaining four arguments are the actual per-candidate capability
// being detected, using the query mechanism (1) makes available.
//
// instanceRequestedApiVersionAtLeast1_3 (ADR-0024 "Accepted Amendment --
// 2026-08-13", Section 3, point 4): true iff the Vulkan Backend's
// instance actually requested VkApplicationInfo::apiVersion >=
// VK_API_VERSION_1_3 (see instance_api_version.h) -- gates the Core
// branch specifically because a core, unsuffixed dynamic-rendering entry
// point is only reliably resolvable when the owning instance itself
// requested that version; a physical device individually reporting
// apiVersion >= 1.3 against an instance that requested less is not
// misclassified as Core-capable by this function. Does not gate the
// Extension branch at all -- KHR-suffixed entry points are resolved via
// vkGetDeviceProcAddr regardless of the instance's requested apiVersion.
[[nodiscard]] DynamicRenderingPath decideDynamicRenderingPath(
    bool physicalDeviceProperties2InstanceExtensionAvailable, bool instanceRequestedApiVersionAtLeast1_3,
    bool apiVersionAtLeast1_3, bool coreFeatureSupported, bool extensionAdvertised, bool extensionFeatureSupported);

}  // namespace atlantis::vulkan_backend::detail
