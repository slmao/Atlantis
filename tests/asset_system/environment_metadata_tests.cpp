#include <atlantis/asset_system/environment_metadata.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

TEST_CASE("environment metadata round-trips its exact fields", "[asset_system]") {
  EnvironmentMetadata original;
  original.assetId = 0x0102030405060708ULL;
  original.sourceLogicalPath = "environments/studio.hdr";
  original.faceSize = 256;
  original.mipCount = 9;
  original.dfgWidth = 128;
  original.dfgHeight = 128;
  const auto parsed = parseEnvironmentMetadata(serializeEnvironmentMetadata(original));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().assetId == original.assetId);
  CHECK(parsed.value().sourceLogicalPath == original.sourceLogicalPath);
  CHECK(parsed.value().faceSize == original.faceSize);
  CHECK(parsed.value().mipCount == original.mipCount);
  CHECK(parsed.value().dfgWidth == original.dfgWidth);
  CHECK(parsed.value().dfgHeight == original.dfgHeight);
}

TEST_CASE("environment metadata parser is strict", "[asset_system]") {
  EnvironmentMetadata metadata;
  metadata.sourceLogicalPath = "environment.hdr";
  const std::string valid = serializeEnvironmentMetadata(metadata);
  SECTION("wrong line count") {
    CHECK(parseEnvironmentMetadata("atlantis_environment_metadata_version: 1\n").error() ==
          MetadataParseError::WrongLineCount);
  }
  SECTION("unknown version") {
    std::string text = valid;
    text.replace(text.find(": 1"), 3, ": 2");
    CHECK(parseEnvironmentMetadata(text).error() == MetadataParseError::UnknownMetadataVersion);
  }
  SECTION("wrong field") {
    std::string text = valid;
    text.replace(text.find("face_size"), 9, "face_sizx");
    CHECK(parseEnvironmentMetadata(text).error() == MetadataParseError::FieldNameMismatch);
  }
  SECTION("uppercase asset id") {
    std::string text = valid;
    const std::size_t id = text.find("0000000000000000");
    text.replace(id, 16, "ABCDEF0000000000");
    CHECK(parseEnvironmentMetadata(text).error() == MetadataParseError::MalformedValue);
  }
}
