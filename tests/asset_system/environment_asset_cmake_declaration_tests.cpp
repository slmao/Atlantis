#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/environment_metadata.h>
#include <atlantis/asset_system/load_environment.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace atlantis::asset_system;

namespace {

[[nodiscard]] std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  REQUIRE(input.is_open());
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
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

TEST_CASE("checked-in environment metadata uses the normalized source path identity", "[asset_system][environment]") {
  const auto metadata = parseEnvironmentMetadata(readText(ATLANTIS_ibl_studio_METADATA_PATH));
  REQUIRE(metadata.isOk());
  CHECK(metadata.value().sourceLogicalPath == "environments/ibl_studio_source.hdr");
  CHECK(metadata.value().assetId == computeAssetId("environments/ibl_studio_source.hdr"));
}
