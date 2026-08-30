#include <atlantis/runtime/scene_load.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/cook.h>
#include <atlantis/asset_system/cook_material.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/cook_texture.h>
#include <atlantis/asset_system/texture_types.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace atlantis::runtime;
using atlantis::asset_system::AssetId;
using atlantis::asset_system::computeAssetId;
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
// all, completing the deferral tests/runtime/scene_manifest_tests.cpp
// (Step 7) disclosed for both V17 and V19.

namespace {

namespace fs = std::filesystem;

std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_scene_load_tests" /
              (label + "_" + std::to_string(gScratchCounter.fetch_add(1)))) {
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
  REQUIRE(cookStaticMesh(sourcePath.string(), logicalPath, artifactPath.string(), metadataPath.string()).isOk());
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
  std::string source = "atlantis_scene_source_version: 3\n";
  source += "node_count: " + std::to_string(meshLogicalPaths.size()) + "\n";
  source += "active_camera: none\n";
  for (std::size_t i = 0; i < meshLogicalPaths.size(); ++i) {
    source += "node: node_id=" + std::to_string(i + 1) +
              " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=" +
              meshLogicalPaths[i] + "\n";
  }
  const fs::path sourcePath = dir / "scene.scene.txt";
  writeFile(sourcePath, source);
  const fs::path artifactPath = dir / "scene.ascene";
  const fs::path metadataPath = dir / "scene.ascene.meta.txt";
  REQUIRE(cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string()).isOk());
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
  std::string source = "atlantis_scene_source_version: 3\n";
  source += "node_count: " + std::to_string(meshLogicalPaths.size()) + "\n";
  source += "active_camera: none\n";
  for (std::size_t i = 0; i < meshLogicalPaths.size(); ++i) {
    source += "node: node_id=" + std::to_string(i + 1) +
              " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=" +
              meshLogicalPaths[i] + " material=" + materialLogicalPaths[i] + "\n";
  }
  const fs::path sourcePath = dir / "scene_with_material.scene.txt";
  writeFile(sourcePath, source);
  const fs::path artifactPath = dir / "scene_with_material.ascene";
  const fs::path metadataPath = dir / "scene_with_material.ascene.meta.txt";
  REQUIRE(cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string()).isOk());
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
  REQUIRE(cookTexture(pixelBytes.data(), kExtent, kExtent, 4, TextureColorSpace::Unorm, logicalPath, artifactPath,
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
  writeFile(sourcePath, "atlantis_material_source_version: 2\n"
                        "kind: unlit_textured\n"
                        "texture: " + textureLogicalPath + "\n"
                        "filter: linear\n"
                        "address_mode: repeat\n");
  const fs::path artifactPath = dir / (logicalPath + ".amaterial");
  const fs::path metadataPath = dir / (logicalPath + ".amaterial.meta.txt");
  REQUIRE(cookMaterial(sourcePath.string(), logicalPath, artifactPath.string(), metadataPath.string()).isOk());
  return CookedMaterialFixture{artifactPath, metadataPath};
}

void writeManifestLine(std::string& manifest, const std::string& logicalPath, const fs::path& artifactPath,
                        const fs::path& metadataPath) {
  manifest += logicalPath + "\t" + artifactPath.string() + "\t" + metadataPath.string() + "\n";
}

[[nodiscard]] BootstrapConfig makeConfig(const fs::path& sceneArtifactPath, const fs::path& sceneMetadataPath,
                                          const fs::path& manifestPath) {
  BootstrapConfig config;
  config.sceneArtifactPath = sceneArtifactPath.string();
  config.sceneMetadataPath = sceneMetadataPath.string();
  config.sceneDependencyManifestPath = manifestPath.string();
  return config;
}

}  // namespace

TEST_CASE("loadAndInstantiateScene V20: rejects an unreadable manifest, no device access", "[runtime][scene]") {
  // Manifest loading (step (a)) is checked before the scene artifact is
  // ever opened (step (b)) -- the scene path below need not resolve to
  // anything real for this test.
  TempDirGuard dir("bad_manifest");
  const BootstrapConfig config = makeConfig(dir.path / "irrelevant.ascene", dir.path / "irrelevant.ascene.meta.txt",
                                             dir.path / "does_not_exist.manifest.txt");

  const auto result = loadAndInstantiateScene(config, /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneManifestLoadFailed);
}

TEST_CASE("loadAndInstantiateScene V20: rejects an unreadable scene artifact, no device access", "[runtime][scene]") {
  TempDirGuard dir("bad_scene_artifact");
  writeFile(dir.path / "empty.manifest.txt", "");
  BootstrapConfig config;
  config.sceneArtifactPath = (dir.path / "does_not_exist.ascene").string();
  config.sceneMetadataPath = (dir.path / "does_not_exist.ascene.meta.txt").string();
  config.sceneDependencyManifestPath = (dir.path / "empty.manifest.txt").string();

  const auto result = loadAndInstantiateScene(config, /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneArtifactLoadFailed);
}

