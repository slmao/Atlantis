#include "vulkan_sampled_texture.h"

#include <algorithm>
#include <limits>

namespace atlantis::vulkan_backend::detail {

namespace {

// Spec 0038/ADR-0085: one uniform shape for both uncompressed and
// block-compressed formats -- bytesPerElement is bytes per texel for
// uncompressed formats (blockExtent 1) and bytes per 4x4-texel block for
// BC7 (blockExtent 4). Every size/alignment computation below derives
// from this pair, so legacy formats keep byte-identical behavior.
struct ElementLayout {
  std::size_t bytesPerElement;
  std::uint32_t blockExtent;
};

[[nodiscard]] constexpr ElementLayout elementLayout(atlantis::rhi::SampledTextureFormat format) noexcept {
  switch (format) {
    case atlantis::rhi::SampledTextureFormat::Rgba8Unorm:
    case atlantis::rhi::SampledTextureFormat::Rgba8Srgb:
    case atlantis::rhi::SampledTextureFormat::Rg16Float:
      return ElementLayout{4, 1};
    case atlantis::rhi::SampledTextureFormat::Rgba16Float:
      return ElementLayout{8, 1};
    case atlantis::rhi::SampledTextureFormat::Bc7Unorm:
    case atlantis::rhi::SampledTextureFormat::Bc7Srgb:
      return ElementLayout{16, 4};
  }
  return ElementLayout{0, 0};
}

[[nodiscard]] constexpr bool isBlockCompressed(atlantis::rhi::SampledTextureFormat format) noexcept {
  return elementLayout(format).blockExtent != 1;
}

// ceil(value / blockExtent) without floating point.
[[nodiscard]] constexpr std::size_t blockCount(std::size_t value, std::uint32_t blockExtent) noexcept {
  return (value + blockExtent - 1) / blockExtent;
}

}  // namespace

bool isValidSampledTextureCreateParams(const atlantis::rhi::SampledTextureCreateParams& params) noexcept {
  if (params.extent.width == 0 || params.extent.height == 0 || params.mipLevelCount == 0) return false;
  if (params.dimension == atlantis::rhi::SampledTextureDimension::TextureCube &&
      params.extent.width != params.extent.height) {
    return false;
  }
  // Spec 0038 Requirement 5: a block-compressed base mip whose width or
  // height is not a whole multiple of the block extent is a recoverable
  // rejection, never a silent pad.
  if (isBlockCompressed(params.format) &&
      (params.extent.width % elementLayout(params.format).blockExtent != 0 ||
       params.extent.height % elementLayout(params.format).blockExtent != 0)) {
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
  const ElementLayout layout = elementLayout(format);
  if (layout.bytesPerElement == 0 || region.bufferOffsetBytes % layout.bytesPerElement != 0) return false;
  // A block-compressed copy region must be block-aligned in each dimension
  // unless it reaches that dimension's edge of the mip level -- Vulkan's
  // own rule for vkCmdCopyBufferToImage into a block-compressed image
  // (imageExtent a multiple of the block extent, or imageOffset +
  // imageExtent equal to the subresource's extent). Every region here
  // starts at offset 0, so an unaligned dimension is valid exactly when it
  // spans the whole level -- which is what a mip chain's sub-block tail
  // levels (2x2, 1x1, 4x2, 2x1, ...) need (Spec 0045, ADR-0093 Decision 3).
  if (layout.blockExtent != 1) {
    const bool widthOk = region.extent.width % layout.blockExtent == 0 || region.extent.width == mipWidth;
    const bool heightOk = region.extent.height % layout.blockExtent == 0 || region.extent.height == mipHeight;
    if (!widthOk || !heightOk) return false;
  }
  const std::size_t elementCountWidth = blockCount(region.extent.width, layout.blockExtent);
  const std::size_t elementCountHeight = blockCount(region.extent.height, layout.blockExtent);
  if (elementCountWidth > std::numeric_limits<std::size_t>::max() / elementCountHeight ||
      elementCountWidth * elementCountHeight >
          std::numeric_limits<std::size_t>::max() / layout.bytesPerElement) {
    return false;
  }
  const std::size_t regionBytes = elementCountWidth * elementCountHeight * layout.bytesPerElement;
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
