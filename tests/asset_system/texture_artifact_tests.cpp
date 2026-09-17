#include <atlantis/asset_system/texture_artifact.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

[[nodiscard]] std::vector<std::uint8_t> makeRgbaBytes(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 4);
  for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(i % 256);
  return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> makeBc7Bytes(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(bc7BlockByteCount(width, height)));
  for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>((i * 7) % 256);
  return bytes;
}

}  // namespace

TEST_CASE("encodeTextureArtifact then decodeTextureArtifact round-trips exactly", "[asset_system]") {
  const auto pixels = makeRgbaBytes(2, 2);
  const auto encoded =
      encodeTextureArtifact(2, 2, TextureColorSpace::Srgb, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  REQUIRE(encoded.size() == kTextureArtifactHeaderSizeBytes + pixels.size());

  const auto decoded = decodeTextureArtifact(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().width == 2);
  CHECK(decoded.value().height == 2);
  CHECK(decoded.value().colorSpace == TextureColorSpace::Srgb);
  CHECK(decoded.value().layout == TextureDataLayout::Rgba8);
  CHECK(decoded.value().pixelBytes == pixels);
}

TEST_CASE("encodeTextureArtifact then decodeTextureArtifact round-trips a BC7 texture", "[asset_system]") {
  const auto blocks = makeBc7Bytes(8, 12);  // 2x3 blocks = 96 bytes
  REQUIRE(blocks.size() == 96);
  const auto encoded = encodeTextureArtifact(8, 12, TextureColorSpace::Unorm, TextureDataLayout::Bc7, blocks.data(),
                                             blocks.size());
  REQUIRE(encoded.size() == kTextureArtifactHeaderSizeBytes + blocks.size());

  const auto decoded = decodeTextureArtifact(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().width == 8);
  CHECK(decoded.value().height == 12);
  CHECK(decoded.value().layout == TextureDataLayout::Bc7);
  CHECK(decoded.value().pixelBytes == blocks);
}

TEST_CASE("bc7BlockByteCount computes ceil-division per dimension", "[asset_system]") {
  CHECK(bc7BlockByteCount(4, 4) == 16);    // exactly one block
  CHECK(bc7BlockByteCount(8, 8) == 64);    // 2x2 blocks
  CHECK(bc7BlockByteCount(12, 8) == 96);   // 3x2 blocks
  CHECK(bc7BlockByteCount(8192, 8192) == 67108864);  // 2048x2048 blocks, the maximum dimension's exact count
}

TEST_CASE("encodeTextureArtifact matches an independently-computed expected byte vector", "[asset_system]") {
  // Pins the little-endian contract for a 1x1 Unorm texture, four pixel
  // bytes {0x11, 0x22, 0x33, 0xFF} -- matching mesh_artifact_tests.cpp's
  // own disclosed-limitation note: the real guarantee against a
  // host-endian regression is texture_artifact.cpp's own
  // appendU32LE-only discipline, verified by code review, not something
  // a byte-comparison test on little-endian-only hardware can fully
  // enforce by itself.
  const std::vector<std::uint8_t> pixels = {0x11, 0x22, 0x33, 0xFF};
  const auto encoded =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());

  const std::vector<std::byte> expected = {
      std::byte{0x41}, std::byte{0x54}, std::byte{0x4C}, std::byte{0x54}, std::byte{0x45}, std::byte{0x58},
      std::byte{0x00}, std::byte{0x00},  // magic "ATLTEX\0\0"
      std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // schemaVersion = 2 (Spec 0038)
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // width = 1
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // height = 1
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // format = 0 (Unorm)
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // mipCount = 1
      std::byte{0x28}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // pixelDataOffset = 40
      std::byte{0x04}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // pixelDataSizeBytes = 4
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // dataLayout = 0 (Rgba8)
      std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0xFF},  // pixel data
  };
  REQUIRE(expected.size() == 44);
  CHECK(encoded == expected);
}

TEST_CASE("decodeTextureArtifact rejects a buffer too small for the header", "[asset_system]") {
  const std::vector<std::byte> tooSmall(10, std::byte{0});
  const auto result = decodeTextureArtifact(tooSmall);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::TruncatedHeader);
}

TEST_CASE("decodeTextureArtifact rejects a bad magic", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  bytes[0] = std::byte{0x00};
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::BadMagic);
}

