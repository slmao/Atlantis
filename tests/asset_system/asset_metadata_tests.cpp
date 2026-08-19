#include <atlantis/asset_system/asset_metadata.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

constexpr std::string_view kValidMetadata =
    "atlantis_asset_metadata_version: 1\n"
    "asset_id: 78c473ee2218581d\n"
    "source_logical_path: meshes/minimal_cube.mesh.txt\n"
    "importer_version: atlantis-asset-cooker/1\n"
    "asset_type: static_mesh\n"
    "vertex_count: 8\n"
    "index_count: 36\n"
    "vertex_stride_bytes: 24\n";

}  // namespace

TEST_CASE("parseAssetMetadata parses a well-formed sidecar", "[asset_system]") {
  const auto result = parseAssetMetadata(kValidMetadata);
  REQUIRE(result.isOk());
  const AssetMetadata& metadata = result.value();
  CHECK(metadata.assetId == 0x78c473ee2218581dULL);
  CHECK(metadata.sourceLogicalPath == "meshes/minimal_cube.mesh.txt");
  CHECK(metadata.importerVersion == "atlantis-asset-cooker/1");
  CHECK(metadata.assetType == "static_mesh");
  CHECK(metadata.vertexCount == 8);
  CHECK(metadata.indexCount == 36);
  CHECK(metadata.vertexStrideBytes == 24);
}

TEST_CASE("serializeAssetMetadata then parseAssetMetadata round-trips exactly", "[asset_system]") {
  AssetMetadata metadata;
  metadata.assetId = 0x78c473ee2218581dULL;
  metadata.sourceLogicalPath = "meshes/minimal_cube.mesh.txt";
  metadata.importerVersion = "atlantis-asset-cooker/1";
  metadata.assetType = "static_mesh";
  metadata.vertexCount = 8;
  metadata.indexCount = 36;
  metadata.vertexStrideBytes = 24;

  const std::string text = serializeAssetMetadata(metadata);
  CHECK(text == kValidMetadata);

  const auto reparsed = parseAssetMetadata(text);
  REQUIRE(reparsed.isOk());
  CHECK(reparsed.value().assetId == metadata.assetId);
  CHECK(reparsed.value().sourceLogicalPath == metadata.sourceLogicalPath);
  CHECK(reparsed.value().importerVersion == metadata.importerVersion);
  CHECK(reparsed.value().assetType == metadata.assetType);
  CHECK(reparsed.value().vertexCount == metadata.vertexCount);
  CHECK(reparsed.value().indexCount == metadata.indexCount);
  CHECK(reparsed.value().vertexStrideBytes == metadata.vertexStrideBytes);
}

TEST_CASE("parseAssetMetadata rejects the wrong line count", "[asset_system]") {
  const auto result = parseAssetMetadata("atlantis_asset_metadata_version: 1\nasset_id: 0000000000000000\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::WrongLineCount);
}

TEST_CASE("parseAssetMetadata rejects an unrecognized schema version", "[asset_system]") {
  std::string text(kValidMetadata);
  text.replace(text.find("version: 1"), std::string_view("version: 1").size(), "version: 2");
  const auto result = parseAssetMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::UnknownMetadataVersion);
}

TEST_CASE("parseAssetMetadata rejects a field-name mismatch", "[asset_system]") {
  const std::string_view wrongFieldName =
      "atlantis_asset_metadata_version: 1\n"
      "wrong_field: 78c473ee2218581d\n"
      "source_logical_path: meshes/minimal_cube.mesh.txt\n"
      "importer_version: atlantis-asset-cooker/1\n"
      "asset_type: static_mesh\n"
      "vertex_count: 8\n"
      "index_count: 36\n"
      "vertex_stride_bytes: 24\n";
  const auto result = parseAssetMetadata(wrongFieldName);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::FieldNameMismatch);
}

TEST_CASE("parseAssetMetadata rejects an uppercase-hex asset_id", "[asset_system]") {
  const std::string_view uppercaseHex =
      "atlantis_asset_metadata_version: 1\n"
      "asset_id: 78C473EE2218581D\n"
      "source_logical_path: meshes/minimal_cube.mesh.txt\n"
      "importer_version: atlantis-asset-cooker/1\n"
      "asset_type: static_mesh\n"
      "vertex_count: 8\n"
      "index_count: 36\n"
      "vertex_stride_bytes: 24\n";
  const auto result = parseAssetMetadata(uppercaseHex);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseAssetMetadata rejects a malformed unsigned field", "[asset_system]") {
  const std::string_view malformedCount =
      "atlantis_asset_metadata_version: 1\n"
      "asset_id: 78c473ee2218581d\n"
      "source_logical_path: meshes/minimal_cube.mesh.txt\n"
      "importer_version: atlantis-asset-cooker/1\n"
      "asset_type: static_mesh\n"
      "vertex_count: not_a_number\n"
      "index_count: 36\n"
      "vertex_stride_bytes: 24\n";
  const auto result = parseAssetMetadata(malformedCount);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}
