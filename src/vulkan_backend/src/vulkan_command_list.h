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
  VulkanCommandList(VkDevice device, VkCommandPool commandPool, VkCommandBuffer commandBuffer);
  ~VulkanCommandList() override;

  VulkanCommandList(const VulkanCommandList&) = delete;
  VulkanCommandList& operator=(const VulkanCommandList&) = delete;
  VulkanCommandList(VulkanCommandList&&) = delete;
  VulkanCommandList& operator=(VulkanCommandList&&) = delete;

  void transitionResource(atlantis::rhi::RenderTarget& target, atlantis::rhi::ResourceState before,
                           atlantis::rhi::ResourceState after) override;
  void clearColor(atlantis::rhi::RenderTarget& target, atlantis::rhi::ClearColorValue color) override;

  // Exists solely for VulkanDevice::submit() (vkEndCommandBuffer,
  // vkQueueSubmit) -- never reached from RHI's public surface.
  [[nodiscard]] VkCommandBuffer commandBuffer() const noexcept { return commandBuffer_; }

 private:
  VkDevice device_;
  VkCommandPool commandPool_;
  VkCommandBuffer commandBuffer_;
};

}  // namespace atlantis::vulkan_backend::detail
