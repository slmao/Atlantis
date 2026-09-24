#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Plan 0043 P7 (Spec 0043 R1, ADR-0091 Decision 2): a CPU-side reference
// for the analytic exponential height fog the ten PBR shaders apply -- a
// literal transcription of Spec 0043 R1 with Plan 0043 P6's series
// threshold, never a shader invocation. The tone_mapping_reference.h
// precedent: unit-tested directly (GPU-independent), and used by GPU
// tests to compute the pixel a fogged frame is expected to show. All
// arithmetic is in float, like the shaders.

namespace atlantis::image_regression {

using FogVec3 = std::array<float, 3>;

// The five fog parameters, in the camera uniform's FogData order.
struct FogParams {
  FogVec3 color{1.0f, 1.0f, 1.0f};  // linear HDR
  float density = 0.0f;             // per metre at `height`; 0 = off
  float height = 0.0f;              // reference height, metres
  float heightFalloff = 0.0f;       // per metre; 0 = homogeneous
  float maxOpacity = 1.0f;          // in [0, 1]
};

// Plan 0043 P6: below this |x|, g(x) uses its Taylor series -- the closed
// form (1 - e^-x) / x cancels catastrophically near 0 and is 0/0 at 0.
inline constexpr float kFogSeriesThreshold = 1e-3f;

// g(x) = (1 - e^-x) / x, g(0) = 1: the height integral of the density
// along the ray, normalised so that a horizontal ray (x = 0) gives 1.
[[nodiscard]] inline float fogHeightIntegral(float x) {
  if (std::abs(x) < kFogSeriesThreshold) return 1.0f - x / 2.0f + x * x / 6.0f;
  return (1.0f - std::exp(-x)) / x;
}

// tau = density * d * e^(-heightFalloff * h) * g(heightFalloff * dy), with
// d = |point - camera|, dy = point.y - camera.y, h = camera.y - height.
[[nodiscard]] inline float fogOpticalDepth(const FogParams& fog, const FogVec3& camera, const FogVec3& point) {
  const float dx = point[0] - camera[0];
  const float dy = point[1] - camera[1];
  const float dz = point[2] - camera[2];
  const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
  const float h = camera[1] - fog.height;
  return fog.density * distance * std::exp(-fog.heightFalloff * h) * fogHeightIntegral(fog.heightFalloff * dy);
}

// f = min(1 - e^-tau, maxOpacity). Density 0 is exactly 0 without
// evaluating tau -- the shaders' uniform branch (Spec 0043 R3).
[[nodiscard]] inline float fogFactor(const FogParams& fog, const FogVec3& camera, const FogVec3& point) {
  if (fog.density == 0.0f) return 0.0f;
  return std::min(1.0f - std::exp(-fogOpticalDepth(fog, camera, point)), fog.maxOpacity);
}

// The fogged radiance, lit * (1 - f) + color * f per channel -- literally
// Spec 0043 R1's form, which the shaders also use. Density 0 returns `lit`
// bit for bit.
[[nodiscard]] inline FogVec3 applyFog(const FogVec3& lit, const FogParams& fog, const FogVec3& camera,
                                      const FogVec3& point) {
  if (fog.density == 0.0f) return lit;
  const float f = fogFactor(fog, camera, point);
  return {lit[0] * (1.0f - f) + fog.color[0] * f, lit[1] * (1.0f - f) + fog.color[1] * f,
          lit[2] * (1.0f - f) + fog.color[2] * f};
}

}  // namespace atlantis::image_regression
