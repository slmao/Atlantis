#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/mesh_source.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

// Plan 0012 Section D3 / ADR-0045, extended by Plan 0017 Section D2/
// ADR-0058 and Plan 0020 Section P1/P4/ADR-0063: the runtime artifact's
// binary layout -- a 40-byte header (magic, schema_version,
// vertex_stride_bytes, an 8-byte-aligned asset_id, counts, offsets)
// followed by raw vertex bytes and std::uint16_t index bytes, all
// unconditionally little-endian regardless of host endianness. Every
// multi-byte field is assembled byte-by-byte via explicit shift/mask;
// vertex floats are first reinterpreted via std::bit_cast<std::uint32_t>
// before that same shift/mask serialization -- this format never
// memcpy's a C++ struct, its padding, or its native representation.
//
// Per-vertex layout (44 bytes, schema version 3): position X/Y/Z at
// byte offsets 0/4/8, color R/G/B at offsets 12/16/20, UV0 U/V at
// offsets 24/28, normal X/Y/Z at offsets 32/36/40 -- no padding. Schema
// version 2 (32 bytes: position + color + UV0, no normal) and schema
// version 1 (24 bytes: position + color only) are both rejected
// outright by decodeMeshArtifact()'s own schema_version check; no
// migration reader is implemented.

inline constexpr std::uint32_t kMeshArtifactSchemaVersion = 3;
inline constexpr std::uint32_t kMeshArtifactVertexStrideBytes = 44;  // 11 floats: position xyz, colour rgb, UV0 uv, normal xyz

// Plan 0020 Section P4: the one, single authoritative source for every
// composition root's own local Vertex struct offsets -- not merely
// documented in the comment above, but real, named, public constants a
// static_assert(offsetof(Vertex, field) == kMeshArtifact*OffsetBytes)
// can check at every compile. All four attributes are named together,
// not normal alone, so no attribute is left an asymmetric,
// comment-only special case.
inline constexpr std::size_t kMeshArtifactPositionOffsetBytes = 0;
inline constexpr std::size_t kMeshArtifactColorOffsetBytes = 12;
inline constexpr std::size_t kMeshArtifactUv0OffsetBytes = 24;
inline constexpr std::size_t kMeshArtifactNormalOffsetBytes = 32;

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
