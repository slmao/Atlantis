#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

// Plan 0018 Section P3: the runtime material artifact's binary layout --
// a fixed 32-byte record (magic, schema_version, kind, texture_asset_id,
// filter, address_mode), unconditionally little-endian regardless of
// host endianness. Every multi-byte field is assembled byte-by-byte via
// explicit shift/mask, matching every other artifact file's own
// discipline exactly -- this format never memcpy's a C++ struct, its
// padding, or its native representation.
//
// Unlike the mesh artifact, this format embeds no AssetId of its own --
// loadMaterialAsset()'s own self-consistency check is entirely
// metadata-side (metadata's own asset_id vs. source_logical_path), the
// exact precedent the texture artifact already establishes (Plan 0018
// Section P3's own closure of Spec 0018 D6's open embedding question).
//
// Unlike the texture artifact, this format has NO variable-length
// payload -- the entire record is a fixed 32 bytes for schema version 1,
// so decodeMaterialArtifact() rejects any size other than exactly 32
// bytes (UnexpectedSize), not merely "too small" (TruncatedHeader).

inline constexpr std::uint32_t kMaterialArtifactSchemaVersion = 1;
inline constexpr std::size_t kMaterialArtifactHeaderSizeBytes = 32;

struct DecodedMaterialArtifact {
  MaterialKind kind = MaterialKind::UnlitTextured;
  AssetId textureAsset = 0;
  MaterialSamplerFilter filter = MaterialSamplerFilter::Linear;
  MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::Repeat;
};

// kind/textureAsset/filter/addressMode are the caller's own already-
// validated values (checked by cookMaterial() before calling this) --
// this function is a pure, always-succeeding byte serializer, matching
// encodeTextureArtifact()'s own "trusted input in, bytes out" shape.
[[nodiscard]] std::vector<std::byte> encodeMaterialArtifact(MaterialKind kind, AssetId textureAsset,
                                                             MaterialSamplerFilter filter,
                                                             MaterialSamplerAddressMode addressMode);

[[nodiscard]] atlantis::Result<DecodedMaterialArtifact, MaterialArtifactDecodeError> decodeMaterialArtifact(
    const std::vector<std::byte>& bytes);

}  // namespace atlantis::asset_system
