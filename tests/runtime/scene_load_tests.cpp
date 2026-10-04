#include <atlantis/runtime/scene_load.h>

#include "catalog_test_support.h"

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/cook.h>
#include <atlantis/asset_system/cook_material.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/cook_texture.h>
#include <atlantis/asset_system/material_artifact.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/texture_artifact.h>
#include <atlantis/asset_system/texture_types.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <atlantis/asset_system/asset_guid.h>
#include <string_view>

namespace {

// Plan 0047 M3: a deterministic, non-nil test identity per logical path, so
// a test's cross-references (scene -> mesh, material -> texture) agree.
[[nodiscard]] atlantis::asset_system::AssetGuid testAssetGuid(std::string_view key) {
  return atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00470047-0047-4047-8047-004700470047").value(), key);
}

// Plan 0047 P9: the guid= a generated node line carries -- the same
// derivation the literal node lines use, keyed by node_id.
[[nodiscard]] std::string testNodeGuidToken(std::uint32_t nodeId) {
  return " guid=" + atlantis::asset_system::toString(atlantis::asset_system::deriveEntityGuid(
                        atlantis::asset_system::parseAssetGuid("00470047-0047-4047-8047-004700470047").value(),
                        "node/" + std::to_string(nodeId)));
}

}  // namespace
using namespace atlantis::runtime;
using atlantis::asset_system::AssetId;
using atlantis::asset_system::cookMaterial;
using atlantis::asset_system::cookScene;
using atlantis::asset_system::cookStaticMesh;
using atlantis::asset_system::cookTexture;
using atlantis::asset_system::TextureColorSpace;
using atlantis::rhi::VertexInputLayout;

// Plan 0015 Section D10 (V17, V19, V20). Every test here calls
// loadAndInstantiateScene() (scene_load.h) directly, with device =
// nullptr -- safe as long as no test exercises a scene with a mesh
// dependency whose loadStaticMeshAsset() call actually SUCCEEDS (that
// is the one and only point this function ever dereferences device;
// see its own header comment). This is what makes V17/V19/V20's own
// manifest/artifact/dependency-unresolved/dependency-load-failure
// paths testable without a real Platform session or GPU Device at
// all.

namespace {

namespace fs = std::filesystem;

// Per-process tag: catch_discover_tests runs each TEST_CASE in its own
// process under ctest -j, and the counter below restarts at 0 in each.
const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_scene_load_tests" /
              (label + "_" + gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
    fs::create_directories(path);
  }
  ~TempDirGuard() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDirGuard(const TempDirGuard&) = delete;
  TempDirGuard& operator=(const TempDirGuard&) = delete;
};

