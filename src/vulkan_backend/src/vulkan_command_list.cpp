#include "vulkan_command_list.h"

#include <atlantis/assert.h>

#include "resource_state_mapping.h"
#include "vulkan_buffer.h"
#include "vulkan_hdr_color_target.h"
#include "vulkan_pipeline.h"
#include "vulkan_render_target.h"
#include "vulkan_render_target_access.h"
#include "vulkan_sampled_texture.h"
#include "vulkan_sampler.h"
#include "vulkan_shadow_map.h"
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
  // Pointer-form, exception-free (Spec 0010/ADR-0038): target may now be
  // a VulkanRenderTarget or a VulkanOffscreenRenderTarget -- a mismatched
  // type here is a programmer error, caught by this assertion, never by
  // a thrown std::bad_cast reaching the render path.
  auto* access = dynamic_cast<VulkanRenderTargetAccess*>(&target);
  ATLANTIS_CHECK_MSG(access != nullptr, "transitionResource() received a RenderTarget not produced by this module");
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
      .image = access->image(),
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

void VulkanCommandList::transitionResource(atlantis::rhi::SampledTexture& target, atlantis::rhi::ResourceState before,
                                            atlantis::rhi::ResourceState after) {
  // Single real implementer -- static_cast, matching VulkanSampledTexture's
  // own header comment (Spec 0016).
  auto& vulkanTexture = static_cast<VulkanSampledTexture&>(target);
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
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, vulkanTexture.mipLevelCount(), 0,
                           vulkanTexture.dimension() == atlantis::rhi::SampledTextureDimension::TextureCube ? 6U
                                                                                                            : 1U},
  };

  vkCmdPipelineBarrier(commandBuffer_, plan.srcStage, plan.dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// Plan 0024 Milestone 2: single real implementer -- static_cast,
// matching transitionResource(SampledTexture&, ...)'s own identical
// precedent immediately above.
void VulkanCommandList::transitionResource(atlantis::rhi::HdrColorTarget& target, atlantis::rhi::ResourceState before,
                                            atlantis::rhi::ResourceState after) {
  auto& vulkanHdrColorTarget = static_cast<VulkanHdrColorTarget&>(target);
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
      .image = vulkanHdrColorTarget.image(),
      .subresourceRange = fullColorResourceRange(),
  };

  vkCmdPipelineBarrier(commandBuffer_, plan.srcStage, plan.dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// Plan 0027 Milestone 2 (ADR-0072 D-4): mirrors transitionResource(Texture&, ...)
// above -- fullDepthResourceRange(), not fullColorResourceRange() (ShadowMap
// is a depth image, like Texture, unlike HdrColorTarget).
void VulkanCommandList::transitionResource(atlantis::rhi::ShadowMap& target, atlantis::rhi::ResourceState before,
                                            atlantis::rhi::ResourceState after) {
  auto& vulkanShadowMap = static_cast<VulkanShadowMap&>(target);
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
      .image = vulkanShadowMap.image(),
      .subresourceRange = fullDepthResourceRange(),
  };

  vkCmdPipelineBarrier(commandBuffer_, plan.srcStage, plan.dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanCommandList::clearColor(atlantis::rhi::RenderTarget& target, atlantis::rhi::ClearColorValue color) {
  // Pointer-form, exception-free -- same rationale as transitionResource()
  // above. Fixed here too even though this spec's own headless path never
  // calls clearColor(), for internal consistency of this already-shared,
  // already-affected class -- leaving this cast unfixed would leave a
  // latent, identical-shape undefined-behavior trap for any future caller
  // that legitimately calls it against an offscreen target.
  auto* access = dynamic_cast<VulkanRenderTargetAccess*>(&target);
  ATLANTIS_CHECK_MSG(access != nullptr, "clearColor() received a RenderTarget not produced by this module");

  // Precondition, enforced by render_graph::execute()'s own algorithm,
  // not re-checked here: target must already be in
  // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL (ResourceState::ColorAttachmentWrite)
  // when this is called -- see resource_state_mapping.h's own note.
  const VkClearColorValue vkClearColor{.float32 = {color.r, color.g, color.b, color.a}};
  const VkImageSubresourceRange range = fullColorResourceRange();

  vkCmdClearColorImage(commandBuffer_, access->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &vkClearColor, 1,
                        &range);
}

void VulkanCommandList::beginRendering(atlantis::rhi::RenderTarget& color, atlantis::rhi::Texture* depth,
                                        atlantis::rhi::ClearColorValue colorClear, float depthClear) {
  // Pointer-form, exception-free -- same rationale as transitionResource()
  // above.
  auto* access = dynamic_cast<VulkanRenderTargetAccess*>(&color);
  ATLANTIS_CHECK_MSG(access != nullptr, "beginRendering() received a RenderTarget not produced by this module");

  VkRenderingAttachmentInfo colorAttachment{};
  colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachment.imageView = access->imageView();
  colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.clearValue.color = VkClearColorValue{.float32 = {colorClear.r, colorClear.g, colorClear.b, colorClear.a}};

  VkRenderingAttachmentInfo depthAttachment{};
  if (depth != nullptr) {
    auto& vulkanDepth = static_cast<VulkanTexture&>(*depth);
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = vulkanDepth.imageView();
    // See resource_state_mapping.cpp's undefinedToDepthAttachmentReadWrite()
    // for why the combined depth/stencil layout is used here rather than
    // the depth-only layout -- this attachment's own transitionResource()
    // call already brought the image to this exact layout, so
    // beginRendering() must name the same one.
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = VkClearDepthStencilValue{depthClear, 0};
  }

  // extent() is part of RenderTarget's own public interface, already
  // polymorphic -- called directly on color, no cast needed.
  const atlantis::rhi::Extent2D extent = color.extent();
  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = VkRect2D{{0, 0}, {extent.width, extent.height}};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachment;
  renderingInfo.pDepthAttachment = depth != nullptr ? &depthAttachment : nullptr;

  ATLANTIS_CHECK(cmdBeginRendering_ != nullptr);
  cmdBeginRendering_(commandBuffer_, &renderingInfo);

  // Pipeline (Section 3/ADR-0025) fixes VK_DYNAMIC_STATE_VIEWPORT/SCISSOR
  // as dynamic state specifically so it survives a resize without
  // recreation -- but that means *something* must actually set them
  // before any draw call each time attachment scope begins, or every
  // vkCmdDraw* call inside it is undefined behavior. beginRendering() is
  // the one place that already knows the target's current extent
  // (renderArea, just above), so it sets both here, once per attachment
  // scope, covering every draw call recorded until the matching
  // endRendering().
  const VkViewport viewport{
      .x = 0.0f,
      .y = 0.0f,
      .width = static_cast<float>(extent.width),
      .height = static_cast<float>(extent.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer_, 0, 1, &renderingInfo.renderArea);
}

// Plan 0024 Milestone 2: a direct copy of beginRendering(RenderTarget&,
// ...) above, with VulkanHdrColorTarget substituted via static_cast
// (single real implementer, no dynamic_cast/VulkanRenderTargetAccess --
// HdrColorTarget is not a RenderTarget).
void VulkanCommandList::beginRendering(atlantis::rhi::HdrColorTarget& color, atlantis::rhi::Texture* depth,
                                        atlantis::rhi::ClearColorValue colorClear, float depthClear) {
  auto& vulkanHdrColorTarget = static_cast<VulkanHdrColorTarget&>(color);

  VkRenderingAttachmentInfo colorAttachment{};
  colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachment.imageView = vulkanHdrColorTarget.imageView();
  colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.clearValue.color = VkClearColorValue{.float32 = {colorClear.r, colorClear.g, colorClear.b, colorClear.a}};

  VkRenderingAttachmentInfo depthAttachment{};
  if (depth != nullptr) {
    auto& vulkanDepth = static_cast<VulkanTexture&>(*depth);
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = vulkanDepth.imageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = VkClearDepthStencilValue{depthClear, 0};
  }

  const atlantis::rhi::Extent2D extent = color.extent();
  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = VkRect2D{{0, 0}, {extent.width, extent.height}};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 1;
  renderingInfo.pColorAttachments = &colorAttachment;
  renderingInfo.pDepthAttachment = depth != nullptr ? &depthAttachment : nullptr;

  ATLANTIS_CHECK(cmdBeginRendering_ != nullptr);
  cmdBeginRendering_(commandBuffer_, &renderingInfo);

  // See beginRendering(RenderTarget&, ...)'s own identical comment
  // above for why this viewport/scissor set is required here.
  const VkViewport viewport{
      .x = 0.0f,
      .y = 0.0f,
      .width = static_cast<float>(extent.width),
      .height = static_cast<float>(extent.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer_, 0, 1, &renderingInfo.renderArea);
}

// Plan 0027 Milestone 2 (ADR-0072 D-2/D-4): a genuinely new depth-only
// scope, not a parameterization of either beginRendering() overload
// above -- both of those unconditionally attach a color image and
// hard-code colorAttachmentCount = 1. Modeled on their own structure
// with the color portion omitted entirely, matching the shadow-casting
// Pipeline's own hasColorAttachment == false shape.
void VulkanCommandList::beginRendering(atlantis::rhi::ShadowMap& depth, float depthClear) {
  auto& vulkanShadowMap = static_cast<VulkanShadowMap&>(depth);

  VkRenderingAttachmentInfo depthAttachment{};
  depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  depthAttachment.imageView = vulkanShadowMap.imageView();
  // See beginRendering(RenderTarget&, ...)'s own identical comment on
  // depthAttachment.imageLayout -- this attachment's own
  // transitionResource() call already brought the image to this exact
  // layout, so this must name the same one.
  depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depthAttachment.clearValue.depthStencil = VkClearDepthStencilValue{depthClear, 0};

  const atlantis::rhi::Extent2D extent = depth.extent();
  VkRenderingInfo renderingInfo{};
  renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderingInfo.renderArea = VkRect2D{{0, 0}, {extent.width, extent.height}};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 0;
  renderingInfo.pColorAttachments = nullptr;
  renderingInfo.pDepthAttachment = &depthAttachment;

  ATLANTIS_CHECK(cmdBeginRendering_ != nullptr);
  cmdBeginRendering_(commandBuffer_, &renderingInfo);

  // See beginRendering(RenderTarget&, ...)'s own identical comment above
  // for why this viewport/scissor set is required here -- sized to the
  // ShadowMap's own extent, independent of the frame's final-target
  // extent (ADR-0072 D-1).
  const VkViewport viewport{
      .x = 0.0f,
      .y = 0.0f,
      .width = static_cast<float>(extent.width),
      .height = static_cast<float>(extent.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer_, 0, 1, &renderingInfo.renderArea);
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
  boundSampledTextureFirstBinding_ = vulkanPipeline.sampledTextureFirstBinding();
  boundSampledTextureBindingCount_ = vulkanPipeline.sampledTextureBindingCount();
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
  const VkBuffer vkBuffer = vulkanBuffer.vkBuffer();

  // See this class's own header comment on lastUpdatedDescriptorSet_/
  // lastUpdatedUniformBuffer_ for why this call is skipped, and only
  // this call, when it would be an exact redundant repeat: Vulkan
  // invalidates a command buffer if a VkDescriptorSet already bound via
  // vkCmdBindDescriptorSets earlier in this same recording is written
  // again via vkUpdateDescriptorSets (no UPDATE_AFTER_BIND, Section 10).
  if (boundDescriptorSet_ != lastUpdatedDescriptorSet_ || vkBuffer != lastUpdatedUniformBuffer_) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = vkBuffer;
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
    lastUpdatedDescriptorSet_ = boundDescriptorSet_;
    lastUpdatedUniformBuffer_ = vkBuffer;
  }

  vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, boundPipelineLayout_, 0, 1,
                           &boundDescriptorSet_, 0, nullptr);
}

void VulkanCommandList::bindTexture(std::uint32_t binding, const atlantis::rhi::SampledTexture& texture,
                                     const atlantis::rhi::Sampler& sampler) {
  ATLANTIS_CHECK(boundDescriptorSet_ != VK_NULL_HANDLE);
  ATLANTIS_CHECK(isSampledTextureBindingInRange(boundSampledTextureFirstBinding_,
                                                boundSampledTextureBindingCount_, binding));
  ATLANTIS_CHECK(binding < textureDescriptorMemos_.size());
  const auto& vulkanTexture = static_cast<const VulkanSampledTexture&>(texture);
  const auto& vulkanSampler = static_cast<const VulkanSampler&>(sampler);

  VkDescriptorImageInfo imageInfo{};
  imageInfo.sampler = vulkanSampler.sampler();
  imageInfo.imageView = vulkanTexture.imageView();
  // Precondition, enforced by render_graph::execute()'s own algorithm, not
  // re-checked here: texture must already be in
  // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL (ResourceState::ShaderRead)
  // when this is called -- see resource_state_mapping.h's own note.
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // Same redundant-write memo as bindUniformBuffer() above, and for the
  // same reason (this class's own header comment on
  // lastUpdatedDescriptorSet_/lastUpdatedUniformBuffer_): a textured
  // Material shared by multiple DrawItems in one frame calls
  // bindTexture() again for every item, each with byte-identical
  // VkDescriptorImageInfo contents, so the redundant vkUpdateDescriptorSets
  // call (and only that call) is skipped when this exact
  // (descriptor set, SampledTexture, Sampler) triple repeats.
  TextureDescriptorMemo& memo = textureDescriptorMemos_[binding];
  if (boundDescriptorSet_ != memo.descriptorSet || &vulkanTexture != memo.texture || &vulkanSampler != memo.sampler) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = boundDescriptorSet_;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    memo.descriptorSet = boundDescriptorSet_;
    memo.texture = &vulkanTexture;
    memo.sampler = &vulkanSampler;
  }

  // Renderer calls this immediately after bindUniformBuffer() (Spec
  // 0016/D3), which has already re-issued vkCmdBindDescriptorSets() for
  // this same set earlier in this draw item -- re-issuing it again here
  // is what actually makes this write visible to the upcoming draw call;
  // it is not merely redundant with bindUniformBuffer()'s own call, since
  // that earlier call could not have known about this write yet.
  vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, boundPipelineLayout_, 0, 1,
                           &boundDescriptorSet_, 0, nullptr);
}

// Plan 0024 Milestone 2 (ADR-0068 D-10): used only by the output-
// transform pass, which draws the fullscreen triangle exactly once per
// frame -- unlike bindTexture(SampledTexture&, ...) above (called
// repeatedly for multiple DrawItems sharing one Material in the same
// recording), there is no repeated-call redundancy to memoize here, so
// this overload always issues the descriptor write unconditionally.
// dstBinding = 0, not 1 -- the output-transform descriptor contract's
// own sole binding (no uniform buffer at binding 0, unlike every
// MaterialKind contract), matching outputTransformExpectedDescriptorContract().
void VulkanCommandList::bindTexture(std::uint32_t binding, const atlantis::rhi::HdrColorTarget& texture,
                                     const atlantis::rhi::Sampler& sampler) {
  ATLANTIS_CHECK(boundDescriptorSet_ != VK_NULL_HANDLE);
  ATLANTIS_CHECK(isSampledTextureBindingInRange(boundSampledTextureFirstBinding_,
                                                boundSampledTextureBindingCount_, binding));
  const auto& vulkanHdrColorTarget = static_cast<const VulkanHdrColorTarget&>(texture);
  const auto& vulkanSampler = static_cast<const VulkanSampler&>(sampler);

  VkDescriptorImageInfo imageInfo{};
  imageInfo.sampler = vulkanSampler.sampler();
  imageInfo.imageView = vulkanHdrColorTarget.imageView();
  // Precondition, enforced by render_graph::execute()'s own algorithm,
  // not re-checked here: texture must already be in
  // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL (ResourceState::ShaderRead)
  // when this is called.
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = boundDescriptorSet_;
  write.dstBinding = binding;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &imageInfo;

  vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
  vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, boundPipelineLayout_, 0, 1,
                           &boundDescriptorSet_, 0, nullptr);
}

// Plan 0027 Milestone 2 (ADR-0072 D-4/D-7): memoized, like
// bindTexture(SampledTexture&, ...) above -- not like
// bindTexture(HdrColorTarget&, ...) just above, which is unconditional
// (drawn once per frame). The shadow-map binding is re-issued once per
// PbrDirectLit/pbr_ibl DrawItem, the same repeated-per-draw pattern the
// base-color sampler already has.
void VulkanCommandList::bindTexture(std::uint32_t binding, const atlantis::rhi::ShadowMap& texture,
                                     const atlantis::rhi::Sampler& sampler) {
  ATLANTIS_CHECK(boundDescriptorSet_ != VK_NULL_HANDLE);
  ATLANTIS_CHECK(isSampledTextureBindingInRange(boundSampledTextureFirstBinding_,
                                                boundSampledTextureBindingCount_, binding));
  ATLANTIS_CHECK(binding < textureDescriptorMemos_.size());
  const auto& vulkanShadowMap = static_cast<const VulkanShadowMap&>(texture);
  const auto& vulkanSampler = static_cast<const VulkanSampler&>(sampler);

  VkDescriptorImageInfo imageInfo{};
  imageInfo.sampler = vulkanSampler.sampler();
  imageInfo.imageView = vulkanShadowMap.imageView();
  // Precondition, enforced by render_graph::execute()'s own algorithm,
  // not re-checked here: texture must already be in
  // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL (ResourceState::ShaderRead)
  // when this is called.
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // Same redundant-write memo as bindTexture(SampledTexture&, ...)
  // above, and for the same reason -- a PbrDirectLit/pbr_ibl Material
  // shared by multiple DrawItems in one frame calls this again for
  // every item, each with byte-identical VkDescriptorImageInfo contents
  // (the one shared ShadowMap/Sampler pair for the whole frame).
  TextureDescriptorMemo& memo = textureDescriptorMemos_[binding];
  if (boundDescriptorSet_ != memo.descriptorSet || &vulkanShadowMap != memo.shadowMap ||
      &vulkanSampler != memo.sampler) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = boundDescriptorSet_;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    memo.descriptorSet = boundDescriptorSet_;
    memo.shadowMap = &vulkanShadowMap;
    memo.sampler = &vulkanSampler;
  }

  // See bindTexture(SampledTexture&, ...)'s own identical comment above
  // for why this re-issue is required, not merely redundant with
  // bindUniformBuffer()'s own earlier call.
  vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, boundPipelineLayout_, 0, 1,
                           &boundDescriptorSet_, 0, nullptr);
}

