#pragma once

#include <cmath>
#include <type_traits>

namespace atlantis::renderer {

// Plan 0031 (Spec 0031 Requirement 7, ADR-0075 Decision 4): the same
// numeric values as Atlantis::AssetSystem's own independent copy
// (scene_types.h's kExposureCompensationEvMin/Max) -- deliberately
// duplicated, not shared, across the AssetSystem/Renderer module
// boundary.
inline constexpr float kExposureCompensationEvMin = -16.0f;
inline constexpr float kExposureCompensationEvMax = 16.0f;

// The sole EV->multiplier implementation in production code (Spec 0031
// Requirement 2) -- called once per frame by Renderer::drawFrame(),
// never per-pixel; the output-transform shader itself performs no
// exp2() of its own.
[[nodiscard]] inline float computeExposureMultiplier(float exposureCompensationEv) {
  return std::exp2(exposureCompensationEv);
}

// Mirrors pbr_push_constants.h's own private-header precedent -- not a
// cross-module contract type, renderer.cpp is its own sole real
// production consumer.
struct alignas(4) ExposurePushConstants {
  float exposureMultiplier = 1.0f;
};
static_assert(std::is_standard_layout_v<ExposurePushConstants>);
static_assert(alignof(ExposurePushConstants) == 4);
static_assert(sizeof(ExposurePushConstants) == 4);

}  // namespace atlantis::renderer
