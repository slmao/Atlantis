#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_guid.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>

// Plan 0015 Step 5 (V13's own mechanism, V27's SceneCookError portion),
// Plan 0047 M5: tests/asset_system/CMakeLists.txt declares a test-only scene
// asset via the real atlantis_add_scene_asset() CMake function -- if the
// custom command itself failed, the ALL target (and therefore this very test
// binary) would never have built at all. This file checks what a build
// failure alone cannot: the configure-time GUID export and that the
// assembled catalog describes the cooked scene.

namespace fs = std::filesystem;

TEST_CASE("atlantis_add_scene_asset() produces an artifact and metadata sidecar", "[asset_system][scene][cmake]") {
  CHECK(fs::exists(ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_ARTIFACT_PATH));
  CHECK(fs::exists(ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_METADATA_PATH));
}

TEST_CASE("atlantis_add_scene_asset()'s exported GUID names the scene's record in the assembled catalog",
          "[asset_system][scene][cmake]") {
  using namespace atlantis::asset_system;

  const auto guid = parseAssetGuid(ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_GUID);
  REQUIRE(guid.isOk());
  const auto catalog = loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  REQUIRE(catalog.isOk());

  const AssetCatalogRecord* record = catalog.value().find(guid.value());
  REQUIRE(record != nullptr);
  CHECK(record->type == CatalogAssetType::Scene);
  CHECK(fs::equivalent(record->artifact, ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_ARTIFACT_PATH));
  CHECK(fs::equivalent(record->metadata, ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_METADATA_PATH));
  CHECK(record->sourceSchema == std::optional<std::uint32_t>{7});
}
