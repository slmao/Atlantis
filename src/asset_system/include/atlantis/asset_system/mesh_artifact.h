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
// ADR-0058: the runtime artifact's binary layout -- a 40-byte header
// (magic, schema_version, vertex_stride_bytes, an 8-byte-aligned
// asset_id, counts, offsets) followed by raw vertex bytes and
// std::uint16_t index bytes, all unconditionally little-endian
// regardless of host endianness. Every multi-byte field is assembled
// byte-by-byte via explicit shift/mask; vertex floats are first
// reinterpreted via std::bit_cast<std::uint32_t> before that same
// shift/mask serialization -- this format never memcpy's a C++ struct,
// its padding, or its native representation.
//
// Per-vertex layout (32 bytes, schema version 2): position X/Y/Z at
// byte offsets 0/4/8, color R/G/B at offsets 12/16/20, UV0 U/V at
// offsets 24/28 -- no padding. Schema version 1 (24 bytes: position +
// color only, no UV0) is rejected outright by decodeMeshArtifact()'s
// own schema_version check; no migration reader is implemented.

inline constexpr std::uint32_t kMeshArtifactSchemaVersion = 2;
inline constexpr std::uint32_t kMeshArtifactVertexStrideBytes = 32;  // 8 floats: position xyz, colour rgb, UV0 uv
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
