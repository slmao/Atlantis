#include <atlantis/runtime/scene_manifest.h>

#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/cook.h>
#include <atlantis/asset_system/cook_material.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

using namespace atlantis::runtime;
using atlantis::asset_system::AssetMetadata;
using atlantis::asset_system::computeAssetId;
using atlantis::asset_system::cookMaterial;
using atlantis::asset_system::cookStaticMesh;
using atlantis::asset_system::serializeAssetMetadata;

// Plan 0015 Section D8. V17 (SceneDependencyUnresolved, confirmed
// before any Entity exists) and V19 (first-reference vs AssetId-sorted
// load order) are deliberately NOT covered by this file -- both are
// genuinely about Runtime's own resolve/load call site (D10's own step
// (e)), which does not exist as code until Step 8's initializeSteps()
// resequencing lands. The V-table's own wording already anticipates
// this ("... or a dedicated Runtime-level test" for V17; "A dedicated
// Runtime-level test", not this file, for V19) -- matching the same
// disclosed-deferral precedent Step 1 already established for
// ValidatedSceneData's own copy/move round-trip test (completed only
// once Step 4's decodeScene() existed).

namespace {

namespace fs = std::filesystem;

std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_scene_manifest_tests" /
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

// Cooks one real mesh asset at logicalPath (e.g. "meshes/a.mesh.txt"),
// returning its own artifact/metadata paths -- a genuine, real-hash
// fixture for the manifest tests that need one (unlike V15's own
// AssetIdCollision test, which uses detail::checkForDuplicatesAndCollisions()
// directly with fabricated values instead, since a real 64-bit FNV-1a
// collision is not something a unit test can feasibly brute-force).
struct CookedMeshFixture {
  fs::path artifactPath;
  fs::path metadataPath;
};

[[nodiscard]] CookedMeshFixture cookFixtureMesh(const fs::path& dir, const std::string& logicalPath) {
  const fs::path sourcePath = dir / "source" / (logicalPath + ".txt");
  writeFile(sourcePath, std::string(kValidTriangleSource));
  const fs::path artifactPath = dir / (logicalPath + ".amesh");
  const fs::path metadataPath = dir / (logicalPath + ".amesh.meta.txt");
  REQUIRE(cookStaticMesh(sourcePath.string(), logicalPath, artifactPath.string(), metadataPath.string()).isOk());
  return CookedMeshFixture{artifactPath, metadataPath};
}

}  // namespace

TEST_CASE("loadSceneDependencyManifest rejects an unreadable manifest file", "[runtime][scene]") {
  const auto result = loadSceneDependencyManifest("does_not_exist_at_all.manifest.txt");
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneManifestError::ManifestUnreadable);
}

TEST_CASE("loadSceneDependencyManifest accepts an empty manifest (zero MESH_DEPENDENCIES)", "[runtime][scene]") {
  TempDirGuard dir("empty");
  const fs::path manifestPath = dir.path / "empty.manifest.txt";
  writeFile(manifestPath, "");
  const auto result = loadSceneDependencyManifest(manifestPath.string());
  REQUIRE(result.isOk());
  CHECK(result.value().entries.empty());
}

TEST_CASE("loadSceneDependencyManifest rejects a line with the wrong number of tabs", "[runtime][scene]") {
  TempDirGuard dir("malformed_tabs");
  const fs::path manifestPath = dir.path / "bad.manifest.txt";
  writeFile(manifestPath, "meshes/a.mesh.txt\tonly_one_tab_field\n");
  const auto result = loadSceneDependencyManifest(manifestPath.string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneManifestError::MalformedEntry);
}

TEST_CASE("loadSceneDependencyManifest rejects a line with an empty field", "[runtime][scene]") {
  TempDirGuard dir("malformed_empty_field");
  const fs::path manifestPath = dir.path / "bad.manifest.txt";
  writeFile(manifestPath, "meshes/a.mesh.txt\t\tsome_metadata_path\n");
  const auto result = loadSceneDependencyManifest(manifestPath.string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneManifestError::MalformedEntry);
}

