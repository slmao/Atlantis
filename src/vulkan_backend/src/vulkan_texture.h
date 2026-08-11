#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/texture.h>
#include <atlantis/rhi/types.h>

// Concrete Vulkan implementation of atlantis::rhi::Texture (Spec 0007 /
// ADR-0023), scoped exclusively to depth-attachment usage this round. See
// vulkan_device.cpp for VulkanDevice::createTexture()'s full allocation
// sequence.
namespace atlantis::vulkan_backend::detail {

// Exclusively owns its VkImage, its own individual VkDeviceMemory
// allocation (device-local, never host-mapped -- no host access to a
// depth attachment is needed or provided this round), and its
// VK_IMAGE_ASPECT_DEPTH_BIT VkImageView. Constructed only via
// VulkanDevice::createTexture(). Non-copyable/non-movable, matching every
// other concrete Vulkan-Backend resource type. Not internally
// thread-safe; caller-thread-only (ADR-0004).
class VulkanTexture final : public atlantis::rhi::Texture {
 public:
  VulkanTexture(VkDevice device, VkImage image, VkDeviceMemory memory, VkImageView imageView,
                atlantis::rhi::Extent2D extent, atlantis::rhi::DepthFormat format);
  ~VulkanTexture() override;

  VulkanTexture(const VulkanTexture&) = delete;
  VulkanTexture& operator=(const VulkanTexture&) = delete;
  VulkanTexture(VulkanTexture&&) = delete;
  VulkanTexture& operator=(VulkanTexture&&) = delete;

  [[nodiscard]] atlantis::rhi::Extent2D extent() const override { return extent_; }
  [[nodiscard]] atlantis::rhi::DepthFormat format() const override { return format_; }

  // Exist solely for VulkanCommandList's beginRendering()/
  // transitionResource() bodies -- never reached from RHI's public
  // surface.
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
