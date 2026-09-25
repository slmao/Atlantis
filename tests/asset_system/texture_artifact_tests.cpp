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
      encodeTextureArtifact(2, 2, TextureColorSpace::Srgb, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
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
  const auto encoded = encodeTextureArtifact(8, 12, TextureColorSpace::Unorm, TextureDataLayout::Bc7, 1, blocks.data(),
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
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());

  const std::vector<std::byte> expected = {
      std::byte{0x41}, std::byte{0x54}, std::byte{0x4C}, std::byte{0x54}, std::byte{0x45}, std::byte{0x58},
      std::byte{0x00}, std::byte{0x00},  // magic "ATLTEX\0\0"
      std::byte{0x03}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // schemaVersion = 3 (Spec 0045)
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
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
  bytes[0] = std::byte{0x00};
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::BadMagic);
}

TEST_CASE("decodeTextureArtifact rejects an unsupported schema version", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
  // schemaVersion's low byte, offset 8: v3 -> each retired version.
  for (const std::uint8_t retired : {std::uint8_t{1}, std::uint8_t{2}}) {
    bytes[8] = std::byte{retired};
    const auto result = decodeTextureArtifact(bytes);
    REQUIRE(result.isErr());
    CHECK(result.error() == TextureArtifactDecodeError::UnsupportedSchemaVersion);
  }
}

TEST_CASE("decodeTextureArtifact rejects a dimension exceeding kMaxTextureDimension", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
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
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
  bytes[20] = std::byte{0x02};  // format's low byte, offset 20: 0 -> 2 (unknown)
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::UnknownFormat);
}

TEST_CASE("decodeTextureArtifact rejects a mip count of 0 or beyond the dimensions' full chain", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
  for (const std::uint8_t count : {std::uint8_t{0}, std::uint8_t{2}}) {  // a 1x1 texture has exactly one level
    bytes[24] = std::byte{count};  // mipCount's low byte, offset 24
    const auto result = decodeTextureArtifact(bytes);
    REQUIRE(result.isErr());
    CHECK(result.error() == TextureArtifactDecodeError::UnsupportedMipCount);
  }
}

TEST_CASE("decodeTextureArtifact rejects an unknown data_layout value", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
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
      encodeTextureArtifact(6, 4, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
  bytes[36] = std::byte{0x01};  // dataLayout = 1 (Bc7)
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::NonAlignedDimensions);
}

TEST_CASE("decodeTextureArtifact rejects an inconsistent pixelDataOffset", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
  bytes[28] = std::byte{0x29};  // pixelDataOffset's low byte, offset 28: 40 -> 41
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::InconsistentPixelDataSize);
}

TEST_CASE("decodeTextureArtifact rejects an inconsistent pixelDataSizeBytes", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
  bytes[32] = std::byte{0x05};  // pixelDataSizeBytes's low byte, offset 32: 4 -> 5
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::InconsistentPixelDataSize);
}

TEST_CASE("decodeTextureArtifact rejects a truncated buffer (total size mismatch)", "[asset_system]") {
  const auto pixels = makeRgbaBytes(1, 1);
  auto bytes =
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
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
      encodeTextureArtifact(4, 4, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
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
      encodeTextureArtifact(1, 1, TextureColorSpace::Unorm, TextureDataLayout::Rgba8, 1, pixels.data(), pixels.size());
  const std::uint32_t huge = 0x00010000;  // 65536 > kMaxTextureDimension
  for (int i = 0; i < 4; ++i) {
    bytes[12 + static_cast<std::size_t>(i)] = static_cast<std::byte>((huge >> (8 * i)) & 0xFFU);  // width
    bytes[16 + static_cast<std::size_t>(i)] = static_cast<std::byte>((huge >> (8 * i)) & 0xFFU);  // height
  }
  const auto result = decodeTextureArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::DimensionExceedsMaximum);
}

// ---------------------------------------------------------------------------
// Spec 0045 (ADR-0093): schema v3 mip chains and the level-layout authority.
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::vector<std::uint8_t> makeChainBytes(std::uint32_t width, std::uint32_t height,
                                                       TextureDataLayout layout, std::uint32_t mipCount) {
  std::vector<std::uint8_t> bytes(
      static_cast<std::size_t>(textureMipChainByteCount(width, height, layout, mipCount)));
  for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>((i * 11 + 3) % 256);
  return bytes;
}

void writeU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFU);
}

}  // namespace

TEST_CASE("fullMipChainLength counts the levels down to 1x1", "[asset_system][mip]") {
  CHECK(fullMipChainLength(1, 1) == 1);
  CHECK(fullMipChainLength(4, 4) == 3);
  CHECK(fullMipChainLength(256, 128) == 9);    // the string-lights DDS
  CHECK(fullMipChainLength(128, 256) == 9);    // 1:2
  CHECK(fullMipChainLength(2560, 1440) == 12); // non-power-of-two (Bistro has one 2560)
  CHECK(fullMipChainLength(4096, 4096) == 13);
  CHECK(fullMipChainLength(8192, 8192) == 14);
  CHECK(fullMipChainLength(0, 4) == 0);
}

