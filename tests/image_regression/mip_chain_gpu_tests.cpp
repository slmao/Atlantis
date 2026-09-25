#include "fixture/mip_test_textures.h"
#include "fixture/textured_quad_fixture.h"
#include "support/pixel_diff.h"

#include <atlantis/rhi/types.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

// Plan 0045 Milestone 2 (Spec 0045 R4/R5; Spec 0036 workflow 1c's "a GPU
// test sampling non-base mips of a BC7 texture"): synthetic BC7 chains
// (fixture/mip_test_textures.h, built at test time, ruling O2) on the
// textured-quad fixture, which mirrors the Runtime's per-level upload and
// derived sampler (ruling O1). Each quad is 204.8 x 256 px on the 512^2
// target (0.8 x 1.0 NDC, identity camera), so a square N^2 texture is
// minified by N / 204.8 horizontally -- LOD log2(N / 204.8) -- and the
// fixture's Nearest mip selection samples level round-down(LOD + 0.5).

using atlantis::image_regression::bc7SolidBlock;
using atlantis::image_regression::cookMipTestTexture;
using atlantis::image_regression::CookedMipTestTexture;
using atlantis::image_regression::kMipTestLevelColours;
using atlantis::image_regression::levelColourChain;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderTexturedQuadFrame;
using atlantis::image_regression::setUpTexturedQuadFixture;
using atlantis::image_regression::solidSingleLevel;
using atlantis::image_regression::TexturedQuadFixture;

namespace {

// The centres of the left (Bc7Unorm) and right (Bc7Srgb) quads.
constexpr std::uint32_t kLeftCentreX = 128;
constexpr std::uint32_t kRightCentreX = 384;
constexpr std::uint32_t kCentreY = 256;

[[nodiscard]] std::array<std::uint8_t, 4> pixelAt(const PixelBuffer& buffer, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * buffer.width + x) * 4;
  return {buffer.rgba8[offset], buffer.rgba8[offset + 1], buffer.rgba8[offset + 2], buffer.rgba8[offset + 3]};
}

// Renders one texture on both quads (Bc7Unorm left, Bc7Srgb right).
[[nodiscard]] PixelBuffer renderOnBothQuads(const CookedMipTestTexture& texture,
                                            std::optional<float> samplerMaxLodOverride = std::nullopt) {
  const std::string artifact = texture.artifactPath.string();
  const std::string metadata = texture.metadataPath.string();
  auto fixtureResult = setUpTexturedQuadFixture(
      artifact.c_str(), metadata.c_str(), artifact.c_str(), metadata.c_str(),
      ATLANTIS_textured_quad_left_ARTIFACT_PATH, ATLANTIS_textured_quad_left_METADATA_PATH,
      ATLANTIS_textured_quad_right_ARTIFACT_PATH, ATLANTIS_textured_quad_right_METADATA_PATH,
      atlantis::rhi::SampledTextureFormat::Bc7Unorm, atlantis::rhi::SampledTextureFormat::Bc7Srgb,
      samplerMaxLodOverride);
  REQUIRE(fixtureResult.isOk());
  TexturedQuadFixture& fixture = fixtureResult.value();
  auto frame = renderTexturedQuadFrame(fixture);
  REQUIRE(frame.isOk());
  REQUIRE(fixture.device->waitIdle().isOk());
  return std::move(frame.value());
}

}  // namespace

TEST_CASE("The synthetic mode-6 encoder reproduces the importer's white fallback block", "[image_regression][mip]") {
  // material_import.cpp's whiteFallbackDds(): all endpoints 127 with both
  // p-bits 1, all indices 0 -- opaque white.
  const atlantis::image_regression::Bc7Block expected = {0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                         0x01, 0,    0,    0,    0,    0,    0,    0};
  CHECK(bc7SolidBlock({255, 255, 255, 255}) == expected);
}

TEST_CASE("A BC7 mip chain's non-base levels are sampled: square textures of 256 to 2048 select levels 0 to 3",
          "[image_regression][gpu][mip]") {
  struct Case {
    std::uint32_t size;
    std::uint32_t expectedLevel;  // LOD log2(size / 204.8) = 0.32, 1.32, 2.32, 3.32
  };
  std::array<std::array<std::uint8_t, 4>, 4> sampledLeft{};
  std::size_t caseIndex = 0;
  for (const Case c : {Case{256, 0}, Case{512, 1}, Case{1024, 2}, Case{2048, 3}}) {
    INFO("texture " << c.size << "^2, expected level " << c.expectedLevel);
    const auto chain = cookMipTestTexture("level_colour_" + std::to_string(c.size), c.size, levelColourChain(c.size));
    REQUIRE(chain.has_value());
    const PixelBuffer frame = renderOnBothQuads(*chain);

    // The reference: the expected level's colour as a single-level texture
    // of the same size and format, through the same fixture and path.
    const auto reference = cookMipTestTexture("level_reference_" + std::to_string(c.size), c.size,
                                              solidSingleLevel(c.size, kMipTestLevelColours[c.expectedLevel]));
    REQUIRE(reference.has_value());
    const PixelBuffer expected = renderOnBothQuads(*reference);

    CHECK(pixelAt(frame, kLeftCentreX, kCentreY) == pixelAt(expected, kLeftCentreX, kCentreY));
    CHECK(pixelAt(frame, kRightCentreX, kCentreY) == pixelAt(expected, kRightCentreX, kCentreY));
    sampledLeft[caseIndex++] = pixelAt(frame, kLeftCentreX, kCentreY);
  }
  // Four different levels were sampled, not one level four times.
  for (std::size_t i = 0; i < sampledLeft.size(); ++i) {
    for (std::size_t j = i + 1; j < sampledLeft.size(); ++j) CHECK(sampledLeft[i] != sampledLeft[j]);
  }
}

TEST_CASE("With the sampler clamped to maxLod 0, a BC7 chain samples only its base level",
          "[image_regression][gpu][mip]") {
  // The discriminator's mechanism on the level-colour chain: 2048^2 at LOD
  // 3.32 samples level 0's colour once maxLod is 0 -- today's aliasing.
  const auto chain = cookMipTestTexture("level_colour_clamped_2048", 2048, levelColourChain(2048));
  REQUIRE(chain.has_value());
  const PixelBuffer clamped = renderOnBothQuads(*chain, 0.0f);
  const auto reference =
      cookMipTestTexture("level_reference_clamped_2048", 2048, solidSingleLevel(2048, kMipTestLevelColours[0]));
  REQUIRE(reference.has_value());
  const PixelBuffer expected = renderOnBothQuads(*reference);
  CHECK(pixelAt(clamped, kLeftCentreX, kCentreY) == pixelAt(expected, kLeftCentreX, kCentreY));
  CHECK(pixelAt(clamped, kRightCentreX, kCentreY) == pixelAt(expected, kRightCentreX, kCentreY));
}
