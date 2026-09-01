#include "support/tone_mapping_reference.h"

#include <cmath>

#include <catch2/catch_test_macros.hpp>

// Plan 0024 Milestone 8 (ADR-0068 D-5/D-6): GPU-independent, hand-
// verified coverage for reinhardTonemap()/srgbOetf() -- no shader, no
// GPU, no image anywhere in this file.

using atlantis::image_regression::reinhardTonemap;
using atlantis::image_regression::srgbOetf;
using atlantis::image_regression::tonemapAndEncodeUnorm;

namespace {
[[nodiscard]] bool withinAbs(float actual, float expected, float epsilon) {
  return std::abs(actual - expected) <= epsilon;
}
}  // namespace

TEST_CASE("reinhardTonemap: negative input floors to 0 before exposure/Reinhard, tonemaps to 0",
          "[image_regression][tone_mapping]") {
  // D-5's own floor -- radiance is never negative by construction, but
  // the floor is a defensive guard against a theoretically-impossible
  // negative geometry-pass output. -0.5 -> floored to 0 -> exposed 0 ->
  // 0/(1+0) = 0.
  CHECK(withinAbs(reinhardTonemap(-0.5f), 0.0f, 1e-6f));
  CHECK(withinAbs(reinhardTonemap(-1000.0f), 0.0f, 1e-6f));
}

TEST_CASE("reinhardTonemap: below-1.0 input", "[image_regression][tone_mapping]") {
  // x = 0.5 -> exposed = 0.5 (kBaselineExposure = 1.0) -> 0.5/1.5 = 1/3.
  CHECK(withinAbs(reinhardTonemap(0.5f), 1.0f / 3.0f, 1e-5f));
}

TEST_CASE("reinhardTonemap: exactly-1.0 input", "[image_regression][tone_mapping]") {
  // x = 1.0 -> exposed = 1.0 -> 1/(1+1) = 0.5.
  CHECK(withinAbs(reinhardTonemap(1.0f), 0.5f, 1e-6f));
}

TEST_CASE("reinhardTonemap: well-above-1.0 input rolls off, never hard-clips",
          "[image_regression][tone_mapping]") {
  // x = 10.0 -> exposed = 10.0 -> 10/11 = 0.909090...
  CHECK(withinAbs(reinhardTonemap(10.0f), 10.0f / 11.0f, 1e-5f));
  // A genuinely large input still produces a value strictly less than
  // 1.0, never clipped to exactly 1.0 by the curve itself (D-5's own
  // "any positive value, however large, still reaches the curve
  // unclipped") -- x/(1+x) -> 1 only in the limit, never reaches it.
  const float huge = reinhardTonemap(1'000'000.0f);
  CHECK(huge < 1.0f);
  CHECK(huge > 0.999f);
}

TEST_CASE("srgbOetf: linear branch at and below the 0.0031308 threshold", "[image_regression][tone_mapping]") {
  CHECK(withinAbs(srgbOetf(0.0f), 0.0f, 1e-6f));
  // Exactly at the threshold, the linear branch applies (x <=
  // 0.0031308): 0.0031308 * 12.92 = 0.040448976.
  CHECK(withinAbs(srgbOetf(0.0031308f), 0.040448976f, 1e-5f));
  CHECK(withinAbs(srgbOetf(0.001f), 0.001f * 12.92f, 1e-6f));
}

TEST_CASE("srgbOetf: power-curve branch above the threshold", "[image_regression][tone_mapping]") {
  // x = 0.5: 1.055 * 0.5^(1/2.4) - 0.055 ~= 0.735357.
  CHECK(withinAbs(srgbOetf(0.5f), 0.735357f, 1e-4f));
  // x = 1.0: 1.055 * 1^(1/2.4) - 0.055 = 1.055 - 0.055 = 1.0 exactly.
  CHECK(withinAbs(srgbOetf(1.0f), 1.0f, 1e-5f));
}

TEST_CASE("tonemapAndEncodeUnorm: the *_Unorm variant's own complete per-channel pipeline, D-5 then D-6",
          "[image_regression][tone_mapping]") {
  // x = 10.0 -> reinhardTonemap -> 10/11 = 0.909091 -> srgbOetf ~=
  // 0.958929 (hand-computed: 1.055 * 0.909091^(1/2.4) - 0.055).
  CHECK(withinAbs(tonemapAndEncodeUnorm(10.0f), 0.958929f, 5e-4f));
  // Composed value must still be a monotonically increasing function
  // of the input over the roll-off range -- a strictly brighter linear
  // input never produces a strictly dimmer encoded output.
  CHECK(tonemapAndEncodeUnorm(2.0f) < tonemapAndEncodeUnorm(10.0f));
  CHECK(tonemapAndEncodeUnorm(10.0f) < tonemapAndEncodeUnorm(1000.0f));
}