TEST_CASE("textureMipLevels lays out a 256x128 BC7 chain like the string-lights DDS", "[asset_system][mip]") {
  const auto levels = textureMipLevels(256, 128, TextureDataLayout::Bc7, 9);
  REQUIRE(levels.size() == 9);
  const std::uint32_t widths[] = {256, 128, 64, 32, 16, 8, 4, 2, 1};
  const std::uint32_t heights[] = {128, 64, 32, 16, 8, 4, 2, 1, 1};
  const std::uint64_t sizes[] = {32768, 8192, 2048, 512, 128, 32, 16, 16, 16};  // sub-block levels: one block
  std::uint64_t offset = 0;
  for (std::size_t i = 0; i < levels.size(); ++i) {
    INFO("level " << i);
    CHECK(levels[i].width == widths[i]);
    CHECK(levels[i].height == heights[i]);
    CHECK(levels[i].sizeBytes == sizes[i]);
    CHECK(levels[i].offsetBytes == offset);
    offset += sizes[i];
  }
  CHECK(offset == 43728);  // + the 148-byte DX10 header = the 43,876-byte file
  CHECK(textureMipChainByteCount(256, 128, TextureDataLayout::Bc7, 9) == 43728);
  // A partial chain is a prefix of the full one.
  CHECK(textureMipChainByteCount(256, 128, TextureDataLayout::Bc7, 2) == 32768 + 8192);
}

TEST_CASE("textureMipLevels lays out RGBA8, 1:2 and non-power-of-two chains", "[asset_system][mip]") {
  const auto rgba = textureMipLevels(4, 2, TextureDataLayout::Rgba8, 3);
  REQUIRE(rgba.size() == 3);
  CHECK(rgba[0].sizeBytes == 32);
  CHECK((rgba[1].width == 2 && rgba[1].height == 1 && rgba[1].sizeBytes == 8 && rgba[1].offsetBytes == 32));
  CHECK((rgba[2].width == 1 && rgba[2].height == 1 && rgba[2].sizeBytes == 4 && rgba[2].offsetBytes == 40));

  const auto tall = textureMipLevels(4, 8, TextureDataLayout::Bc7, 4);
  REQUIRE(tall.size() == 4);
  CHECK((tall[1].width == 2 && tall[1].height == 4));
  CHECK((tall[3].width == 1 && tall[3].height == 1));

  const auto npot = textureMipLevels(2560, 1440, TextureDataLayout::Bc7, 12);
  REQUIRE(npot.size() == 12);
  CHECK((npot[6].width == 40 && npot[6].height == 22));  // floor at every halving
  CHECK((npot[9].width == 5 && npot[9].height == 2));
  CHECK((npot[10].width == 2 && npot[10].height == 1));
  CHECK(npot[9].sizeBytes == 2 * 1 * 16);  // ceil(5/4) x ceil(2/4) blocks
}

TEST_CASE("the largest texture's full chain fits the header's 32-bit size field", "[asset_system][mip]") {
  const std::uint64_t total = textureMipChainByteCount(8192, 8192, TextureDataLayout::Bc7, 14);
  CHECK(total > bc7BlockByteCount(8192, 8192));
  CHECK(total < 0x100000000ULL);
  CHECK(textureMipChainByteCount(8192, 8192, TextureDataLayout::Rgba8, 14) < 0x100000000ULL);
}

TEST_CASE("encodeTextureArtifact then decodeTextureArtifact round-trips full and partial chains in both layouts",
          "[asset_system][mip]") {
  struct Case {
    std::uint32_t width, height;
    TextureDataLayout layout;
    std::uint32_t mipCount;
  };
  for (const Case c : {Case{16, 8, TextureDataLayout::Bc7, 5}, Case{16, 8, TextureDataLayout::Bc7, 2},
                       Case{256, 128, TextureDataLayout::Bc7, 9}, Case{4, 2, TextureDataLayout::Rgba8, 3},
                       Case{8, 8, TextureDataLayout::Rgba8, 1}}) {
    INFO(c.width << "x" << c.height << " levels " << c.mipCount);
    const auto chain = makeChainBytes(c.width, c.height, c.layout, c.mipCount);
    const auto encoded = encodeTextureArtifact(c.width, c.height, TextureColorSpace::Srgb, c.layout, c.mipCount,
                                               chain.data(), chain.size());
    REQUIRE(encoded.size() == kTextureArtifactHeaderSizeBytes + chain.size());
    const auto decoded = decodeTextureArtifact(encoded);
    REQUIRE(decoded.isOk());
    CHECK(decoded.value().mipCount == c.mipCount);
    CHECK(decoded.value().layout == c.layout);
    CHECK(decoded.value().pixelBytes == chain);
  }
}

TEST_CASE("decodeTextureArtifact rejects a chain one byte short or long", "[asset_system][mip]") {
  const auto chain = makeChainBytes(16, 8, TextureDataLayout::Bc7, 5);
  const auto encoded =
      encodeTextureArtifact(16, 8, TextureColorSpace::Unorm, TextureDataLayout::Bc7, 5, chain.data(), chain.size());
  for (const int delta : {-1, 1}) {
    INFO("delta " << delta);
    auto bytes = encoded;
    if (delta < 0) {
      bytes.pop_back();
    } else {
      bytes.push_back(std::byte{0});
    }
    // The header's size field says the altered size, so it is the chain
    // check -- not the total-length check -- that rejects.
    writeU32(bytes, 32, static_cast<std::uint32_t>(chain.size() + delta));
    const auto result = decodeTextureArtifact(bytes);
    REQUIRE(result.isErr());
    CHECK(result.error() == TextureArtifactDecodeError::InconsistentPixelDataSize);
  }
  // A header claiming one fewer level than the payload carries.
  auto fewer = encoded;
  writeU32(fewer, 24, 4);
  const auto result = decodeTextureArtifact(fewer);
  REQUIRE(result.isErr());
  CHECK(result.error() == TextureArtifactDecodeError::InconsistentPixelDataSize);
}
