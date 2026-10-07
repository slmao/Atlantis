#include <atlantis/world/access/runtime_world_access.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/scene_semantic_schema.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Plan 0054 M1 (Spec 0054 R5; ruling Q1; ADR-0105 D4): listEntities() --
// every live entity the boundary addresses, ordered by GUID value, following
// the commands applied; an ECS entity without a GUID is not listed.

namespace {

constexpr std::string_view kTestTag = "world_access_listing_tests";

namespace fs = std::filesystem;
namespace access = atlantis::world::access;
namespace ecs = atlantis::world::ecs;
using atlantis::asset_system::EntityGuid;
using atlantis::asset_system::ValidatedSceneData;

const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

[[nodiscard]] ValidatedSceneData cookAndDecodeScene(const std::string& sourceText) {
  const fs::path dir = fs::temp_directory_path() / ("atlantis_" + std::string(kTestTag)) /
                        ("fixture_" + gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)));
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "scene.scene.txt", std::ios::binary | std::ios::trunc);
    out << sourceText;
  }
  const auto guid = atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00520052-0052-4052-8052-005200520052").value(), "scene");
  REQUIRE(atlantis::asset_system::cookScene((dir / "scene.scene.txt").string(), guid,
                                            (dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string())
              .isOk());
  auto decoded =
      atlantis::asset_system::decodeScene((dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string());
  REQUIRE(decoded.isOk());
  std::error_code ec;
  fs::remove_all(dir, ec);
  return decoded.value();
}

// One node per component kind: a material renderable, a Directional and a
// Point light, the active camera, and a bare node.
constexpr const char* kSceneSource =
    "atlantis_scene_source_version: 7\n"
    "node_count: 5\n"
    "active_camera: 4\n"
    "node: node_id=1 guid=52052052-0001-4052-8052-000000000001 parent=none position=1.0 2.0 3.0 "
    "rotation=0.1 0.2 0.3 scale=1.0 2.0 1.0 mesh=52052052-00aa-4052-8052-0000000000aa "
    "material=52052052-00bb-4052-8052-0000000000bb\n"
    "node: node_id=2 guid=52052052-0002-4052-8052-000000000002 parent=none position=0.0 5.0 0.0 "
    "rotation=0.5 -0.6 0.0 scale=1.0 1.0 1.0 light=directional color=0.6 0.7 1.0 intensity=1.2\n"
    "node: node_id=3 guid=52052052-0003-4052-8052-000000000003 parent=1 position=0.8 0.3 0.5 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 light=point color=1.0 0.6 0.3 intensity=3.0 range=2.5\n"
    "node: node_id=4 guid=52052052-0004-4052-8052-000000000004 parent=none position=0.0 2.2 7.0 "
    "rotation=-0.3054 0.0 0.0 scale=1.0 1.0 1.0 camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0\n"
    "node: node_id=5 guid=52052052-0005-4052-8052-000000000005 parent=none position=0.0 0.0 0.0 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n";

[[maybe_unused]] constexpr std::size_t kRenderableNode = 0;
[[maybe_unused]] constexpr std::size_t kDirectionalNode = 1;
[[maybe_unused]] constexpr std::size_t kPointNode = 2;
[[maybe_unused]] constexpr std::size_t kCameraNode = 3;
[[maybe_unused]] constexpr std::size_t kBareNode = 4;

[[maybe_unused]] [[nodiscard]] EntityGuid newGuid(std::uint32_t n) {
  EntityGuid guid;
  guid.bytes[0] = std::byte{0x52};
  guid.bytes[12] = static_cast<std::byte>((n >> 24) & 0xff);
  guid.bytes[13] = static_cast<std::byte>((n >> 16) & 0xff);
  guid.bytes[14] = static_cast<std::byte>((n >> 8) & 0xff);
  guid.bytes[15] = static_cast<std::byte>(n & 0xff);
  return guid;
}

template <typename T>
[[nodiscard]] atlantis::schema::TypeId typeOf() {
  return ecs::componentTypeId<T>();
}

[[maybe_unused]] [[nodiscard]] atlantis::schema::FieldId field(std::string_view owner, std::string_view name) {
  return atlantis::schema::fieldId(owner, name);
}

[[maybe_unused]] [[nodiscard]] access::AccessError refusal(access::RuntimeWorldAccess& boundary, access::Command command) {
  const access::CommandTicket ticket = boundary.submit(std::move(command));
  const auto report = boundary.applyPending();
  REQUIRE(report.failures.size() == 1);
  CHECK(report.failures[0].ticket == ticket);
  CHECK(report.applied == 0);
  return report.failures[0].error;
}

[[maybe_unused]] [[nodiscard]] access::SetProperty set(const EntityGuid& entity, atlantis::schema::TypeId component,
                                      atlantis::schema::FieldId fieldId, access::PropertyValue value) {
  return access::SetProperty{{entity, component, fieldId}, std::move(value)};
}

// A Point light with a WorldMatrix -- one extraction counts -- built through
// the boundary in the order Correction J1 allows.
[[maybe_unused]] void addPointLight(access::RuntimeWorldAccess& boundary, const EntityGuid& g) {
  using namespace atlantis::world;
  boundary.submit(access::CreateEntity{g});
  boundary.submit(access::AddComponent{g, typeOf<Light>()});
  boundary.submit(set(g, typeOf<Light>(), field("world::Light", "kind"), access::EnumValue{1}));
  boundary.submit(access::AddComponent{g, typeOf<WorldMatrix>()});
  const auto report = boundary.applyPending();
  REQUIRE(report.failures.empty());
}

}  // namespace

