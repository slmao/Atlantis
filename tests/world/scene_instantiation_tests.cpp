#include <atlantis/world/scene_instantiation.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <type_traits>
#include <vector>

using atlantis::asset_system::DecodedCamera;
using atlantis::asset_system::DecodedRenderable;
using atlantis::asset_system::ValidatedSceneData;
using atlantis::asset_system::ValidatedSceneNode;
using atlantis::world::Camera;
using atlantis::world::EntityId;
using atlantis::world::fromValidatedSceneData;
using atlantis::world::kInvalidEntityId;
using atlantis::world::Renderable;
using atlantis::world::Transform;
using atlantis::world::World;

// Plan 0015 Section D9 (V21): the one narrowly-scoped, Plan-pre-
// authorized friend this test translation unit needs -- matching
// World's own EntityLifecycleTestAccess precedent
// (tests/world/entity_lifecycle_tests.cpp) exactly. Declared inside
// atlantis::asset_system to match ValidatedSceneData's own unqualified
// `friend struct ValidatedSceneDataTestAccess;`.
namespace atlantis::asset_system {
struct ValidatedSceneDataTestAccess {
  static ValidatedSceneData make(std::vector<ValidatedSceneNode> nodes,
                                  std::vector<std::optional<std::size_t>> parents,
                                  std::optional<std::size_t> activeCameraIndex) {
    return ValidatedSceneData(std::move(nodes), std::move(parents), activeCameraIndex);
  }
};
}  // namespace atlantis::asset_system

using atlantis::asset_system::ValidatedSceneDataTestAccess;

namespace {

constexpr atlantis::asset_system::AssetId kNode0MeshAsset = 0x0102030405060708ULL;
constexpr atlantis::asset_system::AssetId kNode1MeshAsset = 0xAABBCCDDEEFF0011ULL;

// Three nodes: node 0 a Renderable with no parent; node 1 a Renderable
// child of node 0 (its own, distinct meshAsset makes it independently
// identifiable via renderableEntities(), not just reachable in
// principle); node 2 a Camera, no parent, the scene's own active
// camera.
[[nodiscard]] ValidatedSceneData makeThreeNodeScene() {
  ValidatedSceneNode node0;
  node0.transform = {1.0f, 2.0f, 3.0f, 0.1f, 0.2f, 0.3f, 1.0f, 1.0f, 1.0f};
  node0.renderable = DecodedRenderable{kNode0MeshAsset};

  ValidatedSceneNode node1;
  node1.transform = {4.0f, 5.0f, 6.0f, 0.0f, 0.0f, 0.0f, 2.0f, 2.0f, 2.0f};
  node1.renderable = DecodedRenderable{kNode1MeshAsset};

  ValidatedSceneNode node2;
  node2.transform = {0.0f, 2.2f, 7.0f, -0.3054f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
  node2.camera = DecodedCamera{1.0472f, 0.1f, 100.0f};

  return ValidatedSceneDataTestAccess::make({node0, node1, node2}, {std::nullopt, std::size_t{0}, std::nullopt},
                                             std::size_t{2});
}

}  // namespace

static_assert(std::is_same_v<decltype(fromValidatedSceneData(std::declval<const ValidatedSceneData&>())), World>);

TEST_CASE("fromValidatedSceneData() instantiates every node with its own component data", "[world][scene]") {
  const ValidatedSceneData scene = makeThreeNodeScene();
  World world = fromValidatedSceneData(scene);

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
  CHECK(renderableNode0.value().meshAsset == kNode0MeshAsset);

  const auto transformNode1 = world.getLocalTransform(node1Id);
  REQUIRE(transformNode1.isOk());
  CHECK(transformNode1.value().localPosition.x == 4.0f);
  CHECK(transformNode1.value().localScale.x == 2.0f);

  const auto renderableNode1 = world.getRenderable(node1Id);
  REQUIRE(renderableNode1.isOk());
  CHECK(renderableNode1.value().meshAsset == kNode1MeshAsset);
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
  const ValidatedSceneData scene = makeThreeNodeScene();
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
  const ValidatedSceneData sceneA = makeThreeNodeScene();
  const ValidatedSceneData sceneB = makeThreeNodeScene();

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
  ValidatedSceneNode node0;
  node0.transform = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};

  const ValidatedSceneData scene = ValidatedSceneDataTestAccess::make({node0}, {std::nullopt}, std::nullopt);
  World world = fromValidatedSceneData(scene);
  CHECK_FALSE(world.activeCamera().has_value());
}
