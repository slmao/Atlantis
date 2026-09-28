// Plan 0047 M2 (P20, ruling I5): the one-off migration on synthetic v6/v9
// trees. Never run on the repository's own assets here.

#include <catch2/catch_test_macros.hpp>

#include "cook_command.h"

#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/asset_system/asset_guid.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using atlantis::asset_system::CatalogAssetType;
using atlantis::asset_system::CatalogRoot;
using atlantis::asset_system::parseAssetCatalogSource;
using atlantis::asset_system::parseEntityGuid;
using atlantis::asset_system::toString;
using atlantis::tools::asset_cooker::AssetKind;
using atlantis::tools::asset_cooker::CookCommandRequest;
using atlantis::tools::asset_cooker::parseCookArguments;
using atlantis::tools::asset_cooker::runCookCommand;

namespace {

constexpr const char* kScene =
    "atlantis_scene_source_version: 6\n"
    "node_count: 3\n"
    "active_camera: 3\n"
    "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
    "mesh=meshes/cube.mesh.txt material=materials/red.material.txt\n"
    "node: node_id=2 parent=1 position=1.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=meshes/cube.mesh.txt\n"
    "node: node_id=3 parent=none position=0.0 2.0 7.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
    "camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0\n";

constexpr const char* kOverlay =
    "atlantis_scene_source_version: 6\r\n"
    "node_count: 1\r\n"
    "active_camera: none\r\n"
    "node: node_id=1 parent=none position=0.0 3.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 "
    "light=point color=1.0 0.8 0.5 intensity=2.0 range=5.0\r\n";

constexpr const char* kMaterial =
    "atlantis_material_source_version: 9\n"
    "kind: pbr_direct_lit\n"
    "texture: textures/albedo.png\n"
    "filter: linear\n"
    "normal_map: textures/normal.png\n"
    "emissive_texture: textures/glow.png\n";

// A synthetic asset tree with a declarations list, removed on destruction.
class Tree {
 public:
  Tree() : root_(fs::temp_directory_path() / "atlantis_migrate_0047_test") {
    fs::remove_all(root_);
    fs::create_directories(root_ / "assets");
    write("assets/meshes/cube.mesh.txt", "mesh source\n");
    write("assets/textures/albedo.png", "png");
    write("assets/textures/normal.png", "png");
    write("assets/textures/glow.png", "png");
    write("assets/materials/red.material.txt", kMaterial);
    write("assets/scenes/demo.scene.txt", kScene);
    write("assets/bistro/overlay.scene.txt", kOverlay);
    write("assets/environments/sky.hdr", "hdr");
    declarations_ =
        "mesh\tassets\tmeshes/cube.mesh.txt\n"
        "texture\tassets\ttextures/albedo.png\n"
        "texture\tassets\ttextures/normal.png\n"
        "texture\tassets\ttextures/glow.png\n"
        "material\tassets\tmaterials/red.material.txt\n"
        "scene\tassets\tscenes/demo.scene.txt\n"
        "environment\tassets\tenvironments/sky.hdr\n"
        "gltf_import\tcontent\tbistro/bistro.gltf\n";
  }
  ~Tree() { fs::remove_all(root_); }
  Tree(const Tree&) = delete;
  Tree& operator=(const Tree&) = delete;

