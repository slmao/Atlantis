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
// clearcoatFactor/clearcoatRoughness added by Plan 0035 Milestone 2/
// ADR-0081 -- cross-validated identically, unconditionally present
// (like every other field here), inert except for MaterialKind::PbrClearcoat.
// sheenColor/sheenRoughness added by Plan 0035 Milestone 3/ADR-0081 --
// cross-validated identically, unconditionally present, inert except for
// MaterialKind::PbrSheen.
// anisotropyFactor/anisotropyRotation added by Plan 0035 Milestone 4/
// ADR-0081 -- cross-validated identically, unconditionally present,
// inert except for MaterialKind::PbrAnisotropic.
struct MaterialMetadata {
  AssetId assetId = 0;
  std::string sourceLogicalPath;
  MaterialKind kind = MaterialKind::UnlitTextured;
  AssetId textureAsset = 0;
  float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
  AssetId normalMapTexture = 0;
  float clearcoatFactor = 0.0f;
  float clearcoatRoughness = 0.0f;
  float sheenColor[3] = {0.0f, 0.0f, 0.0f};
  float sheenRoughness = 0.0f;
  float anisotropyFactor = 0.0f;
  float anisotropyRotation = 0.0f;
  // Plan 0041 Milestone 1 (Plan 0041 P2): always written, never optional
  // in this machine-written sidecar -- cross-validated identically.
  float emissiveFactor[3] = {0.0f, 0.0f, 0.0f};
  // Plan 0042 Milestone 1 (Spec 0042 R3): both always written, never
  // optional in this machine-written sidecar -- cross-validated identically.
  MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
  float alphaCutoff = 0.5f;
};

[[nodiscard]] atlantis::Result<MaterialMetadata, MetadataParseError> parseMaterialMetadata(std::string_view text);
[[nodiscard]] std::string serializeMaterialMetadata(const MaterialMetadata& metadata);

}  // namespace atlantis::asset_system
