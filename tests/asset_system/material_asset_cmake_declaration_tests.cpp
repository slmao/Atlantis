#include <atlantis/asset_system/load_material.h>

#include <atlantis/asset_system/asset_id.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

// Plan 0018 Milestone 5: tests/asset_system/CMakeLists.txt declares a
// test-only material asset via the real atlantis_add_material_asset()
// CMake function (with TEXTURE textured_quad_unorm) -- if the custom
// command itself failed, the ALL target (and therefore this very test
// binary) would never have built at all. This file checks what a build
// failure alone cannot: that the cooked artifact/metadata pair actually
// loads correctly through the real Asset System pipeline, not just that
// some file got written.
//
// The negative "TEXTURE not previously declared" FATAL_ERROR path
// (mirroring atlantis_add_scene_asset()'s own MESH_DEPENDENCIES guard,
// which likewise has no automated test anywhere in this codebase --
// FATAL_ERROR aborts the entire CMake configure, so it cannot be
// exercised from inside the very build it would abort) was verified
// manually during Milestone 5's own implementation: temporarily
// changing TEXTURE below to an undeclared name and re-running `cmake -S
// . -B build` failed configure with exactly the expected message,
// naming the undeclared TEXTURE value; reverted afterward with an empty
// `git diff`.

namespace fs = std::filesystem;

TEST_CASE("atlantis_add_material_asset() produces an artifact and metadata sidecar",
          "[asset_system][material][cmake]") {
  CHECK(fs::exists(ATLANTIS_CMAKE_DECLARATION_TEST_MATERIAL_ARTIFACT_PATH));
  CHECK(fs::exists(ATLANTIS_CMAKE_DECLARATION_TEST_MATERIAL_METADATA_PATH));
}

TEST_CASE("atlantis_add_material_asset()'s cooked output loads through loadMaterialAsset()",
          "[asset_system][material][cmake]") {
  const auto result = atlantis::asset_system::loadMaterialAsset(ATLANTIS_CMAKE_DECLARATION_TEST_MATERIAL_ARTIFACT_PATH,
                                                                  ATLANTIS_CMAKE_DECLARATION_TEST_MATERIAL_METADATA_PATH);
  REQUIRE(result.isOk());
  CHECK(result.value().kind == atlantis::asset_system::MaterialKind::UnlitTextured);
  CHECK(result.value().filter == atlantis::asset_system::MaterialSamplerFilter::Linear);
  CHECK(result.value().addressMode == atlantis::asset_system::MaterialSamplerAddressMode::Repeat);
  CHECK(result.value().textureAsset ==
        atlantis::asset_system::computeAssetId("textures/textured_quad_source_unorm.png"));
}
