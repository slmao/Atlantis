#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/types.h>

// Pure, GPU-independent ResourceState -> Vulkan barrier-plan mapping. Does
// not touch a VkCommandBuffer or any live Vulkan object; safe to
// unit-test with literal ResourceState enumerators and no real device.
namespace atlantis::vulkan_backend::detail {

struct ImageBarrierPlan {
  VkImageLayout oldLayout;
  VkImageLayout newLayout;
  VkAccessFlags srcAccessMask;
  VkAccessFlags dstAccessMask;
  VkPipelineStageFlags srcStage;
  VkPipelineStageFlags dstStage;
};

// before must not equal after -- render_graph::execute() never calls
// transitionResource() for a no-op state pair, so this function does not
// need to handle one. Deliberate note, not an oversight: ColorAttachmentWrite
// maps to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, not COLOR_ATTACHMENT_OPTIMAL
// -- clearColor() records vkCmdClearColorImage, which requires GENERAL or
// TRANSFER_DST_OPTIMAL, not the render-pass-only COLOR_ATTACHMENT_OPTIMAL
// layout. The RHI-level enumerator name describes intent ("this pass
// writes the color target"); its concrete Vulkan layout is this round's
// own implementation choice, tied to the one write operation (clearColor)
// that exists. ATLANTIS_CHECK_MSG guards any (before, after) combination
// this round does not define a mapping for -- every combination
// execute()'s own algorithm can ever generate is covered (Plan 0006
// Section 8).
[[nodiscard]] ImageBarrierPlan planTransition(atlantis::rhi::ResourceState before, atlantis::rhi::ResourceState after);

}  // namespace atlantis::vulkan_backend::detail
