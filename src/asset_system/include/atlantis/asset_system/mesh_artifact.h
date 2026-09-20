#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/mesh_source.h>
#include <atlantis/asset_system/mesh_tangent_generation.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

// Plan 0012 Section D3 / ADR-0045, extended by Plan 0017 Section D2/
// ADR-0058, Plan 0020 Section P1/P4/ADR-0063, and Plan 0029 Section
// P1/ADR-0073: the runtime artifact's binary layout -- a 40-byte
// header (magic, schema_version, vertex_stride_bytes, an
// 8-byte-aligned asset_id, counts, offsets) followed by raw vertex
// bytes and std::uint16_t index bytes, all unconditionally
// little-endian regardless of host endianness. Every multi-byte field
// is assembled byte-by-byte via explicit shift/mask; vertex floats are
// first reinterpreted via std::bit_cast<std::uint32_t> before that
// same shift/mask serialization -- this format never memcpy's a C++
// struct, its padding, or its native representation.
//
// Per-vertex layout (60 bytes, schema version 4): position X/Y/Z at
// byte offsets 0/4/8, color R/G/B at offsets 12/16/20, UV0 U/V at
// offsets 24/28, normal X/Y/Z at offsets 32/36/40, tangent X/Y/Z/W
// (handedness) at offsets 44/48/52/56 -- no padding. Schema versions
// 1-3 (24/32/44 bytes) are all rejected outright by
// decodeMeshArtifact()'s own schema_version check; no migration reader
// is implemented.

inline constexpr std::uint32_t kMeshArtifactSchemaVersion = 4;
inline constexpr std::uint32_t kMeshArtifactVertexStrideBytes = 60;  // 15 floats: position xyz, colour rgb, UV0 uv, normal xyz, tangent xyzw

// Plan 0020 Section P4, extended by Plan 0029 Section P1: the one,
// single authoritative source for every composition root's own local
// Vertex struct offsets -- not merely documented in the comment above,
// but real, named, public constants a
// static_assert(offsetof(Vertex, field) == kMeshArtifact*OffsetBytes)
// can check at every compile. All five attributes are named together,
// not tangent alone, so no attribute is left an asymmetric,
// comment-only special case.
inline constexpr std::size_t kMeshArtifactPositionOffsetBytes = 0;
inline constexpr std::size_t kMeshArtifactColorOffsetBytes = 12;
inline constexpr std::size_t kMeshArtifactUv0OffsetBytes = 24;
inline constexpr std::size_t kMeshArtifactNormalOffsetBytes = 32;
inline constexpr std::size_t kMeshArtifactTangentOffsetBytes = 44;

inline constexpr std::size_t kMeshArtifactHeaderSizeBytes = 40;

struct DecodedMeshArtifact {
  AssetId assetId = 0;
  std::uint32_t vertexStrideBytes = 0;
  std::vector<std::byte> vertexBytes;
  std::vector<std::uint16_t> indices;
};

[[nodiscard]] std::vector<std::byte> encodeMeshArtifact(AssetId assetId, const ParsedMeshSource& source,
                                                         const std::vector<VertexTangent>& tangents);

[[nodiscard]] atlantis::Result<DecodedMeshArtifact, ArtifactDecodeError> decodeMeshArtifact(
    const std::vector<std::byte>& bytes);

// Plan 0037 (D2, Spec 0036 workflow 1b): schema version 5 -- the exact
// same 40-byte header and 60-byte vertex layout as version 4 above, but
// std::uint32_t indices, removing version 4's 65,535-vertex ceiling. The
// glTF importer is the only producer; a v5 artifact cannot be rendered
// until workflow 1b parameterizes the RHI's index type (disclosed there,
// not silently worked around). Separate encode/decode siblings, never a
// unified reader: encodeMeshArtifact()/decodeMeshArtifact() keep
// rejecting every version but 4, byte-for-byte unchanged.
inline constexpr std::uint32_t kMeshArtifactSchemaVersionU32 = 5;

struct DecodedMeshArtifactU32 {
  AssetId assetId = 0;
  std::uint32_t vertexStrideBytes = 0;
  std::vector<std::byte> vertexBytes;
  std::vector<std::uint32_t> indices;
};

[[nodiscard]] std::vector<std::byte> encodeMeshArtifactU32(AssetId assetId, const ParsedMeshSource& source,
                                                            const std::vector<VertexTangent>& tangents);

// The importer's own entry point: full-fidelity u32 indices without the
// u16 ParsedMeshSource field in between (Plan 0037 M3). Same v5 byte
// layout as the overload above.
[[nodiscard]] std::vector<std::byte> encodeMeshArtifactU32FromIndices(
    AssetId assetId, const std::vector<MeshSourceVertex>& vertices, const std::vector<std::uint32_t>& indices,
    const std::vector<VertexTangent>& tangents);

[[nodiscard]] atlantis::Result<DecodedMeshArtifactU32, ArtifactDecodeError> decodeMeshArtifactU32(
    const std::vector<std::byte>& bytes);

// Spec 0039/ADR-0087: reads two header fields and nothing else, so a
// caller serving more than one schema can pick a decoder instead of
// guessing, trying one and reading an error, or inventing a second
// source of truth (a filename convention, a manifest field). Added
// beside the two decoders rather than inside either: Plan 0037 D2's
// "separate code paths, not one unified reader" holds -- this sits above
// both, and neither decoder changes.
//
// vertexCount is reported unvalidated, straight from the header, so a
// caller can apply a cheap bound before paying for a full decode (Plan
// 0039 P2's drawable-index-range gate). A header that understates it is
// still caught by the decoder's own offset/size cross-checks.
struct MeshArtifactHeaderPeek {
  std::uint32_t schemaVersion = 0;
  std::uint32_t vertexCount = 0;
};

// Errors are the same two the decoders already return for a malformed
// prefix: TooSmallForHeader, BadMagic. An unrecognized schemaVersion is
// NOT an error here -- it is returned verbatim, and rejecting it belongs
// to the caller that knows which versions it serves.
[[nodiscard]] atlantis::Result<MeshArtifactHeaderPeek, ArtifactDecodeError> peekMeshArtifactHeader(
    const std::vector<std::byte>& bytes);

}  // namespace atlantis::asset_system
