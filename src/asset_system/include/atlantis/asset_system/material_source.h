#pragma once

#include <atlantis/asset_system/material_types.h>
#include <atlantis/result.h>

#include <string>
#include <string_view>

namespace atlantis::asset_system {

// Plan 0018 Section P4: the authoring-facing, not-yet-cooked
// representation of one material -- textureLogicalPath is still a
// logical-path string (not yet resolved to an AssetId, cookMaterial()'s
// own job), matching ParsedSceneNode::meshLogicalPath's own precedent
// exactly.
struct ParsedMaterialSource {
  MaterialKind kind = MaterialKind::UnlitTextured;
  std::string textureLogicalPath;
  MaterialSamplerFilter filter = MaterialSamplerFilter::Linear;
  MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::Repeat;
};

// Plan 0018 Section P2/P4: parse/decode-error conditions specific to the
// authoring grammar itself -- distinct from MaterialCookError (errors.h),
// mirroring SceneSourceParseError's own already-Accepted relationship to
// SceneCookError exactly.
enum class MaterialSourceParseError {
  UnknownSourceVersion,
  MissingField,
  FieldOrderMismatch,
  UnknownKind,
  UnknownFilter,
  UnknownAddressMode,
  TrailingContent,
};

// Strict, fixed-field-order, plain-text grammar extending
// mesh_source.h's/scene_source.h's own established style (anchored-
// prefix field matching, no general parser library). Exactly 5 lines:
// version, kind, texture, filter, address_mode. Never validates that
// textureLogicalPath resolves to anything -- that is exclusively
// cookMaterial()'s own job (normalizeLogicalPath() + computeAssetId(),
// value-level only, ADR-0059 D6/D7).
[[nodiscard]] atlantis::Result<ParsedMaterialSource, MaterialSourceParseError> parseMaterialSource(
    std::string_view text);

// Serializes back to the exact grammar parseMaterialSource() accepts --
// exists for round-trip testing, matching serializeSceneSource()'s own
// established role exactly.
[[nodiscard]] std::string serializeMaterialSource(const ParsedMaterialSource& source);

}  // namespace atlantis::asset_system
