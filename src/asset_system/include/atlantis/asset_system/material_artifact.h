#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

// Plan 0018 Section P3, widened by Plan 0023 Milestone 1, Plan 0029
// Section P6/ADR-0074 Section 1, Plan 0035 Milestone 2/ADR-0081, Plan
// 0035 Milestone 3/ADR-0081, and Plan 0035 Milestone 4/ADR-0081: the
// runtime material artifact's binary layout -- a fixed 96-byte record
// (magic, schema_version, kind, texture_asset_id, filter, address_mode,
// base_color_factor, metallic_factor, roughness_factor,
// normal_map_texture_asset_id, clearcoat_factor, clearcoat_roughness,
// sheen_color, sheen_roughness, anisotropy_factor, anisotropy_rotation),
// unconditionally little-endian regardless of host endianness. Every
// multi-byte field is assembled byte-by-byte via explicit shift/mask
// (floats via std::bit_cast to their own IEEE-754 bit pattern first,
// then the same shift/mask routine), matching every other artifact
// file's own discipline exactly -- this format never memcpy's a C++
// struct, its padding, or its native representation. Exact byte table
// (ADR-0066 item 3, extended by ADR-0074 Section 1, extended by
// ADR-0081 three times): magic(0,8) schema_version(8,4) kind(12,4)
// texture_asset(16,8) filter(24,4) address_mode(28,4)
// base_color_factor(32,16) metallic_factor(48,4) roughness_factor(52,4)
// normal_map_texture_asset_id(56,8) clearcoat_factor(64,4)
// clearcoat_roughness(68,4) sheen_color(72,12) sheen_roughness(84,4)
// anisotropy_factor(88,4) anisotropy_rotation(92,4) emissive_factor(96,12)
// -- total 108, a provable sum (96 existing + 12, Plan 0041 Milestone 1),
// never a struct with implicit padding.
//
// Unlike the mesh artifact, this format embeds no AssetId of its own --
// loadMaterialAsset()'s own self-consistency check is entirely
// metadata-side (metadata's own asset_id vs. source_logical_path), the
// exact precedent the texture artifact already establishes (Plan 0018
// Section P3's own closure of Spec 0018 D6's open embedding question).
//
// Unlike the texture artifact, this format has NO variable-length
// payload -- the entire record is a fixed 108 bytes for schema version 7,
// so decodeMaterialArtifact() rejects any size other than exactly 108
// bytes (UnexpectedSize), not merely "too small" (TruncatedHeader) --
// including a real, old, 96-byte schema-version-6 (or 88-byte
// schema-version-5, or 72-byte
// schema-version-4, or 64-byte schema-version-3, or 56-byte
// schema-version-2, or 32-byte schema-version-1) artifact, all rejected
// outright (no dual-version reader).

// Plan 0035 Milestone 4 (ADR-0081): schema bumped 5 -> 6, header size
// 88 -> 96 (a real, deliberate deviation from ADR-0081's own "one bump
// per BRDF slice" plan, same as Milestones 2/3's own bumps --
// anisotropyFactor/anisotropyRotation are two new plain-scalar fields, 8
// bytes total, appended at the existing tail, offset 88-95). Same
// rationale as every prior bump: no dual-version reader, every existing
// artifact at the old version/size is rejected outright, not silently
// accepted.
// Plan 0041 Milestone 1 (Spec 0041 R4, ADR-0089 Decision 5): schema
// bumped 6 -> 7, header 96 -> 108 -- emissive_factor, three floats (12
// bytes), appended at the existing tail, offset 96-107. Same rationale as
// every prior bump: no dual-version reader.
inline constexpr std::uint32_t kMaterialArtifactSchemaVersion = 7;
inline constexpr std::size_t kMaterialArtifactHeaderSizeBytes = 108;

// The value encodeMaterialArtifact()'s trailing emissive argument
// defaults to, so every pre-existing caller encodes "no emissive".
inline constexpr float kNoEmissiveFactor[3] = {0.0f, 0.0f, 0.0f};

// Every MaterialKind uses this identical 108-byte layout -- never a
// per-kind-length record (ADR-0066 item 3). baseColorFactor/
// metallicFactor/roughnessFactor/clearcoatFactor/clearcoatRoughness/
// sheenColor/sheenRoughness/anisotropyFactor/anisotropyRotation are
// present, but inert, for every kind that does not read them; only
// PbrDirectLit's own realization path reads the first three, only
// PbrClearcoat's reads clearcoatFactor/clearcoatRoughness, only
// PbrSheen's reads sheenColor/sheenRoughness, only PbrAnisotropic's
// reads anisotropyFactor/anisotropyRotation. normalMapTexture (`0` =
// none) is legal (non-zero) only for PbrDirectLit, PbrClearcoat,
// PbrSheen, or PbrAnisotropic -- enforced at parse time, not here.
struct DecodedMaterialArtifact {
  MaterialKind kind = MaterialKind::UnlitTextured;
  AssetId textureAsset = 0;
  MaterialSamplerFilter filter = MaterialSamplerFilter::Linear;
  MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::Repeat;
  float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
  AssetId normalMapTexture = 0;
  // Plan 0035 Milestone 2 (ADR-0081): offset 64(4)/68(4) -- see this
  // header's own top comment for the full byte table.
  float clearcoatFactor = 0.0f;
  float clearcoatRoughness = 0.0f;
  // Plan 0035 Milestone 3 (ADR-0081): offset 72(12)/84(4) -- see this
  // header's own top comment for the full byte table.
  float sheenColor[3] = {0.0f, 0.0f, 0.0f};
  float sheenRoughness = 0.0f;
  // Plan 0035 Milestone 4 (ADR-0081): offset 88(4)/92(4) -- see this
  // header's own top comment for the full byte table.
  float anisotropyFactor = 0.0f;
  float anisotropyRotation = 0.0f;
  // Plan 0041 Milestone 1 (ADR-0089): offset 96(12), range [0, 65504].
  float emissiveFactor[3] = {0.0f, 0.0f, 0.0f};
};

// kind/textureAsset/filter/addressMode/baseColorFactor/metallicFactor/
// roughnessFactor/normalMapTexture/clearcoatFactor/clearcoatRoughness/
// sheenColor/sheenRoughness/anisotropyFactor/anisotropyRotation are the
// caller's own already-validated values (checked by cookMaterial()
// before calling this) -- this function is a pure, always-succeeding
// byte serializer, matching encodeTextureArtifact()'s own "trusted
// input in, bytes out" shape.
[[nodiscard]] std::vector<std::byte> encodeMaterialArtifact(MaterialKind kind, AssetId textureAsset,
                                                             MaterialSamplerFilter filter,
                                                             MaterialSamplerAddressMode addressMode,
                                                             const float (&baseColorFactor)[4],
                                                             float metallicFactor, float roughnessFactor,
                                                             AssetId normalMapTexture, float clearcoatFactor,
                                                             float clearcoatRoughness,
                                                             const float (&sheenColor)[3], float sheenRoughness,
                                                             float anisotropyFactor, float anisotropyRotation,
                                                             const float (&emissiveFactor)[3] = kNoEmissiveFactor);

[[nodiscard]] atlantis::Result<DecodedMaterialArtifact, MaterialArtifactDecodeError> decodeMaterialArtifact(
    const std::vector<std::byte>& bytes);

}  // namespace atlantis::asset_system
