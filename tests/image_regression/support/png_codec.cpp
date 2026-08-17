// The ONE translation unit in this repository that defines these two
// macros. No other file may define either -- ADR-0041's own
// implementation-macro single-translation-unit rule.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
// stb's own implementation is not warning-clean under this project's
// /W4 /WX policy (e.g. sprintf's C4996 deprecation warning) -- this is
// third-party code this project does not own or modify, so its
// warnings are suppressed narrowly, scoped to exactly this include
// block, rather than relaxing atlantis_compiler_warnings itself for
// this translation unit's own code.
#pragma warning(push, 0)
#endif
#include <stb_image.h>
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "png_codec.h"

#include <string>

namespace atlantis::image_regression {

Result<DecodedPng, PngDecodeError> decodePng(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    return Result<DecodedPng, PngDecodeError>::Err(PngDecodeError::FileNotFound);
  }

  const std::string pathString = path.string();

  int width = 0;
  int height = 0;
  int channelsInFile = 0;
  unsigned char* decoded = stbi_load(pathString.c_str(), &width, &height, &channelsInFile, 4);
  if (decoded == nullptr) {
    return Result<DecodedPng, PngDecodeError>::Err(PngDecodeError::DecodeFailed);
  }

  const bool is16Bit = stbi_is_16_bit(pathString.c_str()) != 0;

  // Checked in this order (bit depth before channel count) so a
  // deliberately non-RGBA 16-bit fixture (e.g. 16-bit grayscale, which
  // also fails the channel-count check) is reported as the bit-depth
  // violation it actually is -- see png_codec.h's own note.
  if (is16Bit) {
    stbi_image_free(decoded);
    return Result<DecodedPng, PngDecodeError>::Err(PngDecodeError::UnsupportedBitDepth);
  }
  if (channelsInFile != 4) {
    stbi_image_free(decoded);
    return Result<DecodedPng, PngDecodeError>::Err(PngDecodeError::ChannelCountMismatch);
  }

  DecodedPng result;
  result.channelsInFile = channelsInFile;
  result.is16Bit = is16Bit;
  result.pixels.width = static_cast<std::uint32_t>(width);
  result.pixels.height = static_cast<std::uint32_t>(height);
  const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
  result.pixels.rgba8.assign(decoded, decoded + byteCount);
  stbi_image_free(decoded);

  return Result<DecodedPng, PngDecodeError>::Ok(std::move(result));
}

Result<std::monostate, PngEncodeError> encodePng(const std::filesystem::path& path, const PixelBuffer& pixels) {
  const std::string pathString = path.string();
  const int strideBytes = static_cast<int>(pixels.width) * 4;
  const int written = stbi_write_png(pathString.c_str(), static_cast<int>(pixels.width),
                                      static_cast<int>(pixels.height), 4, pixels.rgba8.data(), strideBytes);
  if (written == 0) {
    return Result<std::monostate, PngEncodeError>::Err(PngEncodeError::WriteFailed);
  }
  return Result<std::monostate, PngEncodeError>::Ok(std::monostate{});
}

}  // namespace atlantis::image_regression
