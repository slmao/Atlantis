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
//
// baseColorFactor/metallicFactor/roughnessFactor (ADR-0066 item 2, Plan
// 0023 Milestone 1): three new, optional trailing fields -- version-2
// grammar, defaults apply when absent. Never range-validated here (that
// is cookMaterial()'s own job, ADR-0066 item 5) -- this struct only
// carries whatever finite/non-finite float value was parsed.
struct ParsedMaterialSource {
  MaterialKind kind = MaterialKind::UnlitTextured;
  std::string textureLogicalPath;
  MaterialSamplerFilter filter = MaterialSamplerFilter::Linear;
  MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::Repeat;
  float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
  // Plan 0029 Section P5/ADR-0074 Section 1: empty = none. A distinct
  // field, distinct type, matching textureLogicalPath's own precedent
  // exactly -- a logical path, not yet resolved to an AssetId
  // (cookMaterial()'s own job).
  std::string normalMapLogicalPath;
  // Plan 0035 Milestone 2/ADR-0081: two new, REQUIRED (not optional)
  // trailing fields whenever kind == PbrClearcoat -- unlike
  // baseColorFactor/metallicFactor/roughnessFactor (optional for every
  // kind, including PbrClearcoat, since PbrClearcoat still needs a base
  // layer), a PbrClearcoat material with no real clearcoat_factor/
  // clearcoat_roughness would be a no-op clearcoat, defeating the
  // purpose of declaring this kind at all -- parseMaterialSource()
  // rejects that combination outright (MissingClearcoatFields) rather
  // than silently defaulting to an inert 0.0f clearcoat. Never read for
  // any other kind, same "present in the struct, inert in practice"
  // shape base_color_factor/etc. already have for UnlitTextured/
  // LitTextured.
  float clearcoatFactor = 0.0f;
  float clearcoatRoughness = 0.0f;
};

// Plan 0018 Section P2/P4: parse/decode-error conditions specific to the
// authoring grammar itself -- distinct from MaterialCookError (errors.h),
// mirroring SceneSourceParseError's own already-Accepted relationship to
// SceneCookError exactly. MalformedNumber added by Plan 0023 Milestone 1
// for the three new optional numeric fields -- a non-numeric literal is
// a grammar error, distinct from an out-of-range one (MaterialCookError,
// checked later, at cook time, ADR-0066 item 5).
enum class MaterialSourceParseError {
  UnknownSourceVersion,
  MissingField,
  FieldOrderMismatch,
  UnknownKind,
  UnknownFilter,
  UnknownAddressMode,
  TrailingContent,
  MalformedNumber,
  // Plan 0029 Section P5/ADR-0074 Section 1: a 9-line source names
  // `normal_map:` for a `kind` other than `pbr_direct_lit`/`pbr_clearcoat`
  // (Plan 0035 widening) -- neither `lit_textured.slang` nor
  // `unlit_textured.slang` declares a normal-map binding, so accepting
  // this combination would silently parse a field with no consumer.
  NormalMapNotSupportedForKind,
  // Plan 0035 Milestone 2/ADR-0081: a 10/11-line source (clearcoat_factor/
  // clearcoat_roughness present) for a `kind` other than `pbr_clearcoat`
  // -- mirrors NormalMapNotSupportedForKind's own reasoning exactly, no
  // other kind's shader reads these fields.
  ClearcoatFieldsNotSupportedForKind,
  // Plan 0035 Milestone 2/ADR-0081: `kind: pbr_clearcoat` with NEITHER
  // clearcoat_factor NOR clearcoat_roughness present (a 5- or 8-line
  // source) -- a clearcoat material with no real clearcoat parameters
  // would silently default to an inert 0.0f clearcoat, defeating the
  // purpose of declaring this kind; rejected outright rather than
  // silently accepted.
  MissingClearcoatFields,
};

// Strict, fixed-field-order, plain-text grammar extending
// mesh_source.h's/scene_source.h's own established style (anchored-
// prefix field matching, no general parser library). Version 2 (Plan
// 0023 Milestone 1, ADR-0066 item 2): exactly 5 lines (version, kind,
// texture, filter, address_mode -- the three new numeric fields absent,
// defaults apply) or exactly 8 lines (the same 5 plus, in this fixed
// order, base_color_factor/metallic_factor/roughness_factor) -- no
// partial subset of the three trailing lines. Version 1 is rejected
// outright, no dual-version reader. Never validates that
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
