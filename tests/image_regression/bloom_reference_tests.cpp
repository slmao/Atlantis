#include "support/bloom_reference.h"

#include <cmath>
#include <cstddef>

#include <catch2/catch_test_macros.hpp>

// Plan 0044 Milestone 1 (Spec 0044 R3, P11): GPU-independent coverage of
// the bloom bright-pass and firefly-weight reference -- no shader, no GPU,
// no image in this file.

using atlantis::image_regression::bloomBrightPass;
using atlantis::image_regression::bloomFireflyWeight;
using atlantis::image_regression::bloomMax3;
using atlantis::image_regression::BloomRgb;
using atlantis::image_regression::kBloomReferenceHighlight;

TEST_CASE("bloomBrightPass: at or below the knee every channel is exactly 0", "[image_regression][bloom]") {
  for (const float threshold : {0.0f, 1.0f, 2.5f}) {
    for (const BloomRgb c : {BloomRgb{0.0f, 0.0f, 0.0f}, BloomRgb{threshold, threshold, threshold},
                             BloomRgb{threshold * 0.5f, 0.0f, threshold}}) {
      const BloomRgb b = bloomBrightPass(c, threshold);
      CHECK(b[0] == 0.0f);
      CHECK(b[1] == 0.0f);
      CHECK(b[2] == 0.0f);
    }
  }
}

TEST_CASE("bloomBrightPass: above the knee keeps c - threshold, compressed by 1 / (1 + max3 / 1000)",
          "[image_regression][bloom]") {
  const BloomRgb c{4.0f, 0.5f, 1.5f};
  const BloomRgb b = bloomBrightPass(c, 1.0f);
  // Excess (3, 0, 0.5); max3 = 3; compression 1 / 1.003.
  const float compression = 1.0f / (1.0f + 3.0f / kBloomReferenceHighlight);
  CHECK(b[0] == 3.0f * compression);
  CHECK(b[1] == 0.0f);
  CHECK(b[2] == 0.5f * compression);
  // Per-channel, so hue shifts toward the channels furthest above the knee.
  CHECK(b[0] > b[2]);
}

TEST_CASE("bloomBrightPass: highlight compression halves a 1000-above-knee source and keeps order",
          "[image_regression][bloom]") {
  const BloomRgb b = bloomBrightPass({1001.0f, 1001.0f, 1001.0f}, 1.0f);
  CHECK(b[0] == 500.0f);  // 1000 / (1 + 1000 / 1000)
  // Monotonic: a brighter source never yields a dimmer excess.
  float previous = 0.0f;
  for (const float value : {2.0f, 10.0f, 100.0f, 1000.0f, 10000.0f}) {
    const float excess = bloomBrightPass({value, 0.0f, 0.0f}, 1.0f)[0];
    CHECK(excess > previous);
    previous = excess;
  }
}

TEST_CASE("bloomBrightPass: threshold 0 passes everything, lightly compressed", "[image_regression][bloom]") {
  const BloomRgb b = bloomBrightPass({0.25f, 0.5f, 0.75f}, 0.0f);
  const float compression = 1.0f / (1.0f + 0.75f / kBloomReferenceHighlight);
  CHECK(b[0] == 0.25f * compression);
  CHECK(b[1] == 0.5f * compression);
  CHECK(b[2] == 0.75f * compression);
}

TEST_CASE("bloomFireflyWeight: 1 for black, halving at max3 = 1, falling with brightness",
          "[image_regression][bloom]") {
  CHECK(bloomFireflyWeight({0.0f, 0.0f, 0.0f}) == 1.0f);
  CHECK(bloomFireflyWeight({1.0f, 0.2f, 0.0f}) == 0.5f);
  CHECK(bloomMax3({0.1f, 7.0f, 3.0f}) == 7.0f);
  // One very hot box (a factor-100 bulb) weighs ~1/100 of a dark box:
  // it still contributes, but cannot dominate a neighbourhood.
  const float hot = bloomFireflyWeight({99.0f, 20.0f, 5.0f});
  CHECK(std::abs(hot - 0.01f) < 1e-6f);
  float previous = 2.0f;
  for (const float value : {0.0f, 0.5f, 2.0f, 10.0f, 100.0f}) {
    const float weight = bloomFireflyWeight({value, 0.0f, 0.0f});
    CHECK(weight < previous);
    previous = weight;
  }
}
