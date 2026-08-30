#include <atlantis/asset_system/material_artifact.h>

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace atlantis::asset_system;

namespace {

constexpr float kDefaultBaseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

}  // namespace

TEST_CASE("encodeMaterialArtifact then decodeMaterialArtifact round-trips exactly", "[asset_system][material]") {
  const auto encoded =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 0x0102030405060708ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f);
  REQUIRE(encoded.size() == kMaterialArtifactHeaderSizeBytes);

  const auto decoded = decodeMaterialArtifact(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().kind == MaterialKind::UnlitTextured);
  CHECK(decoded.value().textureAsset == 0x0102030405060708ULL);
  CHECK(decoded.value().filter == MaterialSamplerFilter::Linear);
  CHECK(decoded.value().addressMode == MaterialSamplerAddressMode::Repeat);
  CHECK(decoded.value().baseColorFactor[0] == 1.0f);
  CHECK(decoded.value().baseColorFactor[1] == 1.0f);
  CHECK(decoded.value().baseColorFactor[2] == 1.0f);
  CHECK(decoded.value().baseColorFactor[3] == 1.0f);
  CHECK(decoded.value().metallicFactor == 1.0f);
  CHECK(decoded.value().roughnessFactor == 1.0f);
}

TEST_CASE("encodeMaterialArtifact then decodeMaterialArtifact round-trips MaterialKind::PbrDirectLit and its own "
          "PBR parameters",
          "[asset_system][material]") {
  const float baseColorFactor[4] = {0.8f, 0.2f, 0.1f, 1.0f};
  const auto encoded =
      encodeMaterialArtifact(MaterialKind::PbrDirectLit, 0x0102030405060708ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, baseColorFactor, 0.5f, 0.25f);
  const auto decoded = decodeMaterialArtifact(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().kind == MaterialKind::PbrDirectLit);
  CHECK(decoded.value().baseColorFactor[0] == 0.8f);
  CHECK(decoded.value().baseColorFactor[1] == 0.2f);
  CHECK(decoded.value().baseColorFactor[2] == 0.1f);
  CHECK(decoded.value().baseColorFactor[3] == 1.0f);
  CHECK(decoded.value().metallicFactor == 0.5f);
  CHECK(decoded.value().roughnessFactor == 0.25f);
}

TEST_CASE("encodeMaterialArtifact round-trips Nearest filter and ClampToEdge address mode",
          "[asset_system][material]") {
  const auto encoded =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 42ULL, MaterialSamplerFilter::Nearest,
                              MaterialSamplerAddressMode::ClampToEdge, kDefaultBaseColorFactor, 1.0f, 1.0f);
  const auto decoded = decodeMaterialArtifact(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().filter == MaterialSamplerFilter::Nearest);
  CHECK(decoded.value().addressMode == MaterialSamplerAddressMode::ClampToEdge);
}

TEST_CASE("encodeMaterialArtifact matches an independently-computed expected byte vector",
          "[asset_system][material]") {
  // Pins the little-endian contract exactly, matching
  // texture_artifact_tests.cpp's own identical pinning test -- the real
  // guarantee against a host-endian regression is
  // material_artifact.cpp's own appendU32LE/appendU64LE/appendFloatLE-only
  // discipline, verified by code review, not something a byte-comparison
  // test on little-endian-only hardware can fully enforce by itself.
  // 1.0f's own IEEE-754 bit pattern is 0x3F800000, little-endian bytes
  // 00 00 80 3F -- independently computed, not copied from production.
  const auto encoded =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 0x0102030405060708ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f);

  const std::vector<std::byte> expected = {
      std::byte{0x41}, std::byte{0x54}, std::byte{0x4C}, std::byte{0x4D}, std::byte{0x41}, std::byte{0x54},
      std::byte{0x00}, std::byte{0x00},                                        // magic "ATLMAT\0\0"
      std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // schemaVersion = 2
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // kind = 0 (UnlitTextured)
      std::byte{0x08}, std::byte{0x07}, std::byte{0x06}, std::byte{0x05}, std::byte{0x04}, std::byte{0x03},
      std::byte{0x02}, std::byte{0x01},                                        // texture_asset_id = 0x0102030405060708
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // filter = 1 (Linear)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // address_mode = 0 (Repeat)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F},      // baseColorFactor[0] = 1.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F},      // baseColorFactor[1] = 1.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F},      // baseColorFactor[2] = 1.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F},      // baseColorFactor[3] = 1.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F},      // metallicFactor = 1.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3F},      // roughnessFactor = 1.0f
  };
  REQUIRE(expected.size() == 56);
  CHECK(encoded == expected);
}

