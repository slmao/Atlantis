#include "support/pixel_diff.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using atlantis::image_regression::compareBuffers;
using atlantis::image_regression::computeDiffVisualization;
using atlantis::image_regression::kChannelTolerance;
using atlantis::image_regression::kDiffAmplificationFactor;
using atlantis::image_regression::kFailingPixelBudget;
using atlantis::image_regression::PixelBuffer;

namespace {

[[nodiscard]] PixelBuffer makeSolidBuffer(std::uint32_t width, std::uint32_t height, std::uint8_t r, std::uint8_t g,
                                           std::uint8_t b, std::uint8_t a) {
  PixelBuffer buffer;
  buffer.width = width;
  buffer.height = height;
  buffer.rgba8.resize(static_cast<std::size_t>(width) * height * 4);
  for (std::size_t pixel = 0; pixel < buffer.rgba8.size() / 4; ++pixel) {
    buffer.rgba8[pixel * 4 + 0] = r;
    buffer.rgba8[pixel * 4 + 1] = g;
    buffer.rgba8[pixel * 4 + 2] = b;
    buffer.rgba8[pixel * 4 + 3] = a;
  }
  return buffer;
}

}  // namespace

TEST_CASE("kChannelTolerance and kFailingPixelBudget are both confirmed zero", "[image_regression][pixel_diff]") {
  REQUIRE(kChannelTolerance == 0);
  REQUIRE(kFailingPixelBudget == 0);
}

TEST_CASE("compareBuffers: two identical buffers pass", "[image_regression][pixel_diff]") {
  const PixelBuffer actual = makeSolidBuffer(4, 4, 10, 20, 30, 255);
  const PixelBuffer golden = makeSolidBuffer(4, 4, 10, 20, 30, 255);

  const auto report = compareBuffers(actual, golden);

  REQUIRE(report.passed);
  REQUIRE(report.maxChannelDiff == 0);
  REQUIRE(report.meanAbsoluteDiff == 0.0);
  REQUIRE(report.outOfToleranceCount == 0);
  REQUIRE(report.outOfTolerancePercentage == 0.0);
}

TEST_CASE("compareBuffers: a single differing pixel anywhere fails", "[image_regression][pixel_diff]") {
  PixelBuffer actual = makeSolidBuffer(4, 4, 10, 20, 30, 255);
  const PixelBuffer golden = makeSolidBuffer(4, 4, 10, 20, 30, 255);

  // One channel, one pixel, off by exactly 1 -- confirms
  // kFailingPixelBudget is genuinely 0, not merely small.
  actual.rgba8[(2 * 4 + 1) * 4 + 0] = 11;

  const auto report = compareBuffers(actual, golden);

  REQUIRE_FALSE(report.passed);
  REQUIRE(report.outOfToleranceCount == 1);
}

TEST_CASE("compareBuffers: a known, constructed set of differing pixels produces the expected report",
          "[image_regression][pixel_diff]") {
  PixelBuffer actual = makeSolidBuffer(2, 2, 0, 0, 0, 255);
  const PixelBuffer golden = makeSolidBuffer(2, 2, 0, 0, 0, 255);

  // Pixel 0: red channel differs by 5.
  actual.rgba8[0 * 4 + 0] = 5;
  // Pixel 2: blue channel differs by 200 (the largest single diff).
  actual.rgba8[2 * 4 + 2] = 200;

  const auto report = compareBuffers(actual, golden);

  REQUIRE_FALSE(report.passed);
  REQUIRE(report.maxChannelDiff == 200);
  REQUIRE(report.outOfToleranceCount == 2);
  REQUIRE(report.outOfTolerancePercentage == 50.0);
  // Sum of absolute diffs across all 16 bytes (2x2x4) is 5 + 200 = 205.
  REQUIRE(report.meanAbsoluteDiff == Catch::Approx(205.0 / 16.0));
}

TEST_CASE("computeDiffVisualization: identical buffers produce an all-opaque-black diff image",
          "[image_regression][pixel_diff]") {
  const PixelBuffer actual = makeSolidBuffer(2, 2, 42, 84, 126, 255);
  const PixelBuffer golden = makeSolidBuffer(2, 2, 42, 84, 126, 255);

  const PixelBuffer diff = computeDiffVisualization(actual, golden);

  REQUIRE(diff.width == 2);
  REQUIRE(diff.height == 2);
  for (std::size_t pixel = 0; pixel < diff.rgba8.size() / 4; ++pixel) {
    REQUIRE(diff.rgba8[pixel * 4 + 0] == 0);
    REQUIRE(diff.rgba8[pixel * 4 + 1] == 0);
    REQUIRE(diff.rgba8[pixel * 4 + 2] == 0);
    REQUIRE(diff.rgba8[pixel * 4 + 3] == 255);
  }
}

TEST_CASE("computeDiffVisualization: RGB channels are amplified and clamp at 255, alpha stays opaque",
          "[image_regression][pixel_diff]") {
  PixelBuffer actual = makeSolidBuffer(1, 1, 10, 0, 0, 0);
  const PixelBuffer golden = makeSolidBuffer(1, 1, 0, 0, 0, 255);
  // Red differs by 10 -> amplified to 10 * kDiffAmplificationFactor.
  // Alpha differs by 255, but is always forced to 255 regardless.

  const PixelBuffer diff = computeDiffVisualization(actual, golden);

  REQUIRE(diff.rgba8[0] == static_cast<std::uint8_t>(std::min(255, 10 * kDiffAmplificationFactor)));
  REQUIRE(diff.rgba8[1] == 0);
  REQUIRE(diff.rgba8[2] == 0);
  REQUIRE(diff.rgba8[3] == 255);
}
