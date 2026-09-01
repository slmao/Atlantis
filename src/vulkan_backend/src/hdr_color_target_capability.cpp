#include "hdr_color_target_capability.h"

namespace atlantis::vulkan_backend::detail {

bool hasRequiredHdrColorTargetFeatures(VkFormatFeatureFlags optimalTilingFeatures) {
  constexpr VkFormatFeatureFlags kRequired =
      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  return (optimalTilingFeatures & kRequired) == kRequired;
}

}  // namespace atlantis::vulkan_backend::detail
