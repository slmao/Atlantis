#include <atlantis/asset_system/material_artifact.h>

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace atlantis::asset_system;

namespace {

constexpr float kDefaultBaseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
// Plan 0035 Milestone 3/ADR-0081: mirrors kDefaultBaseColorFactor's own
// role, for encodeMaterialArtifact()'s two new trailing sheen args.
constexpr float kDefaultSheenColor[3] = {0.0f, 0.0f, 0.0f};
// Plan 0035 Milestone 4/ADR-0081: the two new trailing anisotropy args
// are plain scalars (0.0f, 0.0f), passed directly at each call site --
// no array constant needed, unlike kDefaultSheenColor above.

}  // namespace

TEST_CASE("encodeMaterialArtifact then decodeMaterialArtifact round-trips exactly", "[asset_system][material]") {
  const auto encoded =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 0x0102030405060708ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
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
                              MaterialSamplerAddressMode::Repeat, baseColorFactor, 0.5f, 0.25f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
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
                              MaterialSamplerAddressMode::ClampToEdge, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
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
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);

  const std::vector<std::byte> expected = {
      std::byte{0x41}, std::byte{0x54}, std::byte{0x4C}, std::byte{0x4D}, std::byte{0x41}, std::byte{0x54},
      std::byte{0x00}, std::byte{0x00},                                        // magic "ATLMAT\0\0"
      std::byte{0x09}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // schemaVersion = 9
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
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00},                                        // normal_map_texture_asset_id = 0
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // clearcoat_factor = 0.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // clearcoat_roughness = 0.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // sheen_color[0] = 0.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // sheen_color[1] = 0.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // sheen_color[2] = 0.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // sheen_roughness = 0.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // anisotropy_factor = 0.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // anisotropy_rotation = 0.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // emissive_factor[0] = 0.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // emissive_factor[1] = 0.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // emissive_factor[2] = 0.0f
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},      // alpha_mode = 0 (Opaque)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3F},      // alpha_cutoff = 0.5f (0x3F000000)
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00},                                        // emissive_texture_asset_id = 0
  };
  REQUIRE(expected.size() == 124);
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

TEST_CASE("decodeMaterialArtifact rejects a buffer larger than the fixed 124-byte record",
          "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  bytes.push_back(std::byte{0xFF});
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnexpectedSize);
}

TEST_CASE("decodeMaterialArtifact rejects a bad magic", "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  bytes[0] = std::byte{0x00};
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::BadMagic);
}

TEST_CASE("decodeMaterialArtifact rejects an unsupported schema version", "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  // Plan 0042 Milestone 1, moved by Plan 0046 Milestone 1: this literal
  // must name a value still genuinely unsupported now that 9 (this round's
  // own bump) is valid -- 10 here, not 9.
  bytes[8] = std::byte{0x0A};  // schemaVersion's low byte, offset 8: 9 -> 10 (unsupported)
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnsupportedSchemaVersion);
}

TEST_CASE("decodeMaterialArtifact rejects an unknown kind value", "[asset_system][material]") {
  // Plan 0023 Milestone 1 (Plan 0035 Milestone 2/ADR-0081 widening,
  // Milestone 3 widening again, Milestone 4 widening again): this
  // literal must name a value still genuinely unrecognized now that 5
  // (PbrAnisotropic) is also valid -- 6 here, not 5.
  auto bytes =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  bytes[12] = std::byte{0x06};  // kind's low byte, offset 12: 0 -> 6 (unknown)
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnknownMaterialKind);
}

