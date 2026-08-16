#include "pixel_diff.h"

#include <atlantis/assert.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace atlantis::image_regression {

ComparisonReport compareBuffers(const PixelBuffer& actual, const PixelBuffer& golden) {
  ATLANTIS_CHECK(actual.width == golden.width);
  ATLANTIS_CHECK(actual.height == golden.height);
  ATLANTIS_CHECK(actual.rgba8.size() == golden.rgba8.size());

  ComparisonReport report;
  const std::uint64_t pixelCount = static_cast<std::uint64_t>(actual.width) * actual.height;
  std::uint64_t pixelsOutOfTolerance = 0;
  double sumAbsoluteDiff = 0.0;

  for (std::uint64_t pixel = 0; pixel < pixelCount; ++pixel) {
    bool pixelOutOfTolerance = false;
    for (int channel = 0; channel < 4; ++channel) {
      const std::size_t index = static_cast<std::size_t>(pixel) * 4 + static_cast<std::size_t>(channel);
      const int diff = std::abs(static_cast<int>(actual.rgba8[index]) - static_cast<int>(golden.rgba8[index]));
      report.maxChannelDiff = std::max(report.maxChannelDiff, static_cast<std::uint32_t>(diff));
      sumAbsoluteDiff += diff;
      if (diff > kChannelTolerance) pixelOutOfTolerance = true;
    }
    if (pixelOutOfTolerance) ++pixelsOutOfTolerance;
  }

  report.outOfToleranceCount = pixelsOutOfTolerance;
  report.outOfTolerancePercentage =
      pixelCount == 0 ? 0.0 : (static_cast<double>(pixelsOutOfTolerance) / static_cast<double>(pixelCount)) * 100.0;
  report.meanAbsoluteDiff = actual.rgba8.empty() ? 0.0 : sumAbsoluteDiff / static_cast<double>(actual.rgba8.size());
  report.passed = report.outOfToleranceCount <= kFailingPixelBudget;
  return report;
}

PixelBuffer computeDiffVisualization(const PixelBuffer& actual, const PixelBuffer& golden) {
  ATLANTIS_CHECK(actual.width == golden.width);
  ATLANTIS_CHECK(actual.height == golden.height);
  ATLANTIS_CHECK(actual.rgba8.size() == golden.rgba8.size());

  PixelBuffer diff;
  diff.width = actual.width;
  diff.height = actual.height;
  diff.rgba8.resize(actual.rgba8.size());

  const std::uint64_t pixelCount = static_cast<std::uint64_t>(actual.width) * actual.height;
  for (std::uint64_t pixel = 0; pixel < pixelCount; ++pixel) {
    for (int channel = 0; channel < 3; ++channel) {
      const std::size_t index = static_cast<std::size_t>(pixel) * 4 + static_cast<std::size_t>(channel);
      const int rawDiff = std::abs(static_cast<int>(actual.rgba8[index]) - static_cast<int>(golden.rgba8[index]));
      diff.rgba8[index] = static_cast<std::uint8_t>(std::min(255, rawDiff * kDiffAmplificationFactor));
    }
    diff.rgba8[static_cast<std::size_t>(pixel) * 4 + 3] = 255;
  }

  return diff;
}

}  // namespace atlantis::image_regression
