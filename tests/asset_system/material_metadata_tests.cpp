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

TEST_CASE("parseMaterialMetadata rejects a wrong line count", "[asset_system][material]") {
  const auto result = parseMaterialMetadata("atlantis_material_metadata_version: 1\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::WrongLineCount);
}

TEST_CASE("parseMaterialMetadata rejects an unknown metadata version", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 2\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::UnknownMetadataVersion);
}

TEST_CASE("parseMaterialMetadata rejects a field name mismatch", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 1\n"
      "asset_id: 0000000000000001\n"
      "wrong_field: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::FieldNameMismatch);
}

TEST_CASE("parseMaterialMetadata rejects a malformed kind value", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 1\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: pbr\n"
      "texture_asset: 0000000000000002\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseMaterialMetadata rejects a malformed asset_id (uppercase hex)", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 1\n"
      "asset_id: 00000000000000AB\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 0000000000000002\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseMaterialMetadata rejects a malformed texture_asset (uppercase hex)", "[asset_system][material]") {
  const std::string text =
      "atlantis_material_metadata_version: 1\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.material.txt\n"
      "kind: unlit_textured\n"
      "texture_asset: 00000000000000CD\n";
  const auto result = parseMaterialMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}
