// Plan 0031 Milestone 3: GPU-independent coverage for
// computeExposureMultiplier() -- the sole EV->multiplier
// implementation in production code. Reaches the private Renderer
// header via the same relative-path precedent
// tests/runtime/pbr_reflection_cross_check_tests.cpp already
// established for pbr_push_constants.h.

#include <cmath>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "../../src/renderer/src/exposure.h"

using atlantis::renderer::computeExposureMultiplier;
using atlantis::renderer::kExposureCompensationEvMax;
using atlantis::renderer::kExposureCompensationEvMin;

TEST_CASE("computeExposureMultiplier: EV -1/0/+1 map to exactly 0.5/1.0/2.0", "[renderer][exposure]") {
  CHECK(computeExposureMultiplier(-1.0f) == 0.5f);
  CHECK(computeExposureMultiplier(0.0f) == 1.0f);
  CHECK(computeExposureMultiplier(1.0f) == 2.0f);
}

TEST_CASE("computeExposureMultiplier: the closed boundary [-16, +16] stays finite", "[renderer][exposure]") {
  CHECK(std::isfinite(computeExposureMultiplier(kExposureCompensationEvMin)));
  CHECK(std::isfinite(computeExposureMultiplier(kExposureCompensationEvMax)));
  CHECK(computeExposureMultiplier(kExposureCompensationEvMin) == std::exp2(-16.0f));
  CHECK(computeExposureMultiplier(kExposureCompensationEvMax) == std::exp2(16.0f));
}

TEST_CASE("computeExposureMultiplier: propagates NaN rather than silently clamping", "[renderer][exposure]") {
  // The function itself is a pure exp2() -- range/finiteness
  // enforcement is Renderer::drawFrame()'s own job (Milestone 2), not
  // this function's.
  CHECK(std::isnan(computeExposureMultiplier(std::numeric_limits<float>::quiet_NaN())));
}
