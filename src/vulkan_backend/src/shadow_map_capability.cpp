#include "shadow_map_capability.h"

namespace atlantis::vulkan_backend::detail {

bool hasRequiredShadowMapFeatures(VkFormatFeatureFlags optimalTilingFeatures) {
  constexpr VkFormatFeatureFlags kRequired =
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  return (optimalTilingFeatures & kRequired) == kRequired;
}

}  // namespace atlantis::vulkan_backend::detail
