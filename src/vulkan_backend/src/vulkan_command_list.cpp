#include "vulkan_command_list.h"

#include <atlantis/assert.h>

#include "resource_state_mapping.h"
#include "vulkan_buffer.h"
#include "vulkan_pipeline.h"
#include "vulkan_render_target.h"
#include "vulkan_texture.h"

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

[[nodiscard]] VkImageSubresourceRange fullDepthResourceRange() {
  return VkImageSubresourceRange{
      .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
  };
}

}  // namespace

VulkanCommandList::VulkanCommandList(VkDevice device, VkCommandPool commandPool, VkCommandBuffer commandBuffer,
                                      PFN_vkCmdBeginRenderingKHR cmdBeginRendering,
                                      PFN_vkCmdEndRenderingKHR cmdEndRendering)
    : device_(device),
      commandPool_(commandPool),
      commandBuffer_(commandBuffer),
      cmdBeginRendering_(cmdBeginRendering),
      cmdEndRendering_(cmdEndRendering) {}

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

void VulkanCommandList::transitionResource(atlantis::rhi::Texture& target, atlantis::rhi::ResourceState before,
                                            atlantis::rhi::ResourceState after) {
  auto& vulkanTexture = static_cast<VulkanTexture&>(target);
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
      .image = vulkanTexture.image(),
      .subresourceRange = fullDepthResourceRange(),
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

void VulkanCommandList::beginRendering(atlantis::rhi::RenderTarget& color, atlantis::rhi::Texture* depth,
                                        atlantis::rhi::ClearColorValue colorClear, float depthClear) {
  auto& vulkanTarget = static_cast<VulkanRenderTarget&>(color);

  VkRenderingAttachmentInfo colorAttachment{};
  colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachment.imageView = vulkanTarget.imageView();
  colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.clearValue.color = VkClearColorValue{.float32 = {colorClear.r, colorClear.g, colorClear.b, colorClear.a}};

  VkRenderingAttachmentInfo depthAttachment{};
  if (depth != nullptr) {
    auto& vulkanDepth = static_cast<VulkanTexture&>(*depth);
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = vulkanDepth.imageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = VkClearDepthStencilValue{depthClear, 0};
  }

  const atlantis::rhi::Extent2D extent = vulkanTarget.extent();
  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = VkRect2D{{0, 0}, {extent.width, extent.height}};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachment;
  renderingInfo.pDepthAttachment = depth != nullptr ? &depthAttachment : nullptr;

  ATLANTIS_CHECK(cmdBeginRendering_ != nullptr);
  cmdBeginRendering_(commandBuffer_, &renderingInfo);
}

void VulkanCommandList::endRendering() {
  ATLANTIS_CHECK(cmdEndRendering_ != nullptr);
  cmdEndRendering_(commandBuffer_);
}

void VulkanCommandList::bindPipeline(atlantis::rhi::Pipeline& pipeline) {
  auto& vulkanPipeline = static_cast<VulkanPipeline&>(pipeline);
  vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline.vkPipeline());
  boundPipelineLayout_ = vulkanPipeline.pipelineLayout();
  boundDescriptorSet_ = vulkanPipeline.descriptorSet();
}

void VulkanCommandList::bindVertexBuffer(atlantis::rhi::Buffer& buffer) {
  ATLANTIS_CHECK(buffer.purpose() == atlantis::rhi::BufferPurpose::Vertex);
  auto& vulkanBuffer = static_cast<VulkanBuffer&>(buffer);
  const VkBuffer vkBuffer = vulkanBuffer.vkBuffer();
  const VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &vkBuffer, &offset);
}

void VulkanCommandList::bindIndexBuffer(atlantis::rhi::Buffer& buffer) {
  ATLANTIS_CHECK(buffer.purpose() == atlantis::rhi::BufferPurpose::Index);
  auto& vulkanBuffer = static_cast<VulkanBuffer&>(buffer);
  vkCmdBindIndexBuffer(commandBuffer_, vulkanBuffer.vkBuffer(), 0, VK_INDEX_TYPE_UINT16);
}

void VulkanCommandList::bindUniformBuffer(atlantis::rhi::Buffer& buffer) {
  ATLANTIS_CHECK(buffer.purpose() == atlantis::rhi::BufferPurpose::Uniform);
  ATLANTIS_CHECK(boundDescriptorSet_ != VK_NULL_HANDLE);
  auto& vulkanBuffer = static_cast<VulkanBuffer&>(buffer);

  VkDescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = vulkanBuffer.vkBuffer();
  bufferInfo.offset = 0;
  bufferInfo.range = VK_WHOLE_SIZE;

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = boundDescriptorSet_;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  write.pBufferInfo = &bufferInfo;

  // vkUpdateDescriptorSets returns void -- no VkResult exists for this
  // call (Plan 0007 Section 10).
  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

  vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, boundPipelineLayout_, 0, 1,
                           &boundDescriptorSet_, 0, nullptr);
}

void VulkanCommandList::pushConstant(const void* data, std::size_t sizeBytes) {
  ATLANTIS_CHECK(boundPipelineLayout_ != VK_NULL_HANDLE);
  vkCmdPushConstants(commandBuffer_, boundPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                      static_cast<std::uint32_t>(sizeBytes), data);
}

void VulkanCommandList::drawIndexed(std::uint32_t indexCount) {
  vkCmdDrawIndexed(commandBuffer_, indexCount, 1, 0, 0, 0);
}

}  // namespace atlantis::vulkan_backend::detail
