#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

// Plan 0018 Section P3, widened by Plan 0023 Milestone 1: the runtime
// material artifact's binary layout -- a fixed 56-byte record (magic,
// schema_version, kind, texture_asset_id, filter, address_mode,
// base_color_factor, metallic_factor, roughness_factor), unconditionally
// little-endian regardless of host endianness. Every multi-byte field is
// assembled byte-by-byte via explicit shift/mask (floats via
// std::bit_cast to their own IEEE-754 bit pattern first, then the same
// shift/mask routine), matching every other artifact file's own
// discipline exactly -- this format never memcpy's a C++ struct, its
// padding, or its native representation. Exact byte table (ADR-0066
// item 3): magic(0,8) schema_version(8,4) kind(12,4) texture_asset(16,8)
// filter(24,4) address_mode(28,4) base_color_factor(32,16)
// metallic_factor(48,4) roughness_factor(52,4) -- total 56, a provable
// sum (32 existing + 16 + 4 + 4), never a struct with implicit padding.
//
// Unlike the mesh artifact, this format embeds no AssetId of its own --
// loadMaterialAsset()'s own self-consistency check is entirely
// metadata-side (metadata's own asset_id vs. source_logical_path), the
// exact precedent the texture artifact already establishes (Plan 0018
// Section P3's own closure of Spec 0018 D6's open embedding question).
//
// Unlike the texture artifact, this format has NO variable-length
// payload -- the entire record is a fixed 56 bytes for schema version 2,
// so decodeMaterialArtifact() rejects any size other than exactly 56
// bytes (UnexpectedSize), not merely "too small" (TruncatedHeader) --
// including a real, old, 32-byte schema-version-1 artifact, which is
// rejected outright (no dual-version reader).

inline constexpr std::uint32_t kMaterialArtifactSchemaVersion = 2;
inline constexpr std::size_t kMaterialArtifactHeaderSizeBytes = 56;

// Every MaterialKind uses this identical 56-byte layout -- never a
// per-kind-length record (ADR-0066 item 3). baseColorFactor/
// metallicFactor/roughnessFactor are present, but inert, for
// UnlitTextured/LitTextured; only PbrDirectLit's own realization path
// reads them.
struct DecodedMaterialArtifact {
  MaterialKind kind = MaterialKind::UnlitTextured;
  AssetId textureAsset = 0;
  MaterialSamplerFilter filter = MaterialSamplerFilter::Linear;
  MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::Repeat;
  float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
};

// kind/textureAsset/filter/addressMode/baseColorFactor/metallicFactor/
// roughnessFactor are the caller's own already-validated values (checked
// by cookMaterial() before calling this) -- this function is a pure,
// always-succeeding byte serializer, matching encodeTextureArtifact()'s
// own "trusted input in, bytes out" shape.
[[nodiscard]] std::vector<std::byte> encodeMaterialArtifact(MaterialKind kind, AssetId textureAsset,
                                                             MaterialSamplerFilter filter,
                                                             MaterialSamplerAddressMode addressMode,
                                                             const float (&baseColorFactor)[4],
                                                             float metallicFactor, float roughnessFactor);

[[nodiscard]] atlantis::Result<DecodedMaterialArtifact, MaterialArtifactDecodeError> decodeMaterialArtifact(
    const std::vector<std::byte>& bytes);

}  // namespace atlantis::asset_system
