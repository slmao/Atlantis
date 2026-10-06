#include <atlantis/world/access/runtime_world_access.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Plan 0052 M3 (Spec 0052 R4, R5, R6; rulings Q1 a1, Q4, Q9; J4, J5): the five
// commands, deferred until the owner applies them, applied one by one in
// submission order; one event per success and a ticketed failure per refusal;
// no event for an edit made directly on the ECS.

namespace {

constexpr std::string_view kTestTag = "world_access_command_tests";

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

[[nodiscard]] access::SetProperty set(const EntityGuid& entity, atlantis::schema::TypeId component,
                                      atlantis::schema::FieldId fieldId, access::PropertyValue value) {
  return access::SetProperty{{entity, component, fieldId}, std::move(value)};
}

}  // namespace

TEST_CASE("world access commands: nothing changes until the owner applies, then each command takes effect",
          "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  const EntityGuid g = newGuid(1);

  const access::CommandTicket first = boundary.submit(access::CreateEntity{g});
  CHECK(first.value == 1);
  CHECK_FALSE(boundary.findEntity(g));  // deferred: submit() only records
  CHECK(boundary.drainEvents().empty());

  auto report = boundary.applyPending();
  CHECK(report.applied == 1);
  CHECK(report.failures.empty());
  CHECK(boundary.findEntity(g));
  CHECK(boundary.listComponents(g).value().empty());

  CHECK(boundary.submit(access::AddComponent{g, typeOf<atlantis::world::Transform>()}).value == 2);
  boundary.submit(set(g, typeOf<atlantis::world::Transform>(), field("world::Transform", "localPosition"),
                      std::array<float, 3>{4.0f, 5.0f, 6.0f}));
  report = boundary.applyPending();
  CHECK(report.applied == 2);
  CHECK(boundary.getProperty({g, typeOf<atlantis::world::Transform>(), field("world::Transform", "localPosition")})
            .value() == access::PropertyValue{std::array<float, 3>{4.0f, 5.0f, 6.0f}});

  boundary.submit(access::RemoveComponent{g, typeOf<atlantis::world::Transform>()});
  boundary.submit(access::DestroyEntity{g});
  report = boundary.applyPending();
  CHECK(report.applied == 2);
  CHECK_FALSE(boundary.findEntity(g));

  const std::vector<access::Event> events = boundary.drainEvents();
  REQUIRE(events.size() == 5);
  CHECK(events[0] == access::Event{access::EntityCreated{g}});
  CHECK(events[1] == access::Event{access::ComponentAdded{g, typeOf<atlantis::world::Transform>()}});
  CHECK(std::holds_alternative<access::PropertyChanged>(events[2]));
  CHECK(events[3] == access::Event{access::ComponentRemoved{g, typeOf<atlantis::world::Transform>()}});
  CHECK(events[4] == access::Event{access::EntityDestroyed{g}});
  CHECK(boundary.drainEvents().empty());  // draining empties the queue
  CHECK(boundary.drainFailures().empty());
}

TEST_CASE("world access commands: a batch addresses the entity it creates (ruling Q1 a1), in the order J1 allows",
          "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;
  const EntityGuid g = newGuid(2);

  boundary.submit(access::CreateEntity{g});
  boundary.submit(access::AddComponent{g, typeOf<Light>()});  // Light{} is Directional; no WorldMatrix yet (J1)
  boundary.submit(set(g, typeOf<Light>(), field("world::Light", "kind"), access::EnumValue{1}));  // Point
  boundary.submit(set(g, typeOf<Light>(), field("world::Light", "range"), 4.0f));
  boundary.submit(access::AddComponent{g, typeOf<WorldMatrix>()});
  boundary.submit(set(g, typeOf<WorldMatrix>(), field("world::WorldMatrix", "column3"),
                      std::array<float, 4>{1.0f, 2.0f, 3.0f, 1.0f}));
  const auto report = boundary.applyPending();
  CHECK(report.applied == 6);
  CHECK(report.failures.empty());
  CHECK(boundary.drainEvents().size() == 6);

  const ecs::EntityId id = [&] {
    ecs::EntityId found;
    baked.world.query<const Light>([&](ecs::EntityId candidate, const Light& light) {
      if (light.range == 4.0f) found = candidate;
    });
    return found;
  }();
  REQUIRE(baked.world.isValid(id));
  CHECK(baked.world.get<Light>(id).value().kind == LightKind::Point);
  CHECK(baked.world.get<WorldMatrix>(id).value().column3 == std::array<float, 4>{1.0f, 2.0f, 3.0f, 1.0f});
}

