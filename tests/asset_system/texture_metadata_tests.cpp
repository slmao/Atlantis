#include <atlantis/asset_system/texture_metadata.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

// A well-formed v2 metadata document (8 lines, Spec 0038's data_layout
// line between format and channels_in_file) with per-test overrides
// applied by string replacement.
[[nodiscard]] std::string makeV2Text() {
  return "atlantis_texture_metadata_version: 2\n"
         "asset_id: 0000000000000001\n"
         "source_logical_path: a.png\n"
         "width: 1\n"
         "height: 1\n"
         "format: unorm\n"
         "data_layout: rgba8\n"
         "channels_in_file: 4\n";
}

}  // namespace

TEST_CASE("serializeTextureMetadata then parseTextureMetadata round-trips exactly", "[asset_system]") {
  TextureMetadata original;
  original.assetId = 0x0102030405060708ULL;
  original.sourceLogicalPath = "textures/checker.png";
  original.width = 64;
  original.height = 64;
  original.format = TextureColorSpace::Srgb;
  original.layout = TextureDataLayout::Bc7;  // Spec 0038 (metadata v2)
  original.channelsInFile = 3;

  const std::string text = serializeTextureMetadata(original);
  const auto parsed = parseTextureMetadata(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().assetId == original.assetId);
  CHECK(parsed.value().sourceLogicalPath == original.sourceLogicalPath);
  CHECK(parsed.value().width == original.width);
  CHECK(parsed.value().height == original.height);
  CHECK(parsed.value().format == original.format);
  CHECK(parsed.value().layout == original.layout);
  CHECK(parsed.value().channelsInFile == original.channelsInFile);
}

TEST_CASE("serializeTextureMetadata round-trips the Unorm format value too", "[asset_system]") {
  TextureMetadata original;
  original.format = TextureColorSpace::Unorm;
  const auto parsed = parseTextureMetadata(serializeTextureMetadata(original));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().format == TextureColorSpace::Unorm);
  CHECK(parsed.value().layout == TextureDataLayout::Rgba8);  // the default layout
}

TEST_CASE("parseTextureMetadata rejects a wrong line count", "[asset_system]") {
  const auto result = parseTextureMetadata("atlantis_texture_metadata_version: 2\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::WrongLineCount);
}

TEST_CASE("parseTextureMetadata rejects the retired v1 version", "[asset_system]") {
  // A v1-shaped document padded to v2's own 8-line count so the version
  // line itself is what rejects it -- matching the repository's
  // established exact-equality version discipline. (A literal 7-line v1
  // document would fail WrongLineCount first; both rejections are
  // correct outcomes, this form isolates the version check.)
  const std::string text =
      "atlantis_texture_metadata_version: 1\n"
      "asset_id: 0000000000000001\n"
      "source_logical_path: a.png\n"
      "width: 1\n"
      "height: 1\n"
      "format: unorm\n"
      "data_layout: rgba8\n"
      "channels_in_file: 4\n";
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::UnknownMetadataVersion);
}

TEST_CASE("parseTextureMetadata rejects an unknown metadata version", "[asset_system]") {
  std::string text = makeV2Text();
  text.replace(text.find("version: 2"), sizeof("version: 2") - 1, "version: 3");
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::UnknownMetadataVersion);
}

TEST_CASE("parseTextureMetadata rejects a field name mismatch", "[asset_system]") {
  std::string text = makeV2Text();
  text.replace(text.find("source_logical_path"), sizeof("source_logical_path") - 1, "wrong_field");
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::FieldNameMismatch);
}

TEST_CASE("parseTextureMetadata rejects a malformed format value", "[asset_system]") {
  std::string text = makeV2Text();
  text.replace(text.find("unorm\n"), sizeof("unorm\n") - 1, "rgba16\n");
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseTextureMetadata rejects a malformed data_layout value", "[asset_system]") {
  std::string text = makeV2Text();
  text.replace(text.find("rgba8\n"), sizeof("rgba8\n") - 1, "bc1\n");
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseTextureMetadata rejects a malformed asset_id (uppercase hex)", "[asset_system]") {
  std::string text = makeV2Text();
  text.replace(text.find("0000000000000001"), sizeof("0000000000000001") - 1, "00000000000000AB");
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}
