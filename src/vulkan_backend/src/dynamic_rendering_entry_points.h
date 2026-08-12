#pragma once

#include <optional>

#include "dynamic_rendering.h"

// Pure, GPU-independent selection of which vkGetDeviceProcAddr name pair
// to resolve for a resolved dynamic-rendering path (ADR-0024 "Accepted
// Amendment -- 2026-08-13", Section 3, points 5-6).
// selectDynamicRenderingEntryPointNames() makes no Vulkan call --
// unit-testable with literal DynamicRenderingPath values
// (tests/vulkan_backend/dynamic_rendering_entry_points_tests.cpp).
// Mirrors this codebase's existing decideDynamicRenderingPath()
// extraction pattern.
namespace atlantis::vulkan_backend::detail {

struct DynamicRenderingEntryPointNames {
  const char* beginRenderingName;
  const char* endRenderingName;
};

// path == Core: the unsuffixed core names ("vkCmdBeginRendering"/
// "vkCmdEndRendering") -- reliably resolvable via vkGetDeviceProcAddr
// only because the owning instance requested apiVersion >= 1.3 (see
// instance_api_version.h; this function itself does not know or care
// why, it only names which pair the Core path always wants).
//
// path == Extension: the KHR-suffixed names ("vkCmdBeginRenderingKHR"/
// "vkCmdEndRenderingKHR").
//
// path == Unavailable: std::nullopt -- no entry point is ever
// meaningfully resolved for this path; createDevice() never reaches
// entry-point resolution for a candidate whose resolved path is
// Unavailable.
[[nodiscard]] std::optional<DynamicRenderingEntryPointNames> selectDynamicRenderingEntryPointNames(
    DynamicRenderingPath path);

}  // namespace atlantis::vulkan_backend::detail
