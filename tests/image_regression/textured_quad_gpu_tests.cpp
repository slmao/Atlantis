#include "fixture/textured_quad_fixture.h"
#include "support/golden_validity.h"
#include "support/pixel_diff.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

// Spec 0016/Plan 0016 Milestone 9 (D11, V27-V29, V39-V42): the textured
// fixture's own GPU-required coverage. Structural correctness --
// non-degenerate output, both quads visibly drawn, a real, measurable
// Unorm-vs-Srgb difference, and genuine RenderTarget use (the
// baseline-comparison test, D11's own explicit requirement) -- plus,
// once the golden PNG/sidecar exist (this file's own commit that adds
// them, strictly separate from and after the implementation commit that
// first introduced this file, per Plan 0016's own two-phase golden
// capture process), a full capture-compare cycle against the committed
// golden, mirroring image_regression_gpu_tests.cpp's own identical
// pattern for minimal_cube.

using atlantis::image_regression::compareBuffers;
using atlantis::image_regression::kTexturedQuadExtentPixels;
using atlantis::image_regression::loadAndValidateGolden;
using atlantis::image_regression::writeFailureArtifacts;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderTexturedQuadBaselineFrame;
using atlantis::image_regression::renderTexturedQuadFrame;
using atlantis::image_regression::setUpTexturedQuadFixture;
using atlantis::image_regression::TexturedQuadFixture;

namespace {

[[nodiscard]] std::array<std::uint8_t, 4> pixelAt(const PixelBuffer& buffer, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * buffer.width + x) * 4;
  return {buffer.rgba8[offset], buffer.rgba8[offset + 1], buffer.rgba8[offset + 2], buffer.rgba8[offset + 3]};
}

[[nodiscard]] bool nearBackgroundClearColor(const std::array<std::uint8_t, 4>& pixel) {
  // Renderer's own fixed kBackgroundClearColor (0.05, 0.05, 0.08, 1.0) ->
  // approximately (13, 13, 20, 255) in Rgba8Unorm bytes.
  const int tolerance = 6;
  return std::abs(static_cast<int>(pixel[0]) - 13) <= tolerance &&
         std::abs(static_cast<int>(pixel[1]) - 13) <= tolerance &&
         std::abs(static_cast<int>(pixel[2]) - 20) <= tolerance;
}

// Left quad occupies screen x in roughly [26, 230], right quad [282,
// 486] (clip-space x in [-0.9,-0.1]/[0.1,0.9] mapped to a 512-wide
// viewport); both span y in roughly [128, 384]. Samples a small grid
// within each quad's own region rather than one single pixel, so a
// nearest-filtered checkerboard block boundary landing exactly on one
// sample point does not make this test flaky.
[[nodiscard]] std::vector<std::array<std::uint8_t, 4>> sampleGrid(const PixelBuffer& buffer, std::uint32_t xStart,
                                                                    std::uint32_t xEnd, std::uint32_t yStart,
                                                                    std::uint32_t yEnd, std::uint32_t step) {
  std::vector<std::array<std::uint8_t, 4>> samples;
  for (std::uint32_t y = yStart; y < yEnd; y += step) {
    for (std::uint32_t x = xStart; x < xEnd; x += step) {
      samples.push_back(pixelAt(buffer, x, y));
    }
  }
  return samples;
}

constexpr std::uint32_t kLeftXStart = 60, kLeftXEnd = 200;
constexpr std::uint32_t kRightXStart = 312, kRightXEnd = 452;
constexpr std::uint32_t kYStart = 160, kYEnd = 352;
constexpr std::uint32_t kStep = 20;

constexpr const char* kTexturedQuadGoldenName = "textured_quad/textured_quad_512x512_rgba8unorm";
constexpr const char* kTexturedQuadGoldenSlug = "textured_quad_512x512_rgba8unorm";

[[nodiscard]] std::filesystem::path goldenPngPath(const std::string& goldenName) {
  return std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".png");
}

[[nodiscard]] std::filesystem::path goldenSidecarPath(const std::string& goldenName) {
  return std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".sidecar.txt");
}

}  // namespace

