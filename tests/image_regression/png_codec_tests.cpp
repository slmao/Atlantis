#include "support/pixel_diff.h"
#include "support/png_codec.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// This file never defines stb's own write-implementation macro --
// support/png_codec.cpp is the ONE translation unit in this repository
// that does (ADR-0041). This include only pulls in declarations,
// resolved at link time against that already-compiled implementation.
#include <stb_image_write.h>

using atlantis::image_regression::decodePng;
using atlantis::image_regression::encodePng;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::PngDecodeError;

namespace {

// Removes its path at scope exit, including via Catch2's own
// REQUIRE-failure stack unwinding (a genuine C++ exception -- this
// project never disables exceptions) -- so a temp fixture is cleaned up
// even when an earlier REQUIRE in the same TEST_CASE fails, not only on
// the fully-passing path.
struct TempFileGuard {
  std::filesystem::path path;
  ~TempFileGuard() { std::filesystem::remove(path); }
};

[[nodiscard]] std::filesystem::path uniqueTempPngPath(const std::string& suffix) {
  return std::filesystem::temp_directory_path() / ("atlantis_image_regression_png_codec_test_" + suffix + ".png");
}

[[nodiscard]] std::uint32_t crc32Of(const std::vector<std::uint8_t>& data) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (const std::uint8_t byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1u));
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

[[nodiscard]] std::uint32_t adler32Of(const std::vector<std::uint8_t>& data) {
  std::uint32_t a = 1;
  std::uint32_t b = 0;
  for (const std::uint8_t byte : data) {
    a = (a + byte) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

void appendBigEndianU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void appendChunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
  appendBigEndianU32(out, static_cast<std::uint32_t>(data.size()));
  std::vector<std::uint8_t> typeAndData(type, type + 4);
  typeAndData.insert(typeAndData.end(), data.begin(), data.end());
  out.insert(out.end(), typeAndData.begin(), typeAndData.end());
  appendBigEndianU32(out, crc32Of(typeAndData));
}

// Hand-assembles a minimal, valid, uncompressed (zlib "stored" deflate
// block) 1x1 16-bit grayscale PNG -- deliberately not using
// encodePng()/stb_image_write's own writer, which never emits 16-bit
// output at all: this project's own UnsupportedBitDepth rejection path
// (png_codec.cpp) is exactly what this fixture exists to exercise, so
// constructing the fixture must not depend on that same path. Generated
// programmatically at test run time, written to
// std::filesystem::temp_directory_path(), never checked into this
// repository (Section 5.1).
[[nodiscard]] std::filesystem::path write16BitGrayscalePng() {
  const std::vector<std::uint8_t> rawScanline = {0x00, 0x12, 0x34};  // filter=None, one 16-bit gray pixel

  std::vector<std::uint8_t> zlibStream = {0x78, 0x01};  // zlib header (CMF, FLG)
  zlibStream.push_back(0x01);                           // deflate stored block, BFINAL=1, BTYPE=00
  const auto len = static_cast<std::uint16_t>(rawScanline.size());
  const auto nlen = static_cast<std::uint16_t>(~len);
  zlibStream.push_back(static_cast<std::uint8_t>(len & 0xFF));
  zlibStream.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
  zlibStream.push_back(static_cast<std::uint8_t>(nlen & 0xFF));
  zlibStream.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xFF));
  zlibStream.insert(zlibStream.end(), rawScanline.begin(), rawScanline.end());
  appendBigEndianU32(zlibStream, adler32Of(rawScanline));

  std::vector<std::uint8_t> png = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};  // PNG signature

  std::vector<std::uint8_t> ihdrData;
  appendBigEndianU32(ihdrData, 1);  // width
  appendBigEndianU32(ihdrData, 1);  // height
  ihdrData.push_back(16);           // bit depth
  ihdrData.push_back(0);            // color type: grayscale
  ihdrData.push_back(0);            // compression method
  ihdrData.push_back(0);            // filter method
  ihdrData.push_back(0);            // interlace method
  appendChunk(png, "IHDR", ihdrData);
  appendChunk(png, "IDAT", zlibStream);
  appendChunk(png, "IEND", {});

  const std::filesystem::path path = uniqueTempPngPath("sixteen_bit_grayscale");
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  return path;
}

}  // namespace

TEST_CASE("encodePng/decodePng: round-trips a synthetic buffer byte-for-byte", "[image_regression][png_codec]") {
  PixelBuffer original;
  original.width = 4;
  original.height = 3;
  original.rgba8.resize(static_cast<std::size_t>(original.width) * original.height * 4);
  for (std::size_t i = 0; i < original.rgba8.size(); ++i) {
    original.rgba8[i] = static_cast<std::uint8_t>(i * 7 + 3);
  }

  const std::filesystem::path path = uniqueTempPngPath("roundtrip");
  const TempFileGuard guard{path};
  REQUIRE(encodePng(path, original).isOk());

  const auto decodeResult = decodePng(path);
  REQUIRE(decodeResult.isOk());
  const auto& decoded = decodeResult.value();
  REQUIRE(decoded.pixels.width == original.width);
  REQUIRE(decoded.pixels.height == original.height);
  REQUIRE(decoded.pixels.rgba8 == original.rgba8);
  REQUIRE(decoded.channelsInFile == 4);
  REQUIRE_FALSE(decoded.is16Bit);
}

TEST_CASE("decodePng: FileNotFound for a missing file", "[image_regression][png_codec]") {
  const auto result = decodePng(uniqueTempPngPath("does_not_exist"));
  REQUIRE(result.isErr());
  REQUIRE(result.error() == PngDecodeError::FileNotFound);
}

TEST_CASE("decodePng: ChannelCountMismatch for a real 3-channel PNG", "[image_regression][png_codec]") {
  const std::filesystem::path path = uniqueTempPngPath("three_channel");
  const TempFileGuard guard{path};
  const std::vector<std::uint8_t> rgb(4 * 4 * 3, 128);
  const int written = stbi_write_png(path.string().c_str(), 4, 4, 3, rgb.data(), 4 * 3);
  REQUIRE(written != 0);

  const auto result = decodePng(path);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == PngDecodeError::ChannelCountMismatch);
}

TEST_CASE("decodePng: UnsupportedBitDepth for a 16-bit grayscale PNG", "[image_regression][png_codec]") {
  const std::filesystem::path path = write16BitGrayscalePng();
  const TempFileGuard guard{path};

  const auto result = decodePng(path);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == PngDecodeError::UnsupportedBitDepth);
}