TEST_CASE("world access commands: two field edits of one component in one batch both survive", "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;
  const EntityGuid light = scene.entityGuid(kPointNode);

  boundary.submit(set(light, typeOf<Light>(), field("world::Light", "intensity"), 9.0f));
  boundary.submit(set(light, typeOf<Light>(), field("world::Light", "range"), 6.0f));
  CHECK(boundary.applyPending().applied == 2);
  const Light value = baked.world.get<Light>(*baked.entities.find(light)).value();
  CHECK(value.intensity == 9.0f);
  CHECK(value.range == 6.0f);
}

TEST_CASE("world access commands: a refused command has no effect and is reported by ticket; the batch continues",
          "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;
  const EntityGuid light = scene.entityGuid(kPointNode);
  const EntityGuid g = newGuid(3);

  boundary.submit(set(light, typeOf<Light>(), field("world::Light", "intensity"), 2.0f));
  const auto refused = boundary.submit(set(light, typeOf<Light>(), field("world::Light", "intensity"), std::uint64_t{7}));
  boundary.submit(access::CreateEntity{g});
  boundary.submit(access::DestroyEntity{g});
  const auto afterDestroy = boundary.submit(access::AddComponent{g, typeOf<Transform>()});
  boundary.submit(set(light, typeOf<Light>(), field("world::Light", "range"), 3.0f));

  const auto report = boundary.applyPending();
  CHECK(report.applied == 4);
  REQUIRE(report.failures.size() == 2);
  CHECK(report.failures[0] == access::CommandFailure{refused, access::AccessError::KindMismatch});
  CHECK(report.failures[1] == access::CommandFailure{afterDestroy, access::AccessError::UnknownEntity});
  CHECK(boundary.drainFailures() == report.failures);
  CHECK(boundary.drainFailures().empty());

  const Light value = baked.world.get<Light>(*baked.entities.find(light)).value();
  CHECK(value.intensity == 2.0f);  // the refused write had no effect
  CHECK(value.range == 3.0f);      // later commands still applied

  const auto events = boundary.drainEvents();
  REQUIRE(events.size() == 4);  // one per success, none per failure
  CHECK(std::holds_alternative<access::PropertyChanged>(events[0]));
  CHECK(events[1] == access::Event{access::EntityCreated{g}});
  CHECK(events[2] == access::Event{access::EntityDestroyed{g}});
  CHECK(std::get<access::PropertyChanged>(events[3]).value == access::PropertyValue{3.0f});
}

TEST_CASE("world access commands: a destroyed GUID may be reused for a new entity", "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  const EntityGuid bare = scene.entityGuid(kBareNode);
  boundary.submit(access::DestroyEntity{bare});
  boundary.submit(access::CreateEntity{bare});
  CHECK(boundary.applyPending().applied == 2);
  CHECK(boundary.findEntity(bare));
  CHECK(boundary.listComponents(bare).value().empty());  // a new entity, not the old one
}

TEST_CASE("world access events: an edit made directly on the ECS produces no event (not reactive)",
          "[world][access]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  using namespace atlantis::world;
  const ecs::EntityId light = *baked.entities.find(scene.entityGuid(kPointNode));
  Light value = baked.world.get<Light>(light).value();
  value.intensity = 11.0f;
  REQUIRE(baked.world.set(light, value).isOk());
  REQUIRE(baked.world.add<Camera>(baked.world.createEntity()).isOk());
  CHECK(boundary.applyPending().applied == 0);
  CHECK(boundary.drainEvents().empty());
  // The boundary still reads the world as it is.
  CHECK(boundary.getProperty({scene.entityGuid(kPointNode), typeOf<Light>(), field("world::Light", "intensity")})
            .value() == access::PropertyValue{11.0f});
}
