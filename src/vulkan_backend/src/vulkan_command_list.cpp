#include "vulkan_command_list.h"

#include "resource_state_mapping.h"
#include "vulkan_render_target.h"

namespace atlantis::vulkan_backend::detail {

namespace {

[[nodiscard]] VkImageSubresourceRange fullColorResourceRange() {
  return VkImageSubresourceRange{
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
  };
}

}  // namespace

VulkanCommandList::VulkanCommandList(VkDevice device, VkCommandPool commandPool, VkCommandBuffer commandBuffer)
    : device_(device), commandPool_(commandPool), commandBuffer_(commandBuffer) {}

VulkanCommandList::~VulkanCommandList() { vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer_); }

void VulkanCommandList::transitionResource(atlantis::rhi::RenderTarget& target, atlantis::rhi::ResourceState before,
                                            atlantis::rhi::ResourceState after) {
  auto& vulkanTarget = static_cast<VulkanRenderTarget&>(target);
  const ImageBarrierPlan plan = planTransition(before, after);

  const VkImageMemoryBarrier barrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = nullptr,
      .srcAccessMask = plan.srcAccessMask,
      .dstAccessMask = plan.dstAccessMask,
      .oldLayout = plan.oldLayout,
      .newLayout = plan.newLayout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = vulkanTarget.image(),
      .subresourceRange = fullColorResourceRange(),
  };

  vkCmdPipelineBarrier(commandBuffer_, plan.srcStage, plan.dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanCommandList::clearColor(atlantis::rhi::RenderTarget& target, atlantis::rhi::ClearColorValue color) {
  auto& vulkanTarget = static_cast<VulkanRenderTarget&>(target);

  // Precondition, enforced by render_graph::execute()'s own algorithm,
  // not re-checked here: vulkanTarget must already be in
  // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL (ResourceState::ColorAttachmentWrite)
  // when this is called -- see resource_state_mapping.h's own note.
  const VkClearColorValue vkClearColor{.float32 = {color.r, color.g, color.b, color.a}};
  const VkImageSubresourceRange range = fullColorResourceRange();

  vkCmdClearColorImage(commandBuffer_, vulkanTarget.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &vkClearColor, 1,
                        &range);
}

}  // namespace atlantis::vulkan_backend::detail
