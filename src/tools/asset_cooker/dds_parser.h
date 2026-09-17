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
// Only the base mip's block bytes are extracted (the .atex artifact's
// own mipCount==1 contract, Spec 0038); any additional mip levels an
// input file carries are validated for presence but not returned.

enum class DdsParseError {
  MalformedHeader,        // bad magic, wrong header sizes, bad flags combination, non-2D, array
  UnsupportedFormat,      // valid DDS, but not a BC7 2D texture (Spec's survey-finalized scope)
  Truncated,              // header claims more base-mip bytes than the buffer holds
  NonAlignedDimensions,   // BC7 base mip width/height not a multiple of 4
};

struct DdsBc7Image {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool srgb = false;  // DXGI_BC7_UNORM_SRGB vs DXGI_BC7_UNORM
  std::vector<std::uint8_t> baseMipBlockBytes;
};

[[nodiscard]] atlantis::Result<DdsBc7Image, DdsParseError> parseDdsBc7(const std::uint8_t* bytes,
                                                                         std::size_t byteCount) noexcept;

}  // namespace atlantis::asset_cooker
