#include <atlantis/world/scene_instantiation.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/world_matrix.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>

// Plan 0051 M2 (Spec 0051 R2-R5, rulings Q1, Q2, Q5): bakeScene() against an
// independently instantiated world::World -- one entity per node in node
// order, the authored components, world matrices bit-equal to
// updateTransforms(), the active camera, and no hierarchy in the product.
// Scenes are real source text, cooked and decoded (the
// scene_instantiation_tests.cpp precedent; its helpers are duplicated here).

namespace {

namespace fs = std::filesystem;
namespace ecs = atlantis::world::ecs;
using atlantis::asset_system::ValidatedSceneData;
using atlantis::world::BakedScene;
using atlantis::world::bakeScene;
using atlantis::world::Camera;
using atlantis::world::instantiateScene;
using atlantis::world::Light;
using atlantis::world::Renderable;
using atlantis::world::SceneInstance;
using atlantis::world::Transform;
using atlantis::world::WorldMatrix;

const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

[[nodiscard]] atlantis::asset_system::AssetGuid testAssetGuid(std::string_view key) {
  return atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00510051-0051-4051-8051-005100510051").value(), key);
}

void writeFile(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

[[nodiscard]] ValidatedSceneData cookAndDecodeScene(const std::string& sourceText) {
  const fs::path dir = fs::temp_directory_path() / "atlantis_scene_bake_tests" /
                        ("fixture_" + gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)));
  fs::create_directories(dir);
  const fs::path sourcePath = dir / "scene.scene.txt";
  const fs::path artifactPath = dir / "scene.ascene";
  const fs::path metadataPath = dir / "scene.ascene.meta.txt";
  writeFile(sourcePath, sourceText);
  auto cookResult = atlantis::asset_system::cookScene(sourcePath.string(), testAssetGuid("scene"),
                                                      artifactPath.string(), metadataPath.string());
  REQUIRE(cookResult.isOk());
  auto decodeResult = atlantis::asset_system::decodeScene(artifactPath.string(), metadataPath.string());
  REQUIRE(decodeResult.isOk());
  std::error_code ec;
  fs::remove_all(dir, ec);
  return decodeResult.value();
}

// A five-level hierarchy (3 -> 1 -> 2 -> 4 -> 6) with rotation at every level
// and non-uniform scale at three of them. Node 1 is declared before its
// parent, node 3, so the bake cannot rely on parents preceding children.
// Node 5 is an unparented Directional light.
constexpr const char* kHierarchySceneSource =
    "atlantis_scene_source_version: 7\n"
    "node_count: 6\n"
    "active_camera: 6\n"
    "node: node_id=1 guid=51051051-0001-4051-8051-000000000001 parent=3 position=0.5 -1.0 2.0 "
    "rotation=0.3 -0.2 0.7 scale=1.5 0.25 2.0 mesh=51051051-00aa-4051-8051-0000000000aa "
    "material=51051051-00bb-4051-8051-0000000000bb\n"
    "node: node_id=2 guid=51051051-0002-4051-8051-000000000002 parent=1 position=1.0 0.0 -0.5 "
    "rotation=0.0 0.4 0.0 scale=0.5 3.0 1.0 light=point color=1.0 0.5 0.25 intensity=2.0 range=4.0\n"
    "node: node_id=3 guid=51051051-0003-4051-8051-000000000003 parent=none position=-2.0 1.0 0.0 "
    "rotation=0.2 0.9 -0.1 scale=2.0 0.5 1.5\n"
    "node: node_id=4 guid=51051051-0004-4051-8051-000000000004 parent=2 position=0.0 1.0 0.0 "
    "rotation=-0.6 0.0 0.3 scale=1.0 1.0 1.0 mesh=51051051-00cc-4051-8051-0000000000cc\n"
    "node: node_id=5 guid=51051051-0005-4051-8051-000000000005 parent=none position=0.0 5.0 0.0 "
    "rotation=0.5 -0.6 0.0 scale=1.0 1.0 1.0 light=directional color=0.6 0.7 1.0 intensity=1.2\n"
    "node: node_id=6 guid=51051051-0006-4051-8051-000000000006 parent=4 position=0.0 2.2 7.0 "
    "rotation=-0.3054 0.0 0.0 scale=1.0 1.0 1.0 camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0\n";

constexpr const char* kNoCameraSceneSource =
    "atlantis_scene_source_version: 7\n"
    "node_count: 1\n"
    "active_camera: none\n"
    "node: node_id=1 guid=51051051-0011-4051-8051-000000000011 parent=none position=1.0 2.0 3.0 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=51051051-00aa-4051-8051-0000000000aa\n";

// The reference: the instantiation the bake starts from, solved the way
// Runtime's frame solved it before Spec 0051.
[[nodiscard]] SceneInstance referenceWorld(const ValidatedSceneData& scene) {
  SceneInstance instance = instantiateScene(scene);
  instance.world.updateTransforms();
  return instance;
}

template <typename T>
[[nodiscard]] bool bytesEqual(const T& a, const T& b) {
  static_assert(std::is_trivially_copyable_v<T>);
  return std::memcmp(&a, &b, sizeof(T)) == 0;
}

}  // namespace

