#include <atlantis/asset_system/texture_metadata.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

TEST_CASE("serializeTextureMetadata then parseTextureMetadata round-trips exactly", "[asset_system]") {
  TextureMetadata original;
  original.assetId = 0x0102030405060708ULL;
  original.sourceLogicalPath = "textures/checker.png";
  original.width = 64;
  original.height = 64;
  original.format = TextureColorSpace::Srgb;
  original.channelsInFile = 3;

  const std::string text = serializeTextureMetadata(original);
  const auto parsed = parseTextureMetadata(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().assetId == original.assetId);
  CHECK(parsed.value().sourceLogicalPath == original.sourceLogicalPath);
  CHECK(parsed.value().width == original.width);
  CHECK(parsed.value().height == original.height);
  CHECK(parsed.value().format == original.format);
  CHECK(parsed.value().channelsInFile == original.channelsInFile);
}

TEST_CASE("serializeTextureMetadata round-trips the Unorm format value too", "[asset_system]") {
  TextureMetadata original;
  original.format = TextureColorSpace::Unorm;
  const auto parsed = parseTextureMetadata(serializeTextureMetadata(original));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().format == TextureColorSpace::Unorm);
}

TEST_CASE("parseTextureMetadata rejects a wrong line count", "[asset_system]") {
  const auto result = parseTextureMetadata("atlantis_texture_metadata_version: 1\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::WrongLineCount);
}

TEST_CASE("parseTextureMetadata rejects an unknown metadata version", "[asset_system]") {
  const std::string text =
      "atlantis_texture_metadata_version: 2\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.png\n"
      "width: 1\n"
      "height: 1\n"
      "format: unorm\n"
      "channels_in_file: 4\n";
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::UnknownMetadataVersion);
}

TEST_CASE("parseTextureMetadata rejects a field name mismatch", "[asset_system]") {
  const std::string text =
      "atlantis_texture_metadata_version: 1\n"
      "asset_id: 0000000000000001\n"
      "wrong_field: a.png\n"
      "width: 1\n"
      "height: 1\n"
      "format: unorm\n"
      "channels_in_file: 4\n";
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::FieldNameMismatch);
}

TEST_CASE("parseTextureMetadata rejects a malformed format value", "[asset_system]") {
  const std::string text =
      "atlantis_texture_metadata_version: 1\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.png\n"
      "width: 1\n"
      "height: 1\n"
      "format: rgba16\n"
      "channels_in_file: 4\n";
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseTextureMetadata rejects a malformed asset_id (uppercase hex)", "[asset_system]") {
  const std::string text =
      "atlantis_texture_metadata_version: 1\n"
      "asset_id: 00000000000000AB\n"
      "source_logical_path: a.png\n"
      "width: 1\n"
      "height: 1\n"
      "format: unorm\n"
      "channels_in_file: 4\n";
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}