TEST_CASE("encodeMaterialArtifact then decodeMaterialArtifact round-trips MaterialKind::LitTextured",
          "[asset_system][material]") {
  const auto encoded =
      encodeMaterialArtifact(MaterialKind::LitTextured, 0x0102030405060708ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
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
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  const auto lit =
      encodeMaterialArtifact(MaterialKind::LitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  const auto pbr =
      encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  // Plan 0035 Milestone 2/ADR-0081: PbrClearcoat's own distinct field
  // value, added to this same C4062-protection probe.
  const auto clearcoat =
      encodeMaterialArtifact(MaterialKind::PbrClearcoat, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  // Plan 0035 Milestone 3/ADR-0081: PbrSheen's own distinct field value,
  // added to this same C4062-protection probe.
  const auto sheen =
      encodeMaterialArtifact(MaterialKind::PbrSheen, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  // Plan 0035 Milestone 4/ADR-0081: PbrAnisotropic's own distinct field
  // value, added to this same C4062-protection probe.
  const auto anisotropic =
      encodeMaterialArtifact(MaterialKind::PbrAnisotropic, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  CHECK(unlit[12] == std::byte{0x00});
  CHECK(lit[12] == std::byte{0x01});
  CHECK(pbr[12] == std::byte{0x02});
  CHECK(clearcoat[12] == std::byte{0x03});
  CHECK(sheen[12] == std::byte{0x04});
  CHECK(anisotropic[12] == std::byte{0x05});
}

TEST_CASE("decodeMaterialArtifact rejects an unknown filter value", "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  bytes[24] = std::byte{0x02};  // filter's low byte, offset 24: 1 -> 2 (unknown)
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnknownFilter);
}

TEST_CASE("decodeMaterialArtifact rejects an unknown address_mode value", "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  bytes[28] = std::byte{0x02};  // address_mode's low byte, offset 28: 0 -> 2 (unknown)
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnknownAddressMode);
}

TEST_CASE("decodeMaterialArtifact rejects a baseColorFactor component above 1.0", "[asset_system][material]") {
  const float outOfRange[4] = {1.5f, 1.0f, 1.0f, 1.0f};
  auto bytes = encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                                       MaterialSamplerAddressMode::Repeat, outOfRange, 1.0f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::BaseColorFactorOutOfRange);
}

TEST_CASE("decodeMaterialArtifact rejects a negative metallicFactor", "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, -0.1f, 1.0f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::MaterialFactorOutOfRange);
}

TEST_CASE("decodeMaterialArtifact rejects a roughnessFactor above 1.0", "[asset_system][material]") {
  auto bytes =
      encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.1f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::MaterialFactorOutOfRange);
}

TEST_CASE("encodeMaterialArtifact then decodeMaterialArtifact round-trips a real, non-zero normalMapTexture",
          "[asset_system][material]") {
  // Plan 0029 Section P6/ADR-0074 Section 1: `0` (no normal map) is
  // exercised by every other test in this file via its own trailing
  // argument -- this test is the one real, non-zero round-trip proof.
  const auto encoded =
      encodeMaterialArtifact(MaterialKind::PbrDirectLit, 0x0102030405060708ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f,
                              0x1122334455667788ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  REQUIRE(encoded.size() == kMaterialArtifactHeaderSizeBytes);
  const auto decoded = decodeMaterialArtifact(encoded);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().normalMapTexture == 0x1122334455667788ULL);
}

// ---------------------------------------------------------------------------
// Plan 0041 Milestone 1 (Spec 0041 R4, ADR-0089 Decision 5): schema 7,
// 108 bytes, emissive_factor at offset 96.
// ---------------------------------------------------------------------------

TEST_CASE("encode/decodeMaterialArtifact round-trips emissive_factor at offset 96", "[asset_system][material][emissive]") {
  const float emissive[3] = {40.0f, 0.5f, 65504.0f};
  const auto bytes =
      encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                              MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 0.0f, 0.5f, 0ULL, 0.0f,
                              0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f, emissive);
  REQUIRE(bytes.size() == kMaterialArtifactHeaderSizeBytes);
  // 40.0f = 0x42200000, little-endian 00 00 20 42 -- independently computed.
  CHECK(bytes[96] == std::byte{0x00});
  CHECK(bytes[97] == std::byte{0x00});
  CHECK(bytes[98] == std::byte{0x20});
  CHECK(bytes[99] == std::byte{0x42});
  const auto decoded = decodeMaterialArtifact(bytes);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().emissiveFactor[0] == 40.0f);
  CHECK(decoded.value().emissiveFactor[1] == 0.5f);
  CHECK(decoded.value().emissiveFactor[2] == 65504.0f);
}

TEST_CASE("decodeMaterialArtifact rejects a real-size, 96-byte schema-version-6 artifact",
          "[asset_system][material][emissive]") {
  auto bytes = encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                                      MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL,
                                      0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  bytes.resize(96);
  bytes[8] = std::byte{0x06};
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::TruncatedHeader);
}

TEST_CASE("decodeMaterialArtifact re-validates emissive_factor against [0, 65504]", "[asset_system][material][emissive]") {
  auto bytes = encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                                      MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 0.0f, 0.5f, 0ULL,
                                      0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  // -1.0f = 0xBF800000, little-endian 00 00 80 BF, written into emissive_factor[1].
  bytes[100] = std::byte{0x00};
  bytes[101] = std::byte{0x00};
  bytes[102] = std::byte{0x80};
  bytes[103] = std::byte{0xBF};
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::EmissiveFactorOutOfRange);
}

// ---------------------------------------------------------------------------
// Plan 0042 Milestone 1 (Spec 0042 R3, ADR-0090 Decision 4): schema 8,
// 116 bytes, alpha_mode at offset 108, alpha_cutoff at 112.
// ---------------------------------------------------------------------------

