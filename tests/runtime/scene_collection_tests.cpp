#include <atlantis/runtime/scene_extraction.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/world_matrix.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

// Plan 0051 M3 (Spec 0051 R6, R9; rulings Q3 D3, Q6): the shared collection
// functions over a baked scene -- node order regardless of archetype layout,
// the camera-less case, and Spec 0022's surviving live-edit contract at the
// CPU level (component values, world matrices, entity creation and removal
// are seen by the next collection; Spec 0022 Correction 2026-10-06).

namespace {

// A component no scene carries, used only to split entities across
// archetypes so the raw query order stops being node order.
struct OrderProbe {
  std::uint32_t value = 0;
};

}  // namespace

template <>
struct atlantis::world::ecs::ComponentType<OrderProbe> {
  static constexpr std::string_view kName = "test::OrderProbe";
};

namespace {

namespace fs = std::filesystem;
namespace ecs = atlantis::world::ecs;
using atlantis::asset_system::AssetId;
using atlantis::asset_system::ValidatedSceneData;
using atlantis::world::BakedScene;
using atlantis::world::Light;
using atlantis::world::LightKind;
using atlantis::world::Renderable;
using atlantis::world::WorldMatrix;
using namespace atlantis::runtime;

const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

[[nodiscard]] ValidatedSceneData cookAndDecodeScene(const std::string& sourceText) {
  const fs::path dir = fs::temp_directory_path() / "atlantis_scene_collection_tests" /
                        ("fixture_" + gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)));
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "scene.scene.txt", std::ios::binary | std::ios::trunc);
    out << sourceText;
  }
  const auto guid = atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00510051-0051-4051-8051-005100510052").value(), "scene");
  auto cookResult = atlantis::asset_system::cookScene((dir / "scene.scene.txt").string(), guid,
                                                      (dir / "scene.ascene").string(),
                                                      (dir / "scene.ascene.meta.txt").string());
  REQUIRE(cookResult.isOk());
  auto decodeResult =
      atlantis::asset_system::decodeScene((dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string());
  REQUIRE(decodeResult.isOk());
  std::error_code ec;
  fs::remove_all(dir, ec);
  return decodeResult.value();
}

// Renderables and Point lights alternate in node order; node 7 is the camera.
constexpr const char* kAlternatingSceneSource =
    "atlantis_scene_source_version: 7\n"
    "node_count: 7\n"
    "active_camera: 7\n"
    "node: node_id=1 guid=51051051-0101-4051-8051-000000000101 parent=none position=0.0 0.0 0.0 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=51051051-00aa-4051-8051-0000000000aa "
    "material=51051051-00b1-4051-8051-0000000000b1\n"
    "node: node_id=2 guid=51051051-0102-4051-8051-000000000102 parent=none position=1.0 0.0 0.0 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 light=point color=1.0 0.0 0.0 intensity=1.0 range=2.0\n"
    "node: node_id=3 guid=51051051-0103-4051-8051-000000000103 parent=none position=2.0 0.0 0.0 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=51051051-00aa-4051-8051-0000000000aa "
    "material=51051051-00b2-4051-8051-0000000000b2\n"
    "node: node_id=4 guid=51051051-0104-4051-8051-000000000104 parent=none position=3.0 0.0 0.0 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 light=point color=0.0 1.0 0.0 intensity=2.0 range=3.0\n"
    "node: node_id=5 guid=51051051-0105-4051-8051-000000000105 parent=none position=4.0 0.0 0.0 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=51051051-00ab-4051-8051-0000000000ab\n"
    "node: node_id=6 guid=51051051-0106-4051-8051-000000000106 parent=none position=5.0 0.0 0.0 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 light=point color=0.0 0.0 1.0 intensity=3.0 range=4.0\n"
    "node: node_id=7 guid=51051051-0107-4051-8051-000000000107 parent=none position=0.0 2.2 7.0 "
    "rotation=-0.3054 0.0 0.0 scale=1.0 1.0 1.0 camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0\n";

[[nodiscard]] ecs::EntityId entityOfNode(const BakedScene& baked, const ValidatedSceneData& scene, std::size_t node) {
  const auto entity = baked.entities.find(scene.entityGuid(node));
  REQUIRE(entity.has_value());
  return *entity;
}

[[nodiscard]] std::vector<std::uint32_t> rawQueryOrder(BakedScene& baked) {
  std::vector<std::uint32_t> order;
  baked.world.query<const Renderable>([&](ecs::EntityId id, const Renderable&) { order.push_back(id.index()); });
  return order;
}

}  // namespace

