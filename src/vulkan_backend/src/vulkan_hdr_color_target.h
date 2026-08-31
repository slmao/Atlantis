#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/hdr_color_target.h>
#include <atlantis/rhi/types.h>

// Concrete Vulkan implementation of atlantis::rhi::HdrColorTarget (Plan
// 0024 Milestone 1 / ADR-0068 D-1) -- the scene-referred linear HDR
// color intermediate. See vulkan_device.cpp for
// VulkanDevice::createHdrColorTarget()'s full allocation sequence.
namespace atlantis::vulkan_backend::detail {

// Exclusively owns its VkImage, its own individual VkDeviceMemory
// allocation (device-local, manual -- no VMA, matching every other RHI
// resource's own established pattern, ADR-0015), and its
// VK_IMAGE_ASPECT_COLOR_BIT VkImageView. Constructed only via
// VulkanDevice::createHdrColorTarget(). Non-copyable/non-movable,
// matching every other concrete Vulkan-Backend resource type. Not
// internally thread-safe; caller-thread-only (ADR-0004). Has exactly
// one real Vulkan implementer in this codebase -- accessed via a plain
// static_cast, matching VulkanSampledTexture's/VulkanTexture's own
// single-implementer precedent, never a dynamic_cast/private-access-
// interface pattern (that pattern exists only for RenderTarget, which
// genuinely has two real Vulkan implementers).
class VulkanHdrColorTarget final : public atlantis::rhi::HdrColorTarget {
 public:
  VulkanHdrColorTarget(VkDevice device, VkImage image, VkDeviceMemory memory, VkImageView imageView,
                        atlantis::rhi::Extent2D extent, atlantis::rhi::HdrFormat format);
  ~VulkanHdrColorTarget() override;

  VulkanHdrColorTarget(const VulkanHdrColorTarget&) = delete;
  VulkanHdrColorTarget& operator=(const VulkanHdrColorTarget&) = delete;
  VulkanHdrColorTarget(VulkanHdrColorTarget&&) = delete;
  VulkanHdrColorTarget& operator=(VulkanHdrColorTarget&&) = delete;

  [[nodiscard]] atlantis::rhi::Extent2D extent() const override { return extent_; }
  [[nodiscard]] atlantis::rhi::HdrFormat format() const override { return format_; }

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
  atlantis::rhi::HdrFormat format_;
};

}  // namespace atlantis::vulkan_backend::detail
