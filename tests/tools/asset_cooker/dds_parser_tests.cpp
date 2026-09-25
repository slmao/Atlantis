#include "dds_parser.h"

#include <atlantis/result.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <fstream>
#include <algorithm>
#include <vector>

// Spec 0038 / Plan 0038 Milestone 2: pure, GPU-independent tests for the
// DDS+DX10 BC7 header parser -- every input is a literal byte vector
// built by these helpers, no fixture files. The header layout mirrors
// dds_parser.cpp's own documented offsets.

namespace {

using atlantis::asset_cooker::DdsParseError;
using atlantis::asset_cooker::parseDdsBc7;

constexpr std::size_t kHeaderOffset = 4;    // after "DDS "
constexpr std::size_t kPfOffset = 72;
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
  REQUIRE(result.value().blockBytes.size() == 64);  // 2x2 blocks
  for (std::size_t i = 0; i < 64; ++i) {
    CHECK(result.value().blockBytes[i] == bytes[kDx10Offset + 20 + i]);
  }
}

TEST_CASE("parseDdsBc7 consumes BC7_TYPELESS as linear data", "[asset_cooker][dds]") {
  // Plan 0037's census-gate correction (ruled 2026-09-19): Bistro's 119
  // _ddna normal maps are DXGI 98; they cook as Bc7Unorm.
  const auto bytes = makeDx10Bc7(4, 4, 98);
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isOk());
  CHECK(result.value().srgb == false);
  CHECK(result.value().blockBytes.size() == 16);
}

TEST_CASE("parseDdsBc7 flags the sRGB variant", "[asset_cooker][dds]") {
  const auto bytes = makeDx10Bc7(4, 4, 100);  // DXGI_BC7_UNORM_SRGB
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isOk());
  CHECK(result.value().srgb == true);
  CHECK(result.value().blockBytes.size() == 16);
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
  // DXGI 98 (BC7_TYPELESS) is no longer in this list: Plan 0037's census
  // gate ruled it consumable as linear (see the TYPELESS test above).
  SECTION("BC6H_UF16 (95)") {
    const auto bytes = makeDx10Bc7(4, 4, 95);
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

TEST_CASE("parseDdsBc7 returns the full declared mip chain, level 0 first", "[asset_cooker][dds]") {
  auto bytes = makeDx10Bc7(8, 8, 99);
  // Declare the full chain (DDSD_MIPMAPCOUNT in the flags at header
  // offset 4, the count at offset 24): 8x8, 4x4, 2x2, 1x1 = 64 + 16 + 16 +
  // 16 bytes, appended exactly as DDS stores it, no padding between levels.
  putU32LE(bytes, kHeaderOffset + 4, 0x1000 | 0x20000 /*CAPS|MIPMAPCOUNT*/);
  putU32LE(bytes, kHeaderOffset + 24, 4);
  bytes.insert(bytes.end(), 16, 0xAB);  // level 1
  bytes.insert(bytes.end(), 16, 0xCD);  // level 2 (sub-block: one block)
  bytes.insert(bytes.end(), 16, 0xEF);  // level 3
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isOk());
  CHECK(result.value().mipCount == 4);
  REQUIRE(result.value().blockBytes.size() == 112);
  for (std::size_t i = 0; i < 112; ++i) CHECK(result.value().blockBytes[i] == bytes[kDx10Offset + 20 + i]);
}

// ---------------------------------------------------------------------------
// Spec 0045 ruling Q5: the payload must be exactly the declared chain.
// ---------------------------------------------------------------------------

namespace {

// makeDx10Bc7() plus DDSD_MIPMAPCOUNT = mipCount and the levels past the
// base, exactly sized.
[[nodiscard]] std::vector<std::uint8_t> makeDx10Bc7Chain(std::uint32_t width, std::uint32_t height,
                                                         std::uint32_t mipCount) {
  auto bytes = makeDx10Bc7(width, height, 99);
  putU32LE(bytes, kHeaderOffset + 4, 0x1000 | 0x20000);
  putU32LE(bytes, kHeaderOffset + 24, mipCount);
  for (std::uint32_t level = 1; level < mipCount; ++level) {
    const std::uint32_t w = std::max(1U, width >> level);
    const std::uint32_t h = std::max(1U, height >> level);
    bytes.insert(bytes.end(), static_cast<std::size_t>(((w + 3) / 4) * ((h + 3) / 4)) * 16,
                 static_cast<std::uint8_t>(level));
  }
  return bytes;
}

}  // namespace

TEST_CASE("parseDdsBc7 without DDSD_MIPMAPCOUNT reads exactly one level", "[asset_cooker][dds]") {
  const auto bytes = makeDx10Bc7(8, 4, 99);
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isOk());
  CHECK(result.value().mipCount == 1);
  CHECK(result.value().blockBytes.size() == 32);
}

TEST_CASE("parseDdsBc7 rejects a payload shorter than the declared chain as Truncated", "[asset_cooker][dds]") {
  auto bytes = makeDx10Bc7Chain(16, 8, 5);
  bytes.pop_back();  // the 1x1 level one byte short
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isErr());
  CHECK(result.error() == DdsParseError::Truncated);
}

TEST_CASE("parseDdsBc7 rejects bytes beyond the declared chain as MalformedHeader", "[asset_cooker][dds]") {
  auto bytes = makeDx10Bc7Chain(16, 8, 5);
  bytes.push_back(0);
  const auto trailing = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(trailing.isErr());
  CHECK(trailing.error() == DdsParseError::MalformedHeader);

  // A full chain's payload under a header declaring fewer levels is the
  // same case: bytes the header does not account for.
  auto underDeclared = makeDx10Bc7Chain(16, 8, 5);
  putU32LE(underDeclared, kHeaderOffset + 24, 3);
  const auto result = parseDdsBc7(underDeclared.data(), underDeclared.size());
  REQUIRE(result.isErr());
  CHECK(result.error() == DdsParseError::MalformedHeader);
}

TEST_CASE("parseDdsBc7 rejects a declared mip count of 0 or beyond the full chain", "[asset_cooker][dds]") {
  for (const std::uint32_t count : {0U, 6U}) {  // 16x8 has 5 levels
    auto bytes = makeDx10Bc7Chain(16, 8, 5);
    putU32LE(bytes, kHeaderOffset + 24, count);
    const auto result = parseDdsBc7(bytes.data(), bytes.size());
    REQUIRE(result.isErr());
    CHECK(result.error() == DdsParseError::MalformedHeader);
  }
}

TEST_CASE("parseDdsBc7 reads the committed string-lights DDS's full 9-level chain", "[asset_cooker][dds]") {
  std::ifstream file(ATLANTIS_STRINGLIGHTS_DDS_PATH, std::ios::binary);
  REQUIRE(file.is_open());
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  REQUIRE(bytes.size() == 43876);
  const auto result = parseDdsBc7(bytes.data(), bytes.size());
  REQUIRE(result.isOk());
  CHECK(result.value().width == 256);
  CHECK(result.value().height == 128);
  CHECK(result.value().mipCount == 9);
  REQUIRE(result.value().blockBytes.size() == 43728);  // 43,876 - the 148-byte DX10 header
  CHECK(std::equal(result.value().blockBytes.begin(), result.value().blockBytes.end(), bytes.begin() + 148));
}
