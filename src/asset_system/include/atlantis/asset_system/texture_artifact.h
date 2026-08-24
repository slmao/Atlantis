#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/texture_types.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

// Plan 0016 Section D8: the runtime texture artifact's binary layout --
// a 36-byte header (magic, schema_version, width, height, format,
// mip_count, pixel_data_offset, pixel_data_size_bytes) followed by raw
// RGBA8 pixel bytes, all unconditionally little-endian regardless of
// host endianness. Every multi-byte field is assembled byte-by-byte via
// explicit shift/mask, matching mesh_artifact.h's own discipline exactly
// -- this format never memcpy's a C++ struct, its padding, or its
// native representation. Unlike the mesh artifact, this format embeds
// no AssetId of its own -- loadTextureAsset()'s own self-consistency
// check is entirely metadata-side (metadata's own asset_id vs.
// source_logical_path, plus metadata vs. artifact width/height/format),
// matching this Plan's own explicit decode-side contract.

inline constexpr std::uint32_t kTextureArtifactSchemaVersion = 1;
inline constexpr std::size_t kTextureArtifactHeaderSizeBytes = 36;
inline constexpr std::uint32_t kMaxTextureDimension = 8192;  // 8192*8192*4 = 268,435,456, well within uint32_t

struct DecodedTextureArtifact {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  TextureColorSpace colorSpace = TextureColorSpace::Unorm;
  std::vector<std::uint8_t> pixelBytes;
};

// width/height/pixelByteCount are the caller's own already-validated
// values (checked by cookTexture() before calling this) -- this
// function is a pure, always-succeeding byte serializer, matching
// encodeMeshArtifact()'s own "trusted input in, bytes out" shape;
// pixelByteCount must equal width * height * 4 exactly (caller
// precondition, not re-checked here).
[[nodiscard]] std::vector<std::byte> encodeTextureArtifact(std::uint32_t width, std::uint32_t height,
                                                             TextureColorSpace colorSpace,
                                                             const std::uint8_t* pixelBytes,
                                                             std::size_t pixelByteCount);

[[nodiscard]] atlantis::Result<DecodedTextureArtifact, TextureArtifactDecodeError> decodeTextureArtifact(
    const std::vector<std::byte>& bytes);

}  // namespace atlantis::asset_system
