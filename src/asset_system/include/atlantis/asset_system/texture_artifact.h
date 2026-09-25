#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/texture_types.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

// Plan 0016 Section D8, extended by Spec 0038 (schema v2): the runtime
// texture artifact's binary layout -- a 40-byte header (magic,
// schema_version, width, height, format, mip_count, pixel_data_offset,
// pixel_data_size_bytes, data_layout) followed by pixel bytes, all
// unconditionally little-endian regardless of host endianness. Every
// multi-byte field is assembled byte-by-byte via explicit shift/mask,
// matching mesh_artifact.h's own discipline exactly -- this format never
// memcpy's a C++ struct, its padding, or its native representation.
// Unlike the mesh artifact, this format embeds no AssetId of its own --
// loadTextureAsset()'s own self-consistency check is entirely
// metadata-side (metadata's own asset_id vs. source_logical_path, plus
// metadata vs. artifact width/height/format/layout), matching this
// module's own explicit decode-side contract.
//
// Schema v2 (Spec 0038): the appended trailing data_layout field (0 =
// tightly-packed RGBA8, 1 = BC7 blocks) widened the header from 36 to
// 40 bytes and moved pixel_data_offset's value from 36 to 40. v2 is the
// only version this decoder accepts (exact-equality, matching the
// repository's established no-dual-version-reader discipline); a v1
// artifact fails with UnsupportedSchemaVersion, exactly as a v2 file
// would have failed inside the v1 decoder.
//
// Schema v3 (Spec 0045, ADR-0093 Decision 1): same 40-byte header, same
// offsets. mip_count is live -- 1 <= mip_count <= fullMipChainLength() --
// and the levels follow the header contiguously, level 0 first, each
// tightly packed, with no padding between them. pixel_data_size_bytes is
// the whole chain; per-level offsets are derived by textureMipLevels(),
// never stored. v3 is the only version this decoder accepts.
//
// Pixel-byte packing per level by layout (texture_types.h's
// TextureDataLayout): Rgba8 = w_i * h_i * 4 tightly-packed bytes; Bc7 =
// verbatim BC7 blocks, ceil(w_i/4) * ceil(h_i/4) * 16 bytes, with
// w_i = max(1, width >> i). The base level's dimensions must be multiples
// of 4 for Bc7 (decode rejects otherwise, never pads); levels below it
// are whatever the halving gives, a sub-block level still one block.

inline constexpr std::uint32_t kTextureArtifactSchemaVersion = 3;
inline constexpr std::size_t kTextureArtifactHeaderSizeBytes = 40;
inline constexpr std::uint32_t kMaxTextureDimension = 8192;  // 8192*8192*4 = 268,435,456, well within uint32_t

// ceil(width/4) * ceil(height/4) * 16, the exact BC7 payload byte count
// for a single mip (uint64_t throughout: a crafted header cannot
// overflow the computation before the comparison).
[[nodiscard]] std::uint64_t bc7BlockByteCount(std::uint32_t width, std::uint32_t height) noexcept;

// Spec 0045 / ADR-0093 Decision 2: the one authority on a mip chain's
// layout -- the encoder's caller, the decoder's size check, the cookers
// and the Runtime's upload regions all derive it here, never by their own
// arithmetic. Offsets are relative to the start of the pixel data.
struct TextureMipLevel {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t offsetBytes = 0;
  std::uint64_t sizeBytes = 0;
};

// floor(log2(max(width, height))) + 1: the levels from width x height
// down to 1x1. 0 for a zero dimension.
[[nodiscard]] std::uint32_t fullMipChainLength(std::uint32_t width, std::uint32_t height) noexcept;

// The first mipCount levels of a width x height chain in layout, largest
// first, tightly packed. Precondition (caller-checked): width, height > 0
// and 1 <= mipCount <= fullMipChainLength(width, height).
[[nodiscard]] std::vector<TextureMipLevel> textureMipLevels(std::uint32_t width, std::uint32_t height,
                                                           TextureDataLayout layout, std::uint32_t mipCount);

// The sum of textureMipLevels()' sizes.
[[nodiscard]] std::uint64_t textureMipChainByteCount(std::uint32_t width, std::uint32_t height,
                                                     TextureDataLayout layout, std::uint32_t mipCount);

struct DecodedTextureArtifact {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  TextureColorSpace colorSpace = TextureColorSpace::Unorm;
  TextureDataLayout layout = TextureDataLayout::Rgba8;
  std::uint32_t mipCount = 1;  // Spec 0045 (schema v3)
  std::vector<std::uint8_t> pixelBytes;  // the whole chain, level 0 first
};

// width/height/layout/mipCount/pixelByteCount are the caller's own
// already-validated values (checked by cookTexture()/cookTextureBc7()
// before calling this) -- this function is a pure, always-succeeding
// byte serializer, matching encodeMeshArtifact()'s own "trusted input
// in, bytes out" shape; pixelByteCount must equal
// textureMipChainByteCount() exactly (caller precondition, not re-checked
// here).
[[nodiscard]] std::vector<std::byte> encodeTextureArtifact(std::uint32_t width, std::uint32_t height,
                                                             TextureColorSpace colorSpace,
                                                             TextureDataLayout layout, std::uint32_t mipCount,
                                                             const std::uint8_t* pixelBytes,
                                                             std::size_t pixelByteCount);

[[nodiscard]] atlantis::Result<DecodedTextureArtifact, TextureArtifactDecodeError> decodeTextureArtifact(
    const std::vector<std::byte>& bytes);

}  // namespace atlantis::asset_system
