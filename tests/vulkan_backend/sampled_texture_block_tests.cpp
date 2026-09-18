#include "sampled_texture_format_mapping.h"
#include "vulkan_sampled_texture.h"

#include <atlantis/rhi/types.h>

#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

// Spec 0038/Plan 0038 Milestone 1: pure, GPU-independent tests for the
// two block-compression-aware surfaces -- toVkFormat()'s two new
// enumerators (extracted to sampled_texture_format_mapping.h for exactly
// this purpose, mirroring resource_state_mapping.h's own precedent) and
// the block-aware byte/alignment math in isValidSampledTextureCreateParams()/
// isValidSampledTextureUploadRegion(). Legacy-format cases here are
// regression guards proving the rewrite kept uncompressed behavior
// byte-identical.

namespace {

using atlantis::rhi::Extent2D;
using atlantis::rhi::SampledTextureCreateParams;
using atlantis::rhi::SampledTextureDimension;
using atlantis::rhi::SampledTextureFormat;
using atlantis::rhi::SampledTextureUploadRegion;
using atlantis::vulkan_backend::detail::isValidSampledTextureCreateParams;
using atlantis::vulkan_backend::detail::isValidSampledTextureUploadRegion;
using atlantis::vulkan_backend::detail::toVkFormat;

[[nodiscard]] SampledTextureCreateParams makeCreateParams(
    std::uint32_t width, std::uint32_t height, SampledTextureFormat format,
    SampledTextureDimension dimension = SampledTextureDimension::Texture2D) {
  return SampledTextureCreateParams{.extent = Extent2D{width, height}, .format = format,
                                    .dimension = dimension, .mipLevelCount = 1};
}

}  // namespace

TEST_CASE("toVkFormat maps the two BC7 enumerators", "[vulkan][sampled-texture]") {
  CHECK(toVkFormat(SampledTextureFormat::Bc7Unorm) == VK_FORMAT_BC7_UNORM_BLOCK);
  CHECK(toVkFormat(SampledTextureFormat::Bc7Srgb) == VK_FORMAT_BC7_SRGB_BLOCK);
}

TEST_CASE("toVkFormat keeps every legacy mapping unchanged", "[vulkan][sampled-texture]") {
  CHECK(toVkFormat(SampledTextureFormat::Rgba8Unorm) == VK_FORMAT_R8G8B8A8_UNORM);
  CHECK(toVkFormat(SampledTextureFormat::Rgba8Srgb) == VK_FORMAT_R8G8B8A8_SRGB);
  CHECK(toVkFormat(SampledTextureFormat::Rgba16Float) == VK_FORMAT_R16G16B16A16_SFLOAT);
  CHECK(toVkFormat(SampledTextureFormat::Rg16Float) == VK_FORMAT_R16G16_SFLOAT);
}

TEST_CASE("block-aligned BC7 base mips are valid create params", "[vulkan][sampled-texture]") {
  CHECK(isValidSampledTextureCreateParams(makeCreateParams(4, 4, SampledTextureFormat::Bc7Unorm)));
  CHECK(isValidSampledTextureCreateParams(makeCreateParams(8, 8, SampledTextureFormat::Bc7Srgb)));
  CHECK(isValidSampledTextureCreateParams(makeCreateParams(64, 32, SampledTextureFormat::Bc7Unorm)));
}

TEST_CASE("non-block-aligned BC7 base mips are recoverably rejected", "[vulkan][sampled-texture]") {
  CHECK_FALSE(isValidSampledTextureCreateParams(makeCreateParams(5, 4, SampledTextureFormat::Bc7Unorm)));
  CHECK_FALSE(isValidSampledTextureCreateParams(makeCreateParams(4, 5, SampledTextureFormat::Bc7Srgb)));
  CHECK_FALSE(isValidSampledTextureCreateParams(makeCreateParams(7, 7, SampledTextureFormat::Bc7Unorm)));
}

TEST_CASE("uncompressed formats keep no alignment requirement", "[vulkan][sampled-texture]") {
  CHECK(isValidSampledTextureCreateParams(makeCreateParams(3, 3, SampledTextureFormat::Rgba8Unorm)));
  CHECK(isValidSampledTextureCreateParams(makeCreateParams(1, 7, SampledTextureFormat::Rgba8Srgb)));
}

