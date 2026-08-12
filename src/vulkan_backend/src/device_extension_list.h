#pragma once

#include <string>
#include <vector>

#include "dynamic_rendering.h"

// Pure, GPU-independent construction of the device-extension enable list
// for a resolved dynamic-rendering path (ADR-0024 "Accepted Amendment --
// 2026-08-13", Section 3, points 5-6). buildDeviceExtensionList() makes
// no Vulkan call -- unit-testable with literal DynamicRenderingPath
// values (tests/vulkan_backend/device_extension_list_tests.cpp). Mirrors
// this codebase's existing decideDynamicRenderingPath() extraction
// pattern.
namespace atlantis::vulkan_backend::detail {

// path == Core: excludes VK_KHR_dynamic_rendering and its whole
// promoted-extension dependency chain entirely -- a Core-selected
// physical device's enabled device-extension list contains only
// requiredExtensions, unmodified.
//
// path == Extension: appends VK_KHR_dynamic_rendering's full
// prerequisite chain, per the Vulkan specification's own extension-
// dependency data (VK_KHR_dynamic_rendering -> VK_KHR_depth_stencil_resolve
// -> VK_KHR_create_renderpass2 -> VK_KHR_multiview, VK_KHR_maintenance2),
// in dependency order (lowest-level prerequisite first, the extension
// itself last), after requiredExtensions.
//
// path == Unavailable: never a valid input -- createDevice() never
// attempts vkCreateDevice() (and so never needs an extension list) for a
// candidate whose resolved path is Unavailable; calling this with
// Unavailable is a programmer error (ATLANTIS_CHECK in the .cpp).
//
// requiredExtensions: this repository's own pre-existing, unrelated
// required device extensions (currently just VK_KHR_swapchain), listed
// first, in the order given, unaffected by path.
//
// Deterministic, stable order (requiredExtensions, then -- Extension
// path only -- the fixed dependency-ordered dynamic-rendering chain);
// never contains a duplicate name.
[[nodiscard]] std::vector<std::string> buildDeviceExtensionList(DynamicRenderingPath path,
                                                                  const std::vector<std::string>& requiredExtensions);

}  // namespace atlantis::vulkan_backend::detail