void VulkanCommandList::pushConstant(const void* data, std::size_t sizeBytes) {
  ATLANTIS_CHECK(boundPipelineLayout_ != VK_NULL_HANDLE);
  // Plan 0023 Milestone 3 (ADR-0067 D-4): matches this Pipeline's own
  // widened VkPushConstantRange::stageFlags (vulkan_device.cpp) --
  // VERTEX | FRAGMENT unconditionally, for every Pipeline. Vulkan
  // requires this call's stageFlags to exactly match the range it
  // targets.
  vkCmdPushConstants(commandBuffer_, boundPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, static_cast<std::uint32_t>(sizeBytes), data);
}

void VulkanCommandList::drawIndexed(std::uint32_t indexCount) {
  vkCmdDrawIndexed(commandBuffer_, indexCount, 1, 0, 0, 0);
}

void VulkanCommandList::copyRenderTargetToBuffer(atlantis::rhi::RenderTarget& source,
                                                  atlantis::rhi::Buffer& destination) {
  // Pointer-form, exception-free -- same rationale as transitionResource()
  // above.
  auto* access = dynamic_cast<VulkanRenderTargetAccess*>(&source);
  ATLANTIS_CHECK_MSG(access != nullptr,
                      "copyRenderTargetToBuffer() received a RenderTarget not produced by this module");
  // Only one concrete Buffer implementation exists in Phase 1 (ADR-0001) --
  // this plan does not add a second one, so no dynamic_cast is needed here,
  // unlike source above.
  auto& vulkanBuffer = static_cast<VulkanBuffer&>(destination);
  const atlantis::rhi::Extent2D sourceExtent = source.extent();

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;    // 0 = tightly packed (ADR-0040)
  region.bufferImageHeight = 0;  // 0 = tightly packed (ADR-0040)
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {sourceExtent.width, sourceExtent.height, 1};
  // No additional vkCmdPipelineBarrier for host-visibility is added here --
  // destination's memory is host-coherent (ADR-0023), and
  // Device::waitIdle()'s existing vkDeviceWaitIdle() call already
  // establishes the device-to-host visibility guarantee the Vulkan
  // specification's host-write-ordering rules provide for a fully-drained
  // device, matching this codebase's existing precedent (the camera
  // uniform Buffer's own write-timing argument, ADR-0023).
  vkCmdCopyImageToBuffer(commandBuffer_, access->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          vulkanBuffer.vkBuffer(), 1, &region);
}

