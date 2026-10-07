#include <atlantis/connection/in_process_endpoint.h>
#include <atlantis/connection/runtime_connection.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/world_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Plan 0054 M2 (Spec 0054 R1-R4; rulings Q5, Q7, Q8; ADR-0105 D1, D2, D6):
// a connection answers and applies exactly as the boundary it forwards to;
// several connections coexist -- each its own subscriptions, seeing events
// only from subscription time, failures routed back to their submitter by
// ticket, no connection draining another's queue.

namespace {

constexpr std::string_view kTestTag = "connection_tests";

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

namespace {

using atlantis::connection::ConnectionError;
using atlantis::connection::EventFilter;
using atlantis::connection::EventKind;
using atlantis::connection::EventKindSet;
using atlantis::connection::InProcessEndpoint;
using atlantis::connection::RuntimeConnection;
using atlantis::connection::SubscriptionId;
using atlantis::schema::TypeId;

// What a client can observe of one entity: existence, components, and every
// directly declared field of each component.
struct Observed {
  bool live = false;
  std::vector<TypeId> components;
  std::vector<access::PropertyValue> values;
  friend bool operator==(const Observed&, const Observed&) = default;
};

template <typename Source>
[[nodiscard]] std::vector<Observed> observe(const Source& source, const std::vector<EntityGuid>& guids) {
  std::vector<Observed> out;
  for (const EntityGuid& guid : guids) {
    Observed entity;
    entity.live = source.findEntity(guid);
    if (entity.live) {
      entity.components = source.listComponents(guid).value();
      for (const TypeId component : entity.components) {
        for (const auto& type : atlantis::world::worldSchema()) {
          if (type.id != component) continue;
          for (const auto& f : type.fields) {
            const auto value = source.getProperty({guid, component, f.id});
            if (value.isOk()) entity.values.push_back(value.value());
          }
        }
      }
    }
    out.push_back(std::move(entity));
  }
  return out;
}

[[nodiscard]] std::vector<EntityGuid> trackedGuids(const ValidatedSceneData& scene) {
  std::vector<EntityGuid> guids;
  for (std::size_t node = 0; node < 5; ++node) guids.push_back(scene.entityGuid(node));
  for (std::uint32_t n = 1; n <= 4; ++n) guids.push_back(newGuid(n));
  return guids;
}

[[nodiscard]] std::vector<access::Event> drained(RuntimeConnection& connection, SubscriptionId subscription) {
  auto events = connection.drainEvents(subscription);
  REQUIRE(events.isOk());
  return events.value();
}

}  // namespace

TEST_CASE("connection: every Query answers as the boundary does, and schema() is worldSchema()",
          "[connection]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  InProcessEndpoint endpoint(boundary);
  const auto connection = endpoint.open();
  const std::vector<EntityGuid> guids = trackedGuids(scene);

  CHECK(connection->listEntities() == boundary.listEntities());
  CHECK(observe(*connection, guids) == observe(boundary, guids));
  CHECK(connection->listComponents(newGuid(9)).error() == access::AccessError::UnknownEntity);
  const auto missing = connection->getProperty(
      {scene.entityGuid(kBareNode), typeOf<atlantis::world::Light>(), field("world::Light", "intensity")});
  CHECK(missing.error() == access::AccessError::ComponentMissing);

  const auto schema = connection->schema();
  CHECK(schema.data() == atlantis::world::worldSchema().data());
  CHECK(schema.size() == atlantis::world::worldSchema().size());
}

TEST_CASE("connection: commands and transactions through a connection equal direct submission",
          "[connection]") {
  using namespace atlantis::world;
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  BakedScene bakedA = bakeScene(scene);
  BakedScene bakedB = bakeScene(scene);
  access::RuntimeWorldAccess direct(bakedA);
  access::RuntimeWorldAccess boundaryB(bakedB);
  InProcessEndpoint endpoint(boundaryB);
  const auto connection = endpoint.open();
  const SubscriptionId all = connection->subscribe(EventFilter{});
  const std::vector<EntityGuid> guids = trackedGuids(scene);
  const EntityGuid g = newGuid(1);
  const auto intensity = field("world::Light", "intensity");

  const auto run = [&](auto& target) {
    target.submit(access::CreateEntity{g});
    target.submit(access::DestroyEntity{newGuid(9)});  // refused: UnknownEntity
    (void)target.submitTransaction({access::AddComponent{g, typeOf<Light>()}, set(g, typeOf<Light>(), intensity, 2.0f)});
    (void)target.submitTransaction({access::RemoveComponent{g, typeOf<Light>()},
                                    access::DestroyEntity{scene.entityGuid(kCameraNode)}});  // aborts at 1
    (void)target.submitTransaction({});
  };
  run(direct);
  run(*connection);
  REQUIRE(direct.applyPending().applied == 3);
  REQUIRE(boundaryB.applyPending().applied == 3);

  CHECK(drained(*connection, all) == direct.drainEvents());
  const std::vector<access::CommandFailure> failures = connection->drainFailures();
  CHECK(failures == direct.drainFailures());
  REQUIRE(failures.size() == 2);
  CHECK(failures[1].error == access::AccessError::ActiveCameraProtected);
  CHECK(observe(*connection, guids) == observe(direct, guids));
}

TEST_CASE("connection: failures go back to the connection that submitted them", "[connection]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  InProcessEndpoint endpoint(boundary);
  const auto first = endpoint.open();
  const auto second = endpoint.open();
  const EntityGuid camera = scene.entityGuid(kCameraNode);

  const access::CommandTicket mine = first->submit(access::DestroyEntity{newGuid(9)});
  const access::TransactionTicket theirs =
      second->submitTransaction({access::CreateEntity{newGuid(1)}, access::DestroyEntity{camera}});
  (void)boundary.submit(access::CreateEntity{EntityGuid{}});  // the owner's own: no connection owns it (J4)
  (void)boundary.applyPending();

  CHECK(first->drainFailures() ==
        std::vector<access::CommandFailure>{{mine, access::AccessError::UnknownEntity}});
  const std::vector<access::CommandFailure> secondFailures = second->drainFailures();
  REQUIRE(secondFailures.size() == 1);
  CHECK(theirs.contains(secondFailures[0].ticket));
  CHECK(secondFailures[0].error == access::AccessError::ActiveCameraProtected);
  CHECK(first->drainFailures().empty());
  CHECK(second->drainFailures().empty());
}

TEST_CASE("connection: subscriptions are per connection, see only later events, and do not steal",
          "[connection]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  InProcessEndpoint endpoint(boundary);
  const auto first = endpoint.open();
  const auto second = endpoint.open();
  const SubscriptionId early = first->subscribe(EventFilter{});
  CHECK(early.value == 1);  // ids are endpoint-unique, from 1

  first->submit(access::CreateEntity{newGuid(1)});
  (void)boundary.applyPending();
  const SubscriptionId late = second->subscribe(EventFilter{});  // after the event was applied
  CHECK(late.value == 2);
  CHECK(drained(*second, late).empty());

  second->submit(access::CreateEntity{newGuid(2)});
  (void)boundary.applyPending();
  // The first connection's drain leaves the second's queue intact.
  CHECK(drained(*first, early) == std::vector<access::Event>{access::EntityCreated{newGuid(1)},
                                                             access::EntityCreated{newGuid(2)}});
  CHECK(drained(*second, late) == std::vector<access::Event>{access::EntityCreated{newGuid(2)}});
  CHECK(drained(*first, early).empty());
}

TEST_CASE("connection: filters by kind, entity and component", "[connection]") {
  using namespace atlantis::world;
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  BakedScene baked = bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  InProcessEndpoint endpoint(boundary);
  const auto connection = endpoint.open();
  const EntityGuid g = newGuid(1);
  const EntityGuid h = newGuid(2);
  const auto intensity = field("world::Light", "intensity");

  const SubscriptionId changes = connection->subscribe({EventKindSet::only(EventKind::PropertyChanged), {}, {}});
  const SubscriptionId onG = connection->subscribe({EventKindSet::all(), g, {}});
  const SubscriptionId lights = connection->subscribe({EventKindSet::all(), {}, typeOf<Light>()});
  const SubscriptionId created =
      connection->subscribe({EventKindSet::only(EventKind::EntityCreated).with(EventKind::EntityDestroyed), {}, {}});

  for (const EntityGuid& e : {g, h}) {
    connection->submit(access::CreateEntity{e});
    connection->submit(access::AddComponent{e, typeOf<Light>()});
    connection->submit(set(e, typeOf<Light>(), intensity, 4.0f));
    connection->submit(access::AddComponent{e, typeOf<Transform>()});
  }
  REQUIRE(boundary.applyPending().failures.empty());

  CHECK(drained(*connection, changes) ==
        std::vector<access::Event>{access::PropertyChanged{{g, typeOf<Light>(), intensity}, 4.0f},
                                   access::PropertyChanged{{h, typeOf<Light>(), intensity}, 4.0f}});
  CHECK(drained(*connection, onG) ==
        std::vector<access::Event>{access::EntityCreated{g}, access::ComponentAdded{g, typeOf<Light>()},
                                   access::PropertyChanged{{g, typeOf<Light>(), intensity}, 4.0f},
                                   access::ComponentAdded{g, typeOf<Transform>()}});
  // A component filter never matches EntityCreated/EntityDestroyed.
  CHECK(drained(*connection, lights) ==
        std::vector<access::Event>{access::ComponentAdded{g, typeOf<Light>()},
                                   access::PropertyChanged{{g, typeOf<Light>(), intensity}, 4.0f},
                                   access::ComponentAdded{h, typeOf<Light>()},
                                   access::PropertyChanged{{h, typeOf<Light>(), intensity}, 4.0f}});
  CHECK(drained(*connection, created) ==
        std::vector<access::Event>{access::EntityCreated{g}, access::EntityCreated{h}});
}

TEST_CASE("connection: an unknown or foreign subscription is UnknownSubscription", "[connection]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  InProcessEndpoint endpoint(boundary);
  const auto first = endpoint.open();
  const auto second = endpoint.open();
  const SubscriptionId mine = first->subscribe(EventFilter{});

  CHECK(second->drainEvents(mine).error() == ConnectionError::UnknownSubscription);
  CHECK(second->unsubscribe(mine).error() == ConnectionError::UnknownSubscription);
  CHECK(first->drainEvents(SubscriptionId{}).error() == ConnectionError::UnknownSubscription);
  CHECK(first->unsubscribe(mine).isOk());
  CHECK(first->unsubscribe(mine).error() == ConnectionError::UnknownSubscription);
  CHECK(first->drainEvents(mine).error() == ConnectionError::UnknownSubscription);
  CHECK(atlantis::connection::toString(ConnectionError::UnknownSubscription) == "UnknownSubscription");
}

TEST_CASE("connection: a closed connection's later failures are dropped, the others unaffected", "[connection]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  InProcessEndpoint endpoint(boundary);
  const auto stays = endpoint.open();
  const access::CommandTicket kept = stays->submit(access::DestroyEntity{newGuid(8)});
  {
    const auto closes = endpoint.open();
    (void)closes->submit(access::DestroyEntity{newGuid(9)});
  }  // closed before its command applies
  (void)boundary.applyPending();
  CHECK(stays->drainFailures() == std::vector<access::CommandFailure>{{kept, access::AccessError::UnknownEntity}});
}
