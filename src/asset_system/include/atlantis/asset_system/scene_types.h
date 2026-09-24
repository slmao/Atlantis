#pragma once

#include <atlantis/asset_system/asset_id.h>

#include <cmath>
#include <cstdint>
#include <optional>

namespace atlantis::asset_system {

// Plan 0015 Section D2 / ADR-0053: plain, flat, Atlantis::AssetSystem-
// owned DTOs -- never atlantis::world's own Transform/Camera/Renderable.
// Naming one of those here would give Atlantis::AssetSystem a
// compile-time dependency on Atlantis::World, closing the dependency
// cycle ADR-0052's own Decision exists to avoid (World already depends
// on AssetSystem for AssetId). The real conversion into a real
// world::Transform/Camera/Renderable happens exactly once, inside
// World::fromValidatedSceneData() (src/world/), the one place in this
// pipeline permitted to know both shapes.

struct DecodedTransform {
  float positionX = 0.0f, positionY = 0.0f, positionZ = 0.0f;
  float eulerXRadians = 0.0f, eulerYRadians = 0.0f, eulerZRadians = 0.0f;
  float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
};

// Plan 0043 P3 (Spec 0043 R6-R8, ADR-0091 Decision 4): the camera
// node's optional fog group. The defaults are the "no group" value --
// density 0 means fog off. Flat colour fields, the DecodedLight
// convention.
struct DecodedCameraFog {
  float colorR = 1.0f, colorG = 1.0f, colorB = 1.0f;
  float density = 0.0f;
  float height = 0.0f;
  float heightFalloff = 0.0f;
  float maxOpacity = 1.0f;
};

struct DecodedCamera {
  float fovYRadians = 0.0f;
  float nearZ = 0.0f;
  float farZ = 0.0f;
  float exposureCompensationEv = 0.0f;
  DecodedCameraFog fog;  // Plan 0043: trailing, so positional inits above stay valid
};

// Plan 0043 P2 (Spec 0043 R8): the fog group's value domain, checked at
// cook time and again on decode (both reuse NonFiniteValue, the
// exposure precedent). Density and falloff are finite and >= 0, height
// is finite, maxOpacity is in [0, 1], colour is finite and in
// [0, kFogColorMax] -- HDR, the emissive precedent (ruling Q7).
inline constexpr float kFogColorMax = 65504.0f;

[[nodiscard]] inline bool isValidCameraFog(const DecodedCameraFog& fog) {
  const auto colorOk = [](float c) { return std::isfinite(c) && c >= 0.0f && c <= kFogColorMax; };
  return colorOk(fog.colorR) && colorOk(fog.colorG) && colorOk(fog.colorB) && std::isfinite(fog.density) &&
         fog.density >= 0.0f && std::isfinite(fog.height) && std::isfinite(fog.heightFalloff) &&
         fog.heightFalloff >= 0.0f && std::isfinite(fog.maxOpacity) && fog.maxOpacity >= 0.0f &&
         fog.maxOpacity <= 1.0f;
}

// Plan 0040 (Q2 ruling): the scene grammar's own point-light capacity.
// Runtime's kMaxPointLights (scene_extraction.h) carries the same value;
// tests/runtime ties them by static_assert because ADR-0043 forbids this
// module from including a Runtime header. Deliberately not a Runtime
// constant: the grammar gate must work in cooker-side decode too.
inline constexpr std::uint32_t kMaxPointLightsPerScene = 64;

// Plan 0031 (Spec 0031 Requirement 6/7, ADR-0075 Decision 4): the
// fixed, finite authoring-domain policy for exposureCompensationEv --
// Atlantis::AssetSystem's own independent copy (Atlantis::Renderer
// carries the same values, separately, in src/renderer/src/exposure.h
// -- the two modules must not depend on each other).
inline constexpr float kExposureCompensationEvMin = -16.0f;
inline constexpr float kExposureCompensationEvMax = 16.0f;

// Plan 0018 Section P7 / ADR-0060 Decision item 1: materialAsset is an
// optional, second, independent reference -- std::nullopt means "no
// material scene binding for this node," Runtime's own existing
// fallback path (never this type's own concern, which stays a plain,
// Atlantis::AssetSystem-owned DTO naming no Renderer/RHI type).
struct DecodedRenderable {
  atlantis::asset_system::AssetId meshAsset = 0;
  std::optional<atlantis::asset_system::AssetId> materialAsset;
};

// Spec 0019 D2 / ADR-0061 Decision item 1 / docs/plans/0019-lighting-foundation.md
// P2: a deliberately separate, Atlantis::AssetSystem-owned shape from
// atlantis::world::LightKind/Light -- never names a world:: type, the
// identical reasoning DecodedCamera already establishes. Flat fields
// (colorR/G/B), matching DecodedTransform's own flat-field convention,
// not a nested Vec3-shaped field.
enum class DecodedLightKind { Directional, Point };

struct DecodedLight {
  DecodedLightKind kind = DecodedLightKind::Directional;
  float colorR = 1.0f, colorG = 1.0f, colorB = 1.0f;
  float intensity = 1.0f;
  float range = 0.0f;  // Point only; 0.0f for Directional
};

}  // namespace atlantis::asset_system