TEST_CASE("legacy upload-region math is unchanged", "[vulkan][sampled-texture]") {
  // 4x4 RGBA8 region = 64 bytes.
  CHECK(isValidSampledTextureUploadRegion(Extent2D{4, 4}, SampledTextureFormat::Rgba8Unorm,
                                           SampledTextureDimension::Texture2D, 1, 64,
                                           SampledTextureUploadRegion{.bufferOffsetBytes = 0, .extent = {4, 4}}));
  // Misaligned offset for a 4-byte-per-texel format.
  CHECK_FALSE(isValidSampledTextureUploadRegion(Extent2D{4, 4}, SampledTextureFormat::Rgba8Unorm,
                                                 SampledTextureDimension::Texture2D, 1, 64,
                                                 SampledTextureUploadRegion{.bufferOffsetBytes = 2,
                                                                            .extent = {4, 4}}));
  // Rgba16Float: 8 bytes per texel, offset must be 8-aligned.
  CHECK(isValidSampledTextureUploadRegion(Extent2D{2, 2}, SampledTextureFormat::Rgba16Float,
                                           SampledTextureDimension::Texture2D, 1, 40,
                                           SampledTextureUploadRegion{.bufferOffsetBytes = 8, .extent = {2, 2}}));
  CHECK_FALSE(isValidSampledTextureUploadRegion(Extent2D{2, 2}, SampledTextureFormat::Rgba16Float,
                                                 SampledTextureDimension::Texture2D, 1, 40,
                                                 SampledTextureUploadRegion{.bufferOffsetBytes = 4,
                                                                            .extent = {2, 2}}));
}

TEST_CASE("BC7 upload regions use 16 bytes per 4x4 block", "[vulkan][sampled-texture]") {
  const auto format = SampledTextureFormat::Bc7Unorm;
  // Full 8x8 texture: 2x2 blocks = 64 bytes.
  CHECK(isValidSampledTextureUploadRegion(Extent2D{8, 8}, format, SampledTextureDimension::Texture2D, 1, 64,
                                          SampledTextureUploadRegion{.extent = {8, 8}}));
  // 12x12: 3x3 blocks = 144 bytes, exactly consumed.
  CHECK(isValidSampledTextureUploadRegion(Extent2D{12, 12}, format, SampledTextureDimension::Texture2D, 1, 144,
                                          SampledTextureUploadRegion{.extent = {12, 12}}));
  CHECK_FALSE(isValidSampledTextureUploadRegion(Extent2D{12, 12}, format, SampledTextureDimension::Texture2D, 1,
                                                143, SampledTextureUploadRegion{.extent = {12, 12}}));
  // A 4x4 sub-region of an 8x8 texture is one 16-byte block.
  CHECK(isValidSampledTextureUploadRegion(Extent2D{8, 8}, format, SampledTextureDimension::Texture2D, 1, 16,
                                          SampledTextureUploadRegion{.extent = {4, 4}}));
  // Non-multiple-of-16 offsets are rejected for BC7.
  CHECK_FALSE(isValidSampledTextureUploadRegion(Extent2D{8, 8}, format, SampledTextureDimension::Texture2D, 1, 80,
                                                SampledTextureUploadRegion{.bufferOffsetBytes = 8,
                                                                           .extent = {8, 8}}));
  // Partial-block region extents are rejected (5 is not a multiple of 4).
  CHECK_FALSE(isValidSampledTextureUploadRegion(Extent2D{8, 8}, format, SampledTextureDimension::Texture2D, 1, 64,
                                                SampledTextureUploadRegion{.extent = {5, 4}}));
}

TEST_CASE("BC7 mip-shifted regions size against the mip extent", "[vulkan][sampled-texture]") {
  const auto format = SampledTextureFormat::Bc7Srgb;
  // 16x16 texture, mip 1 is 8x8 = 64 bytes.
  CHECK(isValidSampledTextureUploadRegion(Extent2D{16, 16}, format, SampledTextureDimension::Texture2D, 2, 64,
                                          SampledTextureUploadRegion{.mipLevel = 1, .extent = {8, 8}}));
  // A full-extent region cannot exceed its mip's own dimensions.
  CHECK_FALSE(isValidSampledTextureUploadRegion(Extent2D{16, 16}, format, SampledTextureDimension::Texture2D, 2,
                                                256, SampledTextureUploadRegion{.mipLevel = 1, .extent = {16, 16}}));
}
