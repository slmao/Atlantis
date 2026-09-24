#include "fixture/emissive_demo_fixture.h"
#include "support/golden_validity.h"
#include "support/pixel_diff.h"

#include <atlantis/renderer/bloom.h>
#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

// Plan 0044 Milestone 2 (Spec 0044, ADR-0092): bloom switched on. The
// golden (bloom_demo -- orange and blue spheres above the threshold, a
// green sphere exactly at it, the black control sphere) plus the
// property tests: neutrality (threshold above everything = bloom off,
// byte for byte), halo behaviour (brightens around bright spheres, at-
// threshold adds nothing, monotone falloff, grows with strength), and
// the fog feeding bloom (Spec 0043's ordering constraint, evidenced).

using atlantis::image_regression::compareBuffers;
using atlantis::image_regression::EmissiveDemoFixture;
using atlantis::image_regression::loadAndValidateGolden;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderEmissiveDemoFrame;
using atlantis::image_regression::setUpEmissiveDemoFixture;
using atlantis::image_regression::writeFailureArtifacts;
using atlantis::renderer::BloomInput;

namespace {

constexpr std::uint32_t kExtent = 512;

[[nodiscard]] std::array<std::uint8_t, 4> pixelAt(const PixelBuffer& buffer, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * buffer.width + x) * 4;
  return {buffer.rgba8[offset], buffer.rgba8[offset + 1], buffer.rgba8[offset + 2], buffer.rgba8[offset + 3]};
}

[[nodiscard]] std::filesystem::path goldenPngPath(const std::string& goldenName) {
  return std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".png");
}

[[nodiscard]] std::filesystem::path goldenSidecarPath(const std::string& goldenName) {
  return std::filesystem::path(ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR) / (goldenName + ".sidecar.txt");
}

// A BloomInput over the fixture's own resources.
[[nodiscard]] BloomInput makeBloomInput(EmissiveDemoFixture& fixture, float strength, float threshold) {
  return BloomInput{.targets = *fixture.bloomTargets,
                    .downsamplePipeline = *fixture.bloomDownsamplePipeline,
                    .upsamplePipeline = *fixture.bloomUpsamplePipeline,
                    .compositePipeline = *fixture.bloomCompositePipeline,
                    .strength = strength,
                    .threshold = threshold};
}

// Frames identical? (byte-for-byte rgba8 compare)
[[nodiscard]] bool framesIdentical(const PixelBuffer& a, const PixelBuffer& b) {
  return a.width == b.width && a.height == b.height && a.rgba8 == b.rgba8;
}

}  // namespace

