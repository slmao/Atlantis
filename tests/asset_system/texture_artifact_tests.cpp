#include <atlantis/asset_system/texture_artifact.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

[[nodiscard]] std::vector<std::uint8_t> makeRgbaBytes(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 4);
  for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(i % 256);
  return bytes;
}

}  // namespace

TEST_CASE("encodeTextureArtifact then decodeTextureArtifact round-trips exactly", "[asset_system]") {
  const auto pixels = makeRgbaBytes(2, 2);
  const auto encoded = encodeTextureArtifact(2, 2, TextureColorSpace::Srgb, pixels.data(), pixels.size());
  REQUIRE(encoded.size() == kTextureArtifactHeaderSizeBytes + pixels.size());

  const auto decoded = decodeTextureArtifact(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().width == 2);
  CHECK(decoded.value().height == 2);
  CHECK(decoded.value().colorSpace == TextureColorSpace::Srgb);
  CHECK(decoded.value().pixelBytes == pixels);
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
  const auto encoded = encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, pixels.data(), pixels.size());

  const std::vector<std::byte> expected = {
      std::byte{0x41}, std::byte{0x54}, std::byte{0x4C}, std::byte{0x54}, std::byte{0x45}, std::byte{0x58},
      std::byte{0x00}, std::byte{0x00},  // magic "ATLTEX\0\0"
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // schemaVersion = 1
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // width = 1
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // height = 1
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // format = 0 (Unorm)
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // mipCount = 1
      std::byte{0x24}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // pixelDataOffset = 36
      std::byte{0x04}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // pixelDataSizeBytes = 4
      std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0xFF},  // pixel data
  };
  REQUIRE(expected.size() == 40);
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
  auto bytes = encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, pixels.data(), pixels.size());
  bytes[0] = std::byte{0x00};
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::BadMagic);
}

TEST_CASE("decodeTextureArtifact rejects an unsupported schema version", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes = encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, pixels.data(), pixels.size());
  bytes[8] = std::byte{0x02};  // schemaVersion's low byte, offset 8
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::UnsupportedSchemaVersion);
}

TEST_CASE("decodeTextureArtifact rejects a dimension exceeding kMaxTextureDimension", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes = encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, pixels.data(), pixels.size());
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
  auto bytes = encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, pixels.data(), pixels.size());
  bytes[20] = std::byte{0x02};  // format's low byte, offset 20: 0 -> 2 (unknown)
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::UnknownFormat);
}

TEST_CASE("decodeTextureArtifact rejects a mip count other than 1", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes = encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, pixels.data(), pixels.size());
  bytes[24] = std::byte{0x02};  // mipCount's low byte, offset 24: 1 -> 2
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::UnsupportedMipCount);
}

TEST_CASE("decodeTextureArtifact rejects an inconsistent pixelDataOffset", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes = encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, pixels.data(), pixels.size());
  bytes[28] = std::byte{0x25};  // pixelDataOffset's low byte, offset 28: 36 -> 37
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::InconsistentPixelDataSize);
}

TEST_CASE("decodeTextureArtifact rejects an inconsistent pixelDataSizeBytes", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes = encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, pixels.data(), pixels.size());
  bytes[32] = std::byte{0x05};  // pixelDataSizeBytes's low byte, offset 32: 4 -> 5
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::InconsistentPixelDataSize);
}

TEST_CASE("decodeTextureArtifact rejects a truncated buffer (total size mismatch)", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes = encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, pixels.data(), pixels.size());
  bytes.pop_back();
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
  auto bytes = encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, pixels.data(), pixels.size());
  const std::uint32_t huge = 0x00010000;  // 65536 > kMaxTextureDimension
  for (int i = 0; i < 4; ++i) {
    bytes[12 + static_cast<std::size_t>(i)] = static_cast<std::byte>((huge >> (8 * i)) & 0xFFU);  // width
    bytes[16 + static_cast<std::size_t>(i)] = static_cast<std::byte>((huge >> (8 * i)) & 0xFFU);  // height
  }
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::DimensionExceedsMaximum);
}
