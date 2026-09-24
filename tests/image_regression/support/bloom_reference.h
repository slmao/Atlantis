#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

// Plan 0044 P11 (Spec 0044 R3, ADR-0092 Decision 2): a CPU-side reference
// for bloom's D1 bright-pass and firefly weight -- literal transcriptions
// of what shaders/bloom_downsample/bloom_downsample.slang applies, never a
// shader invocation. The fog_reference.h / tone_mapping_reference.h
// precedent: unit-tested directly, and usable by GPU tests for
// expectations. The chain's blur is not modelled here -- halo shape is
// checked by property tests and goldens, not analytically.

namespace atlantis::image_regression {

using BloomRgb = std::array<float, 3>;

// renderer::kBloomHighlight (src/renderer/include/atlantis/renderer/bloom.h)
// and the shader's own copy -- Filament's `highlight` default. Kept as an
// independent copy: this GPU-independent support library does not link the
// Renderer.
inline constexpr float kBloomReferenceHighlight = 1000.0f;

[[nodiscard]] inline float bloomMax3(const BloomRgb& c) { return std::max(c[0], std::max(c[1], c[2])); }

// Per channel b = max(c - threshold, 0), then b * 1 / (1 + max3(b) / 1000).
// At or below the knee every channel is exactly 0.
[[nodiscard]] inline BloomRgb bloomBrightPass(const BloomRgb& c, float threshold) {
  BloomRgb bright{};
  for (std::size_t i = 0; i < 3; ++i) bright[i] = std::max(c[i] - threshold, 0.0f);
  const float compression = 1.0f / (1.0f + bloomMax3(bright) / kBloomReferenceHighlight);
  for (float& channel : bright) channel *= compression;
  return bright;
}

// The D1 firefly weight applied to each 2x2 box average before the boxes
// are combined and renormalised: 1 / (1 + max3(box)). 1 for a black box,
// falling toward 0 as the box gets brighter.
[[nodiscard]] inline float bloomFireflyWeight(const BloomRgb& box) { return 1.0f / (1.0f + bloomMax3(box)); }

}  // namespace atlantis::image_regression