TEST_CASE("world access listing: the baked entities, in GUID order, exactly those findEntity() accepts",
          "[world][access][listing]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);

  std::vector<EntityGuid> expected;
  for (std::size_t node = 0; node < 5; ++node) expected.push_back(scene.entityGuid(node));
  std::sort(expected.begin(), expected.end());

  const std::vector<EntityGuid> listed = boundary.listEntities();
  CHECK(listed == expected);
  for (const EntityGuid& guid : listed) CHECK(boundary.findEntity(guid));
  // GUID value order is also the order of the canonical text (RFC 9562,
  // lowercase hex in byte order), which is what a client prints.
  for (std::size_t i = 1; i < listed.size(); ++i) {
    CHECK(atlantis::asset_system::toString(listed[i - 1]) < atlantis::asset_system::toString(listed[i]));
  }
}

TEST_CASE("world access listing: follows created, destroyed and re-created entities once applied",
          "[world][access][listing]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  const std::vector<EntityGuid> before = boundary.listEntities();
  const EntityGuid created = newGuid(7);
  const EntityGuid bare = scene.entityGuid(kBareNode);

  boundary.submit(access::CreateEntity{created});
  boundary.submit(access::DestroyEntity{bare});
  CHECK(boundary.listEntities() == before);  // nothing changes until applied
  REQUIRE(boundary.applyPending().failures.empty());

  std::vector<EntityGuid> expected = before;
  expected.erase(std::find(expected.begin(), expected.end(), bare));
  expected.insert(std::lower_bound(expected.begin(), expected.end(), created), created);
  CHECK(boundary.listEntities() == expected);

  // A destroyed GUID re-created names the new entity and is listed again.
  boundary.submit(access::CreateEntity{bare});
  REQUIRE(boundary.applyPending().failures.empty());
  expected.insert(std::lower_bound(expected.begin(), expected.end(), bare), bare);
  CHECK(boundary.listEntities() == expected);
}

TEST_CASE("world access listing: only GUID-addressable live entities are listed", "[world][access][listing]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  const std::vector<EntityGuid> before = boundary.listEntities();

  // An entity created directly on the ECS has no GUID: not addressable, not listed.
  const ecs::EntityId anonymous = baked.world.createEntity();
  REQUIRE(baked.world.isValid(anonymous));
  CHECK(boundary.listEntities() == before);

  // An entity destroyed directly on the ECS is no longer live: not listed,
  // exactly as findEntity() no longer accepts it.
  const EntityGuid bare = scene.entityGuid(kBareNode);
  REQUIRE(baked.world.destroyEntity(*baked.entities.find(bare)).isOk());
  CHECK_FALSE(boundary.findEntity(bare));
  std::vector<EntityGuid> expected = before;
  expected.erase(std::find(expected.begin(), expected.end(), bare));
  CHECK(boundary.listEntities() == expected);
}
