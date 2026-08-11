#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/command_list.h>

// Concrete Vulkan implementation of atlantis::rhi::CommandList
// (ADR-0020). See vulkan_device.cpp for where this is constructed
// (VulkanDevice::createCommandList()).
namespace atlantis::vulkan_backend::detail {

// Owns its VkCommandBuffer's allocation from device/commandPool (frees it
// in its destructor via vkFreeCommandBuffers) but does not own device or
// commandPool themselves -- both must outlive this object (caller-
// enforced; VulkanDevice's own single-retained-submission state machine
// is what guarantees this in practice, see VulkanDevice's header
// comment). transitionResource()/clearColor() static_cast the
// rhi::RenderTarget& argument to VulkanRenderTarget& -- safe because
// only Vulkan Backend ever constructs one in Phase 1 (ADR-0001's single-
// backend constraint). Caller-owned only while being recorded into;
// ownership transfers to VulkanDevice at submit() -- a caller never
// destroys one it has submitted. Not copyable, not movable, not
// thread-safe.
class VulkanCommandList final : public atlantis::rhi::CommandList {
 public:
  // cmdBeginRendering/cmdEndRendering: VulkanDevice's own resolved
  // dynamic-rendering entry points (Section 8/10 -- whichever of the
  // Core/Extension path that Device selected), borrowed for this
  // CommandList's whole lifetime; VulkanDevice must outlive it (same
  // caller-enforced tier as device/commandPool below).
  VulkanCommandList(VkDevice device, VkCommandPool commandPool, VkCommandBuffer commandBuffer,
                     PFN_vkCmdBeginRenderingKHR cmdBeginRendering, PFN_vkCmdEndRenderingKHR cmdEndRendering);
  ~VulkanCommandList() override;

  VulkanCommandList(const VulkanCommandList&) = delete;
  VulkanCommandList& operator=(const VulkanCommandList&) = delete;
  VulkanCommandList(VulkanCommandList&&) = delete;
  VulkanCommandList& operator=(VulkanCommandList&&) = delete;

  void transitionResource(atlantis::rhi::RenderTarget& target, atlantis::rhi::ResourceState before,
                           atlantis::rhi::ResourceState after) override;
  void clearColor(atlantis::rhi::RenderTarget& target, atlantis::rhi::ClearColorValue color) override;

  void beginRendering(atlantis::rhi::RenderTarget& color, atlantis::rhi::Texture* depth,
                       atlantis::rhi::ClearColorValue colorClear, float depthClear) override;
  void endRendering() override;

  void bindPipeline(atlantis::rhi::Pipeline& pipeline) override;
  void bindVertexBuffer(atlantis::rhi::Buffer& buffer) override;
  void bindIndexBuffer(atlantis::rhi::Buffer& buffer) override;
  void bindUniformBuffer(atlantis::rhi::Buffer& buffer) override;
  void pushConstant(const void* data, std::size_t sizeBytes) override;
  void drawIndexed(std::uint32_t indexCount) override;

  // Exists solely for VulkanDevice::submit() (vkEndCommandBuffer,
  // vkQueueSubmit) -- never reached from RHI's public surface.
  [[nodiscard]] VkCommandBuffer commandBuffer() const noexcept { return commandBuffer_; }

 private:
  VkDevice device_;
  VkCommandPool commandPool_;
  VkCommandBuffer commandBuffer_;
  PFN_vkCmdBeginRenderingKHR cmdBeginRendering_;
  PFN_vkCmdEndRenderingKHR cmdEndRendering_;

  // Set by bindPipeline(), read by bindUniformBuffer()/pushConstant() --
  // the currently-bound Pipeline's own VkPipelineLayout/VkDescriptorSet
  // (Section 10). Non-owning; null until the first bindPipeline() call in
  // this recording. A programmer error (ATLANTIS_CHECK) to call
  // bindUniformBuffer()/pushConstant() before any bindPipeline().
  VkPipelineLayout boundPipelineLayout_ = VK_NULL_HANDLE;
  VkDescriptorSet boundDescriptorSet_ = VK_NULL_HANDLE;
};

}  // namespace atlantis::vulkan_backend::detail
