#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/shadow_map.h>
#include <atlantis/rhi/types.h>

// Concrete Vulkan implementation of atlantis::rhi::ShadowMap (Plan 0027
// Milestone 1 / ADR-0072 D-1). See vulkan_device.cpp for
// VulkanDevice::createShadowMap()'s full allocation sequence.
namespace atlantis::vulkan_backend::detail {

// Exclusively owns its VkImage, its own individual VkDeviceMemory
// allocation (device-local, manual -- no VMA, matching every other RHI
// resource's own established pattern), and its VK_IMAGE_ASPECT_DEPTH_BIT
// VkImageView. Constructed only via VulkanDevice::createShadowMap().
// Non-copyable/non-movable, matching VulkanHdrColorTarget's own
// identical shape. Not internally thread-safe; caller-thread-only
// (ADR-0004).
class VulkanShadowMap final : public atlantis::rhi::ShadowMap {
 public:
  VulkanShadowMap(VkDevice device, VkImage image, VkDeviceMemory memory, VkImageView imageView,
                   atlantis::rhi::Extent2D extent, atlantis::rhi::DepthFormat format);
  ~VulkanShadowMap() override;

  VulkanShadowMap(const VulkanShadowMap&) = delete;
  VulkanShadowMap& operator=(const VulkanShadowMap&) = delete;
  VulkanShadowMap(VulkanShadowMap&&) = delete;
  VulkanShadowMap& operator=(VulkanShadowMap&&) = delete;

  [[nodiscard]] atlantis::rhi::Extent2D extent() const override { return extent_; }
  [[nodiscard]] atlantis::rhi::DepthFormat format() const override { return format_; }

  // Exist solely for VulkanCommandList's transitionResource()/
  // beginRendering()/bindTexture() bodies -- never reached from RHI's
  // public surface.
  [[nodiscard]] VkImage image() const noexcept { return image_; }
  [[nodiscard]] VkImageView imageView() const noexcept { return imageView_; }

 private:
  VkDevice device_;  // non-owning; must outlive this object (caller-enforced)
  VkImage image_;
  VkDeviceMemory memory_;
  VkImageView imageView_;
  atlantis::rhi::Extent2D extent_;
  atlantis::rhi::DepthFormat format_;
};

}  // namespace atlantis::vulkan_backend::detail
