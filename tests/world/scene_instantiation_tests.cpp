#include <atlantis/world/scene_instantiation.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/logical_path.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using atlantis::asset_system::AssetId;
using atlantis::asset_system::computeAssetId;
using atlantis::asset_system::cookScene;
using atlantis::asset_system::decodeScene;
using atlantis::asset_system::normalizeLogicalPath;
using atlantis::asset_system::ValidatedSceneData;
using atlantis::world::Camera;
using atlantis::world::EntityId;
using atlantis::world::fromValidatedSceneData;
using atlantis::world::kInvalidEntityId;
using atlantis::world::Renderable;
using atlantis::world::Transform;
using atlantis::world::World;

// Plan 0015 Section D2/D9 (final review round, 2026-08-24): a prior
// revision of this file constructed ValidatedSceneData directly via a
// test-only friend (ValidatedSceneDataTestAccess). That friend has
// been removed from validated_scene_data.h entirely -- decodeScene()
// is now, without exception, the ONLY way any ValidatedSceneData
// instance can exist anywhere in this codebase, including here. Every
// scene this file exercises is therefore real, authored scene-source
// text, cooked via the real cookScene() and decoded via the real
// decodeScene(), exactly like any other consumer.

namespace {

namespace fs = std::filesystem;

std::atomic<int> gScratchCounter{0};

struct TempDirGuard {
  fs::path path;
  explicit TempDirGuard(const std::string& label)
      : path(fs::temp_directory_path() / "atlantis_scene_instantiation_tests" /
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

// Cooks sourceText via the real cookScene(), then decodes the result
// via the real decodeScene() -- the sole entry point capable of
// producing a ValidatedSceneData instance anywhere in this codebase.
[[nodiscard]] ValidatedSceneData cookAndDecodeScene(const std::string& sourceText) {
  static std::atomic<int> counter{0};
  const fs::path dir = fs::temp_directory_path() / "atlantis_scene_instantiation_tests" /
                        ("fixture_" + std::to_string(counter.fetch_add(1)));
  fs::create_directories(dir);
  const fs::path sourcePath = dir / "scene.scene.txt";
  const fs::path artifactPath = dir / "scene.ascene";
  const fs::path metadataPath = dir / "scene.ascene.meta.txt";
  writeFile(sourcePath, sourceText);

  auto cookResult = cookScene(sourcePath.string(), artifactPath.string(), metadataPath.string());
  REQUIRE(cookResult.isOk());
  auto decodeResult = decodeScene(artifactPath.string(), metadataPath.string());
  REQUIRE(decodeResult.isOk());

  std::error_code ec;
  fs::remove_all(dir, ec);
  return decodeResult.value();
}

[[nodiscard]] AssetId meshAssetIdFor(const std::string& logicalPath) {
  const auto normalized = normalizeLogicalPath(logicalPath);
  REQUIRE(normalized.isOk());
  return computeAssetId(normalized.value());
}

constexpr const char* kNode0MeshPath = "meshes/scene_instantiation_node0.mesh.txt";
constexpr const char* kNode1MeshPath = "meshes/scene_instantiation_node1.mesh.txt";

// Three nodes: node 1 a Renderable with no parent; node 2 a Renderable
// child of node 1 (its own, distinct mesh path makes it independently
// identifiable via renderableEntities(), not just reachable in
// principle); node 3 a Camera, no parent, the scene's own active
// camera. Values match the prior revision's own hand-built fixture
// exactly, just authored as real scene source text instead.
constexpr const char* kThreeNodeSceneSource =
    "atlantis_scene_source_version: 1\n"
    "node_count: 3\n"
    "active_camera: 3\n"
    "node: node_id=1 parent=none position=1.0 2.0 3.0 rotation=0.1 0.2 0.3 scale=1.0 1.0 1.0 "
    "mesh=meshes/scene_instantiation_node0.mesh.txt\n"
    "node: node_id=2 parent=1 position=4.0 5.0 6.0 rotation=0.0 0.0 0.0 scale=2.0 2.0 2.0 "
    "mesh=meshes/scene_instantiation_node1.mesh.txt\n"
    "node: node_id=3 parent=none position=0.0 2.2 7.0 rotation=-0.3054 0.0 0.0 scale=1.0 1.0 1.0 "
    "camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0\n";

}  // namespace

static_assert(std::is_same_v<decltype(fromValidatedSceneData(std::declval<const ValidatedSceneData&>())), World>);

TEST_CASE("fromValidatedSceneData() instantiates every node with its own component data", "[world][scene]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kThreeNodeSceneSource);
  World world = fromValidatedSceneData(scene);

  const AssetId node0MeshAsset = meshAssetIdFor(kNode0MeshPath);
  const AssetId node1MeshAsset = meshAssetIdFor(kNode1MeshPath);

  // renderableEntities() returns Renderable-bearing entities in
  // ascending slot-index order (World's own documented guarantee) --
  // fromValidatedSceneData() creates entities in ascending node-index
  // order (D9's own pass 1), so node0's entity sorts before node1's.
  const std::vector<EntityId> renderables = world.renderableEntities();
  REQUIRE(renderables.size() == 2);
  const EntityId node0Id = renderables[0];
  const EntityId node1Id = renderables[1];
  CHECK(node0Id.index() == 0);
  CHECK(node0Id.generation() == 0);
  CHECK(node1Id.index() == 1);
  CHECK(node1Id.generation() == 0);

  REQUIRE(world.activeCamera().has_value());
  const EntityId node2Id = *world.activeCamera();
  CHECK(node2Id.index() == 2);
  CHECK(node2Id.generation() == 0);

  const auto transformNode0 = world.getLocalTransform(node0Id);
  REQUIRE(transformNode0.isOk());
  CHECK(transformNode0.value().localPosition.x == 1.0f);
  CHECK(transformNode0.value().localPosition.y == 2.0f);
  CHECK(transformNode0.value().localPosition.z == 3.0f);
  CHECK(transformNode0.value().localEulerAnglesRadians.x == 0.1f);
  CHECK(transformNode0.value().localEulerAnglesRadians.y == 0.2f);
  CHECK(transformNode0.value().localEulerAnglesRadians.z == 0.3f);
  CHECK(transformNode0.value().localScale.x == 1.0f);

  const auto renderableNode0 = world.getRenderable(node0Id);
  REQUIRE(renderableNode0.isOk());
  CHECK(renderableNode0.value().meshAsset == node0MeshAsset);

  const auto transformNode1 = world.getLocalTransform(node1Id);
  REQUIRE(transformNode1.isOk());
  CHECK(transformNode1.value().localPosition.x == 4.0f);
  CHECK(transformNode1.value().localScale.x == 2.0f);

  const auto renderableNode1 = world.getRenderable(node1Id);
  REQUIRE(renderableNode1.isOk());
  CHECK(renderableNode1.value().meshAsset == node1MeshAsset);
  CHECK_FALSE(world.getCamera(node1Id).isOk());  // node1 has no Camera

  const auto cameraNode2 = world.getCamera(node2Id);
  REQUIRE(cameraNode2.isOk());
  CHECK(cameraNode2.value().fovYRadians == 1.0472f);
  CHECK(cameraNode2.value().nearZ == 0.1f);
  CHECK(cameraNode2.value().farZ == 100.0f);
  CHECK_FALSE(world.getRenderable(node2Id).isOk());  // node2 has no Renderable

  const auto transformNode2 = world.getLocalTransform(node2Id);
  REQUIRE(transformNode2.isOk());
  CHECK(transformNode2.value().localPosition.y == 2.2f);
  CHECK(transformNode2.value().localEulerAnglesRadians.x == -0.3054f);
}

TEST_CASE("fromValidatedSceneData() sets up the parent hierarchy correctly", "[world][scene]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kThreeNodeSceneSource);
  World world = fromValidatedSceneData(scene);

  const std::vector<EntityId> renderables = world.renderableEntities();
  REQUIRE(renderables.size() == 2);
  const EntityId node0Id = renderables[0];
  const EntityId node1Id = renderables[1];

  // node1's own parent is exactly node0 -- the real hierarchy edge
  // D9's own pass 2 established, checked via getParent(), not merely
  // via the scene's own declared parent index.
  const auto parentOfNode1 = world.getParent(node1Id);
  REQUIRE(parentOfNode1.isOk());
  CHECK(parentOfNode1.value() == node0Id);

  // node0 has no parent -- getParent() on a root entity is Ok(kInvalidEntityId),
  // not Err (matching hierarchy_tests.cpp's own established convention).
  const auto parentOfNode0 = world.getParent(node0Id);
  REQUIRE(parentOfNode0.isOk());
  CHECK(parentOfNode0.value() == kInvalidEntityId);

  // node2 (the active camera) also has no parent.
  REQUIRE(world.activeCamera().has_value());
  const auto parentOfNode2 = world.getParent(*world.activeCamera());
  REQUIRE(parentOfNode2.isOk());
  CHECK(parentOfNode2.value() == kInvalidEntityId);
}

TEST_CASE("fromValidatedSceneData() produces deterministic, repeatable EntityId sequences", "[world][scene]") {
  // Two independently cooked-and-decoded ValidatedSceneData instances
  // from the identical source text -- cookScene()'s own determinism
  // (V12) plus decodeScene()'s own purity together guarantee these are
  // two genuinely independent instances, not the same one reused.
  const ValidatedSceneData sceneA = cookAndDecodeScene(kThreeNodeSceneSource);
  const ValidatedSceneData sceneB = cookAndDecodeScene(kThreeNodeSceneSource);

  World worldA = fromValidatedSceneData(sceneA);
  World worldB = fromValidatedSceneData(sceneB);

  const std::vector<EntityId> renderablesA = worldA.renderableEntities();
  const std::vector<EntityId> renderablesB = worldB.renderableEntities();
  REQUIRE(renderablesA.size() == 2);
  REQUIRE(renderablesB.size() == 2);
  for (std::size_t i = 0; i < 2; ++i) {
    CHECK(renderablesA[i].index() == renderablesB[i].index());
    CHECK(renderablesA[i].generation() == renderablesB[i].generation());
  }

  REQUIRE(worldA.activeCamera().has_value());
  REQUIRE(worldB.activeCamera().has_value());
  CHECK(worldA.activeCamera()->index() == worldB.activeCamera()->index());
  CHECK(worldA.activeCamera()->generation() == worldB.activeCamera()->generation());
}

TEST_CASE("fromValidatedSceneData() leaves an empty active camera when the scene declares none", "[world][scene]") {
  constexpr const char* kPlainSceneSource =
      "atlantis_scene_source_version: 1\n"
      "node_count: 1\n"
      "active_camera: none\n"
      "node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n";
  const ValidatedSceneData scene = cookAndDecodeScene(kPlainSceneSource);
  World world = fromValidatedSceneData(scene);
  CHECK_FALSE(world.activeCamera().has_value());
}
