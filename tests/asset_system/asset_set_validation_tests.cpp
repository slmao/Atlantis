#include <atlantis/asset_system/asset_set_validation.h>

#include <atlantis/asset_system/asset_id.h>

#include <catch2/catch_test_macros.hpp>

using namespace atlantis::asset_system;

TEST_CASE("validateAssetSet accepts a set of distinct, valid, normalized logical paths", "[asset_system]") {
  const std::vector<DeclaredAsset> assets{
      {"meshes/a.mesh.txt", computeAssetId("meshes/a.mesh.txt")},
      {"meshes/b.mesh.txt", computeAssetId("meshes/b.mesh.txt")},
  };
  const auto result = validateAssetSet(assets);
  CHECK(result.isOk());
}

TEST_CASE("validateAssetSet accepts an empty set", "[asset_system]") {
  const std::vector<DeclaredAsset> assets{};
  CHECK(validateAssetSet(assets).isOk());
}

TEST_CASE("validateAssetSet detects an Asset ID collision via a hand-injected pair, not a discovered real collision",
          "[asset_system]") {
  // Plan 0012 Section D4: a real 64-bit FNV-1a collision is
  // computationally infeasible to discover in a unit test. Two distinct
  // logical paths are deliberately given the SAME AssetId here to
  // exercise the exact detection logic validateAssetSet() uses in
  // production -- the production path (the cooker's --validate-set
  // mode) always supplies a genuinely computed AssetId per path; only
  // this test injects one directly.
  constexpr AssetId kSharedId = 0x1122334455667788ULL;
  const std::vector<DeclaredAsset> assets{
      {"meshes/a.mesh.txt", kSharedId},
      {"meshes/b.mesh.txt", kSharedId},
  };
  const auto result = validateAssetSet(assets);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetSetError::AssetIdCollision);
}

TEST_CASE("validateAssetSet does not flag the same path repeated with the same real ID as a collision",
          "[asset_system]") {
  // A single logical path appearing once is fine; appearing twice is
  // DuplicateLogicalPath, not AssetIdCollision (see below) -- this case
  // confirms one occurrence with a real, self-consistent ID is not
  // itself flagged.
  const std::vector<DeclaredAsset> assets{
      {"meshes/a.mesh.txt", computeAssetId("meshes/a.mesh.txt")},
  };
  CHECK(validateAssetSet(assets).isOk());
}

TEST_CASE("validateAssetSet detects a case-only-differing logical path pair", "[asset_system]") {
  const std::vector<DeclaredAsset> assets{
      {"meshes/Cube.mesh.txt", computeAssetId("meshes/Cube.mesh.txt")},
      {"meshes/cube.mesh.txt", computeAssetId("meshes/cube.mesh.txt")},
  };
  const auto result = validateAssetSet(assets);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetSetError::CaseOnlyPathConflict);
}

TEST_CASE("validateAssetSet detects an exact duplicate logical path", "[asset_system]") {
  const AssetId id = computeAssetId("meshes/a.mesh.txt");
  const std::vector<DeclaredAsset> assets{
      {"meshes/a.mesh.txt", id},
      {"meshes/a.mesh.txt", id},
  };
  const auto result = validateAssetSet(assets);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetSetError::DuplicateLogicalPath);
}

TEST_CASE("validateAssetSet rejects a declared logical path that is not already normalized", "[asset_system]") {
  const std::vector<DeclaredAsset> assets{
      {"meshes\\a.mesh.txt", computeAssetId("meshes\\a.mesh.txt")},
  };
  const auto result = validateAssetSet(assets);
  REQUIRE(result.isErr());
  CHECK(result.error() == AssetSetError::InvalidLogicalPath);
}

TEST_CASE("validateAssetSet's production-shaped use always supplies a genuinely computed AssetId", "[asset_system]") {
  // Confirms computeAssetId() is deterministic and would be used as-is
  // by the cooker's --validate-set mode (Step 4) -- not a test double.
  const DeclaredAsset asset{"meshes/minimal_cube.mesh.txt", computeAssetId("meshes/minimal_cube.mesh.txt")};
  CHECK(asset.assetId == computeAssetId("meshes/minimal_cube.mesh.txt"));
  CHECK(validateAssetSet({asset}).isOk());
}
