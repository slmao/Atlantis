#pragma once

#include <cstdint>

// Pure, GPU-independent decision for the Vulkan Backend instance's
// requested VkApplicationInfo::apiVersion (ADR-0024 "Accepted Amendment
// -- 2026-08-13", Section 3). decideRequestedInstanceApiVersion() makes
// no Vulkan call -- unit-testable with literal values
// (tests/vulkan_backend/instance_api_version_tests.cpp). Mirrors this
// codebase's existing decideDynamicRenderingPath() extraction pattern.
namespace atlantis::vulkan_backend::detail {

// loaderVersionQueryAvailable: true iff
// vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion") resolved
// to a non-null function pointer -- per the Vulkan specification, a
// NULL result there is itself the documented way to detect a genuine
// Vulkan-1.0-only loader (no such command exists on one). When false,
// loaderVersion is never meaningfully queried by the caller and is
// ignored here.
//
// loaderVersion: the value vkEnumerateInstanceVersion() itself returned,
// only meaningful when loaderVersionQueryAvailable is true.
//
// Returns the exact VkApplicationInfo::apiVersion value the caller
// should request: VK_API_VERSION_1_3 if and only if the loader itself
// reports at least that version; VK_API_VERSION_1_0 otherwise (including
// whenever loaderVersionQueryAvailable is false) -- byte-for-byte the
// same request this repository has shipped since Spec 0003 for every
// loader that does not support 1.3. No other return value is ever
// produced -- this function's whole contract is "1.0 or 1.3, nothing
// else," matching the amendment's own binary framing (never a hard
// error here; see vulkan_instance.cpp for why a loader below 1.3 is not
// an error condition).
[[nodiscard]] std::uint32_t decideRequestedInstanceApiVersion(bool loaderVersionQueryAvailable,
                                                                std::uint32_t loaderVersion);

}  // namespace atlantis::vulkan_backend::detail
