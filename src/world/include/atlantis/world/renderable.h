#pragma once

#include <atlantis/asset_system/asset_id.h>

#include <optional>

namespace atlantis::world {

// Plan 0018 Section P8 / ADR-0060 Decision item 1: materialAsset is a
// plain, optional AssetId -- std::nullopt means "no material scene
// binding for this entity," Runtime's own existing fallback path
// (World itself constructs no Renderer/RHI type and gains no new
// dependency; AssetId is a type World already depends on
// Atlantis::AssetSystem for, via meshAsset).
struct Renderable {
  atlantis::asset_system::AssetId meshAsset = 0;
  std::optional<atlantis::asset_system::AssetId> materialAsset;
};

}  // namespace atlantis::world