TEST_CASE("encode/decodeMaterialArtifact round-trips alpha_mode and alpha_cutoff at offsets 108/112",
          "[asset_system][material][transparency]") {
  const float noEmissive[3] = {0.0f, 0.0f, 0.0f};
  const auto bytes = encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                                            MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 0.0f, 0.5f,
                                            0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f, noEmissive,
                                            MaterialAlphaMode::Mask, 0.25f);
  REQUIRE(bytes.size() == kMaterialArtifactHeaderSizeBytes);
  // alpha_mode = 1 (Mask); 0.25f = 0x3E800000, little-endian 00 00 80 3E.
  CHECK(bytes[108] == std::byte{0x01});
  CHECK(bytes[109] == std::byte{0x00});
  CHECK(bytes[112] == std::byte{0x00});
  CHECK(bytes[113] == std::byte{0x00});
  CHECK(bytes[114] == std::byte{0x80});
  CHECK(bytes[115] == std::byte{0x3E});
  const auto decoded = decodeMaterialArtifact(bytes);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().alphaMode == MaterialAlphaMode::Mask);
  CHECK(decoded.value().alphaCutoff == 0.25f);

  const auto blendBytes = encodeMaterialArtifact(
      MaterialKind::PbrSheen, 1ULL, MaterialSamplerFilter::Linear, MaterialSamplerAddressMode::Repeat,
      kDefaultBaseColorFactor, 0.0f, 0.5f, 0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f, noEmissive,
      MaterialAlphaMode::Blend, 0.5f);
  CHECK(blendBytes[108] == std::byte{0x02});
  const auto blendDecoded = decodeMaterialArtifact(blendBytes);
  REQUIRE(blendDecoded.isOk());
  CHECK(blendDecoded.value().alphaMode == MaterialAlphaMode::Blend);
}

TEST_CASE("decodeMaterialArtifact rejects a real-size, 108-byte schema-version-7 artifact",
          "[asset_system][material][transparency]") {
  auto bytes = encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                                      MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL,
                                      0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  bytes.resize(108);
  bytes[8] = std::byte{0x07};
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::TruncatedHeader);
}

TEST_CASE("decodeMaterialArtifact rejects an unknown alpha_mode value", "[asset_system][material][transparency]") {
  auto bytes = encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                                      MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 0.0f, 0.5f, 0ULL,
                                      0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  bytes[108] = std::byte{0x03};
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::UnknownAlphaMode);
}

TEST_CASE("decodeMaterialArtifact re-validates alpha_cutoff against [0, 1]", "[asset_system][material][transparency]") {
  auto bytes = encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                                      MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 0.0f, 0.5f, 0ULL,
                                      0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  // 1.5f = 0x3FC00000, little-endian 00 00 C0 3F.
  bytes[112] = std::byte{0x00};
  bytes[113] = std::byte{0x00};
  bytes[114] = std::byte{0xC0};
  bytes[115] = std::byte{0x3F};
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::MaterialFactorOutOfRange);
}

// ---------------------------------------------------------------------------
// Plan 0046 Milestone 1 (ADR-0096): schema 9, 124 bytes,
// emissive_texture_asset_id at offset 116.
// ---------------------------------------------------------------------------

TEST_CASE("encode/decodeMaterialArtifact round-trips emissive_texture_asset_id at offset 116",
          "[asset_system][material][emissive]") {
  const float emissive[3] = {100.0f, 100.0f, 100.0f};
  const auto bytes = encodeMaterialArtifact(MaterialKind::PbrDirectLit, 1ULL, MaterialSamplerFilter::Linear,
                                            MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 0.0f, 0.5f,
                                            0ULL, 0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f, emissive,
                                            MaterialAlphaMode::Opaque, 0.5f, 0x1122334455667788ULL);
  REQUIRE(bytes.size() == 124);
  CHECK(bytes[116] == std::byte{0x88});
  CHECK(bytes[117] == std::byte{0x77});
  CHECK(bytes[122] == std::byte{0x22});
  CHECK(bytes[123] == std::byte{0x11});
  const auto decoded = decodeMaterialArtifact(bytes);
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().emissiveTexture == 0x1122334455667788ULL);
  CHECK(decoded.value().emissiveFactor[0] == 100.0f);
}

TEST_CASE("decodeMaterialArtifact rejects a real-size, 116-byte schema-version-8 artifact",
          "[asset_system][material][emissive]") {
  auto bytes = encodeMaterialArtifact(MaterialKind::UnlitTextured, 1ULL, MaterialSamplerFilter::Linear,
                                      MaterialSamplerAddressMode::Repeat, kDefaultBaseColorFactor, 1.0f, 1.0f, 0ULL,
                                      0.0f, 0.0f, kDefaultSheenColor, 0.0f, 0.0f, 0.0f);
  bytes.resize(116);
  bytes[8] = std::byte{0x08};
  const auto result = decodeMaterialArtifact(bytes);
  REQUIRE(result.isErr());
  CHECK(result.error() == MaterialArtifactDecodeError::TruncatedHeader);
}