TEST_CASE("loadSceneDependencyManifest rejects a malformed logical path", "[runtime][scene]") {
  TempDirGuard dir("malformed_logical_path");
  const fs::path manifestPath = dir.path / "bad.manifest.txt";
  writeFile(manifestPath, "../escapes_the_asset_root.mesh.txt\tsome_artifact\tsome_metadata\n");
  const auto result = loadSceneDependencyManifest(manifestPath.string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneManifestError::MalformedEntry);
}

TEST_CASE("loadSceneDependencyManifest V14: rejects a duplicate logical path", "[runtime][scene]") {
  TempDirGuard dir("duplicate_path");
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  const fs::path manifestPath = dir.path / "dup.manifest.txt";
  writeFile(manifestPath, "meshes/a.mesh.txt\t" + mesh.artifactPath.string() + "\t" + mesh.metadataPath.string() +
                              "\n" + "meshes/a.mesh.txt\t" + mesh.artifactPath.string() + "\t" +
                              mesh.metadataPath.string() + "\n");
  const auto result = loadSceneDependencyManifest(manifestPath.string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneManifestError::DuplicateLogicalPath);
}

TEST_CASE("detail::checkForDuplicatesAndCollisions V15: rejects two distinct logical paths sharing an AssetId",
          "[runtime][scene]") {
  // A genuine 64-bit FNV-1a collision is not feasible to brute-force
  // in a unit test -- this test exercises the same detection algorithm
  // loadSceneDependencyManifest() itself calls, with a fabricated
  // shared AssetId, matching asset_set_validation_tests.cpp's own
  // already-Accepted technique for AssetSetError::AssetIdCollision.
  constexpr atlantis::asset_system::AssetId kSharedId = 0x1122334455667788ULL;
  const std::vector<detail::ManifestEntryForCollisionCheck> entries{
      {kSharedId, "meshes/a.mesh.txt"},
      {kSharedId, "meshes/b.mesh.txt"},
  };
  const auto result = detail::checkForDuplicatesAndCollisions(entries);
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneManifestError::AssetIdCollision);
}

TEST_CASE("detail::checkForDuplicatesAndCollisions does not flag the same path/id pair as a collision",
          "[runtime][scene]") {
  const atlantis::asset_system::AssetId id = computeAssetId("meshes/a.mesh.txt");
  const std::vector<detail::ManifestEntryForCollisionCheck> entries{{id, "meshes/a.mesh.txt"}};
  CHECK(detail::checkForDuplicatesAndCollisions(entries).isOk());
}

TEST_CASE("loadSceneDependencyManifest V16: rejects a metadata/artifact AssetId mismatch", "[runtime][scene]") {
  TempDirGuard dir("metadata_mismatch");
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");

  // Overwrite the metadata sidecar with an otherwise well-formed
  // record whose own assetId does not match what the manifest's own
  // logical-path field computes.
  AssetMetadata wrongMetadata;
  wrongMetadata.assetId = 0xFFFFFFFFFFFFFFFFULL;
  wrongMetadata.sourceLogicalPath = "meshes/a.mesh.txt";
  wrongMetadata.importerVersion = "atlantis-asset-cooker/1";
  wrongMetadata.assetType = "static_mesh";
  wrongMetadata.vertexCount = 3;
  wrongMetadata.indexCount = 3;
  wrongMetadata.vertexStrideBytes = 24;
  writeFile(mesh.metadataPath, serializeAssetMetadata(wrongMetadata));

  const fs::path manifestPath = dir.path / "mismatch.manifest.txt";
  writeFile(manifestPath,
            "meshes/a.mesh.txt\t" + mesh.artifactPath.string() + "\t" + mesh.metadataPath.string() + "\n");
  const auto result = loadSceneDependencyManifest(manifestPath.string());
  REQUIRE(result.isErr());
  CHECK(result.error() == SceneManifestError::MetadataArtifactMismatch);
}

