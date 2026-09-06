#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/result.h>

#include <string>
#include <string_view>

namespace atlantis::asset_system {

// Plan 0018 Section P3: the material metadata sidecar's field semantics
// -- a new, dedicated shape mirroring TextureMetadata's own precedent
// exactly. Wire encoding: strict, anchored-prefix, versioned flat text,
// matching TextureMetadata/SceneMetadata's own established grammar
// discipline -- this module's parser is new, independent code, never
// shared with either of theirs.
//
// baseColorFactor/metallicFactor/roughnessFactor added by Plan 0023
// Milestone 1 (ADR-0066 item 4) -- cross-validated against the
// artifact's own decoded values by loadMaterialAsset(), exactly as
// kind/textureAsset already are.
// normalMapTexture added by Plan 0029 Section P6/ADR-0074 Section 1 --
// cross-validated against the artifact's own decoded value by
// loadMaterialAsset(), exactly as kind/textureAsset already are.
struct MaterialMetadata {
  AssetId assetId = 0;
  std::string sourceLogicalPath;
  MaterialKind kind = MaterialKind::UnlitTextured;
  AssetId textureAsset = 0;
  float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
  AssetId normalMapTexture = 0;
};

[[nodiscard]] atlantis::Result<MaterialMetadata, MetadataParseError> parseMaterialMetadata(std::string_view text);
[[nodiscard]] std::string serializeMaterialMetadata(const MaterialMetadata& metadata);

}  // namespace atlantis::asset_system
