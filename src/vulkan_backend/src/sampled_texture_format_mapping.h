#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/types.h>

// Spec 0038/ADR-0085: SampledTextureFormat -> VkFormat translation, as
// its own pure, GPU-independent private header/source pair (mirroring
// resource_state_mapping.h / hdr_color_target_capability.h's own
// established precedent for unit-testable pure classification functions
// -- vulkan_device.cpp previously carried this switch as a file-local
// static, unreachable from tests).
namespace atlantis::vulkan_backend::detail {

[[nodiscard]] VkFormat toVkFormat(atlantis::rhi::SampledTextureFormat format) noexcept;

}  // namespace atlantis::vulkan_backend::detail