TEST_CASE("loadAndInstantiateScene V17: a referenced AssetId with no manifest entry fails with "
          "SceneDependencyUnresolved, before any Entity could exist",
          "[runtime][scene]") {
  TempDirGuard dir("unresolved_dependency");
  const CookedSceneFixture scene = cookFixtureScene(dir.path, {"meshes/never_declared.mesh.txt"});
  // Empty manifest -- the scene's own one reference has no entry at all.
  writeFile(dir.path / "empty.manifest.txt", "");
  const BootstrapConfig config = makeConfig(scene.artifactPath, scene.metadataPath, dir.path / "empty.manifest.txt");

  // device = nullptr proves this path never reaches step (e)'s own
  // device dereference, let alone step (f)'s fromValidatedSceneData()
  // call -- there is no World, and therefore no Entity, anywhere on
  // this Result's own Err path; the function returns before either
  // could ever be constructed.
  const auto result = loadAndInstantiateScene(config, /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneDependencyUnresolved);
}

TEST_CASE("loadAndInstantiateScene V20: a dependency whose own artifact fails to load fails with "
          "SceneDependencyLoadFailed",
          "[runtime][scene]") {
  // loadSceneDependencyManifest() itself (step (a)) already validates
  // every entry's own metadata sidecar (D8 step 5) -- a manifest entry
  // must carry a REAL, valid metadata path to pass manifest loading at
  // all. To reach step (e)'s own loadStaticMeshAsset() failure, only
  // the ARTIFACT path (never read during manifest validation) may be
  // missing.
  TempDirGuard dir("dependency_load_failed");
  const CookedSceneFixture scene = cookFixtureScene(dir.path, {"meshes/a.mesh.txt"});
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  std::string manifest;
  writeManifestLine(manifest, "meshes/a.mesh.txt", dir.path / "does_not_exist.amesh", mesh.metadataPath);
  writeFile(dir.path / "bad.manifest.txt", manifest);
  const BootstrapConfig config = makeConfig(scene.artifactPath, scene.metadataPath, dir.path / "bad.manifest.txt");

  const auto result = loadAndInstantiateScene(config, /*device=*/nullptr, VertexInputLayout{});
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
            "atlantis_scene_source_version: 3\n"
            "node_count: 1\n"
            "active_camera: none\n"
            "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n");
  const fs::path artifactPath = dir.path / "plain.ascene";
  const fs::path metadataPath = dir.path / "plain.ascene.meta.txt";
  REQUIRE(cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string()).isOk());
  writeFile(dir.path / "empty.manifest.txt", "");
  const BootstrapConfig config = makeConfig(artifactPath, metadataPath, dir.path / "empty.manifest.txt");

  const auto result = loadAndInstantiateScene(config, /*device=*/nullptr, VertexInputLayout{});
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
  const AssetId idA = computeAssetId(kCandidateA);
  const AssetId idB = computeAssetId(kCandidateB);
  REQUIRE(idA != idB);  // not a collision test; any distinct pair works

  const std::string firstReferenced = idA > idB ? kCandidateA : kCandidateB;   // the numerically LARGER one
  const std::string secondReferenced = idA > idB ? kCandidateB : kCandidateA;  // the numerically SMALLER one

  const CookedSceneFixture scene = cookFixtureScene(dir.path, {firstReferenced, secondReferenced});
  // Both cooked for real metadata (loadSceneDependencyManifest() itself
  // validates every entry's own metadata sidecar, D8 step 5) -- only
  // the FIRST entry's own manifest line then substitutes a missing
  // artifact path, since that field is never read during manifest
  // validation, only later, at step (e).
  const CookedMeshFixture validFirstMesh = cookFixtureMesh(dir.path, firstReferenced);
  const CookedMeshFixture validSecondMesh = cookFixtureMesh(dir.path, secondReferenced);

  std::string manifest;
  writeManifestLine(manifest, firstReferenced, dir.path / "does_not_exist.amesh", validFirstMesh.metadataPath);
  writeManifestLine(manifest, secondReferenced, validSecondMesh.artifactPath, validSecondMesh.metadataPath);
  writeFile(dir.path / "order.manifest.txt", manifest);
  const BootstrapConfig config = makeConfig(scene.artifactPath, scene.metadataPath, dir.path / "order.manifest.txt");

  const auto result = loadAndInstantiateScene(config, /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneDependencyLoadFailed);
}

// Plan 0018 Milestone 11 regression coverage (PR #88 final review round --
// this exact case was in the Approved Plan's own Milestone 11 test list
// but was never actually added). Material resolution (step (d)) runs
// entirely before any mesh or material is ever LOADED (step (e)) -- a
// material AssetId with no manifest entry is therefore caught before
// device is ever dereferenced, even though this scene's own mesh
// reference IS resolvable (only resolved, never loaded, on this path).
TEST_CASE("loadAndInstantiateScene: an unresolvable material AssetId fails scene load fatally with "
          "SceneDependencyUnresolved (Spec 0018 D4 case 2), before any Entity could exist, no device access",
          "[runtime][scene][material]") {
  TempDirGuard dir("unresolved_material");
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  const CookedSceneFixture scene = cookFixtureSceneWithMaterials(dir.path, {"meshes/a.mesh.txt"},
                                                                  {"materials/never_declared.material.txt"});
  std::string manifest;
  writeManifestLine(manifest, "meshes/a.mesh.txt", mesh.artifactPath, mesh.metadataPath);
  // No manifest entry for materials/never_declared.material.txt at all.
  writeFile(dir.path / "manifest.txt", manifest);
  const BootstrapConfig config = makeConfig(scene.artifactPath, scene.metadataPath, dir.path / "manifest.txt");

  const auto result = loadAndInstantiateScene(config, /*device=*/nullptr, VertexInputLayout{});
  REQUIRE(result.isErr());
  CHECK(result.error() == RuntimeInitError::SceneDependencyUnresolved);
}