TEST_CASE("Textured quad fixture renders a non-degenerate frame with both quads visible",
          "[image_regression][gpu][textured_quad]") {
  auto fixtureResult = setUpTexturedQuadFixture(
      ATLANTIS_textured_quad_unorm_ARTIFACT_PATH, ATLANTIS_textured_quad_unorm_METADATA_PATH,
      ATLANTIS_textured_quad_srgb_ARTIFACT_PATH, ATLANTIS_textured_quad_srgb_METADATA_PATH,
      ATLANTIS_textured_quad_left_ARTIFACT_PATH, ATLANTIS_textured_quad_left_METADATA_PATH,
      ATLANTIS_textured_quad_right_ARTIFACT_PATH, ATLANTIS_textured_quad_right_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  TexturedQuadFixture& fixture = fixtureResult.value();

  auto frameResult = renderTexturedQuadFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();
  REQUIRE(frame.width == kTexturedQuadExtentPixels);
  REQUIRE(frame.height == kTexturedQuadExtentPixels);

  const auto leftSamples = sampleGrid(frame, kLeftXStart, kLeftXEnd, kYStart, kYEnd, kStep);
  const auto rightSamples = sampleGrid(frame, kRightXStart, kRightXEnd, kYStart, kYEnd, kStep);

  // Neither quad's own region is still the untouched background clear
  // color -- both quads were genuinely drawn into, not skipped.
  bool leftHasContent = false;
  for (const auto& pixel : leftSamples) {
    if (!nearBackgroundClearColor(pixel)) {
      leftHasContent = true;
      break;
    }
  }
  bool rightHasContent = false;
  for (const auto& pixel : rightSamples) {
    if (!nearBackgroundClearColor(pixel)) {
      rightHasContent = true;
      break;
    }
  }
  CHECK(leftHasContent);
  CHECK(rightHasContent);

  // Not a black frame and not garbage: every alpha byte in the captured
  // color image is 255 (Rgba8Unorm color attachment, no blending).
  for (std::uint32_t y = 0; y < frame.height; y += 32) {
    for (std::uint32_t x = 0; x < frame.width; x += 32) {
      CHECK(pixelAt(frame, x, y)[3] == 255);
    }
  }
}

TEST_CASE("Textured quad fixture shows a real Unorm-vs-Srgb difference at the same relative sample position",
          "[image_regression][gpu][textured_quad]") {
  auto fixtureResult = setUpTexturedQuadFixture(
      ATLANTIS_textured_quad_unorm_ARTIFACT_PATH, ATLANTIS_textured_quad_unorm_METADATA_PATH,
      ATLANTIS_textured_quad_srgb_ARTIFACT_PATH, ATLANTIS_textured_quad_srgb_METADATA_PATH,
      ATLANTIS_textured_quad_left_ARTIFACT_PATH, ATLANTIS_textured_quad_left_METADATA_PATH,
      ATLANTIS_textured_quad_right_ARTIFACT_PATH, ATLANTIS_textured_quad_right_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  TexturedQuadFixture& fixture = fixtureResult.value();

  auto frameResult = renderTexturedQuadFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();

  // Both quads are geometrically identical (same size, same UV mapping,
  // shifted only in X) and sample the same checkerboard source -- a
  // sample at the same relative (dx, dy) offset from each quad's own
  // left edge lands on the same nominal checkerboard cell in both. The
  // two quads differ only in which SampledTextureFormat their own
  // Material was created against (Rgba8Unorm vs Rgba8Srgb) -- sampling
  // an Rgba8Srgb image applies the GPU's own hardware sRGB-to-linear
  // decode before the shader ever sees the value, while Rgba8Unorm does
  // not; the render target itself is Rgba8Unorm (no re-encode on
  // write), so a genuine hardware difference must show up as different
  // captured byte values at matching relative positions, for at least
  // one of the sampled cells (a cell exactly at 0x00 or 0xFF has no
  // visible sRGB/linear difference, so more than one relative offset is
  // checked rather than relying on exactly one).
  const std::uint32_t leftWidth = kLeftXEnd - kLeftXStart;
  bool foundDifference = false;
  for (std::uint32_t dy = 0; dy < kYEnd - kYStart; dy += kStep) {
    for (std::uint32_t dx = 0; dx < leftWidth; dx += kStep) {
      const auto leftPixel = pixelAt(frame, kLeftXStart + dx, kYStart + dy);
      const auto rightPixel = pixelAt(frame, kRightXStart + dx, kYStart + dy);
      if (leftPixel[0] != rightPixel[0] || leftPixel[1] != rightPixel[1] || leftPixel[2] != rightPixel[2]) {
        foundDifference = true;
      }
    }
  }
  CHECK(foundDifference);
}

TEST_CASE("Textured quad fixture's captured frame differs from a freshly-cleared, undrawn baseline in both quad "
          "regions -- proof the RenderTarget is genuinely used",
          "[image_regression][gpu][textured_quad]") {
  auto fixtureResult = setUpTexturedQuadFixture(
      ATLANTIS_textured_quad_unorm_ARTIFACT_PATH, ATLANTIS_textured_quad_unorm_METADATA_PATH,
      ATLANTIS_textured_quad_srgb_ARTIFACT_PATH, ATLANTIS_textured_quad_srgb_METADATA_PATH,
      ATLANTIS_textured_quad_left_ARTIFACT_PATH, ATLANTIS_textured_quad_left_METADATA_PATH,
      ATLANTIS_textured_quad_right_ARTIFACT_PATH, ATLANTIS_textured_quad_right_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  TexturedQuadFixture& fixture = fixtureResult.value();

  auto baselineResult = renderTexturedQuadBaselineFrame(fixture);
  REQUIRE(baselineResult.isOk());
  const PixelBuffer& baseline = baselineResult.value();

  auto frameResult = renderTexturedQuadFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();

  const auto baselineLeft = sampleGrid(baseline, kLeftXStart, kLeftXEnd, kYStart, kYEnd, kStep);
  const auto frameLeft = sampleGrid(frame, kLeftXStart, kLeftXEnd, kYStart, kYEnd, kStep);
  REQUIRE(baselineLeft.size() == frameLeft.size());
  bool leftDiffers = false;
  for (std::size_t i = 0; i < baselineLeft.size(); ++i) {
    if (baselineLeft[i] != frameLeft[i]) leftDiffers = true;
  }
  CHECK(leftDiffers);

  const auto baselineRight = sampleGrid(baseline, kRightXStart, kRightXEnd, kYStart, kYEnd, kStep);
  const auto frameRight = sampleGrid(frame, kRightXStart, kRightXEnd, kYStart, kYEnd, kStep);
  REQUIRE(baselineRight.size() == frameRight.size());
  bool rightDiffers = false;
  for (std::size_t i = 0; i < baselineRight.size(); ++i) {
    if (baselineRight[i] != frameRight[i]) rightDiffers = true;
  }
  CHECK(rightDiffers);
}

TEST_CASE("Full capture-compare cycle against the committed textured_quad golden passes",
          "[image_regression][gpu][textured_quad]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  const std::filesystem::path actualArtifact = outputDir / (std::string(kTexturedQuadGoldenSlug) + "_actual.png");
  const std::filesystem::path diffArtifact = outputDir / (std::string(kTexturedQuadGoldenSlug) + "_diff.png");
  std::filesystem::remove(actualArtifact);
  std::filesystem::remove(diffArtifact);

  auto fixtureResult = setUpTexturedQuadFixture(
      ATLANTIS_textured_quad_unorm_ARTIFACT_PATH, ATLANTIS_textured_quad_unorm_METADATA_PATH,
      ATLANTIS_textured_quad_srgb_ARTIFACT_PATH, ATLANTIS_textured_quad_srgb_METADATA_PATH,
      ATLANTIS_textured_quad_left_ARTIFACT_PATH, ATLANTIS_textured_quad_left_METADATA_PATH,
      ATLANTIS_textured_quad_right_ARTIFACT_PATH, ATLANTIS_textured_quad_right_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  TexturedQuadFixture& fixture = fixtureResult.value();

  auto renderResult = renderTexturedQuadFrame(fixture);
  REQUIRE(renderResult.isOk());
  const PixelBuffer& actual = renderResult.value();

  auto goldenResult =
      loadAndValidateGolden(goldenPngPath(kTexturedQuadGoldenName), goldenSidecarPath(kTexturedQuadGoldenName));
  {
    INFO("INVALID GOLDEN: the committed textured_quad golden must load and validate cleanly");
    REQUIRE(goldenResult.isOk());
  }
  const auto& validatedGolden = goldenResult.value();

  REQUIRE(actual.width == validatedGolden.pixels.width);
  REQUIRE(actual.height == validatedGolden.pixels.height);

  const auto report = compareBuffers(actual, validatedGolden.pixels);
  if (!report.passed) {
    (void)writeFailureArtifacts(outputDir, kTexturedQuadGoldenSlug, actual, validatedGolden.pixels);
  }
  REQUIRE(report.passed);

  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Textured quad fixture: repeated render cycles against the same fixture succeed independently",
          "[image_regression][gpu][textured_quad]") {
  auto fixtureResult = setUpTexturedQuadFixture(
      ATLANTIS_textured_quad_unorm_ARTIFACT_PATH, ATLANTIS_textured_quad_unorm_METADATA_PATH,
      ATLANTIS_textured_quad_srgb_ARTIFACT_PATH, ATLANTIS_textured_quad_srgb_METADATA_PATH,
      ATLANTIS_textured_quad_left_ARTIFACT_PATH, ATLANTIS_textured_quad_left_METADATA_PATH,
      ATLANTIS_textured_quad_right_ARTIFACT_PATH, ATLANTIS_textured_quad_right_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  TexturedQuadFixture& fixture = fixtureResult.value();

  auto firstResult = renderTexturedQuadFrame(fixture);
  REQUIRE(firstResult.isOk());
  auto secondResult = renderTexturedQuadFrame(fixture);
  REQUIRE(secondResult.isOk());

  CHECK(firstResult.value().rgba8 == secondResult.value().rgba8);
}
