#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/environment_metadata.h>
#include <atlantis/asset_system/load_environment.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>

using namespace atlantis::asset_system;

namespace {

[[nodiscard]] std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  REQUIRE(input.is_open());
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

// Plan 0047: the committed catalog source is the only home of an asset's
// GUID; a cooked sidecar must record exactly that GUID.
[[nodiscard]] AssetGuid catalogGuid(std::string_view logicalPath) {
  std::ifstream input(ATLANTIS_ASSET_CATALOG_SOURCE_PATH, std::ios::binary);
  REQUIRE(input.is_open());
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const auto catalog = parseAssetCatalogSource(text);
  REQUIRE(catalog.isOk());
  const CatalogSourceEntry* entry = catalog.value().find(CatalogRoot::Assets, logicalPath);
  REQUIRE(entry != nullptr);
  return entry->guid;
}

}  // namespace

TEST_CASE("atlantis_add_environment_asset produces a loadable fixed-quality artifact", "[asset_system][environment]") {
  CHECK(std::filesystem::exists(ATLANTIS_ibl_studio_ARTIFACT_PATH));
  CHECK(std::filesystem::exists(ATLANTIS_ibl_studio_METADATA_PATH));
  const auto loaded = loadEnvironmentAsset(ATLANTIS_ibl_studio_ARTIFACT_PATH, ATLANTIS_ibl_studio_METADATA_PATH);
  REQUIRE(loaded.isOk());
  CHECK(loaded.value().faceSize == 256);
  CHECK(loaded.value().mipCount == 9);
  CHECK(loaded.value().dfgWidth == 128);
  CHECK(loaded.value().dfgHeight == 128);
}

TEST_CASE("checked-in environment metadata records its catalog identity", "[asset_system][environment]") {
  const auto metadata = parseEnvironmentMetadata(readText(ATLANTIS_ibl_studio_METADATA_PATH));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().sourceLogicalPath == "environments/ibl_studio_source.hdr");
  CHECK(metadata.value().assetGuid == catalogGuid("environments/ibl_studio_source.hdr"));
  CHECK(metadata.value().assetId == assetKey(metadata.value().assetGuid));
}
