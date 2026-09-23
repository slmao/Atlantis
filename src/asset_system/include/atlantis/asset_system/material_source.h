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
  // Plan 0035 Milestone 3/ADR-0081: two new, REQUIRED (not optional)
  // trailing fields whenever kind == PbrSheen -- mirrors
  // clearcoatFactor/clearcoatRoughness's own identical reasoning
  // immediately above (a PbrSheen material with no real sheenColor/
  // sheenRoughness would be a no-op sheen, defeating the purpose of
  // declaring this kind at all); parseMaterialSource() rejects that
  // combination outright (MissingSheenFields). sheenColor is RGB,
  // linear-space, following baseColorFactor's own established ADR-0066
  // convention. Never read for any other kind.
  float sheenColor[3] = {0.0f, 0.0f, 0.0f};
  float sheenRoughness = 0.0f;
  // Plan 0035 Milestone 4/ADR-0081: two new, REQUIRED (not optional)
  // trailing fields whenever kind == PbrAnisotropic -- mirrors
  // clearcoatFactor/clearcoatRoughness's/sheenColor/sheenRoughness's own
  // identical reasoning above. anisotropyFactor is -1..1
  // (strength/sign, Spec 0035's own field definition), never
  // range-validated here (cookMaterial()'s own job). Never read for any
  // other kind.
  float anisotropyFactor = 0.0f;
  float anisotropyRotation = 0.0f;
  // Plan 0041 Milestone 1 (Spec 0041 R1/R4, ruling O1): the one OPTIONAL
  // field of the v7 grammar -- an `emissive_factor: r g b` line
  // identified by its prefix, absent meaning (0, 0, 0). Never
  // range-validated here (cookMaterial()'s own job, [0, 65504]).
  float emissiveFactor[3] = {0.0f, 0.0f, 0.0f};
  // Plan 0042 Milestone 1 (Spec 0042 R3, Plan 0042 P1): two more OPTIONAL
  // prefix-identified lines, `alpha_mode: opaque|mask|blend` and
  // `alpha_cutoff: x`, after emissive_factor. alphaCutoff is never
  // range-validated here (cookMaterial()'s own job, [0, 1]).
  MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
  float alphaCutoff = 0.5f;
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
  // Plan 0035 Milestone 3/ADR-0081: a 10/11-line source (sheen_color/
  // sheen_roughness present) for a `kind` other than `pbr_sheen` --
  // mirrors ClearcoatFieldsNotSupportedForKind's own reasoning exactly,
  // no other kind's shader reads these fields.
  SheenFieldsNotSupportedForKind,
  // Plan 0035 Milestone 3/ADR-0081: `kind: pbr_sheen` with NEITHER
  // sheen_color NOR sheen_roughness present (a 5- or 8-line source) --
  // mirrors MissingClearcoatFields's own reasoning exactly.
  MissingSheenFields,
  // Plan 0035 Milestone 4/ADR-0081: a 10/11-line source (anisotropy_factor/
  // anisotropy_rotation present) for a `kind` other than
  // `pbr_anisotropic` -- mirrors ClearcoatFieldsNotSupportedForKind's/
  // SheenFieldsNotSupportedForKind's own reasoning exactly.
  AnisotropyFieldsNotSupportedForKind,
  // Plan 0035 Milestone 4/ADR-0081: `kind: pbr_anisotropic` with NEITHER
  // anisotropy_factor NOR anisotropy_rotation present (a 5- or 8-line
  // source) -- mirrors MissingClearcoatFields's/MissingSheenFields's own
  // reasoning exactly.
  MissingAnisotropyFields,
  // Plan 0041 Milestone 1 (Spec 0041 R3): an `emissive_factor:` line on
  // `kind: unlit_textured`/`kind: lit_textured` -- neither shader reads
  // it (Spec 0041 Investigation 3), so it is rejected rather than
  // silently ignored, mirroring NormalMapNotSupportedForKind.
  EmissiveNotSupportedForKind,
  // Plan 0042 Milestone 1 (Plan 0042 P2, ruling Q3): an `alpha_mode:` or
  // `alpha_cutoff:` line on `kind: unlit_textured`/`kind: lit_textured`
  // -- neither shader can honour it, mirroring EmissiveNotSupportedForKind.
  AlphaModeNotSupportedForKind,
  // Plan 0042 Milestone 1: an `alpha_mode:` value other than opaque/mask/
  // blend, mirroring UnknownFilter/UnknownAddressMode.
  UnknownAlphaMode,
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