void writeFile(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

constexpr std::string_view kValidTriangleSource =
    "atlantis_static_mesh_source_version: 3\n"
    "vertex_count: 3\n"
    "index_count: 3\n"
    "vertex: 0.0 0.0 0.0 1.0 0.0 0.0 0.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.0 0.577350269 0.577350269 0.577350269\n"
    "vertex: 0.0 1.0 0.0 0.0 0.0 1.0 0.0 1.0 0.577350269 0.577350269 0.577350269\n"
    "index: 0 1 2\n";

struct CookedMeshFixture {
  fs::path artifactPath;
  fs::path metadataPath;
};

[[nodiscard]] CookedMeshFixture cookFixtureMesh(const fs::path& dir, const std::string& logicalPath) {
  const fs::path sourcePath = dir / "mesh_source" / (logicalPath + ".txt");
  writeFile(sourcePath, std::string(kValidTriangleSource));
  const fs::path artifactPath = dir / (logicalPath + ".amesh");
  const fs::path metadataPath = dir / (logicalPath + ".amesh.meta.txt");
  REQUIRE(cookStaticMesh(sourcePath.string(), logicalPath, testAssetGuid(logicalPath), artifactPath.string(), metadataPath.string()).isOk());
  return CookedMeshFixture{artifactPath, metadataPath};
}

struct CookedSceneFixture {
  fs::path artifactPath;
  fs::path metadataPath;
};

// Cooks a real scene referencing meshLogicalPaths in exactly the given
// order (node 1 references meshLogicalPaths[0], node 2 references
// meshLogicalPaths[1], etc.) -- meshLogicalPaths' own element order is
// therefore this scene's own first-reference order.
[[nodiscard]] CookedSceneFixture cookFixtureScene(const fs::path& dir,
                                                   const std::vector<std::string>& meshLogicalPaths) {
  std::string source = "atlantis_scene_source_version: 7\n";
  source += "node_count: " + std::to_string(meshLogicalPaths.size()) + "\n";
  source += "active_camera: none\n";
  for (std::size_t i = 0; i < meshLogicalPaths.size(); ++i) {
    source += "node: node_id=" + std::to_string(i + 1) + testNodeGuidToken(static_cast<std::uint32_t>(i + 1)) +
              " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=" +
              atlantis::asset_system::toString(testAssetGuid(meshLogicalPaths[i])) + "\n";
  }
  const fs::path sourcePath = dir / "scene.scene.txt";
  writeFile(sourcePath, source);
  const fs::path artifactPath = dir / "scene.ascene";
  const fs::path metadataPath = dir / "scene.ascene.meta.txt";
  REQUIRE(cookScene(sourcePath.string(), testAssetGuid("scene"), artifactPath.string(), metadataPath.string()).isOk());
  return CookedSceneFixture{artifactPath, metadataPath};
}

// Plan 0018 Milestone 11 regression coverage (PR #88 final review round):
// every node names BOTH a mesh and a material (materialLogicalPaths[i] for
// meshLogicalPaths[i]) -- the grammar's own 13-token case (Plan 0018
// Section P6) never accepts material= without mesh=.
[[nodiscard]] CookedSceneFixture cookFixtureSceneWithMaterials(
    const fs::path& dir, const std::vector<std::string>& meshLogicalPaths,
    const std::vector<std::string>& materialLogicalPaths) {
  REQUIRE(meshLogicalPaths.size() == materialLogicalPaths.size());
  std::string source = "atlantis_scene_source_version: 7\n";
  source += "node_count: " + std::to_string(meshLogicalPaths.size()) + "\n";
  source += "active_camera: none\n";
  for (std::size_t i = 0; i < meshLogicalPaths.size(); ++i) {
    source += "node: node_id=" + std::to_string(i + 1) + testNodeGuidToken(static_cast<std::uint32_t>(i + 1)) +
              " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=" +
              atlantis::asset_system::toString(testAssetGuid(meshLogicalPaths[i])) + " material=" +
              atlantis::asset_system::toString(testAssetGuid(materialLogicalPaths[i])) + "\n";
  }
  const fs::path sourcePath = dir / "scene_with_material.scene.txt";
  writeFile(sourcePath, source);
  const fs::path artifactPath = dir / "scene_with_material.ascene";
  const fs::path metadataPath = dir / "scene_with_material.ascene.meta.txt";
  REQUIRE(cookScene(sourcePath.string(), testAssetGuid("scene"), artifactPath.string(), metadataPath.string()).isOk());
  return CookedSceneFixture{artifactPath, metadataPath};
}

struct CookedTextureFixture {
  fs::path artifactPath;
  fs::path metadataPath;
};

[[nodiscard]] CookedTextureFixture cookFixtureTexture(const fs::path& dir, const std::string& logicalPath) {
  constexpr std::uint32_t kExtent = 2;
  const std::vector<std::uint8_t> pixelBytes(static_cast<std::size_t>(kExtent) * kExtent * 4, 0x7F);
  const fs::path artifactPath = dir / (logicalPath + ".atex");
  const fs::path metadataPath = dir / (logicalPath + ".atex.meta.txt");
  REQUIRE(cookTexture(pixelBytes.data(), kExtent, kExtent, 4, TextureColorSpace::Unorm, logicalPath, testAssetGuid(logicalPath), artifactPath,
                       metadataPath)
              .isOk());
  return CookedTextureFixture{artifactPath, metadataPath};
}

struct CookedMaterialFixture {
  fs::path artifactPath;
  fs::path metadataPath;
};

// textureLogicalPath is never validated by cookMaterial() itself (ADR-0059
// D6/D7, value-level-only reference) -- callers that want a fully
// resolvable material must separately cook and manifest-declare that same
// logical path; callers that want a deliberately-unresolvable texture
// reference may pass a logical path that is never cooked/declared at all.
[[nodiscard]] CookedMaterialFixture cookFixtureMaterial(const fs::path& dir, const std::string& logicalPath,
                                                          const std::string& textureLogicalPath) {
  const fs::path sourcePath = dir / "material_source" / (logicalPath + ".txt");
  writeFile(sourcePath, "atlantis_material_source_version: 10\n"
                        "kind: unlit_textured\n"
                        "texture: " + atlantis::asset_system::toString(testAssetGuid(textureLogicalPath)) + "\n"
                        "filter: linear\n"
                        "address_mode: repeat\n");
  const fs::path artifactPath = dir / (logicalPath + ".amaterial");
  const fs::path metadataPath = dir / (logicalPath + ".amaterial.meta.txt");
  REQUIRE(cookMaterial(sourcePath.string(), logicalPath, testAssetGuid(logicalPath), artifactPath.string(), metadataPath.string()).isOk());
  return CookedMaterialFixture{artifactPath, metadataPath};
}

using atlantis::asset_system::CatalogAssetType;
using atlantis::runtime::test_support::CatalogBuilder;

[[nodiscard]] BootstrapConfig makeConfig(const fs::path& catalogPath, const std::string& sceneLogicalPath = "scene") {
  BootstrapConfig config;
  config.assetCatalogPath = catalogPath.string();
  config.sceneAsset = testAssetGuid(sceneLogicalPath);
  return config;
}

}  // namespace


