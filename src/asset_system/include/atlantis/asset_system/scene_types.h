#pragma once

#include <atlantis/asset_system/asset_id.h>

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

struct DecodedCamera {
  float fovYRadians = 0.0f;
  float nearZ = 0.0f;
  float farZ = 0.0f;
};

// Plan 0018 Section P7 / ADR-0060 Decision item 1: materialAsset is an
// optional, second, independent reference -- std::nullopt means "no
// material scene binding for this node," Runtime's own existing
// fallback path (never this type's own concern, which stays a plain,
// Atlantis::AssetSystem-owned DTO naming no Renderer/RHI type).
struct DecodedRenderable {
  atlantis::asset_system::AssetId meshAsset = 0;
  std::optional<atlantis::asset_system::AssetId> materialAsset;
};

// Spec 0019 D2 / ADR-0061 Decision item 1 / plans/0019-lighting-foundation.md
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
