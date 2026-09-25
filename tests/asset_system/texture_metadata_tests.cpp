#include <atlantis/asset_system/texture_metadata.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

namespace {

// A well-formed v3 metadata document (9 lines: Spec 0038's data_layout
// line between format and channels_in_file, then Spec 0045's mip_count
// line right after it, ruling O3) with per-test overrides applied by
// string replacement.
[[nodiscard]] std::string makeV3Text() {
  return "atlantis_texture_metadata_version: 3\n"
         "asset_id: 0000000000000001\n"
         "source_logical_path: a.png\n"
         "width: 1\n"
         "height: 1\n"
         "format: unorm\n"
         "data_layout: rgba8\n"
         "mip_count: 1\n"
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
  original.mipCount = 12;                    // Spec 0045 (metadata v3)
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
  CHECK(parsed.value().mipCount == original.mipCount);
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
  const auto result = parseTextureMetadata("atlantis_texture_metadata_version: 3\n");
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::WrongLineCount);
}

TEST_CASE("parseTextureMetadata rejects the retired v1 and v2 versions", "[asset_system]") {
  // Retired-version documents padded to v3's own 9-line count so the
  // version line itself is what rejects them -- matching the repository's
  // established exact-equality version discipline.
  for (const char* retired : {"version: 1", "version: 2"}) {
    std::string text = makeV3Text();
    text.replace(text.find("version: 3"), sizeof("version: 3") - 1, retired);
    const auto result = parseTextureMetadata(text);
    REQUIRE(result.isErr());
    CHECK(result.error() == MetadataParseError::UnknownMetadataVersion);
  }
  // A literal 8-line v2 document (no mip_count line) fails on its line
  // count first -- also a correct rejection.
  std::string v2 = makeV3Text();
  v2.replace(v2.find("version: 3"), sizeof("version: 3") - 1, "version: 2");
  v2.erase(v2.find("mip_count: 1\n"), sizeof("mip_count: 1\n") - 1);
  const auto result = parseTextureMetadata(v2);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::WrongLineCount);
}

TEST_CASE("parseTextureMetadata rejects an unknown metadata version", "[asset_system]") {
  std::string text = makeV3Text();
  text.replace(text.find("version: 3"), sizeof("version: 3") - 1, "version: 4");
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::UnknownMetadataVersion);
}

TEST_CASE("parseTextureMetadata rejects a field name mismatch", "[asset_system]") {
  std::string text = makeV3Text();
  text.replace(text.find("source_logical_path"), sizeof("source_logical_path") - 1, "wrong_field");
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::FieldNameMismatch);
}

TEST_CASE("parseTextureMetadata rejects a malformed format value", "[asset_system]") {
  std::string text = makeV3Text();
  text.replace(text.find("unorm\n"), sizeof("unorm\n") - 1, "rgba16\n");
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseTextureMetadata rejects a malformed data_layout value", "[asset_system]") {
  std::string text = makeV3Text();
  text.replace(text.find("rgba8\n"), sizeof("rgba8\n") - 1, "bc1\n");
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseTextureMetadata rejects a malformed asset_id (uppercase hex)", "[asset_system]") {
  std::string text = makeV3Text();
  text.replace(text.find("0000000000000001"), sizeof("0000000000000001") - 1, "00000000000000AB");
  const auto result = parseTextureMetadata(text);
  REQUIRE(result.isErr());
  CHECK(result.error() == MetadataParseError::MalformedValue);
}

TEST_CASE("parseTextureMetadata reads mip_count after data_layout and rejects a malformed or misplaced one",
          "[asset_system][mip]") {
  std::string text = makeV3Text();
  text.replace(text.find("mip_count: 1"), sizeof("mip_count: 1") - 1, "mip_count: 9");
  const auto parsed = parseTextureMetadata(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().mipCount == 9);

  std::string malformed = makeV3Text();
  malformed.replace(malformed.find("mip_count: 1"), sizeof("mip_count: 1") - 1, "mip_count: -1");
  const auto malformedResult = parseTextureMetadata(malformed);
  REQUIRE(malformedResult.isErr());
  CHECK(malformedResult.error() == MetadataParseError::MalformedValue);

  // mip_count and channels_in_file swapped.
  std::string swapped = makeV3Text();
  swapped.replace(swapped.find("mip_count: 1\nchannels_in_file: 4"),
                  sizeof("mip_count: 1\nchannels_in_file: 4") - 1, "channels_in_file: 4\nmip_count: 1");
  const auto swappedResult = parseTextureMetadata(swapped);
  REQUIRE(swappedResult.isErr());
  CHECK(swappedResult.error() == MetadataParseError::FieldNameMismatch);
}