TEST_CASE("loadAndInstantiateScene: an unreadable catalog fails with AssetCatalogLoadFailed, no device access",
          "[runtime][scene]") {
  TempDirGuard dir("bad_catalog");
  const BootstrapConfig config = makeConfig(dir.path / "does_not_exist.catalog.txt");

  const auto result = loadAndInstantiateScene(config, /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::AssetCatalogLoadFailed);
}

TEST_CASE("loadAndInstantiateScene: a malformed catalog fails with AssetCatalogLoadFailed", "[runtime][scene]") {
  TempDirGuard dir("malformed_catalog");
  writeFile(dir.path / "catalog.txt", "not a catalog\n");

  const auto result = loadAndInstantiateScene(makeConfig(dir.path / "catalog.txt"), nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::AssetCatalogLoadFailed);
}

TEST_CASE("loadAndInstantiateScene: a scene GUID with no catalog record fails with SceneNotInCatalog",
          "[runtime][scene]") {
  TempDirGuard dir("scene_not_in_catalog");
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  const fs::path catalog =
      CatalogBuilder(dir.path).addMesh("meshes/a.mesh.txt", mesh.artifactPath, mesh.metadataPath).write();

  const auto result = loadAndInstantiateScene(makeConfig(catalog), nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneNotInCatalog);
}

TEST_CASE("loadAndInstantiateScene: a scene GUID whose record is not a scene fails with SceneNotInCatalog",
          "[runtime][scene]") {
  TempDirGuard dir("scene_is_a_mesh");
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "scene");
  const fs::path catalog = CatalogBuilder(dir.path).addMesh("scene", mesh.artifactPath, mesh.metadataPath).write();

  const auto result = loadAndInstantiateScene(makeConfig(catalog), nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneNotInCatalog);
}

