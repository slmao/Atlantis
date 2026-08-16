#pragma once

#include <cstdint>
#include <vector>

namespace atlantis::image_regression {

struct PixelBuffer {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  // Tightly packed RGBA8, width * height * 4 bytes -- the exact layout
  // OffscreenTarget's readback Buffer and a decoded PNG both already
  // share (ADR-0040 / ADR-0041's "no vertical flip" note).
  std::vector<std::uint8_t> rgba8;
};

struct ComparisonReport {
  bool passed = false;
  std::uint32_t maxChannelDiff = 0;
  double meanAbsoluteDiff = 0.0;
  std::uint64_t outOfToleranceCount = 0;
  double outOfTolerancePercentage = 0.0;
};

// ADR-0042: confirmed, not configurable -- no function below accepts a
// caller-supplied tolerance or budget override.
inline constexpr std::uint8_t kChannelTolerance = 0;
inline constexpr std::uint64_t kFailingPixelBudget = 0;

// Preconditions: actual.width == golden.width && actual.height ==
// golden.height (format/extent mismatch is the caller's job to check
// first -- see golden_validity.h; this function assumes matching shape
// and asserts it, ATLANTIS_CHECK, not a Result -- a shape mismatch
// reaching this function is a caller precondition violation, not a
// recoverable comparison outcome).
[[nodiscard]] ComparisonReport compareBuffers(const PixelBuffer& actual, const PixelBuffer& golden);

// ADR-0042's own "Failure output" contract: a per-pixel absolute
// difference visualization, amplified for visibility. Same
// preconditions/shape-assertion as compareBuffers() above. RGB channels
// carry min(255, |actual-golden| * kDiffAmplificationFactor); alpha is
// always forced to 255 regardless of any alpha difference, so an
// all-identical pair renders as fully opaque black (0,0,0,255) in an
// ordinary image viewer, not fully transparent. Pure function -- writes
// nothing to disk itself; see golden_validity.h's writeFailureArtifacts()
// for the disk-writing step.
inline constexpr int kDiffAmplificationFactor = 16;
[[nodiscard]] PixelBuffer computeDiffVisualization(const PixelBuffer& actual, const PixelBuffer& golden);

}  // namespace atlantis::image_regression
