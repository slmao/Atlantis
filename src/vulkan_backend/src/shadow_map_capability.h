#pragma once

#include <vulkan/vulkan_core.h>

// Pure, GPU-independent VkFormatFeatureFlags -> bool classification for
// the ShadowMap resource, mirroring hdr_color_target_capability.h's own
// identical shape -- unit-testable with literal, synthetic
// VkFormatFeatureFlags values and no real device.
namespace atlantis::vulkan_backend::detail {

// Plan 0027 Milestone 1 (ADR-0072 D-1): both bits this design actually
// uses -- VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT (the shadow-
// casting pass writes it) and VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT (the
// main draw pass samples it). This check is a genuine correctness
// requirement, not defensive redundancy: D32_SFLOAT's own
// SAMPLED_IMAGE_BIT support is unconditionally guaranteed by the Vulkan
// spec, but DEPTH_STENCIL_ATTACHMENT_BIT is guaranteed only
// collectively across {D32_SFLOAT, X8_D24_UNORM_PACK32} -- this exact
// combination on D32_SFLOAT specifically is not spec-guaranteed on
// every conformant device.
[[nodiscard]] bool hasRequiredShadowMapFeatures(VkFormatFeatureFlags optimalTilingFeatures);

}  // namespace atlantis::vulkan_backend::detail
