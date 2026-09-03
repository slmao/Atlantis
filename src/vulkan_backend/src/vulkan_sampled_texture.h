#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/types.h>

// Concrete Vulkan implementation of atlantis::rhi::SampledTexture (Spec
// 0016 / ADR-0055) -- a general, sampled 2D color image, independent of
// VulkanTexture (which remains scoped exclusively to depth-attachment
// usage). See vulkan_device.cpp for VulkanDevice::createSampledTexture()'s
// full allocation sequence.
namespace atlantis::vulkan_backend::detail {

[[nodiscard]] bool isValidSampledTextureCreateParams(
    const atlantis::rhi::SampledTextureCreateParams& params) noexcept;
[[nodiscard]] bool isValidSampledTextureUploadRegion(
    atlantis::rhi::Extent2D textureExtent, atlantis::rhi::SampledTextureFormat format,
    atlantis::rhi::SampledTextureDimension dimension, std::uint32_t mipLevelCount,
    std::size_t sourceSizeBytes, const atlantis::rhi::SampledTextureUploadRegion& region) noexcept;

// Exclusively owns its VkImage, its own individual VkDeviceMemory
// allocation (device-local, manual -- no VMA, matching every other RHI
// resource's own established pattern, ADR-0015), and its
// VK_IMAGE_ASPECT_COLOR_BIT VkImageView. Constructed only via
// VulkanDevice::createSampledTexture(). Non-copyable/non-movable,
// matching every other concrete Vulkan-Backend resource type. Not
// internally thread-safe; caller-thread-only (ADR-0004). Has exactly
// one real Vulkan implementer in this codebase -- accessed via a plain
// static_cast, matching VulkanTexture's own single-implementer
// precedent, never a dynamic_cast/private-access-interface pattern
// (that pattern exists only for RenderTarget, which genuinely has two
// real Vulkan implementers).
class VulkanSampledTexture final : public atlantis::rhi::SampledTexture {
 public:
  VulkanSampledTexture(VkDevice device, VkImage image, VkDeviceMemory memory, VkImageView imageView,
                        atlantis::rhi::Extent2D extent, atlantis::rhi::SampledTextureFormat format,
                        atlantis::rhi::SampledTextureDimension dimension, std::uint32_t mipLevelCount);
  ~VulkanSampledTexture() override;

  VulkanSampledTexture(const VulkanSampledTexture&) = delete;
  VulkanSampledTexture& operator=(const VulkanSampledTexture&) = delete;
  VulkanSampledTexture(VulkanSampledTexture&&) = delete;
  VulkanSampledTexture& operator=(VulkanSampledTexture&&) = delete;

  [[nodiscard]] atlantis::rhi::Extent2D extent() const override { return extent_; }
  [[nodiscard]] atlantis::rhi::SampledTextureFormat format() const override { return format_; }
  [[nodiscard]] atlantis::rhi::SampledTextureDimension dimension() const override { return dimension_; }
  [[nodiscard]] std::uint32_t mipLevelCount() const override { return mipLevelCount_; }

  // Exist solely for VulkanCommandList's copyBufferToTexture()/
  // transitionResource()/bindTexture() bodies -- never reached from
  // RHI's public surface.
  [[nodiscard]] VkImage image() const noexcept { return image_; }
  [[nodiscard]] VkImageView imageView() const noexcept { return imageView_; }

 private:
  VkDevice device_;  // non-owning; must outlive this object (caller-enforced)
  VkImage image_;
  VkDeviceMemory memory_;
  VkImageView imageView_;
  atlantis::rhi::Extent2D extent_;
  atlantis::rhi::SampledTextureFormat format_;
  atlantis::rhi::SampledTextureDimension dimension_;
  std::uint32_t mipLevelCount_;
};

}  // namespace atlantis::vulkan_backend::detail
