#pragma once

#include <vulkan/vulkan_core.h>

// Pure, GPU-independent VkFormatFeatureFlags -> bool classifications
// for HDR render targets and sampled textures. These do not touch a
// VkPhysicalDevice or any live Vulkan object; safe to unit-test with
// literal, synthetic VkFormatFeatureFlags values and no real device --
// mirrors resource_state_mapping.h's own identical "private header,
// pure function, unit-testable without Vulkan" shape exactly. Moved
// out of vulkan_device.cpp's own anonymous namespace (where it lived
// as an implementation detail through Milestone 1) specifically so
// tests can reach them.
namespace atlantis::vulkan_backend::detail {

// Plan 0024 Milestone 1 (ADR-0068 D-2): only the two
// optimalTilingFeatures bits this design actually uses are checked --
// VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT (the geometry pass writes the
// HdrColorTarget) and VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT (the output-
// transform pass samples it). Deliberately NOT checked, because not
// used by this design: COLOR_ATTACHMENT_BLEND_BIT (this engine has
// zero alpha-blending capability anywhere, blendEnable = VK_FALSE on
// every Pipeline); SAMPLED_IMAGE_FILTER_LINEAR_BIT (the output-
// transform pass samples at an exact 1:1 texel mapping,
// Filter::Nearest); TRANSFER_SRC_BIT/TRANSFER_DST_BIT (HdrColorTarget
// is never read back or copied to/from directly).
[[nodiscard]] bool hasRequiredHdrColorTargetFeatures(VkFormatFeatureFlags optimalTilingFeatures);

// Sampled environment textures are linearly filtered and populated by
// transfer, so every format must support all three operations.
[[nodiscard]] bool hasRequiredSampledTextureFeatures(VkFormatFeatureFlags optimalTilingFeatures);

}  // namespace atlantis::vulkan_backend::detail
