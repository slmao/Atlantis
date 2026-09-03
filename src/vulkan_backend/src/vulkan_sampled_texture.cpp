#include "vulkan_sampled_texture.h"

#include <algorithm>
#include <limits>

namespace atlantis::vulkan_backend::detail {

namespace {

[[nodiscard]] std::size_t bytesPerTexel(atlantis::rhi::SampledTextureFormat format) noexcept {
  switch (format) {
    case atlantis::rhi::SampledTextureFormat::Rgba8Unorm:
    case atlantis::rhi::SampledTextureFormat::Rgba8Srgb:
    case atlantis::rhi::SampledTextureFormat::Rg16Float:
      return 4;
    case atlantis::rhi::SampledTextureFormat::Rgba16Float:
      return 8;
  }
  return 0;
}

}  // namespace

bool isValidSampledTextureCreateParams(const atlantis::rhi::SampledTextureCreateParams& params) noexcept {
  if (params.extent.width == 0 || params.extent.height == 0 || params.mipLevelCount == 0) return false;
  if (params.dimension == atlantis::rhi::SampledTextureDimension::TextureCube &&
      params.extent.width != params.extent.height) {
    return false;
  }
  std::uint32_t maximumMipCount = 0;
  for (std::uint32_t size = std::max(params.extent.width, params.extent.height); size != 0; size >>= 1U) {
    ++maximumMipCount;
  }
  return params.mipLevelCount <= maximumMipCount;
}

bool isValidSampledTextureUploadRegion(
    atlantis::rhi::Extent2D textureExtent, atlantis::rhi::SampledTextureFormat format,
    atlantis::rhi::SampledTextureDimension dimension, std::uint32_t mipLevelCount,
    std::size_t sourceSizeBytes, const atlantis::rhi::SampledTextureUploadRegion& region) noexcept {
  const std::uint32_t layerCount =
      dimension == atlantis::rhi::SampledTextureDimension::TextureCube ? 6U : 1U;
  if (region.mipLevel >= mipLevelCount || region.arrayLayer >= layerCount || region.extent.width == 0 ||
      region.extent.height == 0) {
    return false;
  }
  const std::uint32_t mipWidth = std::max(1U, textureExtent.width >> region.mipLevel);
  const std::uint32_t mipHeight = std::max(1U, textureExtent.height >> region.mipLevel);
  if (region.extent.width > mipWidth || region.extent.height > mipHeight) return false;
  const std::size_t texelBytes = bytesPerTexel(format);
  if (texelBytes == 0 || region.bufferOffsetBytes % texelBytes != 0) return false;
  const std::size_t width = region.extent.width;
  const std::size_t height = region.extent.height;
  if (width > std::numeric_limits<std::size_t>::max() / height ||
      width * height > std::numeric_limits<std::size_t>::max() / texelBytes) {
    return false;
  }
  const std::size_t regionBytes = width * height * texelBytes;
  return region.bufferOffsetBytes <= sourceSizeBytes && regionBytes <= sourceSizeBytes - region.bufferOffsetBytes;
}

VulkanSampledTexture::VulkanSampledTexture(VkDevice device, VkImage image, VkDeviceMemory memory,
                                            VkImageView imageView, atlantis::rhi::Extent2D extent,
                                            atlantis::rhi::SampledTextureFormat format,
                                            atlantis::rhi::SampledTextureDimension dimension,
                                            std::uint32_t mipLevelCount)
    : device_(device),
      image_(image),
      memory_(memory),
      imageView_(imageView),
      extent_(extent),
      format_(format),
      dimension_(dimension),
      mipLevelCount_(mipLevelCount) {}

VulkanSampledTexture::~VulkanSampledTexture() {
  vkDestroyImageView(device_, imageView_, nullptr);
  vkFreeMemory(device_, memory_, nullptr);
  vkDestroyImage(device_, image_, nullptr);
}

}  // namespace atlantis::vulkan_backend::detail