  void write(const std::string& relative, const std::string& text) const {
    fs::create_directories((root_ / relative).parent_path());
    std::ofstream(root_ / relative, std::ios::binary) << text;
  }
  [[nodiscard]] std::string read(const std::string& relative) const {
    std::ifstream in(root_ / relative, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  [[nodiscard]] fs::path path(const std::string& relative) const { return root_ / relative; }

  std::string declarations_;

  [[nodiscard]] int migrate(const std::vector<std::string>& extraScenes = {"bistro/overlay.scene.txt"}) const {
    write("declarations.txt", declarations_);
    std::vector<std::string> args = {"--kind=migrate-0047", "--declarations=" + path("declarations.txt").string(),
                                     "--asset-root=" + path("assets").string(),
                                     "--catalog-source-out=" + path("assets/asset_catalog.txt").string()};
    for (const std::string& extra : extraScenes) args.push_back("--scene-source=" + extra);
    CookCommandRequest request;
    std::ostringstream err;
    REQUIRE(parseCookArguments(args, request, err));
    std::ostringstream captured;
    std::streambuf* const previous = std::cout.rdbuf(captured.rdbuf());
    const int exitCode = runCookCommand(request);
    std::cout.rdbuf(previous);
    return exitCode;
  }

 private:
  fs::path root_;
};

[[nodiscard]] std::string guidOf(const Tree& tree, const std::string& path) {
  const auto catalog = parseAssetCatalogSource(tree.read("assets/asset_catalog.txt"));
  REQUIRE(catalog.isOk());
  const auto* entry = catalog.value().find(CatalogRoot::Assets, path);
  REQUIRE(entry != nullptr);
  return toString(entry->guid);
}

// Replaces each " guid=<36 chars>" in `text` with "" and returns the guids.
[[nodiscard]] std::string stripNodeGuids(std::string text, std::vector<std::string>& guids) {
  for (std::size_t at = text.find(" guid="); at != std::string::npos; at = text.find(" guid=", at)) {
    guids.push_back(text.substr(at + 6, 36));
    text.erase(at, 6 + 36);
  }
  return text;
}

[[nodiscard]] std::string replaceAll(std::string text, const std::string& from, const std::string& to) {
  for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
    text.replace(at, from.size(), to);
  }
  return text;
}

}  // namespace

TEST_CASE("migrate-0047 writes one catalog entry per declaration", "[migrate_0047]") {
  const Tree tree;
  REQUIRE(tree.migrate() == 0);
  const auto catalog = parseAssetCatalogSource(tree.read("assets/asset_catalog.txt"));
  REQUIRE(catalog.isOk());
  REQUIRE(catalog.value().entries().size() == 8);
  CHECK(catalog.value().find(CatalogRoot::Assets, "materials/red.material.txt")->type == CatalogAssetType::Material);
  CHECK(catalog.value().find(CatalogRoot::Content, "bistro/bistro.gltf")->type == CatalogAssetType::GltfImport);
  CHECK(catalog.value().find(CatalogRoot::Assets, "bistro/overlay.scene.txt") == nullptr);
  for (const auto& entry : catalog.value().entries()) {
    CHECK((entry.guid.bytes[6] & std::byte{0xF0}) == std::byte{0x40});
  }
}

TEST_CASE("migrate-0047 rewrites a scene to v7: node GUIDs added, references replaced, nothing else",
          "[migrate_0047]") {
  const Tree tree;
  REQUIRE(tree.migrate() == 0);
  std::vector<std::string> nodeGuids;
  std::string restored = stripNodeGuids(tree.read("assets/scenes/demo.scene.txt"), nodeGuids);
  REQUIRE(nodeGuids.size() == 3);
  for (const std::string& guid : nodeGuids) CHECK(parseEntityGuid(guid).isOk());
  CHECK(std::set<std::string>(nodeGuids.begin(), nodeGuids.end()).size() == 3);

  restored = replaceAll(restored, "version: 7", "version: 6");
  restored = replaceAll(restored, guidOf(tree, "meshes/cube.mesh.txt"), "meshes/cube.mesh.txt");
  restored = replaceAll(restored, guidOf(tree, "materials/red.material.txt"), "materials/red.material.txt");
  CHECK(restored == kScene);
  CHECK(tree.read("assets/scenes/demo.scene.txt").find("node: node_id=1 guid=") != std::string::npos);
}

TEST_CASE("migrate-0047 rewrites a material to v10 with texture GUIDs", "[migrate_0047]") {
  const Tree tree;
  REQUIRE(tree.migrate() == 0);
  std::string restored = replaceAll(tree.read("assets/materials/red.material.txt"), "version: 10", "version: 9");
  for (const char* texture : {"textures/albedo.png", "textures/normal.png", "textures/glow.png"}) {
    restored = replaceAll(restored, guidOf(tree, texture), texture);
  }
  CHECK(restored == kMaterial);
}

TEST_CASE("migrate-0047 rewrites an undeclared scene source named explicitly, keeping CRLF", "[migrate_0047]") {
  const Tree tree;
  REQUIRE(tree.migrate() == 0);
  std::vector<std::string> nodeGuids;
  const std::string restored =
      replaceAll(stripNodeGuids(tree.read("assets/bistro/overlay.scene.txt"), nodeGuids), "version: 7", "version: 6");
  CHECK(nodeGuids.size() == 1);
  CHECK(restored == kOverlay);
}

TEST_CASE("migrate-0047 fails without writing anything", "[migrate_0047]") {
  const auto expectUntouched = [](const Tree& tree) {
    CHECK_FALSE(fs::exists(tree.path("assets/asset_catalog.txt")));
    CHECK(tree.read("assets/scenes/demo.scene.txt") == kScene);
    CHECK(tree.read("assets/materials/red.material.txt") == kMaterial);
    CHECK(tree.read("assets/bistro/overlay.scene.txt") == kOverlay);
  };

  SECTION("a scene reference that is not declared") {
    Tree tree;
    tree.declarations_ = replaceAll(tree.declarations_, "mesh\tassets\tmeshes/cube.mesh.txt\n", "");
    CHECK(tree.migrate() != 0);
    expectUntouched(tree);
  }
  SECTION("a material reference of the wrong type") {
    Tree tree;
    tree.declarations_ = replaceAll(tree.declarations_, "texture\tassets\ttextures/glow.png", "mesh\tassets\ttextures/glow.png");
    CHECK(tree.migrate() != 0);
    expectUntouched(tree);
  }
  SECTION("an unexpected source version") {
    Tree tree;
    tree.write("assets/scenes/demo.scene.txt", replaceAll(kScene, "version: 6", "version: 5"));
    CHECK(tree.migrate() != 0);
    CHECK_FALSE(fs::exists(tree.path("assets/asset_catalog.txt")));
    CHECK(tree.read("assets/materials/red.material.txt") == kMaterial);
  }
  SECTION("an explicit scene source that is already declared") {
    const Tree tree;
    CHECK(tree.migrate({"scenes/demo.scene.txt"}) != 0);
    expectUntouched(tree);
  }
  SECTION("a duplicate declaration") {
    Tree tree;
    tree.declarations_ += "mesh\tassets\tmeshes/cube.mesh.txt\n";
    CHECK(tree.migrate() != 0);
    expectUntouched(tree);
  }
  SECTION("an existing catalog source") {
    const Tree tree;
    tree.write("assets/asset_catalog.txt", "keep");
    CHECK(tree.migrate() != 0);
    CHECK(tree.read("assets/asset_catalog.txt") == "keep");
    CHECK(tree.read("assets/scenes/demo.scene.txt") == kScene);
  }
}

TEST_CASE("migrate-0047 needs its three inputs and never runs from a cook manifest", "[migrate_0047]") {
  for (const std::vector<std::string>& args : std::vector<std::vector<std::string>>{
           {"--kind=migrate-0047", "--asset-root=a", "--catalog-source-out=c"},
           {"--kind=migrate-0047", "--declarations=d", "--catalog-source-out=c"},
           {"--kind=migrate-0047", "--declarations=d", "--asset-root=a"},
       }) {
    CookCommandRequest request;
    std::ostringstream err;
    CHECK_FALSE(parseCookArguments(args, request, err));
  }

  const fs::path dir = fs::temp_directory_path() / "atlantis_migrate_manifest_test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  std::ofstream(dir / "asset_list.txt") << "";
  std::ofstream(dir / "cook_manifest.txt") << "--kind=migrate-0047 --declarations=d --asset-root=a "
                                              "--catalog-source-out=c\n";
  CookCommandRequest request;
  request.kind = AssetKind::CookManifest;
  request.importDir = dir.string();
  request.cookedDir = (dir / "cooked").string();
  request.contentParent = dir.string();
  request.manifestOutPath = (dir / "manifest.txt").string();
  CHECK(runCookCommand(request) != 0);
  fs::remove_all(dir);
}
