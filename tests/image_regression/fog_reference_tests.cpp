#include "support/fog_reference.h"

#include <cmath>
#include <cstddef>

#include <catch2/catch_test_macros.hpp>

// Plan 0043 Milestone 1 (Spec 0043 R1, P7): GPU-independent coverage of
// the height-fog reference -- no shader, no GPU, no image in this file.

using atlantis::image_regression::applyFog;
using atlantis::image_regression::FogParams;
using atlantis::image_regression::fogFactor;
using atlantis::image_regression::fogHeightIntegral;
using atlantis::image_regression::fogOpticalDepth;
using atlantis::image_regression::FogVec3;
using atlantis::image_regression::kFogSeriesThreshold;

namespace {
[[nodiscard]] bool withinRel(float actual, float expected, float relEpsilon) {
  return std::abs(actual - expected) <= relEpsilon * std::abs(expected);
}

[[nodiscard]] FogParams fogWith(float density, float height, float heightFalloff, float maxOpacity = 1.0f) {
  FogParams fog;
  fog.color = {0.8f, 0.6f, 0.4f};
  fog.density = density;
  fog.height = height;
  fog.heightFalloff = heightFalloff;
  fog.maxOpacity = maxOpacity;
  return fog;
}
}  // namespace

TEST_CASE("fogFactor: density 0 is exactly 0 and applyFog returns the lit value bit for bit", "[image_regression][fog]") {
  // Height and falloff chosen so that e^(-falloff * h) overflows to
  // infinity: were tau evaluated, 0 * inf would be NaN. The density-0
  // early-out must not evaluate it (the shaders' uniform branch).
  const FogParams fog = fogWith(0.0f, 1000.0f, 1.0f);
  const FogVec3 camera{0.0f, 0.0f, 0.0f};
  const FogVec3 point{0.0f, 0.0f, -10.0f};
  CHECK(fogFactor(fog, camera, point) == 0.0f);

  const FogVec3 lit{0.123f, 4.5f, 65504.0f};
  const FogVec3 fogged = applyFog(lit, fog, camera, point);
  CHECK(fogged[0] == lit[0]);
  CHECK(fogged[1] == lit[1]);
  CHECK(fogged[2] == lit[2]);
}

TEST_CASE("fogFactor: falloff 0 is homogeneous fog, 1 - e^(-density * d), for any ray direction",
          "[image_regression][fog]") {
  const FogParams fog = fogWith(0.05f, 3.0f, 0.0f);
  const FogVec3 camera{1.0f, 2.0f, 3.0f};
  // A rising, a falling and a horizontal ray, each of length 13 (3-4-12).
  const FogVec3 points[] = {{4.0f, 6.0f, 15.0f}, {4.0f, -2.0f, 15.0f}, {1.0f + 5.0f, 2.0f, 3.0f + 12.0f}};
  for (const FogVec3& point : points) {
    const float dx = point[0] - camera[0], dy = point[1] - camera[1], dz = point[2] - camera[2];
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    CHECK(withinRel(fogFactor(fog, camera, point), 1.0f - std::exp(-0.05f * d), 1e-6f));
  }
}

TEST_CASE("fogOpticalDepth: a horizontal ray gives density * d * e^(-falloff * h)", "[image_regression][fog]") {
  const FogParams fog = fogWith(0.1f, 1.0f, 0.5f);
  const FogVec3 camera{0.0f, 3.0f, 0.0f};  // h = 3 - 1 = 2
  const FogVec3 point{6.0f, 3.0f, -8.0f};  // d = 10, dy = 0
  CHECK(withinRel(fogOpticalDepth(fog, camera, point), 0.1f * 10.0f * std::exp(-0.5f * 2.0f), 1e-6f));
}

TEST_CASE("fogOpticalDepth: a sloped ray integrates the height density exactly", "[image_regression][fog]") {
  // Closed form of density * integral_0^d e^(-k (c.y + s * dy / d - H)) ds
  // = density * d * e^(-k h) * (1 - e^(-k dy)) / (k dy), computed in double.
  const FogParams fog = fogWith(0.2f, 0.5f, 0.3f);
  const FogVec3 camera{0.0f, 2.0f, 0.0f};
  const FogVec3 point{0.0f, 8.0f, -8.0f};  // d = 10, dy = 6
  const double k = 0.3, h = 1.5, dy = 6.0, d = 10.0;
  const double expected = 0.2 * d * std::exp(-k * h) * (1.0 - std::exp(-k * dy)) / (k * dy);
  CHECK(withinRel(fogOpticalDepth(fog, camera, point), static_cast<float>(expected), 1e-5f));
}