void VulkanCommandList::copyBufferToTexture(atlantis::rhi::Buffer& source, atlantis::rhi::SampledTexture& destination) {
  const atlantis::rhi::SampledTextureUploadRegion region{.extent = destination.extent()};
  copyBufferToTexture(source, destination, std::span<const atlantis::rhi::SampledTextureUploadRegion>(&region, 1));
}

void VulkanCommandList::copyBufferToTexture(
    atlantis::rhi::Buffer& source, atlantis::rhi::SampledTexture& destination,
    std::span<const atlantis::rhi::SampledTextureUploadRegion> regions) {
  ATLANTIS_CHECK(source.purpose() == atlantis::rhi::BufferPurpose::Staging);
  // Only one concrete Buffer implementation exists in Phase 1 (ADR-0001) --
  // no dynamic_cast needed, matching copyRenderTargetToBuffer()'s own note
  // on destination there.
  auto& vulkanBuffer = static_cast<VulkanBuffer&>(source);
  auto& vulkanTexture = static_cast<VulkanSampledTexture&>(destination);
  ATLANTIS_CHECK(!regions.empty());
  std::vector<VkBufferImageCopy> vkRegions;
  vkRegions.reserve(regions.size());
  for (const atlantis::rhi::SampledTextureUploadRegion& sourceRegion : regions) {
    ATLANTIS_CHECK(isValidSampledTextureUploadRegion(destination.extent(), destination.format(),
                                                     destination.dimension(), destination.mipLevelCount(),
                                                     source.sizeBytes(), sourceRegion));
    VkBufferImageCopy region{};
    region.bufferOffset = sourceRegion.bufferOffsetBytes;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, sourceRegion.mipLevel, sourceRegion.arrayLayer, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {sourceRegion.extent.width, sourceRegion.extent.height, 1};
    vkRegions.push_back(region);
  }
  // Precondition, enforced by buildTextureUploadPass() (Spec 0016 D4), not
  // re-checked here: destination must already be in
  // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL (ResourceState::TransferDestination)
  // when this is called.
  vkCmdCopyBufferToImage(commandBuffer_, vulkanBuffer.vkBuffer(), vulkanTexture.image(),
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<std::uint32_t>(vkRegions.size()),
                          vkRegions.data());
}

}  // namespace atlantis::vulkan_backend::detail
