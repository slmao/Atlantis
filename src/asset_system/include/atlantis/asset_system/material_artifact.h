#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

// Plan 0018 Section P3, widened by Plan 0023 Milestone 1 and Plan 0029
// Section P6/ADR-0074 Section 1: the runtime material artifact's binary
// layout -- a fixed 64-byte record (magic, schema_version, kind,
// texture_asset_id, filter, address_mode, base_color_factor,
// metallic_factor, roughness_factor, normal_map_texture_asset_id),
// unconditionally little-endian regardless of host endianness. Every
// multi-byte field is assembled byte-by-byte via explicit shift/mask
// (floats via std::bit_cast to their own IEEE-754 bit pattern first,
// then the same shift/mask routine), matching every other artifact
// file's own discipline exactly -- this format never memcpy's a C++
// struct, its padding, or its native representation. Exact byte table
// (ADR-0066 item 3, extended by ADR-0074 Section 1): magic(0,8)
// schema_version(8,4) kind(12,4) texture_asset(16,8) filter(24,4)
// address_mode(28,4) base_color_factor(32,16) metallic_factor(48,4)
// roughness_factor(52,4) normal_map_texture_asset_id(56,8) -- total 64,
// a provable sum (56 existing + 8), never a struct with implicit
// padding.
//
// Unlike the mesh artifact, this format embeds no AssetId of its own --
// loadMaterialAsset()'s own self-consistency check is entirely
// metadata-side (metadata's own asset_id vs. source_logical_path), the
// exact precedent the texture artifact already establishes (Plan 0018
// Section P3's own closure of Spec 0018 D6's open embedding question).
//
// Unlike the texture artifact, this format has NO variable-length
// payload -- the entire record is a fixed 64 bytes for schema version 3,
// so decodeMaterialArtifact() rejects any size other than exactly 64
// bytes (UnexpectedSize), not merely "too small" (TruncatedHeader) --
// including a real, old, 56-byte schema-version-2 (or 32-byte
// schema-version-1) artifact, both rejected outright (no dual-version
// reader).

inline constexpr std::uint32_t kMaterialArtifactSchemaVersion = 3;
inline constexpr std::size_t kMaterialArtifactHeaderSizeBytes = 64;

// Every MaterialKind uses this identical 64-byte layout -- never a
// per-kind-length record (ADR-0066 item 3). baseColorFactor/
// metallicFactor/roughnessFactor are present, but inert, for
// UnlitTextured/LitTextured; only PbrDirectLit's own realization path
// reads them. normalMapTexture (`0` = none) is legal (non-zero) only
// for PbrDirectLit -- enforced at parse time, not here.
struct DecodedMaterialArtifact {
  MaterialKind kind = MaterialKind::UnlitTextured;
  AssetId textureAsset = 0;
  MaterialSamplerFilter filter = MaterialSamplerFilter::Linear;
  MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::Repeat;
  float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
  AssetId normalMapTexture = 0;
};

// kind/textureAsset/filter/addressMode/baseColorFactor/metallicFactor/
// roughnessFactor/normalMapTexture are the caller's own already-
// validated values (checked by cookMaterial() before calling this) --
// this function is a pure, always-succeeding byte serializer, matching
// encodeTextureArtifact()'s own "trusted input in, bytes out" shape.
[[nodiscard]] std::vector<std::byte> encodeMaterialArtifact(MaterialKind kind, AssetId textureAsset,
                                                             MaterialSamplerFilter filter,
                                                             MaterialSamplerAddressMode addressMode,
                                                             const float (&baseColorFactor)[4],
                                                             float metallicFactor, float roughnessFactor,
                                                             AssetId normalMapTexture);

[[nodiscard]] atlantis::Result<DecodedMaterialArtifact, MaterialArtifactDecodeError> decodeMaterialArtifact(
    const std::vector<std::byte>& bytes);

}  // namespace atlantis::asset_system