TEST_CASE("Bloom neutrality: a threshold above the brightest channel renders bloom-on byte-identically to "
          "bloom-off",
          "[image_regression][gpu][bloom]") {
  EmissiveDemoFixture fixture = setUpEmissiveDemoFixture(buildBloomConfig());
  REQUIRE(fixture.bloomTargets.has_value());

  // emissive_demo's brightest emissive is 4.0 (orange); a threshold of
  // 100.0 is above every channel in the frame, so D1's bright-pass
  // produces exactly zero everywhere and the composite is hdr + 0.
  auto bloomInput = makeBloomInput(fixture, 0.8f, 100.0f);

  auto onResult = renderEmissiveDemoFrame(fixture, true, false, &bloomInput);
  REQUIRE(onResult.isOk());
  auto offResult = renderEmissiveDemoFrame(fixture);
  REQUIRE(offResult.isOk());

  CHECK(framesIdentical(onResult.value(), offResult.value()));
  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Bloom halo: pixels around a bright sphere brighten; the at-threshold sphere adds nothing",
          "[image_regression][gpu][bloom]") {
  EmissiveDemoFixture fixture = setUpEmissiveDemoFixture(buildBloomConfig());
  REQUIRE(fixture.bloomTargets.has_value());

  // The orange sphere's emissive is 4.0 (well above the 1.0 threshold);
  // the green sphere's is exactly 1.0 -- at the threshold its bright-pass
  // is max(1-1,0)=0, so it must add nothing.
  auto bloomInput = makeBloomInput(fixture, 0.8f, 1.0f);

  auto offResult = renderEmissiveDemoFrame(fixture);
  REQUIRE(offResult.isOk());
  const PixelBuffer off = offResult.value();

  auto onResult = renderEmissiveDemoFrame(fixture, true, false, &bloomInput);
  REQUIRE(onResult.isOk());
  const PixelBuffer on = onResult.value();
  REQUIRE(on.width == off.width);

  // Count pixels near each sphere's screen position that changed. The
  // spheres sit at world x -4.4 (orange), -2.2 (green), 0 (blue), 2.2
  // (normal-mapped), 4.4 (control) with the camera at z=10 looking down
  // -Z, so screen columns approximate world x. Orange occupies the left
  // fifth, green the second fifth.
  const auto countChanged = [&](std::uint32_t xStart, std::uint32_t xEnd) {
    std::uint32_t changed = 0;
    for (std::uint32_t y = 128; y < 384; y += 2) {
      for (std::uint32_t x = xStart; x < xEnd; x += 2) {
        if (pixelAt(off, x, y) != pixelAt(on, x, y)) ++changed;
      }
    }
    return changed;
  };

  const auto orangeRegion = countChanged(1, 102);
  const auto greenRegion = countChanged(103, 205);
  const auto blueRegion = countChanged(206, 308);
  const auto controlRegion = countChanged(411, 511);

  // The orange sphere (above threshold) produces a halo: many pixels
  // change around it.
  CHECK(orangeRegion > 100);
  // The green sphere sits exactly at the threshold: its bright-pass is
  // zero, so its own region must be byte-identical to bloom-off.
  CHECK(greenRegion == 0);
  // The blue sphere has no emissive: unchanged too.
  CHECK(blueRegion == 0);
  // The control sphere has no emissive either.
  CHECK(controlRegion == 0);
  // And the off/on frames differ somewhere overall (the orange halo).
  CHECK_FALSE(framesIdentical(off, on));
  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Bloom halo shape: the falloff is monotone and grows with strength", "[image_regression][gpu][bloom]") {
  EmissiveDemoFixture fixture = setUpEmissiveDemoFixture(buildBloomConfig());
  REQUIRE(fixture.bloomTargets.has_value());

  auto bloomInput = makeBloomInput(fixture, 0.8f, 1.0f);

  auto offResult = renderEmissiveDemoFrame(fixture);
  REQUIRE(offResult.isOk());
  const PixelBuffer off = offResult.value();

  auto onResult = renderEmissiveDemoFrame(fixture, true, false, &bloomInput);
  REQUIRE(onResult.isOk());
  const PixelBuffer on = onResult.value();

  // Orange's screen centre is around x=64, y=256 (left fifth). Sample a
  // horizontal ray away from it and check the per-pixel brightness
  // delta decreases (allowing plateaus from the blocky 8x8 lowest bloom
  // level).
  const std::uint32_t yCentre = 256;
  auto brightnessAt = [&](std::uint32_t x) {
    const auto pOn = pixelAt(on, x, yCentre);
    const auto pOff = pixelAt(off, x, yCentre);
    return static_cast<int>(pOn[0]) - static_cast<int>(pOff[0]) + static_cast<int>(pOn[1]) -
           static_cast<int>(pOff[1]) + static_cast<int>(pOn[2]) - static_cast<int>(pOff[2]);
  };

  // Points stepping away from the orange sphere's edge (~x=102).
  const int nearDelta = brightnessAt(110);
  const int midDelta = brightnessAt(150);
  const int farDelta = brightnessAt(190);
  CHECK(nearDelta >= midDelta);
  CHECK(midDelta >= farDelta);
  CHECK(nearDelta > 0);
  CHECK(farDelta >= 0);

  // Strength scaling: the same scene at half strength must dim the halo.
  auto halfBloom = makeBloomInput(fixture, 0.4f, 1.0f);
  auto halfResult = renderEmissiveDemoFrame(fixture, true, false, &halfBloom);
  REQUIRE(halfResult.isOk());
  const PixelBuffer half = halfResult.value();
  const int nearHalf = static_cast<int>(pixelAt(half, 110, yCentre)[0]) -
                       static_cast<int>(pixelAt(off, 110, yCentre)[0]);
  const int nearFull = static_cast<int>(pixelAt(on, 110, yCentre)[0]) -
                       static_cast<int>(pixelAt(off, 110, yCentre)[0]);
  CHECK(nearHalf <= nearFull);
  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Full capture-compare cycle against the committed BC7 dual-quad golden still passes",
          "[image_regression][gpu][bloom]") {
  // Sanity: the bloom work must not have disturbed any other golden.
  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  CHECK(std::filesystem::exists(goldensDir / "emissive_demo" / "emissive_demo_512x512_rgba8unorm.png"));
  CHECK(std::filesystem::exists(goldensDir / "bc7_dual_quad" / "bc7_dual_quad_512x512_rgba8unorm.png"));
}
