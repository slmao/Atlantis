#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>

// Concrete Vulkan implementation of atlantis::rhi::Sampler (Spec 0016 /
// ADR-0055). See vulkan_device.cpp for VulkanDevice::createSampler()'s
// full creation sequence.
namespace atlantis::vulkan_backend::detail {

// Exclusively owns its VkSampler. Constructed only via
// VulkanDevice::createSampler(). Non-copyable/non-movable, matching
// every other concrete Vulkan-Backend resource type. Not internally
// thread-safe; caller-thread-only (ADR-0004). Has exactly one real
// Vulkan implementer in this codebase -- accessed via a plain
// static_cast, matching VulkanSampledTexture's own single-implementer
// precedent.
class VulkanSampler final : public atlantis::rhi::Sampler {
 public:
  VulkanSampler(VkDevice device, VkSampler sampler, atlantis::rhi::Filter filter,
                atlantis::rhi::AddressMode addressMode, atlantis::rhi::MipFilter mipFilter, float minLod,
                float maxLod);
  ~VulkanSampler() override;

  VulkanSampler(const VulkanSampler&) = delete;
  VulkanSampler& operator=(const VulkanSampler&) = delete;
  VulkanSampler(VulkanSampler&&) = delete;
  VulkanSampler& operator=(VulkanSampler&&) = delete;

  [[nodiscard]] atlantis::rhi::Filter filter() const override { return filter_; }
  [[nodiscard]] atlantis::rhi::AddressMode addressMode() const override { return addressMode_; }
  [[nodiscard]] atlantis::rhi::MipFilter mipFilter() const override { return mipFilter_; }
  [[nodiscard]] float minLod() const override { return minLod_; }
  [[nodiscard]] float maxLod() const override { return maxLod_; }

  // Exists solely for VulkanCommandList's bindTexture() body -- never
  // reached from RHI's public surface.
  [[nodiscard]] VkSampler sampler() const noexcept { return sampler_; }

 private:
  VkDevice device_;  // non-owning; must outlive this object (caller-enforced)
  VkSampler sampler_;
  atlantis::rhi::Filter filter_;
  atlantis::rhi::AddressMode addressMode_;
  atlantis::rhi::MipFilter mipFilter_;
  float minLod_;
  float maxLod_;
};

}  // namespace atlantis::vulkan_backend::detail
