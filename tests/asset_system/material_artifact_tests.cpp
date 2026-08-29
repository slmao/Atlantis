#include <atlantis/asset_system/material_artifact.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

TEST_CASE("encodeMaterialArtifact then decodeMaterialArtifact round-trips exactly", "[asset_system][material]") {
  const auto encoded = encodeMaterialArtifact(MaterialKind::UnlitTextured, 0x0102030405060708ULL,
                                               MaterialSamplerFilter::Linear, MaterialSamplerAddressMode::Repeat);
  REQUIRE(encoded.size() == kMaterialArtifactHeaderSizeBytes);

  const auto decoded = decodeMaterialArtifact(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().kind == MaterialKind::UnlitTextured);
  CHECK(decoded.value().textureAsset == 0x0102030405060708ULL);
  CHECK(decoded.value().filter == MaterialSamplerFilter::Linear);
  CHECK(decoded.value().addressMode == MaterialSamplerAddressMode::Repeat);
}

TEST_CASE("encodeMaterialArtifact round-trips Nearest filter and ClampToEdge address mode",
          "[asset_system][material]") {
  const auto encoded = encodeMaterialArtifact(MaterialKind::UnlitTextured, 42ULL, MaterialSamplerFilter::Nearest,
                                               MaterialSamplerAddressMode::ClampToEdge);
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
  // material_artifact.cpp's own appendU32LE/appendU64LE-only discipline,
  // verified by code review, not something a byte-comparison test on
  // little-endian-only hardware can fully enforce by itself.
  const auto encoded = encodeMaterialArtifact(MaterialKind::UnlitTextured, 0x0102030405060708ULL,
                                               MaterialSamplerFilter::Linear, MaterialSamplerAddressMode::Repeat);

  const std::vector<std::byte> expected = {
      std::byte{0x41}, std::byte{0x54}, std::byte{0x4C}, std::byte{0x4D}, std::byte{0x41}, std::byte{0x54},
      std::byte{0x00}, std::byte{0x00},  // magic "ATLMAT\0\0"
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // schemaVersion = 1
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // kind = 0 (UnlitTextured)
      std::byte{0x08}, std::byte{0x07}, std::byte{0x06}, std::byte{0x05}, std::byte{0x04}, std::byte{0x03},
      std::byte{0x02}, std::byte{0x01},  // texture_asset_id = 0x0102030405060708
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // filter = 1 (Linear)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},  // address_mode = 0 (Repeat)
  };
  REQUIRE(expected.size() == 32);
  CHECK(encoded == expected);
}

TEST_CASE("decodeMaterialArtifact rejects a buffer too small for the header", "[asset_system][material]") {
  const std::vector<std::byte> tooSmall(10, std::byte{0});
  const auto result = decodeMaterialArtifact(tooSmall);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::TruncatedHeader);
}

TEST_CASE("decodeMaterialArtifact rejects a buffer larger than the fixed 32-byte record",
          "[asset_system][material]") {
  auto bytes = encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                                       MaterialSamplerAddressMode::Repeat);
  bytes.push_back(std::byte{0xFF});
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnexpectedSize);
}

TEST_CASE("decodeMaterialArtifact rejects a bad magic", "[asset_system][material]") {
  auto bytes = encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                                       MaterialSamplerAddressMode::Repeat);
  bytes[0] = std::byte{0x00};
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::BadMagic);
}

TEST_CASE("decodeMaterialArtifact rejects an unsupported schema version", "[asset_system][material]") {
  auto bytes = encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                                       MaterialSamplerAddressMode::Repeat);
  bytes[8] = std::byte{0x02};  // schemaVersion's low byte, offset 8
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnsupportedSchemaVersion);
}

TEST_CASE("decodeMaterialArtifact rejects an unknown kind value", "[asset_system][material]") {
  // Plan 0019 P5: this literal must name a value still genuinely
  // unrecognized now that 1 (LitTextured) is valid -- 2 here, not 1.
  auto bytes = encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                                       MaterialSamplerAddressMode::Repeat);
  bytes[12] = std::byte{0x02};  // kind's low byte, offset 12: 0 -> 2 (unknown)
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnknownMaterialKind);
}

TEST_CASE("encodeMaterialArtifact then decodeMaterialArtifact round-trips MaterialKind::LitTextured",
          "[asset_system][material]") {
  const auto encoded = encodeMaterialArtifact(MaterialKind::LitTextured, 0x0102030405060708ULL,
                                                MaterialSamplerFilter::Linear, MaterialSamplerAddressMode::Repeat);
  const auto decoded = decodeMaterialArtifact(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().kind == MaterialKind::LitTextured);
  CHECK(decoded.value().textureAsset == 0x0102030405060708ULL);
}

// Plan 0019 P5/V7, V24: kindToField()'s own no-default switch, real,
// not merely decorative -- a positive probe (temporarily removing the
// LitTextured case) reproduces a real C4062 build failure; this is the
// restored, negative half, verified empty-diff against the positive
// probe's own reversion.
TEST_CASE("kindToField()'s own C4062 protection: LitTextured decodes to field 1, distinct from UnlitTextured's 0",
          "[asset_system][material]") {
  const auto unlit = encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                                              MaterialSamplerAddressMode::Repeat);
  const auto lit = encodeMaterialArtifact(MaterialKind::LitTextured, 1ULL, MaterialSamplerFilter::Linear,
                                            MaterialSamplerAddressMode::Repeat);
  CHECK(unlit[12] == std::byte{0x00});
  CHECK(lit[12] == std::byte{0x01});
}

TEST_CASE("decodeMaterialArtifact rejects an unknown filter value", "[asset_system][material]") {
  auto bytes = encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                                       MaterialSamplerAddressMode::Repeat);
  bytes[24] = std::byte{0x02};  // filter's low byte, offset 24: 1 -> 2 (unknown)
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnknownFilter);
}

TEST_CASE("decodeMaterialArtifact rejects an unknown address_mode value", "[asset_system][material]") {
  auto bytes = encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                                       MaterialSamplerAddressMode::Repeat);
  bytes[28] = std::byte{0x02};  // address_mode's low byte, offset 28: 0 -> 2 (unknown)
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnknownAddressMode);
}
