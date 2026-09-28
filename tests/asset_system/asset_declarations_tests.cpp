// Plan 0047 M2 (P10): the configure-time declarations list. Every line is
// "type<TAB>root<TAB>path" with a catalog-source type and root and a
// normalized, unique path; test-directory declarations are included; the
// Bistro import root appears exactly when its content is present.

#include <catch2/catch_test_macros.hpp>

#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/asset_system/logical_path.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using atlantis::asset_system::normalizeLogicalPath;
using atlantis::asset_system::parseCatalogAssetType;
using atlantis::asset_system::parseCatalogRoot;

namespace {

struct Declaration {
  std::string type;
  std::string root;
  std::string path;
};

[[nodiscard]] std::vector<std::string> readLines(const std::string& path) {
  std::ifstream in(path);
  REQUIRE(in.is_open());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) lines.push_back(line);
  }
  return lines;
}

[[nodiscard]] std::vector<Declaration> readDeclarations() {
  std::vector<Declaration> declarations;
  for (const std::string& line : readLines(ATLANTIS_ASSET_DECLARATIONS_PATH)) {
    const std::size_t first = line.find('\t');
    const std::size_t second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
    REQUIRE(second != std::string::npos);
    REQUIRE(line.find('\t', second + 1) == std::string::npos);
    declarations.push_back({line.substr(0, first), line.substr(first + 1, second - first - 1), line.substr(second + 1)});
  }
  return declarations;
}

[[nodiscard]] bool contains(const std::vector<Declaration>& declarations, const Declaration& wanted) {
  for (const Declaration& d : declarations) {
    if (d.type == wanted.type && d.root == wanted.root && d.path == wanted.path) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("Every declaration line is well formed and unique", "[asset_declarations]") {
  const std::vector<Declaration> declarations = readDeclarations();
  REQUIRE_FALSE(declarations.empty());
  std::set<std::pair<std::string, std::string>> keys;
  for (const Declaration& d : declarations) {
    INFO(d.type << " " << d.root << " " << d.path);
    CHECK(parseCatalogAssetType(d.type).has_value());
    CHECK(parseCatalogRoot(d.root).has_value());
    const auto normalized = normalizeLogicalPath(d.path);
    REQUIRE(normalized.isOk());
    CHECK(normalized.value() == d.path);
    CHECK(keys.insert({d.root, d.path}).second);
  }
}

TEST_CASE("Declarations cover each asset kind and the test-directory fixtures", "[asset_declarations]") {
  const std::vector<Declaration> declarations = readDeclarations();
  CHECK(contains(declarations, {"mesh", "assets", "meshes/minimal_cube.mesh.txt"}));
  CHECK(contains(declarations, {"texture", "assets", "textures/paris_stringlights_diff.dds"}));
  CHECK(contains(declarations, {"environment", "assets", "environments/ibl_studio_source.hdr"}));
  CHECK(contains(declarations, {"scene", "assets", "scenes/integrated_showcase_demo.scene.txt"}));
  CHECK(contains(declarations, {"material", "assets", "_test_fixtures/cmake_material_declaration_test.material.txt"}));
  CHECK(contains(declarations, {"scene", "assets", "_test_fixtures/cmake_scene_declaration_test.scene.txt"}));
}

TEST_CASE("The Bistro import root is declared exactly when its content is present", "[asset_declarations]") {
  const bool contentPresent =
      std::filesystem::exists(std::filesystem::path(ATLANTIS_BISTRO_CONTENT_DIR) / "bistro.gltf");
  CHECK(contains(readDeclarations(), {"gltf_import", "content", "bistro/bistro.gltf"}) == contentPresent);
}

TEST_CASE("Every path in declared_assets.txt is also a declaration", "[asset_declarations]") {
  const std::vector<Declaration> declarations = readDeclarations();
  for (const std::string& path : readLines(ATLANTIS_DECLARED_ASSETS_PATH)) {
    bool found = false;
    for (const Declaration& d : declarations) found = found || (d.root == "assets" && d.path == path);
    INFO(path);
    CHECK(found);
  }
}