TEST_CASE("decodeTextureArtifact rejects an unsupported schema version", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  bytes[8] = std::byte{0x01};  // schemaVersion's low byte, offset 8: v2 -> v1 (the retired version)
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::UnsupportedSchemaVersion);
}

TEST_CASE("decodeTextureArtifact rejects a dimension exceeding kMaxTextureDimension", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  // width field, offset 12: set to kMaxTextureDimension + 1.
  const std::uint32_t tooLarge = kMaxTextureDimension + 1;
  bytes[12] = static_cast<std::byte>(tooLarge & 0xFFU);
  bytes[13] = static_cast<std::byte>((tooLarge >> 8) & 0xFFU);
  bytes[14] = static_cast<std::byte>((tooLarge >> 16) & 0xFFU);
  bytes[15] = static_cast<std::byte>((tooLarge >> 24) & 0xFFU);
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::DimensionExceedsMaximum);
}

TEST_CASE("decodeTextureArtifact rejects an unknown format value", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  bytes[20] = std::byte{0x02};  // format's low byte, offset 20: 0 -> 2 (unknown)
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::UnknownFormat);
}

TEST_CASE("decodeTextureArtifact rejects a mip count other than 1", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  bytes[24] = std::byte{0x02};  // mipCount's low byte, offset 24: 1 -> 2
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::UnsupportedMipCount);
}

TEST_CASE("decodeTextureArtifact rejects an unknown data_layout value", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  bytes[36] = std::byte{0x02};  // dataLayout's low byte, offset 36: 0 -> 2 (unknown)
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::UnknownDataLayout);
}

TEST_CASE("decodeTextureArtifact rejects a non-4-aligned Bc7 base mip", "[asset_system]") {
  // A hand-crafted header: width 6 (not a multiple of 4) with layout
  // Bc7. The size fields are made consistent with an RGBA8 payload so
  // the alignment rejection, not the size rejection, is what fires.
  const auto pixels = makeRgbaBytes(6, 4);
  auto bytes =
      encodeTextureArtifact(6, 4, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  bytes[36] = std::byte{0x01};  // dataLayout = 1 (Bc7)
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::NonAlignedDimensions);
}

TEST_CASE("decodeTextureArtifact rejects an inconsistent pixelDataOffset", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  bytes[28] = std::byte{0x29};  // pixelDataOffset's low byte, offset 28: 40 -> 41
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::InconsistentPixelDataSize);
}

TEST_CASE("decodeTextureArtifact rejects an inconsistent pixelDataSizeBytes", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  bytes[32] = std::byte{0x05};  // pixelDataSizeBytes's low byte, offset 32: 4 -> 5
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::InconsistentPixelDataSize);
}

TEST_CASE("decodeTextureArtifact rejects a truncated buffer (total size mismatch)", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  bytes.pop_back();
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::InconsistentPixelDataSize);
}

TEST_CASE("decodeTextureArtifact rejects a Bc7 payload whose size matches RGBA8 instead of blocks", "[asset_system]") {
  // Header flipped to Bc7 on a payload really sized for RGBA8: the
  // expected-size derivation by layout must catch the mismatch.
  const auto pixels = makeRgbaBytes(4, 4);
  auto bytes =
      encodeTextureArtifact(4, 4, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  bytes[36] = std::byte{0x01};  // dataLayout = 1 (Bc7); expected size becomes 64, actual 64 -> consistent!
  // 4x4 RGBA8 = 64 bytes, and 4x4 BC7 = 1 block = 16 bytes: sizes DO
  // differ, so this header is inconsistent and must be rejected.
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::InconsistentPixelDataSize);
}

TEST_CASE("decodeTextureArtifact's overflow-check ordering rejects a header whose 32-bit-truncated size would "
          "otherwise pass",
          "[asset_system]") {
  // A crafted header naming dimensions large enough that width*height*4
  // would overflow a 32-bit product if computed narrowly -- the
  // DimensionExceedsMaximum bound check (against kMaxTextureDimension)
  // must reject this before any 32-bit multiplication is ever attempted.
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, pixels.data(), pixels.size());
  const std::uint32_t huge = 0x00010000;  // 65536 > kMaxTextureDimension
  for (int i = 0; i < 4; ++i) {
    bytes[12 + static_cast<std::size_t>(i)] = static_cast<std::byte>((huge >> (8 * i)) & 0xFFU);  // width
    bytes[16 + static_cast<std::size_t>(i)] = static_cast<std::byte>((huge >> (8 * i)) & 0xFFU);  // height
  }
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::DimensionExceedsMaximum);
}
