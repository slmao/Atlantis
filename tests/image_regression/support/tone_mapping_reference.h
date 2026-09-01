#pragma once

#include <cmath>

// Plan 0024 Milestone 8 (ADR-0068 D-5/D-6): CPU-side reference
// implementations of the output-transform shaders' own exact math --
// literal, hand-verifiable transcriptions of the same formulas
// shaders/output_transform_unorm/output_transform_unorm.slang and
// shaders/output_transform_srgb/output_transform_srgb.slang apply,
// never a shader invocation of any kind. Used two ways: (1) directly
// unit-tested against hand-computed values (GPU-independent); (2) by
// real-GPU tests to compute the pixel value a captured frame is
// expected to show, compared against what was actually captured
// (display-equivalence, roll-off).

namespace atlantis::image_regression {

// ADR-0068 D-5: fixed-exposure Reinhard tone-mapping, per channel.
//   x = max(x, 0)                    -- floor only, defensive (D-5's
//                                        own note: radiance is never
//                                        negative by construction)
//   exposed = x * kBaselineExposure  -- kBaselineExposure = 1.0, no
//                                        auto-exposure
//   tonemapped = exposed / (1 + exposed)
inline constexpr float kBaselineExposure = 1.0f;

[[nodiscard]] inline float reinhardTonemap(float linearValue) {
  const float floored = linearValue > 0.0f ? linearValue : 0.0f;
  const float exposed = floored * kBaselineExposure;
  return exposed / (1.0f + exposed);
}

// ADR-0068 D-6: the exact piecewise sRGB OETF, applied only by the
// *_Unorm output-transform variant (the *_Srgb variant writes
// reinhardTonemap()'s own output unencoded, relying on the GPU's
// fixed-function output-merger to perform the identical encode in
// hardware on store into a VK_FORMAT_*_SRGB view).
[[nodiscard]] inline float srgbOetf(float linearValue) {
  if (linearValue <= 0.0031308f) return linearValue * 12.92f;
  return 1.055f * std::pow(linearValue, 1.0f / 2.4f) - 0.055f;
}

// Convenience composition matching the *_Unorm variant's own complete
// per-channel pipeline (D-5 then D-6) end to end.
[[nodiscard]] inline float tonemapAndEncodeUnorm(float linearValue) { return srgbOetf(reinhardTonemap(linearValue)); }

}  // namespace atlantis::image_regression