TEST_CASE("fogHeightIntegral: g(0) is 1 and g is continuous across the series threshold",
          "[image_regression][fog]") {
  CHECK(fogHeightIntegral(0.0f) == 1.0f);
  CHECK(fogHeightIntegral(-0.0f) == 1.0f);
  for (const float sign : {1.0f, -1.0f}) {
    const float below = std::nextafter(kFogSeriesThreshold, 0.0f) * sign;  // series side
    const float at = kFogSeriesThreshold * sign;                            // closed-form side
    const double exactAt = (1.0 - std::exp(-static_cast<double>(at))) / static_cast<double>(at);
    CHECK(withinRel(fogHeightIntegral(below), static_cast<float>(exactAt), 1e-5f));
    // The float closed form just past the threshold carries ~1e-4 of
    // cancellation error -- far below one 8-bit LSB of fog.
    CHECK(withinRel(fogHeightIntegral(at), static_cast<float>(exactAt), 2e-4f));
  }
  // And the whole ray is continuous as dy -> 0 through the threshold.
  const FogParams fog = fogWith(0.1f, 0.0f, 0.5f);
  const FogVec3 camera{0.0f, 1.0f, 0.0f};
  const float flat = fogOpticalDepth(fog, camera, {0.0f, 1.0f, -10.0f});
  for (const float dy : {1e-5f, 1.9e-3f, 2.1e-3f, -1.9e-3f, -2.1e-3f}) {
    CHECK(withinRel(fogOpticalDepth(fog, camera, {0.0f, 1.0f + dy, -10.0f}), flat, 2e-3f));
  }
}

TEST_CASE("fogFactor: grows with distance and density, and falls as the view rises out of the fog",
          "[image_regression][fog]") {
  const FogVec3 camera{0.0f, 1.0f, 0.0f};
  const FogParams fog = fogWith(0.05f, 0.0f, 0.2f);

  float previous = 0.0f;
  for (const float z : {-2.0f, -5.0f, -10.0f, -20.0f, -40.0f}) {
    const float f = fogFactor(fog, camera, {0.0f, 1.0f, z});
    CHECK(f > previous);
    previous = f;
  }

  previous = 0.0f;
  for (const float density : {0.01f, 0.02f, 0.05f, 0.1f, 0.5f}) {
    const float f = fogFactor(fogWith(density, 0.0f, 0.2f), camera, {0.0f, 1.0f, -10.0f});
    CHECK(f > previous);
    previous = f;
  }

  // Camera and point raised together: same ray, thinner fog.
  previous = 1.0f;
  for (const float lift : {0.0f, 1.0f, 2.0f, 5.0f, 10.0f}) {
    const float f = fogFactor(fog, {0.0f, 1.0f + lift, 0.0f}, {3.0f, 0.5f + lift, -10.0f});
    CHECK(f < previous);
    previous = f;
  }

  // Camera alone rising above a fixed ground point.
  previous = 1.0f;
  for (const float cameraY : {0.0f, 2.0f, 5.0f, 10.0f, 20.0f}) {
    const float f = fogFactor(fog, {0.0f, cameraY, 0.0f}, {0.0f, 0.0f, -10.0f});
    CHECK(f < previous);
    previous = f;
  }
}

TEST_CASE("fogFactor: maxOpacity clamps, and full opacity replaces the lit value with the fog colour",
          "[image_regression][fog]") {
  const FogVec3 camera{0.0f, 0.0f, 0.0f};
  const FogVec3 point{0.0f, 0.0f, -100.0f};

  CHECK(fogFactor(fogWith(10.0f, 0.0f, 0.0f, 0.35f), camera, point) == 0.35f);
  CHECK(fogFactor(fogWith(10.0f, 0.0f, 0.0f, 0.0f), camera, point) == 0.0f);
  // Below the clamp the clamp is inactive.
  const float unclamped = fogFactor(fogWith(0.001f, 0.0f, 0.0f, 1.0f), camera, point);
  CHECK(fogFactor(fogWith(0.001f, 0.0f, 0.0f, 0.9f), camera, point) == unclamped);

  const FogParams dense = fogWith(10.0f, 0.0f, 0.0f, 1.0f);
  REQUIRE(fogFactor(dense, camera, point) == 1.0f);
  const FogVec3 fogged = applyFog({5.0f, 0.0f, 0.25f}, dense, camera, point);
  CHECK(fogged[0] == dense.color[0]);
  CHECK(fogged[1] == dense.color[1]);
  CHECK(fogged[2] == dense.color[2]);
}

TEST_CASE("applyFog: mixes lit * (1 - f) + color * f per channel", "[image_regression][fog]") {
  const FogParams fog = fogWith(0.05f, 0.0f, 0.1f, 0.8f);
  const FogVec3 camera{0.0f, 2.0f, 0.0f};
  const FogVec3 point{1.0f, 0.5f, -12.0f};
  const float f = fogFactor(fog, camera, point);
  REQUIRE(f > 0.0f);
  REQUIRE(f < 0.8f);
  const FogVec3 lit{2.0f, 0.5f, 0.0f};
  const FogVec3 fogged = applyFog(lit, fog, camera, point);
  for (std::size_t c = 0; c < 3; ++c) CHECK(fogged[c] == lit[c] * (1.0f - f) + fog.color[c] * f);
}
