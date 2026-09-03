#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/pipeline.h>

// Concrete Vulkan implementation of atlantis::rhi::Pipeline (Spec 0007 /
// ADR-0024 / ADR-0025). See vulkan_device.cpp for
// VulkanDevice::createPipeline()'s full construction sequence and this
// Plan's "Camera uniform binding -- full candidate design" subsection for
// the VkDescriptorSetLayout/VkDescriptorPool/VkDescriptorSet design this
// type participates in.
namespace atlantis::vulkan_backend::detail {

[[nodiscard]] bool isSampledTextureBindingInRange(std::uint32_t firstBinding,
                                                   std::uint32_t bindingCount,
                                                   std::uint32_t binding) noexcept;

// Exclusively owns its VkPipeline, VkPipelineLayout, VkDescriptorSetLayout,
// and the one VkDescriptorSet allocated for it from VulkanDevice's
// Device-level VkDescriptorPool (freed here, at this object's own
// destruction -- never by VulkanDevice). Constructed only via
// VulkanDevice::createPipeline(). Non-copyable/non-movable, matching
// every other concrete Vulkan-Backend resource type. Not internally
// thread-safe; caller-thread-only (ADR-0004).
class VulkanPipeline final : public atlantis::rhi::Pipeline {
 public:
  VulkanPipeline(VkDevice device, VkDescriptorPool descriptorPool, VkPipeline pipeline,
                 VkPipelineLayout pipelineLayout, VkDescriptorSetLayout descriptorSetLayout,
                 VkDescriptorSet descriptorSet, std::uint32_t sampledTextureFirstBinding,
                 std::uint32_t sampledTextureBindingCount);
  ~VulkanPipeline() override;

  VulkanPipeline(const VulkanPipeline&) = delete;
  VulkanPipeline& operator=(const VulkanPipeline&) = delete;
  VulkanPipeline(VulkanPipeline&&) = delete;
  VulkanPipeline& operator=(VulkanPipeline&&) = delete;

  // Exist solely for VulkanCommandList's bindPipeline()/bindUniformBuffer()/
  // pushConstant() bodies -- never reached from RHI's public surface.
  [[nodiscard]] VkPipeline vkPipeline() const noexcept { return pipeline_; }
  [[nodiscard]] VkPipelineLayout pipelineLayout() const noexcept { return pipelineLayout_; }
  [[nodiscard]] VkDescriptorSet descriptorSet() const noexcept { return descriptorSet_; }
  [[nodiscard]] std::uint32_t sampledTextureFirstBinding() const noexcept { return sampledTextureFirstBinding_; }
  [[nodiscard]] std::uint32_t sampledTextureBindingCount() const noexcept { return sampledTextureBindingCount_; }

 private:
  VkDevice device_;                  // non-owning; must outlive this object (caller-enforced)
  VkDescriptorPool descriptorPool_;  // non-owning; VulkanDevice's pool, must outlive this object
  VkPipeline pipeline_;
  VkPipelineLayout pipelineLayout_;
  VkDescriptorSetLayout descriptorSetLayout_;
  VkDescriptorSet descriptorSet_;
  std::uint32_t sampledTextureFirstBinding_;
  std::uint32_t sampledTextureBindingCount_;
};

}  // namespace atlantis::vulkan_backend::detail
