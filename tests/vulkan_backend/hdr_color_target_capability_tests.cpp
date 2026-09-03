#include "hdr_color_target_capability.h"
#include "vulkan_pipeline.h"
#include "vulkan_sampled_texture.h"

#include <catch2/catch_test_macros.hpp>

// Plan 0024 Milestone 8 (ADR-0068 D-2): GPU-independent classification
// coverage for hasRequiredHdrColorTargetFeatures() -- synthetic
// VkFormatFeatureFlags inputs only, no real VkPhysicalDevice/Device
// anywhere in this file. Mirrors resource_state_mapping_tests.cpp's
// own identical "private header, pure function" test shape.

using atlantis::vulkan_backend::detail::hasRequiredHdrColorTargetFeatures;
using atlantis::vulkan_backend::detail::hasRequiredSampledTextureFeatures;
using atlantis::vulkan_backend::detail::isSampledTextureBindingInRange;
using atlantis::vulkan_backend::detail::isValidSampledTextureCreateParams;
using atlantis::vulkan_backend::detail::isValidSampledTextureUploadRegion;

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

TEST_CASE("hasRequiredSampledTextureFeatures requires sampling, linear filtering, and transfer destination",
          "[vulkan_backend][sampled_texture_capability]") {
  constexpr VkFormatFeatureFlags kRequired = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                              VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  CHECK(hasRequiredSampledTextureFeatures(kRequired));
  CHECK(hasRequiredSampledTextureFeatures(kRequired | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT));
  CHECK_FALSE(hasRequiredSampledTextureFeatures(kRequired & ~VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT));
  CHECK_FALSE(hasRequiredSampledTextureFeatures(kRequired & ~VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT));
  CHECK_FALSE(hasRequiredSampledTextureFeatures(kRequired & ~VK_FORMAT_FEATURE_TRANSFER_DST_BIT));
  CHECK_FALSE(hasRequiredSampledTextureFeatures(0));
}

TEST_CASE("sampled texture create-parameter validation covers 2D and cube mip preconditions",
          "[vulkan_backend][sampled_texture_validation]") {
  using atlantis::rhi::SampledTextureCreateParams;
  using atlantis::rhi::SampledTextureDimension;
  CHECK(isValidSampledTextureCreateParams({.extent = {8, 4}}));
  CHECK(isValidSampledTextureCreateParams(
      {.extent = {8, 8}, .dimension = SampledTextureDimension::TextureCube, .mipLevelCount = 4}));
  CHECK_FALSE(isValidSampledTextureCreateParams({.extent = {0, 8}}));
  CHECK_FALSE(isValidSampledTextureCreateParams({.extent = {8, 8}, .mipLevelCount = 0}));
  CHECK_FALSE(isValidSampledTextureCreateParams(
      {.extent = {8, 4}, .dimension = SampledTextureDimension::TextureCube}));
  CHECK_FALSE(isValidSampledTextureCreateParams({.extent = {8, 8}, .mipLevelCount = 5}));
}

TEST_CASE("sampled texture upload-region validation checks mip, layer, extent, alignment, and source bounds",
          "[vulkan_backend][sampled_texture_validation]") {
  using atlantis::rhi::SampledTextureDimension;
  using atlantis::rhi::SampledTextureFormat;
  using atlantis::rhi::SampledTextureUploadRegion;
  constexpr auto valid = SampledTextureUploadRegion{.bufferOffsetBytes = 64,
                                                     .mipLevel = 1,
                                                     .arrayLayer = 5,
                                                     .extent = {4, 4}};
  CHECK(isValidSampledTextureUploadRegion({8, 8}, SampledTextureFormat::Rgba16Float,
                                          SampledTextureDimension::TextureCube, 4, 192, valid));
  CHECK_FALSE(isValidSampledTextureUploadRegion(
      {8, 8}, SampledTextureFormat::Rgba16Float, SampledTextureDimension::TextureCube, 4, 192,
      SampledTextureUploadRegion{.bufferOffsetBytes = 64, .mipLevel = 4, .arrayLayer = 5, .extent = {1, 1}}));
  CHECK_FALSE(isValidSampledTextureUploadRegion(
      {8, 8}, SampledTextureFormat::Rgba16Float, SampledTextureDimension::TextureCube, 4, 192,
      SampledTextureUploadRegion{.bufferOffsetBytes = 64, .mipLevel = 1, .arrayLayer = 6, .extent = {4, 4}}));
  CHECK_FALSE(isValidSampledTextureUploadRegion(
      {8, 8}, SampledTextureFormat::Rgba16Float, SampledTextureDimension::TextureCube, 4, 192,
      SampledTextureUploadRegion{.bufferOffsetBytes = 64, .mipLevel = 1, .arrayLayer = 5, .extent = {5, 4}}));
  CHECK_FALSE(isValidSampledTextureUploadRegion(
      {8, 8}, SampledTextureFormat::Rgba16Float, SampledTextureDimension::TextureCube, 4, 192,
      SampledTextureUploadRegion{.bufferOffsetBytes = 65, .mipLevel = 1, .arrayLayer = 5, .extent = {4, 4}}));
  CHECK_FALSE(isValidSampledTextureUploadRegion(
      {8, 8}, SampledTextureFormat::Rgba16Float, SampledTextureDimension::TextureCube, 4, 191, valid));
  CHECK_FALSE(isValidSampledTextureUploadRegion(
      {8, 8}, SampledTextureFormat::Rg16Float, SampledTextureDimension::Texture2D, 1, 64,
      SampledTextureUploadRegion{.bufferOffsetBytes = 0, .mipLevel = 0, .arrayLayer = 1, .extent = {4, 4}}));
}

TEST_CASE("sampled texture binding validation accepts only the bound Pipeline's contiguous range",
          "[vulkan_backend][sampled_texture_validation]") {
  CHECK_FALSE(isSampledTextureBindingInRange(0, 0, 0));
  CHECK(isSampledTextureBindingInRange(0, 1, 0));
  CHECK_FALSE(isSampledTextureBindingInRange(0, 1, 1));
  CHECK_FALSE(isSampledTextureBindingInRange(1, 3, 0));
  CHECK(isSampledTextureBindingInRange(1, 3, 1));
  CHECK(isSampledTextureBindingInRange(1, 3, 2));
  CHECK(isSampledTextureBindingInRange(1, 3, 3));
  CHECK_FALSE(isSampledTextureBindingInRange(1, 3, 4));
}
