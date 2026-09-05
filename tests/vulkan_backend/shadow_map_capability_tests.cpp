#include "shadow_map_capability.h"

#include <catch2/catch_test_macros.hpp>

// Plan 0027 Milestone 1 (ADR-0072 D-1): GPU-independent classification
// coverage for hasRequiredShadowMapFeatures() -- synthetic
// VkFormatFeatureFlags inputs only, no real VkPhysicalDevice/Device
// anywhere in this file. Mirrors hdr_color_target_capability_tests.cpp's
// own identical "private header, pure function" test shape.

using atlantis::vulkan_backend::detail::hasRequiredShadowMapFeatures;

TEST_CASE("hasRequiredShadowMapFeatures: both required bits present returns true",
          "[vulkan_backend][shadow_map_capability]") {
  constexpr VkFormatFeatureFlags kBoth =
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  CHECK(hasRequiredShadowMapFeatures(kBoth));

  // Extra, unrelated bits set alongside both required ones must not
  // change the outcome -- this is a "required bits present" check, not
  // an exact-match check.
  constexpr VkFormatFeatureFlags kBothPlusExtras =
      kBoth | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
  CHECK(hasRequiredShadowMapFeatures(kBothPlusExtras));
}

TEST_CASE("hasRequiredShadowMapFeatures: DEPTH_STENCIL_ATTACHMENT_BIT missing returns false",
          "[vulkan_backend][shadow_map_capability]") {
  constexpr VkFormatFeatureFlags kSampledOnly = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  CHECK_FALSE(hasRequiredShadowMapFeatures(kSampledOnly));
}

TEST_CASE("hasRequiredShadowMapFeatures: SAMPLED_IMAGE_BIT missing returns false",
          "[vulkan_backend][shadow_map_capability]") {
  constexpr VkFormatFeatureFlags kDepthAttachmentOnly = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
  CHECK_FALSE(hasRequiredShadowMapFeatures(kDepthAttachmentOnly));
}

TEST_CASE("hasRequiredShadowMapFeatures: both required bits missing returns false",
          "[vulkan_backend][shadow_map_capability]") {
  // A real-sounding but irrelevant flag set -- confirms this is a
  // genuine two-bit check, not a "any bit at all" or "non-zero" check.
  constexpr VkFormatFeatureFlags kUnrelated =
      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  CHECK_FALSE(hasRequiredShadowMapFeatures(kUnrelated));
  CHECK_FALSE(hasRequiredShadowMapFeatures(0));
}