TEST_CASE("decodeMaterialArtifact rejects a buffer too small for the header", "[asset_system][material]") {
  const std::vector<std::byte> tooSmall(10, std::byte{0});
  const auto result = decodeMaterialArtifact(tooSmall);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::TruncatedHeader);
}

TEST_CASE("decodeMaterialArtifact rejects a real, old, 32-byte schema-version-1 artifact",
          "[asset_system][material]") {
  // A genuine, hand-assembled schema-version-1 (Plan 0018) artifact --
  // not a truncated version-2 one -- confirming the "no dual-version
  // reader" contract (ADR-0066 item 3) rejects it outright by size, not
  // merely by version field, since a 32-byte buffer is TruncatedHeader
  // under the current 56-byte-only decoder.
  std::vector<std::byte> oldArtifact(32, std::byte{0});
  const std::array<char, 8> magic = {'A', 'T', 'L', 'M', 'A', 'T', '\0', '\0'};
  for (std::size_t i = 0; i < magic.size(); ++i) oldArtifact[i] = static_cast<std::byte>(magic[i]);
  oldArtifact[8] = std::byte{0x01};  // schemaVersion = 1, low byte
  const auto result = decodeMaterialArtifact(oldArtifact);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::TruncatedHeader);
}

TEST_CASE("decodeMaterialArtifact rejects a buffer larger than the fixed 56-byte record",
          "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f);
  bytes.push_back(std::byte{0xFF});
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnexpectedSize);
}

TEST_CASE("decodeMaterialArtifact rejects a bad magic", "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f);
  bytes[0] = std::byte{0x00};
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::BadMagic);
}

TEST_CASE("decodeMaterialArtifact rejects an unsupported schema version", "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f);
  bytes[8] = std::byte{0x03};  // schemaVersion's low byte, offset 8: 2 -> 3 (unsupported)
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnsupportedSchemaVersion);
}

TEST_CASE("decodeMaterialArtifact rejects an unknown kind value", "[asset_system][material]") {
  // Plan 0023 Milestone 1: this literal must name a value still
  // genuinely unrecognized now that 2 (PbrDirectLit) is also valid -- 3
  // here, not 2.
  auto bytes =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f);
  bytes[12] = std::byte{0x03};  // kind's low byte, offset 12: 0 -> 3 (unknown)
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnknownMaterialKind);
}

TEST_CASE("encodeMaterialArtifact then decodeMaterialArtifact round-trips MaterialKind::LitTextured",
          "[asset_system][material]") {
  const auto encoded =
      encodeMaterialArtifact(MaterialKind::LitTextured, 0x0102030405060708ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f);
  const auto decoded = decodeMaterialArtifact(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().kind == MaterialKind::LitTextured);
  CHECK(decoded.value().textureAsset == 0x0102030405060708ULL);
}

// Plan 0019 P5/V7, V24, extended by Plan 0023 Milestone 1: kindToField()'s
// own no-default switch, real, not merely decorative -- a positive probe
// (temporarily removing a case) reproduces a real C4062 build failure;
// this is the restored, negative half, verified empty-diff against the
// positive probe's own reversion.
TEST_CASE("kindToField()'s own C4062 protection: each MaterialKind decodes to its own distinct field value",
          "[asset_system][material]") {
  const auto unlit =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f);
  const auto lit =
      encodeMaterialArtifact(MaterialKind::LitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f);
  const auto pbr =
      encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f);
  CHECK(unlit[12] == std::byte{0x00});
  CHECK(lit[12] == std::byte{0x01});
  CHECK(pbr[12] == std::byte{0x02});
}

TEST_CASE("decodeMaterialArtifact rejects an unknown filter value", "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f);
  bytes[24] = std::byte{0x02};  // filter's low byte, offset 24: 1 -> 2 (unknown)
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnknownFilter);
}

TEST_CASE("decodeMaterialArtifact rejects an unknown address_mode value", "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f);
  bytes[28] = std::byte{0x02};  // address_mode's low byte, offset 28: 0 -> 2 (unknown)
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnknownAddressMode);
}

TEST_CASE("decodeMaterialArtifact rejects a baseColorFactor component above 1.0", "[asset_system][material]") {
  const float outOfRange[4] = {1.5f, 1.0f, 1.0f, 1.0f};
  auto bytes = encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                                       MaterialSamplerAddressMode::Repeat, outOfRange, 1.0f, 1.0f);
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::BaseColorFactorOutOfRange);
}

TEST_CASE("decodeMaterialArtifact rejects a negative metallicFactor", "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, -0.1f, 1.0f);
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::MaterialFactorOutOfRange);
}

TEST_CASE("decodeMaterialArtifact rejects a roughnessFactor above 1.0", "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.1f);
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::MaterialFactorOutOfRange);
}
