#pragma once

#include "pixel_diff.h"

#include <atlantis/result.h>

#include <filesystem>
#include <variant>

namespace atlantis::image_regression {

enum class PngDecodeError { FileNotFound, DecodeFailed, ChannelCountMismatch, UnsupportedBitDepth };
enum class PngEncodeError { WriteFailed };

struct DecodedPng {
  PixelBuffer pixels;
  // stb's own channels_in_file out-parameter -- the file's real,
  // as-encoded channel count, independent of the forced 4-channel
  // *output buffer* (ADR-0041's own "why forcing 4 channels is not
  // enough on its own"). Already validated == 4 and 8-bit by the time
  // this struct is returned Ok -- see decodePng()'s own contract below;
  // callers do not need to re-check these two fields, they exist for
  // diagnostic/logging use only.
  int channelsInFile = 0;
  bool is16Bit = false;
};

// Decodes path with desired_channels = 4 (always). Additionally reads
// stb's own channels_in_file out-parameter and calls
// stbi_is_16_bit()/stbi_is_16_bit_from_memory(); returns
// Err(UnsupportedBitDepth) if 16-bit, or Err(ChannelCountMismatch) if
// channels_in_file != 4 -- checked in that order so a 16-bit,
// non-4-channel file (e.g. 16-bit grayscale) is reported as the
// bit-depth violation, not masked by the channel-count check -- these
// two checks run even though decode itself "succeeded" per stb's own
// return value, since a forced-4-channel decode of a real-3-channel
// file is exactly the silent-masking failure mode ADR-0041 requires
// catching.
[[nodiscard]] atlantis::Result<DecodedPng, PngDecodeError> decodePng(const std::filesystem::path& path);

// Never calls stbi_flip_vertically_on_write() -- writes pixels.rgba8
// exactly as given, row 0 first (ADR-0041's own row-order contract).
[[nodiscard]] atlantis::Result<std::monostate, PngEncodeError> encodePng(const std::filesystem::path& path,
                                                                          const PixelBuffer& pixels);

}  // namespace atlantis::image_regression
