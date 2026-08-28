#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// Plan 0015 Step 5 (V13's own mechanism, V27's SceneCookError portion):
// tests/asset_system/CMakeLists.txt declares a test-only scene asset
// via the real atlantis_add_scene_asset() CMake function (with
// MESH_DEPENDENCIES minimal_cube) -- if the custom command itself
// failed, the ALL target (and therefore this very test binary) would
// never have built at all. This file checks what a build failure
// alone cannot: that the generated manifest's own tab-separated
// triple format (Plan 0015 Section D8) is actually correct, not just
// that some file got written.

namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::string readFileText(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

TEST_CASE("atlantis_add_scene_asset() produces an artifact and metadata sidecar", "[asset_system][scene][cmake]") {
  CHECK(fs::exists(ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_ARTIFACT_PATH));
  CHECK(fs::exists(ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_METADATA_PATH));
}

TEST_CASE("atlantis_add_scene_asset()'s generated manifest is a well-formed, "
          "three-entry (mesh/material/texture) tab-separated triple set",
          "[asset_system][scene][cmake]") {
  // Plan 0018 Section P9: the declaration this manifest comes from gained
  // MATERIAL_DEPENDENCIES/TEXTURE_DEPENDENCIES alongside the original
  // MESH_DEPENDENCIES -- one line per dependency, same unchanged 3-column
  // format, order matching declaration order (mesh, then material, then
  // texture; SceneDependencyResolver::find() sorts by AssetId for lookup,
  // so this order is a generator-mechanics detail, not a public contract).
  REQUIRE(fs::exists(ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_MANIFEST_PATH));
  std::string content = readFileText(ATLANTIS_CMAKE_DECLARATION_TEST_SCENE_MANIFEST_PATH);

  // CMake's own file(GENERATE) writes this project's actual Windows
  // toolchain's native line ending (\r\n), confirmed empirically here
  // rather than assumed -- Step 7's own manifest parser (scene_manifest.cpp)
  // must tolerate a trailing '\r' per line for the same reason
  // mesh_source.cpp's own splitLines() already does. Stripped here so
  // this test's own expectation stays about the manifest's logical
  // content, not this one generator's own line-ending choice.
  if (!content.empty() && content.back() == '\n') content.pop_back();
  if (!content.empty() && content.back() == '\r') content.pop_back();

  const std::string meshLine = std::string(ATLANTIS_minimal_cube_LOGICAL_PATH) + "\t" +
                                ATLANTIS_minimal_cube_ARTIFACT_PATH + "\t" + ATLANTIS_minimal_cube_METADATA_PATH;
  const std::string materialLine = std::string(ATLANTIS_cmake_declaration_test_material_LOGICAL_PATH) + "\t" +
                                    ATLANTIS_cmake_declaration_test_material_ARTIFACT_PATH + "\t" +
                                    ATLANTIS_cmake_declaration_test_material_METADATA_PATH;
  const std::string textureLine = std::string(ATLANTIS_textured_quad_unorm_LOGICAL_PATH) + "\t" +
                                   ATLANTIS_textured_quad_unorm_ARTIFACT_PATH + "\t" +
                                   ATLANTIS_textured_quad_unorm_METADATA_PATH;
  const std::string expected = meshLine + "\r\n" + materialLine + "\r\n" + textureLine;

  // Re-strip \r for a line-ending-independent comparison, splitting on
  // \n only, matching this test's own already-established discipline.
  std::string normalizedContent;
  for (char c : content) {
    if (c != '\r') normalizedContent += c;
  }
  std::string normalizedExpected;
  for (char c : expected) {
    if (c != '\r') normalizedExpected += c;
  }
  CHECK(normalizedContent == normalizedExpected);
}
