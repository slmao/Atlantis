#include "sampled_texture_format_mapping.h"

#include <atlantis/assert.h>

namespace atlantis::vulkan_backend::detail {

VkFormat toVkFormat(atlantis::rhi::SampledTextureFormat format) noexcept {
  switch (format) {
    case atlantis::rhi::SampledTextureFormat::Rgba8Unorm:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case atlantis::rhi::SampledTextureFormat::Rgba8Srgb:
      return VK_FORMAT_R8G8B8A8_SRGB;
    case atlantis::rhi::SampledTextureFormat::Rgba16Float:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case atlantis::rhi::SampledTextureFormat::Rg16Float:
      return VK_FORMAT_R16G16_SFLOAT;
    case atlantis::rhi::SampledTextureFormat::Bc7Unorm:
      return VK_FORMAT_BC7_UNORM_BLOCK;
    case atlantis::rhi::SampledTextureFormat::Bc7Srgb:
      return VK_FORMAT_BC7_SRGB_BLOCK;
  }
  ATLANTIS_CHECK_MSG(false, "toVkFormat(SampledTextureFormat) called with an unhandled enumerator");
  return VK_FORMAT_UNDEFINED;
}

}  // namespace atlantis::vulkan_backend::detail
