#include "hdr_color_target_capability.h"

#include <catch2/catch_test_macros.hpp>

// Plan 0024 Milestone 8 (ADR-0068 D-2): GPU-independent classification
// coverage for hasRequiredHdrColorTargetFeatures() -- synthetic
// VkFormatFeatureFlags inputs only, no real VkPhysicalDevice/Device
// anywhere in this file. Mirrors resource_state_mapping_tests.cpp's
// own identical "private header, pure function" test shape.

using atlantis::vulkan_backend::detail::hasRequiredHdrColorTargetFeatures;

TEST_CASE("hasRequiredHdrColorTargetFeatures: both required bits present returns true",
          "[vulkan_backend][hdr_color_target_capability]") {
  constexpr VkFormatFeatureFlags kBoth =
      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  CHECK(hasRequiredHdrColorTargetFeatures(kBoth));

  // Extra, unrelated bits set alongside both required ones must not
  // change the outcome -- this is a "required bits present" check, not
  // an exact-match check.
  constexpr VkFormatFeatureFlags kBothPlusExtras =
      kBoth | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
  CHECK(hasRequiredHdrColorTargetFeatures(kBothPlusExtras));
}

TEST_CASE("hasRequiredHdrColorTargetFeatures: COLOR_ATTACHMENT_BIT missing returns false",
          "[vulkan_backend][hdr_color_target_capability]") {
  constexpr VkFormatFeatureFlags kSampledOnly = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  CHECK_FALSE(hasRequiredHdrColorTargetFeatures(kSampledOnly));
}

TEST_CASE("hasRequiredHdrColorTargetFeatures: SAMPLED_IMAGE_BIT missing returns false",
          "[vulkan_backend][hdr_color_target_capability]") {
  constexpr VkFormatFeatureFlags kColorAttachmentOnly = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
  CHECK_FALSE(hasRequiredHdrColorTargetFeatures(kColorAttachmentOnly));
}

TEST_CASE("hasRequiredHdrColorTargetFeatures: both required bits missing returns false",
          "[vulkan_backend][hdr_color_target_capability]") {
  // A real-sounding but irrelevant flag set -- confirms this is a
  // genuine two-bit check, not a "any bit at all" or "non-zero" check.
  constexpr VkFormatFeatureFlags kUnrelated =
      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  CHECK_FALSE(hasRequiredHdrColorTargetFeatures(kUnrelated));
  CHECK_FALSE(hasRequiredHdrColorTargetFeatures(0));
}

// Deliberately NOT checked by this design (D-2's own explicit list) --
// each alone, without both required bits also present, must still
// return false, confirming these are genuinely irrelevant to the
// classification, not silently sufficient substitutes.
TEST_CASE("hasRequiredHdrColorTargetFeatures: unused-by-design bits alone do not satisfy the check",
          "[vulkan_backend][hdr_color_target_capability]") {
  CHECK_FALSE(hasRequiredHdrColorTargetFeatures(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT));
  CHECK_FALSE(hasRequiredHdrColorTargetFeatures(VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT));
  CHECK_FALSE(hasRequiredHdrColorTargetFeatures(VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                                 VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                                                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                                                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT));
}