TEST_CASE("loadAndInstantiateScene: a scene record with an unsupported artifact schema fails with "
          "UnsupportedArtifactSchema",
          "[runtime][scene]") {
  TempDirGuard dir("scene_schema");
  const CookedSceneFixture scene = cookFixtureScene(dir.path, {"meshes/a.mesh.txt"});
  const fs::path catalog =
      CatalogBuilder(dir.path).add(CatalogAssetType::Scene, "scene", scene.artifactPath, scene.metadataPath, 6).write();

  const auto result = loadAndInstantiateScene(makeConfig(catalog), nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::UnsupportedArtifactSchema);
}

TEST_CASE("loadAndInstantiateScene V20: rejects an unreadable scene artifact, no device access", "[runtime][scene]") {
  TempDirGuard dir("bad_scene_artifact");
  const fs::path catalog =
      CatalogBuilder(dir.path)
          .addScene("scene",
                    CookedSceneFixture{dir.path / "does_not_exist.ascene", dir.path / "does_not_exist.ascene.meta.txt"},
                    {})
          .write();

  const auto result = loadAndInstantiateScene(makeConfig(catalog), /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneArtifactLoadFailed);
}

TEST_CASE("loadAndInstantiateScene V17: a referenced AssetId with no catalog record fails with "
          "SceneDependencyUnresolved, before any Entity could exist",
          "[runtime][scene]") {
  TempDirGuard dir("unresolved_dependency");
  const CookedSceneFixture scene = cookFixtureScene(dir.path, {"meshes/never_declared.mesh.txt"});
  // The scene's own one mesh reference has no record at all.
  const fs::path catalog = CatalogBuilder(dir.path).addScene("scene", scene, {}).write();

  // device = nullptr proves this path never reaches step (e)'s own
  // device dereference, let alone step (f)'s fromValidatedSceneData()
  // call -- there is no World, and therefore no Entity, anywhere on
  // this Result's own Err path.
  const auto result = loadAndInstantiateScene(makeConfig(catalog), /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneDependencyUnresolved);
}

TEST_CASE("loadAndInstantiateScene: a mesh reference whose record is another asset type fails with "
          "DependencyTypeMismatch",
          "[runtime][scene]") {
  TempDirGuard dir("dependency_type_mismatch");
  const CookedSceneFixture scene = cookFixtureScene(dir.path, {"meshes/a.mesh.txt"});
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  // The record under the mesh GUID claims to be a texture.
  const fs::path catalog = CatalogBuilder(dir.path)
                               .addScene("scene", scene, {testAssetGuid("meshes/a.mesh.txt")})
                               .add(CatalogAssetType::Texture, "meshes/a.mesh.txt", mesh.artifactPath,
                                    mesh.metadataPath, atlantis::asset_system::kTextureArtifactSchemaVersion)
                               .write();

  const auto result = loadAndInstantiateScene(makeConfig(catalog), /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::DependencyTypeMismatch);
}

TEST_CASE("loadAndInstantiateScene: a mesh record with an unsupported artifact schema fails with "
          "UnsupportedArtifactSchema",
          "[runtime][scene]") {
  TempDirGuard dir("mesh_schema");
  const CookedSceneFixture scene = cookFixtureScene(dir.path, {"meshes/a.mesh.txt"});
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  const fs::path catalog = CatalogBuilder(dir.path)
                               .addScene("scene", scene, {testAssetGuid("meshes/a.mesh.txt")})
                               .addMesh("meshes/a.mesh.txt", mesh.artifactPath, mesh.metadataPath, 3)
                               .write();

  const auto result = loadAndInstantiateScene(makeConfig(catalog), /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::UnsupportedArtifactSchema);
}

