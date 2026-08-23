#pragma once

#include <atlantis/asset_system/asset_id.h>

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

struct DecodedRenderable {
  atlantis::asset_system::AssetId meshAsset = 0;
};

}  // namespace atlantis::asset_system
