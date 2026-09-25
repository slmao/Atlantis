#pragma once

#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_cooker {

// Spec 0038: pure, file-format-level DDS parsing for the one shape this
// pipeline accepts -- a BC7 (DXGI 98/99/100 family) 2D texture. Lives in
// Tools (never Asset System) exactly like the stb_image decode does:
// Asset System's cookTextureBc7() takes already-parsed block bytes.
// GPU-independent and Vulkan-free, unit-testable with literal byte
// inputs (matching the repository's established pure-classification
// precedent).
//
// Spec 0045 (ADR-0093 Decision 2): every level the file declares is
// returned -- dwMipMapCount levels when DDSD_MIPMAPCOUNT is set, else one
// -- as one contiguous chain, level 0 first, exactly as DDS stores it (no
// padding between levels). Ruling Q5: the payload after the header must
// be exactly that chain's byte count (texture_artifact.h's
// textureMipChainByteCount()) -- fewer bytes are Truncated, more are
// MalformedHeader, and so is a declared count of 0 or one beyond the
// dimensions' full chain.

enum class DdsParseError {
  MalformedHeader,        // bad magic/sizes/flags, non-2D, array, bad mip count, trailing bytes
  UnsupportedFormat,      // valid DDS, but not a BC7 2D texture (Spec's survey-finalized scope)
  Truncated,              // the header's declared chain needs more bytes than the buffer holds
  NonAlignedDimensions,   // BC7 base mip width/height not a multiple of 4
};

struct DdsBc7Image {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool srgb = false;  // DXGI_BC7_UNORM_SRGB vs DXGI_BC7_UNORM
  std::uint32_t mipCount = 1;
  std::vector<std::uint8_t> blockBytes;  // the whole chain, level 0 first
};

[[nodiscard]] atlantis::Result<DdsBc7Image, DdsParseError> parseDdsBc7(const std::uint8_t* bytes,
                                                                         std::size_t byteCount) noexcept;

}  // namespace atlantis::asset_cooker