TEST_CASE("collectRenderables/collectLights: node order holds when entities span archetypes", "[runtime][bake]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kAlternatingSceneSource);
  BakedScene baked = atlantis::world::bakeScene(scene);
  const std::vector<RenderableExtractionInput> before = collectRenderables(baked);
  const std::vector<LightExtractionInput> lightsBefore = collectLights(baked);
  REQUIRE(rawQueryOrder(baked) == std::vector<std::uint32_t>{0, 2, 4});

  // Move the first renderable and the first light into archetypes created
  // after their peers'.
  REQUIRE(baked.world.add<OrderProbe>(entityOfNode(baked, scene, 0)).isOk());
  REQUIRE(baked.world.add<OrderProbe>(entityOfNode(baked, scene, 1)).isOk());

  // Not vacuous: the raw query order is no longer node order.
  REQUIRE(rawQueryOrder(baked) != std::vector<std::uint32_t>{0, 2, 4});

  const std::vector<RenderableExtractionInput> after = collectRenderables(baked);
  REQUIRE(after.size() == 3);
  for (std::size_t i = 0; i < after.size(); ++i) {
    INFO("renderable " << i);
    CHECK(after[i].renderable.meshAsset == before[i].renderable.meshAsset);
    CHECK(after[i].renderable.materialAsset == before[i].renderable.materialAsset);
    CHECK(after[i].worldMatrix == before[i].worldMatrix);
  }
  CHECK(after[0].worldMatrix[12] == 0.0f);  // node 1, then node 3, then node 5
  CHECK(after[1].worldMatrix[12] == 2.0f);
  CHECK(after[2].worldMatrix[12] == 4.0f);

  const std::vector<LightExtractionInput> lightsAfter = collectLights(baked);
  REQUIRE(lightsAfter.size() == 3);
  for (std::size_t i = 0; i < lightsAfter.size(); ++i) {
    INFO("light " << i);
    CHECK(lightsAfter[i].light.intensity == lightsBefore[i].light.intensity);
    CHECK(lightsAfter[i].worldMatrix == lightsBefore[i].worldMatrix);
  }
  CHECK(lightsAfter[0].light.intensity == 1.0f);
  CHECK(lightsAfter[2].light.intensity == 3.0f);
}

TEST_CASE("collectReferencedMaterialIds: first-reference order, absent materials skipped, no repeats",
          "[runtime][bake]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kAlternatingSceneSource);
  BakedScene baked = atlantis::world::bakeScene(scene);
  std::vector<RenderableExtractionInput> renderables = collectRenderables(baked);
  REQUIRE(renderables.size() == 3);
  renderables.push_back(renderables[0]);  // a repeat of the first material
  const std::vector<AssetId> ids = collectReferencedMaterialIds(renderables);
  REQUIRE(ids.size() == 2);
  CHECK(ids[0] == *renderables[0].renderable.materialAsset);
  CHECK(ids[1] == *renderables[1].renderable.materialAsset);
  CHECK_FALSE(renderables[2].renderable.materialAsset.has_value());
}

TEST_CASE("collectActiveCamera: the active camera's component and world matrix; none without one",
          "[runtime][bake]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kAlternatingSceneSource);
  BakedScene baked = atlantis::world::bakeScene(scene);
  const auto camera = collectActiveCamera(baked);
  REQUIRE(camera.has_value());
  CHECK(camera->worldMatrix[13] == 2.2f);
  CHECK(camera->camera.farZ == 100.0f);

  REQUIRE(baked.world.destroyEntity(*baked.activeCamera).isOk());
  CHECK_FALSE(collectActiveCamera(baked).has_value());
}

TEST_CASE("Spec 0022 surviving contract (CPU): Light, Camera and WorldMatrix edits and Light entity creation and "
          "removal are seen by the next collection",
          "[runtime][bake][dynamic]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kAlternatingSceneSource);
  BakedScene baked = atlantis::world::bakeScene(scene);
  const ecs::EntityId middleLight = entityOfNode(baked, scene, 3);

  // A Light component value.
  Light brighter = baked.world.get<Light>(middleLight).value();
  brighter.intensity = 7.5f;
  REQUIRE(baked.world.set(middleLight, brighter).isOk());
  CHECK(collectLights(baked)[1].light.intensity == 7.5f);

  // A world matrix.
  WorldMatrix moved = baked.world.get<WorldMatrix>(middleLight).value();
  moved.column3 = {-2.0f, 3.0f, 0.5f, 1.0f};
  REQUIRE(baked.world.set(middleLight, moved).isOk());
  const auto lightsAfterMove = collectLights(baked);
  CHECK(lightsAfterMove[1].worldMatrix[12] == -2.0f);
  CHECK(lightsAfterMove[1].worldMatrix[13] == 3.0f);
  CHECK(lightsAfterMove[1].worldMatrix[14] == 0.5f);

  // A Camera component value.
  atlantis::world::Camera camera = baked.world.get<atlantis::world::Camera>(*baked.activeCamera).value();
  camera.bloom.strength = 0.4f;
  REQUIRE(baked.world.set(*baked.activeCamera, camera).isOk());
  CHECK(collectActiveCamera(baked)->camera.bloom.strength == 0.4f);

  // Light entity creation: a new entity sorts after every scene entity.
  const ecs::EntityId added = baked.world.createEntity();
  Light point;
  point.kind = LightKind::Point;
  point.intensity = 9.0f;
  point.range = 1.0f;
  REQUIRE(baked.world.add(added, point).isOk());
  REQUIRE(baked.world.add(added, WorldMatrix{}).isOk());
  const auto lightsAfterAdd = collectLights(baked);
  REQUIRE(lightsAfterAdd.size() == 4);
  CHECK(lightsAfterAdd[3].light.intensity == 9.0f);

  // Light entity removal.
  REQUIRE(baked.world.destroyEntity(middleLight).isOk());
  const auto lightsAfterRemove = collectLights(baked);
  REQUIRE(lightsAfterRemove.size() == 3);
  CHECK(lightsAfterRemove[0].light.intensity == 1.0f);
  CHECK(lightsAfterRemove[1].light.intensity == 3.0f);
  CHECK(lightsAfterRemove[2].light.intensity == 9.0f);
}
