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

TEST_CASE("atlantis_add_scene_asset()'s generated manifest is a well-formed, single-entry tab-separated triple",
          "[asset_system][scene][cmake]") {
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

  const std::string expectedLine = std::string(ATLANTIS_minimal_cube_LOGICAL_PATH) + "\t" +
                                    ATLANTIS_minimal_cube_ARTIFACT_PATH + "\t" + ATLANTIS_minimal_cube_METADATA_PATH;
  CHECK(content == expectedLine);
}
