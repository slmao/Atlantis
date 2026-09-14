#include <atlantis/asset_system/material_metadata.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

TEST_CASE("serializeMaterialMetadata then parseMaterialMetadata round-trips exactly", "[asset_system][material]") {
  MaterialMetadata original;
  original.assetId = 0x0102030405060708ULL;
  original.sourceLogicalPath = "materials/unlit_textured_quad.material.txt";
  original.kind = MaterialKind::UnlitTextured;
  original.textureAsset = 0x1122334455667788ULL;

  const std::string text = serializeMaterialMetadata(original);
  const auto parsed = parseMaterialMetadata(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().assetId == original.assetId);
  CHECK(parsed.value().sourceLogicalPath == original.sourceLogicalPath);
  CHECK(parsed.value().kind == original.kind);
  CHECK(parsed.value().textureAsset == original.textureAsset);
  // Plan 0023 Milestone 1: the three new fields round-trip too, at
  // their own documented defaults here (this test never sets them).
  CHECK(parsed.value().baseColorFactor[0] == original.baseColorFactor[0]);
  CHECK(parsed.value().baseColorFactor[1] == original.baseColorFactor[1]);
  CHECK(parsed.value().baseColorFactor[2] == original.baseColorFactor[2]);
  CHECK(parsed.value().baseColorFactor[3] == original.baseColorFactor[3]);
  CHECK(parsed.value().metallicFactor == original.metallicFactor);
  CHECK(parsed.value().roughnessFactor == original.roughnessFactor);
}

// Plan 0019 Section P5/D11: the metadata sidecar's own "kind:" field
// round-trips a LitTextured material too -- a real, previously-
// undisclosed gap this Plan's own Milestone 5 missed (serializeMaterialMetadata()
// unconditionally emitted "unlit_textured" regardless of metadata.kind,
// found via a real loadMaterialAsset() MetadataArtifactMismatch failure
// while building this Plan's own lighting_demo_scene fixture, not by
// inspection alone). This is exactly the test that would have caught it.
TEST_CASE("serializeMaterialMetadata then parseMaterialMetadata round-trips a LitTextured kind exactly",
          "[asset_system][material][light]") {
  MaterialMetadata original;
  original.assetId = 0x0102030405060708ULL;
  original.sourceLogicalPath = "materials/lit_textured_quad.material.txt";
  original.kind = MaterialKind::LitTextured;
  original.textureAsset = 0x1122334455667788ULL;

  const std::string text = serializeMaterialMetadata(original);
  CHECK(text.find("kind: lit_textured\n") != std::string::npos);
  const auto parsed = parseMaterialMetadata(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().kind == MaterialKind::LitTextured);
  CHECK(parsed.value().assetId == original.assetId);
  CHECK(parsed.value().sourceLogicalPath == original.sourceLogicalPath);
  CHECK(parsed.value().textureAsset == original.textureAsset);
}

// Plan 0023 Milestone 1: the metadata sidecar's own "kind:" field
// round-trips a PbrDirectLit material, together with its own three new,
// non-default numeric fields.
TEST_CASE("serializeMaterialMetadata then parseMaterialMetadata round-trips a PbrDirectLit kind and its own PBR "
          "fields exactly",
          "[asset_system][material]") {
  MaterialMetadata original;
  original.assetId = 0x0102030405060708ULL;
  original.sourceLogicalPath = "materials/pbr_dielectric_rough.material.txt";
  original.kind = MaterialKind::PbrDirectLit;
  original.textureAsset = 0x1122334455667788ULL;
  original.baseColorFactor[0] = 0.8f;
  original.baseColorFactor[1] = 0.2f;
  original.baseColorFactor[2] = 0.1f;
  original.baseColorFactor[3] = 1.0f;
  original.metallicFactor = 0.0f;
  original.roughnessFactor = 0.75f;

  const std::string text = serializeMaterialMetadata(original);
  CHECK(text.find("kind: pbr_direct_lit\n") != std::string::npos);
  const auto parsed = parseMaterialMetadata(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().kind == MaterialKind::PbrDirectLit);
  CHECK(parsed.value().baseColorFactor[0] == original.baseColorFactor[0]);
  CHECK(parsed.value().baseColorFactor[1] == original.baseColorFactor[1]);
  CHECK(parsed.value().baseColorFactor[2] == original.baseColorFactor[2]);
  CHECK(parsed.value().baseColorFactor[3] == original.baseColorFactor[3]);
  CHECK(parsed.value().metallicFactor == original.metallicFactor);
  CHECK(parsed.value().roughnessFactor == original.roughnessFactor);
}

