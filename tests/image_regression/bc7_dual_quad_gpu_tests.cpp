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

// Spec 0038/Plan 0038 Milestone 3b: the BC7 dual-quad golden's own
// GPU-required coverage -- the exact Spec 0016 textured-quad pattern
// (structural non-degeneracy, same-relative-position Unorm-vs-Srgb
// difference, baseline-render-target proof, determinism, golden compare),
// re-pointed at the two Bc7-layout fixtures (real Bistro streetlights BC7
// data; the sRGB variant's block bytes are byte-identical, only the DDS
// dxgiFormat header field differs). This proves the entire
// cook(schema v2) -> load -> create(Bc7*) -> block-aware upload ->
// bind -> sample path end-to-end on real Vulkan hardware, and that the
// GPU's own hardware sRGB decode is visibly, measurably active on BC7
// texels -- ADR-0057's "proof, real and GPU-based" standard, reused.

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
  const int tolerance = 6;
  return std::abs(static_cast<int>(pixel[0]) - 13) <= tolerance &&
         std::abs(static_cast<int>(pixel[1]) - 13) <= tolerance &&
         std::abs(static_cast<int>(pixel[2]) - 20) <= tolerance;
}

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

constexpr const char* kBc7GoldenName = "bc7_dual_quad/bc7_dual_quad_512x512_rgba8unorm";
constexpr const char* kBc7GoldenSlug = "bc7_dual_quad_512x512_rgba8unorm";

[[nodiscard]] std::filesystem::path goldenPngPath(const std::string& goldenName) {
  return std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".png");
}

[[nodiscard]] std::filesystem::path goldenSidecarPath(const std::string& goldenName) {
  return std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".sidecar.txt");
}

[[nodiscard]] atlantis::Result<TexturedQuadFixture, atlantis::image_regression::TexturedQuadSetupError>
setUpBc7Fixture() {
  return setUpTexturedQuadFixture(
      ATLANTIS_bc7_stringlights_unorm_ARTIFACT_PATH, ATLANTIS_bc7_stringlights_unorm_METADATA_PATH,
      ATLANTIS_bc7_stringlights_srgb_ARTIFACT_PATH, ATLANTIS_bc7_stringlights_srgb_METADATA_PATH,
      ATLANTIS_textured_quad_left_ARTIFACT_PATH, ATLANTIS_textured_quad_left_METADATA_PATH,
      ATLANTIS_textured_quad_right_ARTIFACT_PATH, ATLANTIS_textured_quad_right_METADATA_PATH,
      atlantis::rhi::SampledTextureFormat::Bc7Unorm, atlantis::rhi::SampledTextureFormat::Bc7Srgb);
}

}  // namespace

TEST_CASE("BC7 dual quad renders a non-degenerate frame with both quads visible",
          "[image_regression][gpu][bc7_dual_quad]") {
  auto fixtureResult = setUpBc7Fixture();
  REQUIRE(fixtureResult.isOk());
  TexturedQuadFixture& fixture = fixtureResult.value();

  auto frameResult = renderTexturedQuadFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();
  REQUIRE(frame.width == kTexturedQuadExtentPixels);
  REQUIRE(frame.height == kTexturedQuadExtentPixels);

  const auto leftSamples = sampleGrid(frame, kLeftXStart, kLeftXEnd, kYStart, kYEnd, kStep);
  const auto rightSamples = sampleGrid(frame, kRightXStart, kRightXEnd, kYStart, kYEnd, kStep);

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

  // Not a black frame and not garbage. Unlike the checkerboard source,
  // the streetlights photo carries its own alpha channel (bulb-sprite
  // cutouts -- genuinely transparent texels legitimately reach the
  // opaque target as sampled alpha), so the alpha check is restricted
  // to the four frame corners, which lie outside both quads and must
  // still hold the clear color's own alpha=255.
  CHECK(pixelAt(frame, 2, 2)[3] == 255);
  CHECK(pixelAt(frame, frame.width - 3, 2)[3] == 255);
  CHECK(pixelAt(frame, 2, frame.height - 3)[3] == 255);
  CHECK(pixelAt(frame, frame.width - 3, frame.height - 3)[3] == 255);
}

TEST_CASE("BC7 dual quad shows a real hardware sRGB decode difference on identical block data",
          "[image_regression][gpu][bc7_dual_quad]") {
  auto fixtureResult = setUpBc7Fixture();
  REQUIRE(fixtureResult.isOk());
  TexturedQuadFixture& fixture = fixtureResult.value();

  auto frameResult = renderTexturedQuadFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();

  // Both quads sample byte-identical BC7 blocks (only the DDS dxgiFormat
  // header field differs between the two fixtures); the quads differ
  // only in Bc7Unorm vs Bc7Srgb. A captured byte difference at matching
  // relative positions is therefore the GPU's own sRGB-to-linear decode
  // acting on BC7 texels -- the exact proof Spec 0038's testing plan
  // names, and this test's own reason to exist.
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

TEST_CASE("BC7 dual quad's captured frame differs from a cleared baseline in both quad regions",
          "[image_regression][gpu][bc7_dual_quad]") {
  auto fixtureResult = setUpBc7Fixture();
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

TEST_CASE("BC7 dual quad: repeated render cycles against the same fixture succeed independently",
          "[image_regression][gpu][bc7_dual_quad]") {
  auto fixtureResult = setUpBc7Fixture();
  REQUIRE(fixtureResult.isOk());
  TexturedQuadFixture& fixture = fixtureResult.value();

  auto firstResult = renderTexturedQuadFrame(fixture);
  REQUIRE(firstResult.isOk());
  auto secondResult = renderTexturedQuadFrame(fixture);
  REQUIRE(secondResult.isOk());

  CHECK(firstResult.value().rgba8 == secondResult.value().rgba8);
}

TEST_CASE("Full capture-compare cycle against the committed BC7 dual-quad golden passes",
          "[image_regression][gpu][bc7_dual_quad]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  const std::filesystem::path actualArtifact = outputDir / (std::string(kBc7GoldenSlug) + "_actual.png");
  const std::filesystem::path diffArtifact = outputDir / (std::string(kBc7GoldenSlug) + "_diff.png");
  std::filesystem::remove(actualArtifact);
  std::filesystem::remove(diffArtifact);

  auto fixtureResult = setUpBc7Fixture();
  REQUIRE(fixtureResult.isOk());
  TexturedQuadFixture& fixture = fixtureResult.value();

  auto renderResult = renderTexturedQuadFrame(fixture);
  REQUIRE(renderResult.isOk());
  const PixelBuffer& actual = renderResult.value();

  auto goldenResult = loadAndValidateGolden(goldenPngPath(kBc7GoldenName), goldenSidecarPath(kBc7GoldenName));
  {
    INFO("INVALID GOLDEN: the committed BC7 dual-quad golden must load and validate cleanly");
    REQUIRE(goldenResult.isOk());
  }
  const auto& validatedGolden = goldenResult.value();

  REQUIRE(actual.width == validatedGolden.pixels.width);
  REQUIRE(actual.height == validatedGolden.pixels.height);

  const auto report = compareBuffers(actual, validatedGolden.pixels);
  if (!report.passed) {
    (void)writeFailureArtifacts(outputDir, kBc7GoldenSlug, actual, validatedGolden.pixels);
  }
  REQUIRE(report.passed);

  REQUIRE(fixture.device->waitIdle().isOk());
}