TEST_CASE(
    "loadSceneDependencyManifest Step 5 accepts a manifest entry whose metadata sidecar is a material, not a mesh",
    "[runtime][scene]") {
  // Plan 0018 Milestone 16 regression coverage: Step 5's own cross-check
  // previously called only parseAssetMetadata() (the mesh-specific
  // 8-line shape), so ANY texture/material manifest entry -- never
  // exercised before material_demo_scene, the first scene to declare
  // MATERIAL_DEPENDENCIES/TEXTURE_DEPENDENCIES -- always failed with
  // MetadataArtifactMismatch even when perfectly well-formed. This test
  // cooks a real material asset (no real texture needed -- cookMaterial()
  // never validates the referenced texture's own existence, ADR-0059
  // D6/D7) and confirms the manifest cross-check now accepts it.
  TempDirGuard dir("material_sidecar");
  const std::string logicalPath = "materials/a.material.txt";
  const fs::path sourcePath = dir.path / "source" / (logicalPath + ".txt");
  writeFile(sourcePath, "atlantis_material_source_version: 1\n"
                        "kind: unlit_textured\n"
                        "texture: textures/does_not_need_to_exist.png\n"
                        "filter: linear\n"
                        "address_mode: repeat\n");
  const fs::path artifactPath = dir.path / (logicalPath + ".amaterial");
  const fs::path metadataPath = dir.path / (logicalPath + ".amaterial.meta.txt");
  REQUIRE(cookMaterial(sourcePath.string(), logicalPath, artifactPath.string(), metadataPath.string()).isOk());

  const fs::path manifestPath = dir.path / "material.manifest.txt";
  writeFile(manifestPath, logicalPath + "\t" + artifactPath.string() + "\t" + metadataPath.string() + "\n");

  const auto result = loadSceneDependencyManifest(manifestPath.string());
  REQUIRE(result.isOk());
  const auto* found = result.value().find(computeAssetId(logicalPath));
  REQUIRE(found != nullptr);
  CHECK(found->artifactPath == artifactPath.string());
}

TEST_CASE("loadSceneDependencyManifest V18: an unreferenced entry does not fail the load", "[runtime][scene]") {
  TempDirGuard dir("unreferenced_entry");
  const CookedMeshFixture referenced = cookFixtureMesh(dir.path, "meshes/referenced.mesh.txt");
  const CookedMeshFixture unreferenced = cookFixtureMesh(dir.path, "meshes/unreferenced.mesh.txt");

  const fs::path manifestPath = dir.path / "with_extra.manifest.txt";
  writeFile(manifestPath, "meshes/referenced.mesh.txt\t" + referenced.artifactPath.string() + "\t" +
                              referenced.metadataPath.string() + "\n" + "meshes/unreferenced.mesh.txt\t" +
                              unreferenced.artifactPath.string() + "\t" + unreferenced.metadataPath.string() + "\n");

  const auto result = loadSceneDependencyManifest(manifestPath.string());
  REQUIRE(result.isOk());
  CHECK(result.value().entries.size() == 2);

  // Only the referenced entry is ever looked up -- the resolver itself
  // places no requirement on the unreferenced one being touched.
  const auto referencedId = computeAssetId("meshes/referenced.mesh.txt");
  const auto* found = result.value().find(referencedId);
  REQUIRE(found != nullptr);
  CHECK(found->artifactPath == referenced.artifactPath.string());
}

TEST_CASE("SceneDependencyResolver::find is a point lookup -- not found for an unknown AssetId",
          "[runtime][scene]") {
  TempDirGuard dir("point_lookup");
  const CookedMeshFixture mesh = cookFixtureMesh(dir.path, "meshes/a.mesh.txt");
  const fs::path manifestPath = dir.path / "one.manifest.txt";
  writeFile(manifestPath,
            "meshes/a.mesh.txt\t" + mesh.artifactPath.string() + "\t" + mesh.metadataPath.string() + "\n");

  const auto result = loadSceneDependencyManifest(manifestPath.string());
  REQUIRE(result.isOk());
  CHECK(result.value().find(0xDEADBEEFDEADBEEFULL) == nullptr);
  CHECK(result.value().find(computeAssetId("meshes/a.mesh.txt")) != nullptr);
}