// Real, disclosed regression coverage (found during Plan 0023's own
// final review): std::to_string(float)'s fixed 6-decimal-place
// formatting does not round-trip every float32 value exactly (unlike
// the "round" literals every other test in this file happens to use) --
// this would have failed against serializeMaterialMetadata()'s own
// former std::to_string()-based formatting, proving the fix
// (std::to_chars(), a shortest-round-trip guarantee) is real.
TEST_CASE("serializeMaterialMetadata then parseMaterialMetadata round-trips a metallicFactor value "
          "std::to_string(float) would NOT preserve exactly",
          "[asset_system][material]") {
  MaterialMetadata original;
  original.assetId = 0x0102030405060708ULL;
  original.sourceLogicalPath = "materials/pbr_precise.material.txt";
  original.kind = MaterialKind::PbrDirectLit;
  original.textureAsset = 0x1122334455667788ULL;
  original.metallicFactor = 0.123456789f;
  original.roughnessFactor = 1.0f / 3.0f;

  const std::string text = serializeMaterialMetadata(original);
  const auto parsed = parseMaterialMetadata(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().metallicFactor == original.metallicFactor);
  CHECK(parsed.value().roughnessFactor == original.roughnessFactor);
}

TEST_CASE("parseMaterialMetadata rejects a wrong line count", "[asset_system][material]") {
  const auto result = parseMaterialMetadata("atlantis_material_metadata_version: 5\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::WrongLineCount);
}

TEST_CASE("parseMaterialMetadata rejects an unknown metadata version", "[asset_system][material]") {
  // Plan 0035 Milestone 2/ADR-0081, widened by Milestones 3/4: version 2
  // here, not 3, 4, or 5 -- this literal must name a value still
  // genuinely unknown now that 5 (this round's own bump) is current;
  // line count is the new 15 (anisotropy_factor/anisotropy_rotation
  // appended) so the version check -- which runs only after the
  // line-count check -- is actually reached.
  const std::string text =
      "atlantis_material_metadata_version: 2\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n"
      "base_color_factor: 1.000000 1.000000 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 0000000000000000\n"
      "clearcoat_factor: 0.000000\n"
      "clearcoat_roughness: 0.000000\n"
      "sheen_color: 0.000000 0.000000 0.000000\n"
      "sheen_roughness: 0.000000\n"
      "anisotropy_factor: 0.000000\n"
      "anisotropy_rotation: 0.000000\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::UnknownMetadataVersion);
}

TEST_CASE("parseMaterialMetadata rejects the retired version 1 (wrong line count too, but version is checked first)",
          "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 1\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::WrongLineCount);
}

TEST_CASE("parseMaterialMetadata rejects a field name mismatch", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 5\n"
      "asset_id: 0000000000000001\n"
      "wrong_field: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n"
      "base_color_factor: 1.000000 1.000000 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 0000000000000000\n"
      "clearcoat_factor: 0.000000\n"
      "clearcoat_roughness: 0.000000\n"
      "sheen_color: 0.000000 0.000000 0.000000\n"
      "sheen_roughness: 0.000000\n"
      "anisotropy_factor: 0.000000\n"
      "anisotropy_rotation: 0.000000\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::FieldNameMismatch);
}

TEST_CASE("parseMaterialMetadata rejects a malformed kind value", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 5\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: pbr\n"
      "texture_asset: 0000000000000002\n"
      "base_color_factor: 1.000000 1.000000 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 0000000000000000\n"
      "clearcoat_factor: 0.000000\n"
      "clearcoat_roughness: 0.000000\n"
      "sheen_color: 0.000000 0.000000 0.000000\n"
      "sheen_roughness: 0.000000\n"
      "anisotropy_factor: 0.000000\n"
      "anisotropy_rotation: 0.000000\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseMaterialMetadata rejects a malformed asset_id (uppercase hex)", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 5\n"
      "asset_id: 00000000000000AB\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n"
      "base_color_factor: 1.000000 1.000000 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 0000000000000000\n"
      "clearcoat_factor: 0.000000\n"
      "clearcoat_roughness: 0.000000\n"
      "sheen_color: 0.000000 0.000000 0.000000\n"
      "sheen_roughness: 0.000000\n"
      "anisotropy_factor: 0.000000\n"
      "anisotropy_rotation: 0.000000\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseMaterialMetadata rejects a malformed texture_asset (uppercase hex)", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 5\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 00000000000000CD\n"
      "base_color_factor: 1.000000 1.000000 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 0000000000000000\n"
      "clearcoat_factor: 0.000000\n"
      "clearcoat_roughness: 0.000000\n"
      "sheen_color: 0.000000 0.000000 0.000000\n"
      "sheen_roughness: 0.000000\n"
      "anisotropy_factor: 0.000000\n"
      "anisotropy_rotation: 0.000000\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseMaterialMetadata rejects a malformed base_color_factor component", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 5\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n"
      "base_color_factor: 1.000000 not-a-number 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 0000000000000000\n"
      "clearcoat_factor: 0.000000\n"
      "clearcoat_roughness: 0.000000\n"
      "sheen_color: 0.000000 0.000000 0.000000\n"
      "sheen_roughness: 0.000000\n"
      "anisotropy_factor: 0.000000\n"
      "anisotropy_rotation: 0.000000\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

// Plan 0029 Section P6/ADR-0074 Section 1: the metadata sidecar's own
// unconditional normal_map_texture field -- present on every material,
// `0000000000000000` when absent (this test never sets it, exactly
// like the round-trip test above never sets baseColorFactor/etc.).
TEST_CASE("serializeMaterialMetadata then parseMaterialMetadata round-trips a real, non-zero normalMapTexture",
          "[asset_system][material]") {
  MaterialMetadata original;
  original.assetId = 0x0102030405060708ULL;
  original.sourceLogicalPath = "materials/pbr_normal_mapped.material.txt";
  original.kind = MaterialKind::PbrDirectLit;
  original.textureAsset = 0x1122334455667788ULL;
  original.normalMapTexture = 0xaabbccdd00112233ULL;

  const std::string text = serializeMaterialMetadata(original);
  CHECK(text.find("normal_map_texture: aabbccdd00112233\n") != std::string::npos);
  const auto parsed = parseMaterialMetadata(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().normalMapTexture == original.normalMapTexture);
}

TEST_CASE("parseMaterialMetadata rejects a malformed normal_map_texture (uppercase hex)",
          "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 5\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n"
      "base_color_factor: 1.000000 1.000000 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 00000000000000EF\n"
      "clearcoat_factor: 0.000000\n"
      "clearcoat_roughness: 0.000000\n"
      "sheen_color: 0.000000 0.000000 0.000000\n"
      "sheen_roughness: 0.000000\n"
      "anisotropy_factor: 0.000000\n"
      "anisotropy_rotation: 0.000000\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

// Plan 0035 Milestone 2/ADR-0081: the metadata sidecar's own two new,
// unconditional clearcoat fields -- malformed-value coverage mirroring
// every other field's own identical pattern in this file.
TEST_CASE("parseMaterialMetadata rejects a malformed clearcoat_factor", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 5\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n"
      "base_color_factor: 1.000000 1.000000 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 0000000000000000\n"
      "clearcoat_factor: not-a-number\n"
      "clearcoat_roughness: 0.000000\n"
      "sheen_color: 0.000000 0.000000 0.000000\n"
      "sheen_roughness: 0.000000\n"
      "anisotropy_factor: 0.000000\n"
      "anisotropy_rotation: 0.000000\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseMaterialMetadata rejects a malformed clearcoat_roughness", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 5\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n"
      "base_color_factor: 1.000000 1.000000 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 0000000000000000\n"
      "clearcoat_factor: 0.000000\n"
      "clearcoat_roughness: not-a-number\n"
      "sheen_color: 0.000000 0.000000 0.000000\n"
      "sheen_roughness: 0.000000\n"
      "anisotropy_factor: 0.000000\n"
      "anisotropy_rotation: 0.000000\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("serializeMaterialMetadata then parseMaterialMetadata round-trips a PbrClearcoat kind and its own "
          "clearcoat fields exactly",
          "[asset_system][material]") {
  MaterialMetadata original;
  original.assetId = 0x0102030405060708ULL;
  original.sourceLogicalPath = "materials/clearcoat_test.material.txt";
  original.kind = MaterialKind::PbrClearcoat;
  original.textureAsset = 0x1122334455667788ULL;
  original.clearcoatFactor = 0.8f;
  original.clearcoatRoughness = 0.05f;

  const std::string text = serializeMaterialMetadata(original);
  CHECK(text.find("kind: pbr_clearcoat\n") != std::string::npos);
  const auto parsed = parseMaterialMetadata(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().kind == MaterialKind::PbrClearcoat);
  CHECK(parsed.value().clearcoatFactor == original.clearcoatFactor);
  CHECK(parsed.value().clearcoatRoughness == original.clearcoatRoughness);
}

// Plan 0035 Milestone 3/ADR-0081: the metadata sidecar's own two new,
// unconditional sheen fields -- malformed-value coverage mirroring
// clearcoat_factor/clearcoat_roughness's own identical pattern above.
TEST_CASE("parseMaterialMetadata rejects a malformed sheen_color component", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 5\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n"
      "base_color_factor: 1.000000 1.000000 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 0000000000000000\n"
      "clearcoat_factor: 0.000000\n"
      "clearcoat_roughness: 0.000000\n"
      "sheen_color: 0.000000 not-a-number 0.000000\n"
      "sheen_roughness: 0.000000\n"
      "anisotropy_factor: 0.000000\n"
      "anisotropy_rotation: 0.000000\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseMaterialMetadata rejects a malformed sheen_roughness", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 5\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n"
      "base_color_factor: 1.000000 1.000000 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 0000000000000000\n"
      "clearcoat_factor: 0.000000\n"
      "clearcoat_roughness: 0.000000\n"
      "sheen_color: 0.000000 0.000000 0.000000\n"
      "sheen_roughness: not-a-number\n"
      "anisotropy_factor: 0.000000\n"
      "anisotropy_rotation: 0.000000\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("serializeMaterialMetadata then parseMaterialMetadata round-trips a PbrSheen kind and its own sheen "
          "fields exactly",
          "[asset_system][material]") {
  MaterialMetadata original;
  original.assetId = 0x0102030405060708ULL;
  original.sourceLogicalPath = "materials/sheen_test.material.txt";
  original.kind = MaterialKind::PbrSheen;
  original.textureAsset = 0x1122334455667788ULL;
  original.sheenColor[0] = 0.7f;
  original.sheenColor[1] = 0.6f;
  original.sheenColor[2] = 0.5f;
  original.sheenRoughness = 0.35f;

  const std::string text = serializeMaterialMetadata(original);
  CHECK(text.find("kind: pbr_sheen\n") != std::string::npos);
  const auto parsed = parseMaterialMetadata(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().kind == MaterialKind::PbrSheen);
  CHECK(parsed.value().sheenColor[0] == original.sheenColor[0]);
  CHECK(parsed.value().sheenColor[1] == original.sheenColor[1]);
  CHECK(parsed.value().sheenColor[2] == original.sheenColor[2]);
  CHECK(parsed.value().sheenRoughness == original.sheenRoughness);
}

// Plan 0035 Milestone 4/ADR-0081: the metadata sidecar's own two new,
// unconditional anisotropy fields -- malformed-value coverage mirroring
// clearcoat_factor/clearcoat_roughness's/sheen_color/sheen_roughness's
// own identical pattern above.
TEST_CASE("parseMaterialMetadata rejects a malformed anisotropy_factor", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 5\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n"
      "base_color_factor: 1.000000 1.000000 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 0000000000000000\n"
      "clearcoat_factor: 0.000000\n"
      "clearcoat_roughness: 0.000000\n"
      "sheen_color: 0.000000 0.000000 0.000000\n"
      "sheen_roughness: 0.000000\n"
      "anisotropy_factor: not-a-number\n"
      "anisotropy_rotation: 0.000000\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseMaterialMetadata rejects a malformed anisotropy_rotation", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 5\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n"
      "base_color_factor: 1.000000 1.000000 1.000000 1.000000\n"
      "metallic_factor: 1.000000\n"
      "roughness_factor: 1.000000\n"
      "normal_map_texture: 0000000000000000\n"
      "clearcoat_factor: 0.000000\n"
      "clearcoat_roughness: 0.000000\n"
      "sheen_color: 0.000000 0.000000 0.000000\n"
      "sheen_roughness: 0.000000\n"
      "anisotropy_factor: 0.000000\n"
      "anisotropy_rotation: not-a-number\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("serializeMaterialMetadata then parseMaterialMetadata round-trips a PbrAnisotropic kind and its own "
          "anisotropy fields exactly",
          "[asset_system][material]") {
  MaterialMetadata original;
  original.assetId = 0x0102030405060708ULL;
  original.sourceLogicalPath = "materials/anisotropic_test.material.txt";
  original.kind = MaterialKind::PbrAnisotropic;
  original.textureAsset = 0x1122334455667788ULL;
  original.anisotropyFactor = -0.6f;
  original.anisotropyRotation = 1.5707963f;

  const std::string text = serializeMaterialMetadata(original);
  CHECK(text.find("kind: pbr_anisotropic\n") != std::string::npos);
  const auto parsed = parseMaterialMetadata(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().kind == MaterialKind::PbrAnisotropic);
  CHECK(parsed.value().anisotropyFactor == original.anisotropyFactor);
  CHECK(parsed.value().anisotropyRotation == original.anisotropyRotation);
}