static_assert(std::is_nothrow_move_constructible_v<BakedScene>);
static_assert(!std::is_copy_constructible_v<BakedScene>);
static_assert(std::is_same_v<decltype(bakeScene(std::declval<const ValidatedSceneData&>())), BakedScene>);

TEST_CASE("bakeScene: one entity per node, in node order, bound to each node's EntityGuid", "[world][bake]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kHierarchySceneSource);
  const BakedScene baked = bakeScene(scene);
  REQUIRE(baked.entities.size() == scene.nodeCount());
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    INFO("node " << i);
    const auto entity = baked.entities.find(scene.entityGuid(i));
    REQUIRE(entity.has_value());
    CHECK(baked.world.isValid(*entity));
    CHECK(entity->index() == i);
  }
}

TEST_CASE("bakeScene: each entity carries exactly its node's authored components", "[world][bake]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kHierarchySceneSource);
  const SceneInstance reference = referenceWorld(scene);
  const BakedScene baked = bakeScene(scene);
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    INFO("node " << i);
    const auto source = *reference.entities.find(scene.entityGuid(i));
    const auto entity = *baked.entities.find(scene.entityGuid(i));

    const auto transform = baked.world.get<Transform>(entity);
    REQUIRE(transform.isOk());
    CHECK(bytesEqual(transform.value(), reference.world.getLocalTransform(source).value()));

    const auto camera = reference.world.getCamera(source);
    REQUIRE(baked.world.has<Camera>(entity).value() == camera.isOk());
    if (camera.isOk()) CHECK(bytesEqual(baked.world.get<Camera>(entity).value(), camera.value()));

    const auto light = reference.world.getLight(source);
    REQUIRE(baked.world.has<Light>(entity).value() == light.isOk());
    if (light.isOk()) CHECK(bytesEqual(baked.world.get<Light>(entity).value(), light.value()));

    const auto renderable = reference.world.getRenderable(source);
    REQUIRE(baked.world.has<Renderable>(entity).value() == renderable.isOk());
    if (renderable.isOk()) {
      const Renderable value = baked.world.get<Renderable>(entity).value();
      CHECK(value.meshAsset == renderable.value().meshAsset);
      CHECK(value.materialAsset == renderable.value().materialAsset);
    }
  }
}

TEST_CASE("bakeScene: world matrices are bit-equal to updateTransforms() through a multi-level, non-uniformly "
          "scaled hierarchy",
          "[world][bake]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kHierarchySceneSource);
  const SceneInstance reference = referenceWorld(scene);
  const BakedScene baked = bakeScene(scene);
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    INFO("node " << i);
    const auto source = *reference.entities.find(scene.entityGuid(i));
    const auto entity = *baked.entities.find(scene.entityGuid(i));
    const auto expected = reference.world.getWorldMatrix(source);
    REQUIRE(expected.isOk());
    const auto actual = baked.world.get<WorldMatrix>(entity);
    REQUIRE(actual.isOk());
    CHECK(bytesEqual(atlantis::world::toColumnMajor(actual.value()), expected.value()));
  }

  // Not vacuous: the deepest node's world translation differs from its own
  // local position, so the parents' transforms were composed in.
  const auto deepest = *baked.entities.find(scene.entityGuid(5));
  const WorldMatrix m = baked.world.get<WorldMatrix>(deepest).value();
  const Transform local = baked.world.get<Transform>(deepest).value();
  CHECK((m.column3[0] != local.localPosition.x || m.column3[1] != local.localPosition.y ||
         m.column3[2] != local.localPosition.z));
}

TEST_CASE("bakeScene: the active camera is the active camera node's entity", "[world][bake]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kHierarchySceneSource);
  const BakedScene baked = bakeScene(scene);
  REQUIRE(scene.activeCameraIndex().has_value());
  REQUIRE(baked.activeCamera.has_value());
  CHECK(*baked.activeCamera == *baked.entities.find(scene.entityGuid(*scene.activeCameraIndex())));
  CHECK(baked.world.has<Camera>(*baked.activeCamera).value());
}

TEST_CASE("bakeScene: a scene without an active camera bakes with none", "[world][bake]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kNoCameraSceneSource);
  const BakedScene baked = bakeScene(scene);
  CHECK_FALSE(baked.activeCamera.has_value());
  CHECK(baked.entities.size() == 1);
}

TEST_CASE("bakeScene: the product has no hierarchy -- every entity is a Transform and a WorldMatrix plus its "
          "authored component",
          "[world][bake]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kHierarchySceneSource);
  BakedScene baked = bakeScene(scene);
  std::size_t visited = 0;
  baked.world.query<const Transform, const WorldMatrix>(
      [&](ecs::EntityId, const Transform&, const WorldMatrix&) { ++visited; });
  CHECK(visited == scene.nodeCount());
  // The only component types the bake writes are the five World components;
  // a node with none of Camera/Light/Renderable (node 3) has exactly two.
  const auto bare = *baked.entities.find(scene.entityGuid(2));
  CHECK_FALSE(baked.world.has<Camera>(bare).value());
  CHECK_FALSE(baked.world.has<Light>(bare).value());
  CHECK_FALSE(baked.world.has<Renderable>(bare).value());
}
