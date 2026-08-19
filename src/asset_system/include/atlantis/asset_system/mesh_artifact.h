#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/mesh_source.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

// Plan 0012 Section D3 / ADR-0045: the runtime artifact's binary
// layout -- a 40-byte header (magic, schema_version,
// vertex_stride_bytes, an 8-byte-aligned asset_id, counts, offsets)
// followed by raw vertex bytes and std::uint16_t index bytes, all
// unconditionally little-endian regardless of host endianness. Every
// multi-byte field is assembled byte-by-byte via explicit shift/mask;
// vertex floats are first reinterpreted via std::bit_cast<std::uint32_t>
// before that same shift/mask serialization -- this format never
// memcpy's a C++ struct, its padding, or its native representation.

inline constexpr std::uint32_t kMeshArtifactSchemaVersion = 1;
inline constexpr std::uint32_t kMeshArtifactVertexStrideBytes = 24;  // 6 floats: position xyz, colour rgb
inline constexpr std::size_t kMeshArtifactHeaderSizeBytes = 40;

struct DecodedMeshArtifact {
  AssetId assetId = 0;
  std::uint32_t vertexStrideBytes = 0;
  std::vector<std::byte> vertexBytes;
  std::vector<std::uint16_t> indices;
};

[[nodiscard]] std::vector<std::byte> encodeMeshArtifact(AssetId assetId, const ParsedMeshSource& source);

[[nodiscard]] atlantis::Result<DecodedMeshArtifact, ArtifactDecodeError> decodeMeshArtifact(
    const std::vector<std::byte>& bytes);

}  // namespace atlantis::asset_system
