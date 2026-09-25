#include "fixture/mip_test_textures.h"
#include "fixture/textured_quad_fixture.h"
#include "support/golden_validity.h"
#include "support/pixel_diff.h"

#include <atlantis/rhi/types.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

// ---------------------------------------------------------------------------
// Plan 0045 Milestone 2, golden commit (ADR-0042 Initial baseline
// bootstrap): mip_chain_demo was captured by
// atlantis_image_regression_mip_chain_demo_golden_generator against the
// clean, already-committed tree at its recorded source_revision; these two
// TEST_CASEs land with it. The golden is the 1024^2 checker chain on both
// quads at LOD ~2.32: uniform average grey. Its discriminator renders the
// same chain base-mip only (sampler maxLod 0) -- the 1-texel checker
// point-sampled every ~5 texels, a moire -- and must fail.
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kMipChainDemoGoldenName = "mip_chain_demo/mip_chain_demo_512x512_rgba8unorm";
constexpr const char* kMipChainDemoGoldenSlug = "mip_chain_demo_512x512_rgba8unorm";

[[nodiscard]] PixelBuffer renderMipChainDemo(std::optional<float> samplerMaxLodOverride) {
  const auto checker = cookMipTestTexture(
      "mip_chain_demo_checker", atlantis::image_regression::kMipChainDemoTextureSize,
      atlantis::image_regression::checkerChain(atlantis::image_regression::kMipChainDemoTextureSize));
  REQUIRE(checker.has_value());
  return renderOnBothQuads(*checker, samplerMaxLodOverride);
}

[[nodiscard]] atlantis::image_regression::ValidatedGolden loadMipChainDemoGolden() {
  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto goldenResult = atlantis::image_regression::loadAndValidateGolden(
      goldensDir / (std::string(kMipChainDemoGoldenName) + ".png"),
      goldensDir / (std::string(kMipChainDemoGoldenName) + ".sidecar.txt"));
  INFO("INVALID GOLDEN: the committed mip_chain_demo golden must load and validate cleanly");
  REQUIRE(goldenResult.isOk());
  return std::move(goldenResult.value());
}

}  // namespace

TEST_CASE("Full capture-compare cycle against the committed mip_chain_demo golden passes",
          "[image_regression][gpu][mip]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  std::filesystem::remove(outputDir / (std::string(kMipChainDemoGoldenSlug) + "_actual.png"));
  std::filesystem::remove(outputDir / (std::string(kMipChainDemoGoldenSlug) + "_diff.png"));

  const PixelBuffer actual = renderMipChainDemo(std::nullopt);
  const auto golden = loadMipChainDemoGolden();
  REQUIRE(actual.width == golden.pixels.width);
  REQUIRE(actual.height == golden.pixels.height);
  const auto report = atlantis::image_regression::compareBuffers(actual, golden.pixels);
  if (!report.passed) {
    (void)atlantis::image_regression::writeFailureArtifacts(outputDir, kMipChainDemoGoldenSlug, actual,
                                                            golden.pixels);
  }
  REQUIRE(report.passed);
}

TEST_CASE("The mip_chain_demo frame sampled base-mip only (maxLod 0) fails comparison against the real golden",
          "[image_regression][gpu][mip]") {
  const PixelBuffer baseMipOnly = renderMipChainDemo(0.0f);
  const auto golden = loadMipChainDemoGolden();
  const auto report = atlantis::image_regression::compareBuffers(baseMipOnly, golden.pixels);
  CHECK_FALSE(report.passed);
  // The checker's dark and light texels (1 and 253) against the average
  // grey: the moire moves channels far from the golden's uniform grey.
  CHECK(report.maxChannelDiff > 60);
  // Kept as review evidence under the build tree's failure-artifact
  // directory (never committed): the aliased frame and its diff.
  (void)atlantis::image_regression::writeFailureArtifacts(
      ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR, std::string(kMipChainDemoGoldenSlug) + "_base_mip_only", baseMipOnly,
      golden.pixels);
}
