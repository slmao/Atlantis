#include "dds_parser.h"

#include <atlantis/result.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Spec 0038 / Plan 0038 Milestone 2: pure, GPU-independent tests for the
// DDS+DX10 BC7 header parser -- every input is a literal byte vector
// built by these helpers, no fixture files. The header layout mirrors
// dds_parser.cpp's own documented offsets.

namespace {

using atlantis::asset_cooker::DdsParseError;
using atlantis::asset_cooker::parseDdsBc7;

constexpr std::size_t kHeaderOffset = 4;    // after "DDS "
constexpr std::size_t kPfOffset = 76;
constexpr std::size_t kDx10Offset = 128;

void putU32LE(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    bytes[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
  }
}

[[nodiscard]] std::vector<std::uint8_t> makeDx10Bc7(std::uint32_t width, std::uint32_t height, std::uint32_t dxgiFormat,
                                                    std::uint32_t arraySize = 1,
                                                    std::uint32_t resourceDimension = 3) {
  const std::size_t baseMipBytes =
      static_cast<std::size_t>(((width + 3) / 4) * ((height + 3) / 4)) * 16;
  std::vector<std::uint8_t> bytes(kDx10Offset + 20 + baseMipBytes, 0);
  std::memcpy(bytes.data(), "DDS ", 4);
  putU32LE(bytes, kHeaderOffset + 0, 124);         // header size
  putU32LE(bytes, kHeaderOffset + 8, height);      // height
  putU32LE(bytes, kHeaderOffset + 12, width);      // width
  putU32LE(bytes, kHeaderOffset + kPfOffset, 32);  // pixel format size
  const std::uint32_t fourCCDX10 = ('D' << 0) | ('X' << 8) | ('1' << 16) | ('0' << 24);
  putU32LE(bytes, kHeaderOffset + kPfOffset + 8, fourCCDX10);
  putU32LE(bytes, kDx10Offset + 0, dxgiFormat);
  putU32LE(bytes, kDx10Offset + 4, resourceDimension);
  putU32LE(bytes, kDx10Offset + 12, arraySize);
  for (std::size_t i = 0; i < baseMipBytes; ++i) {
    bytes[kDx10Offset + 20 + i] = static_cast<std::uint8_t>((i * 13 + 1) % 256);
  }
  return bytes;
}

}  // namespace

TEST_CASE("parseDdsBc7 accepts a DX10 BC7_UNORM 2D texture and extracts verbatim base-mip blocks",
          "[asset_cooker][dds]") {
  const auto bytes = makeDx10Bc7(8, 8, 99);  // DXGI_BC7_UNORM
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isOk());
  CHECK(result.value().width == 8);
  CHECK(result.value().height == 8);
  CHECK(result.value().srgb == false);
  REQUIRE(result.value().baseMipBlockBytes.size() == 64);  // 2x2 blocks
  for (std::size_t i = 0; i < 64; ++i) {
    CHECK(result.value().baseMipBlockBytes[i] == bytes[kDx10Offset + 20 + i]);
  }
}

TEST_CASE("parseDdsBc7 flags the sRGB variant", "[asset_cooker][dds]") {
  const auto bytes = makeDx10Bc7(4, 4, 100);  // DXGI_BC7_UNORM_SRGB
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isOk());
  CHECK(result.value().srgb == true);
  CHECK(result.value().baseMipBlockBytes.size() == 16);
}

TEST_CASE("parseDdsBc7 accepts the legacy fourCC BC7 spelling as linear", "[asset_cooker][dds]") {
  const std::size_t baseMipBytes = 16;
  std::vector<std::uint8_t> bytes(128 + baseMipBytes, 0);
  std::memcpy(bytes.data(), "DDS ", 4);
  putU32LE(bytes, kHeaderOffset + 0, 124);
  putU32LE(bytes, kHeaderOffset + 8, 4);
  putU32LE(bytes, kHeaderOffset + 12, 4);
  putU32LE(bytes, kHeaderOffset + kPfOffset, 32);
  const std::uint32_t fourCCBC7 = ('B' << 0) | ('C' << 8) | ('7' << 16);
  putU32LE(bytes, kHeaderOffset + kPfOffset + 8, fourCCBC7);
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isOk());
  CHECK(result.value().srgb == false);
  CHECK(result.value().width == 4);
}

TEST_CASE("parseDdsBc7 rejects a bad magic", "[asset_cooker][dds]") {
  auto bytes = makeDx10Bc7(4, 4, 99);
  bytes[0] = 'X';
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isErr());
  CHECK(result.error() == DdsParseError::MalformedHeader);
}

TEST_CASE("parseDdsBc7 rejects unsupported DXGI formats", "[asset_cooker][dds]") {
  SECTION("BC7_TYPELESS (98)") {
    const auto bytes = makeDx10Bc7(4, 4, 98);
    const auto result = parseDdsBc7(bytes.data(), bytes.size());
    REQUIRE(result.isErr());
    CHECK(result.error() == DdsParseError::UnsupportedFormat);
  }
  SECTION("BC1_UNORM (71)") {
    const auto bytes = makeDx10Bc7(4, 4, 71);
    const auto result = parseDdsBc7(bytes.data(), bytes.size());
    REQUIRE(result.isErr());
    CHECK(result.error() == DdsParseError::UnsupportedFormat);
  }
}

TEST_CASE("parseDdsBc7 rejects a non-2D or array resource", "[asset_cooker][dds]") {
  SECTION("1D resource dimension") {
    const auto bytes = makeDx10Bc7(4, 4, 99, 1, 2 /*TEXTURE1D*/);
    const auto result = parseDdsBc7(bytes.data(), bytes.size());
    REQUIRE(result.isErr());
    CHECK(result.error() == DdsParseError::MalformedHeader);
  }
  SECTION("array size 2") {
    const auto bytes = makeDx10Bc7(4, 4, 99, 2);
    const auto result = parseDdsBc7(bytes.data(), bytes.size());
    REQUIRE(result.isErr());
    CHECK(result.error() == DdsParseError::MalformedHeader);
  }
}

TEST_CASE("parseDdsBc7 rejects non-4-aligned dimensions", "[asset_cooker][dds]") {
  const auto bytes = makeDx10Bc7(6, 4, 99);
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isErr());
  CHECK(result.error() == DdsParseError::NonAlignedDimensions);
}

TEST_CASE("parseDdsBc7 rejects a truncated payload", "[asset_cooker][dds]") {
  auto bytes = makeDx10Bc7(8, 8, 99);  // needs 64 block bytes
  bytes.resize(bytes.size() - 1);      // one byte short
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isErr());
  CHECK(result.error() == DdsParseError::Truncated);
}

TEST_CASE("parseDdsBc7 ignores extra mips beyond the base", "[asset_cooker][dds]") {
  auto bytes = makeDx10Bc7(8, 8, 99);
  // Declare two mips (DDSD_MIPMAPCOUNT at header offset 4's flag field,
  // count at offset 28) and append a plausible second-mip payload --
  // the parse must still succeed and return only the base mip.
  putU32LE(bytes, kHeaderOffset + 4, 0x1000 | 0x20000 /*CAPS|MIPMAPCOUNT*/);
  putU32LE(bytes, kHeaderOffset + 28, 2);
  bytes.insert(bytes.end(), 16, 0xAB);  // mip 1 (4x4 = one block)
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isOk());
  CHECK(result.value().baseMipBlockBytes.size() == 64);
}