TEST_CASE("loadAndInstantiateScene V20: a dependency whose own artifact fails to load fails with "
          "SceneDependencyLoadFailed",
          "[runtime][scene]") {
  TempDirGuard dir("dependency_load_failed");
  const CookedSceneFixture scene = cookFixtureScene(dir.path, {"meshes/a.mesh.txt"});
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  // The mesh record names an artifact that does not exist.
  const fs::path catalog = CatalogBuilder(dir.path)
                               .addScene("scene", scene, {testAssetGuid("meshes/a.mesh.txt")})
                               .addMesh("meshes/a.mesh.txt", dir.path / "does_not_exist.amesh", mesh.metadataPath)
                               .write();

  const auto result = loadAndInstantiateScene(makeConfig(catalog), /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneDependencyLoadFailed);
}

TEST_CASE("loadAndInstantiateScene: a scene with no Renderable references succeeds with an empty "
          "meshResourceMap and never touches device",
          "[runtime][scene]") {
  TempDirGuard dir("no_renderables");
  // A zero-node scene would be rejected as EmptyScene -- use one
  // plain, mesh-less node instead, still with zero Renderables.
  const fs::path sourcePath = dir.path / "plain.scene.txt";
  writeFile(sourcePath,
            "atlantis_scene_source_version: 7\n"
            "node_count: 1\n"
            "active_camera: none\n"
            "node: node_id=1 guid=e68122c6-1bb2-8f1f-b185-358f58780b05 parent=none position=0.0 0.0 0.0 "
            "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  const fs::path artifactPath = dir.path / "plain.ascene";
  const fs::path metadataPath = dir.path / "plain.ascene.meta.txt";
  REQUIRE(cookScene(sourcePath.string(), testAssetGuid("scene"), artifactPath.string(), metadataPath.string()).isOk());
  const fs::path catalog =
      CatalogBuilder(dir.path).addScene("scene", CookedSceneFixture{artifactPath, metadataPath}, {}).write();

  const auto result = loadAndInstantiateScene(makeConfig(catalog), /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isOk());
  CHECK(result.value().meshResourceMap.empty());
  CHECK(result.value().world.renderableEntities().empty());
}

TEST_CASE("loadAndInstantiateScene V19: load order follows first-reference order, not AssetId-numeric order",
          "[runtime][scene]") {
  // firstReferencedLogicalPath's own artifact is deliberately missing;
  // secondReferencedLogicalPath's own artifact is real and valid, and
  // its own AssetId is numerically smaller than the first's. If load
  // order were (incorrectly) AssetId-sorted, the second entry would be
  // attempted before the first -- its loadStaticMeshAsset() would
  // succeed, and the very next line would dereference device (nullptr
  // here), aborting this test process. If load order is (correctly)
  // first-reference-based, the first entry's own missing artifact is
  // hit immediately, returning Err(SceneDependencyLoadFailed) before
  // the second entry -- or device -- is ever touched. A clean,
  // non-aborting Err is this test's own pass criterion.
  TempDirGuard dir("first_reference_order");

  constexpr const char* kCandidateA = "meshes/candidate_a.mesh.txt";
  constexpr const char* kCandidateB = "meshes/candidate_b.mesh.txt";
  const AssetId idA = atlantis::asset_system::assetKey(testAssetGuid(kCandidateA));
  const AssetId idB = atlantis::asset_system::assetKey(testAssetGuid(kCandidateB));
  REQUIRE(idA != idB);  // not a collision test; any distinct pair works

  const std::string firstReferenced = idA > idB ? kCandidateA : kCandidateB;   // the numerically LARGER one
  const std::string secondReferenced = idA > idB ? kCandidateB : kCandidateA;  // the numerically SMALLER one

  const CookedSceneFixture scene = cookFixtureScene(dir.path, {firstReferenced, secondReferenced});
  const CookedMeshFixture validFirstMesh = cookFixtureMesh(dir.path, firstReferenced);
  const CookedMeshFixture validSecondMesh = cookFixtureMesh(dir.path, secondReferenced);

  // Only the FIRST record substitutes a missing artifact path.
  const fs::path catalog =
      CatalogBuilder(dir.path)
          .addScene("scene", scene, {testAssetGuid(firstReferenced), testAssetGuid(secondReferenced)})
          .addMesh(firstReferenced, dir.path / "does_not_exist.amesh", validFirstMesh.metadataPath)
          .addMesh(secondReferenced, validSecondMesh.artifactPath, validSecondMesh.metadataPath)
          .write();

  const auto result = loadAndInstantiateScene(makeConfig(catalog), /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneDependencyLoadFailed);
}

// Plan 0018 Milestone 11 regression coverage (PR #88 final review round).
// Material resolution (step (d)) runs entirely before any mesh or material
// is ever LOADED (step (e)) -- a material AssetId with no catalog record is
// therefore caught before device is ever dereferenced, even though this
// scene's own mesh reference IS resolvable (only resolved, never loaded, on
// this path).
TEST_CASE("loadAndInstantiateScene: an unresolvable material AssetId fails scene load fatally with "
          "SceneDependencyUnresolved (Spec 0018 D4 case 2), before any Entity could exist, no device access",
          "[runtime][scene][material]") {
  TempDirGuard dir("unresolved_material");
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  const CookedSceneFixture scene = cookFixtureSceneWithMaterials(dir.path, {"meshes/a.mesh.txt"},
                                                                  {"materials/never_declared.material.txt"});
  // No record for materials/never_declared.material.txt at all.
  const fs::path catalog = CatalogBuilder(dir.path)
                               .addScene("scene", scene, {testAssetGuid("meshes/a.mesh.txt")})
                               .addMesh("meshes/a.mesh.txt", mesh.artifactPath, mesh.metadataPath)
                               .write();

  const auto result = loadAndInstantiateScene(makeConfig(catalog), /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneDependencyUnresolved);
}

TEST_CASE("AssetCatalog: two scenes sharing a mesh resolve it to one record", "[runtime][scene][catalog]") {
  TempDirGuard dir("shared_mesh");
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/shared.mesh.txt");
  const CookedSceneFixture sceneA = cookFixtureScene(dir.path, {"meshes/shared.mesh.txt"});
  // cookFixtureScene() writes one fixed filename; move scene A aside first.
  fs::rename(sceneA.artifactPath, dir.path / "a.ascene");
  fs::rename(sceneA.metadataPath, dir.path / "a.ascene.meta.txt");
  const CookedSceneFixture sceneB = cookFixtureScene(dir.path, {"meshes/shared.mesh.txt"});
  const auto sharedGuid = testAssetGuid("meshes/shared.mesh.txt");
  // Two distinct scene records (distinct logical paths, hence GUIDs), one
  // shared mesh record.
  const fs::path catalogPath =
      CatalogBuilder(dir.path)
          .addMesh("meshes/shared.mesh.txt", mesh.artifactPath, mesh.metadataPath)
          .addScene("scene_a", CookedSceneFixture{dir.path / "a.ascene", dir.path / "a.ascene.meta.txt"}, {sharedGuid})
          .addScene("scene_b", sceneB, {sharedGuid})
          .write();

  const auto catalog = atlantis::asset_system::loadAssetCatalog(catalogPath.string());
  REQUIRE(catalog.isOk());
  REQUIRE(catalog.value().size() == 3);
  const auto* viaGuid = catalog.value().find(sharedGuid);
  const auto* viaId = catalog.value().find(atlantis::asset_system::assetKey(sharedGuid));
  REQUIRE(viaGuid != nullptr);
  CHECK(viaGuid == viaId);
  for (const char* sceneName : {"scene_a", "scene_b"}) {
    const auto* scene = catalog.value().find(testAssetGuid(sceneName));
    REQUIRE(scene != nullptr);
    REQUIRE(scene->dependencies.size() == 1);
    CHECK(catalog.value().find(scene->dependencies[0]) == viaGuid);
  }
}
