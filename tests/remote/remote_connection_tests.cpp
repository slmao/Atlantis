#include "remote_fixture.h"

#include <atlantis/world/world_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

// Plan 0055 M2 (Spec 0055 R2; ADR-0106 D2): a RemoteConnection is the Spec
// 0054 connection over a socket -- the 0054 connection suite's cases
// (tests/connection/connection_tests.cpp) re-run with each connection a
// RemoteConnection to a RemoteServer, against a direct boundary (or an
// InProcess connection) on an identical world. Every observable must be equal.

namespace {

namespace access = atlantis::world::access;
namespace ecs = atlantis::world::ecs;
using atlantis::asset_system::EntityGuid;
using atlantis::asset_system::ValidatedSceneData;
using atlantis::connection::ConnectionError;
using atlantis::connection::EventFilter;
using atlantis::connection::EventKind;
using atlantis::connection::EventKindSet;
using atlantis::connection::RuntimeConnection;
using atlantis::connection::SubscriptionId;
using atlantis::remote::test::cookAndDecodeScene;
using atlantis::remote::test::kSceneSource;
using atlantis::remote::test::ServedWorld;
using atlantis::schema::TypeId;

constexpr std::size_t kPointNode = 2;
constexpr std::size_t kCameraNode = 3;
constexpr std::size_t kBareNode = 4;

[[nodiscard]] EntityGuid newGuid(std::uint32_t n) {
  EntityGuid guid;
  guid.bytes[0] = std::byte{0x52};
  guid.bytes[12] = static_cast<std::byte>((n >> 24) & 0xff);
  guid.bytes[13] = static_cast<std::byte>((n >> 16) & 0xff);
  guid.bytes[14] = static_cast<std::byte>((n >> 8) & 0xff);
  guid.bytes[15] = static_cast<std::byte>(n & 0xff);
  return guid;
}

template <typename T>
[[nodiscard]] TypeId typeOf() {
  return ecs::componentTypeId<T>();
}

[[nodiscard]] atlantis::schema::FieldId field(std::string_view owner, std::string_view name) {
  return atlantis::schema::fieldId(owner, name);
}

[[nodiscard]] access::SetProperty set(const EntityGuid& entity, TypeId component, atlantis::schema::FieldId fieldId,
                                      access::PropertyValue value) {
  return access::SetProperty{{entity, component, fieldId}, std::move(value)};
}

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

TEST_CASE("remote: every Query answers as the boundary does, and the schema arrives whole", "[remote][connection]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  const auto session = world.attach();
  RuntimeConnection& connection = session->connection();
  const std::vector<EntityGuid> guids = trackedGuids(scene);

  CHECK(session->scene() == atlantis::remote::test::fixtureSceneGuid());
  CHECK(connection.listEntities() == world.boundary.listEntities());
  CHECK(observe(connection, guids) == observe(world.boundary, guids));
  CHECK(connection.listComponents(newGuid(9)).error() == access::AccessError::UnknownEntity);
  const auto missing = connection.getProperty(
      {scene.entityGuid(kBareNode), typeOf<atlantis::world::Light>(), field("world::Light", "intensity")});
  CHECK(missing.error() == access::AccessError::ComponentMissing);

  const auto schema = connection.schema();
  const auto expected = atlantis::world::worldSchema();
  REQUIRE(schema.size() == expected.size());
  for (std::size_t i = 0; i < schema.size(); ++i) {
    CHECK(schema[i].id == expected[i].id);
    CHECK(schema[i].name == expected[i].name);
    CHECK(schema[i].fields.size() == expected[i].fields.size());
    CHECK(schema[i].constants.size() == expected[i].constants.size());
  }
  CHECK_FALSE(session->failure().has_value());
}

TEST_CASE("remote: commands and transactions through a remote connection equal direct submission",
          "[remote][connection]") {
  using namespace atlantis::world;
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  BakedScene bakedA = bakeScene(scene);
  access::RuntimeWorldAccess direct(bakedA);
  ServedWorld world(scene);
  const auto session = world.attach();
  RuntimeConnection& connection = session->connection();
  const SubscriptionId all = connection.subscribe(EventFilter{});
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
  run(connection);
  REQUIRE(direct.applyPending().applied == 3);
  REQUIRE(world.boundary.applyPending().applied == 3);

  CHECK(drained(connection, all) == direct.drainEvents());
  const std::vector<access::CommandFailure> failures = connection.drainFailures();
  CHECK(failures == direct.drainFailures());
  REQUIRE(failures.size() == 2);
  CHECK(failures[1].error == access::AccessError::ActiveCameraProtected);
  CHECK(observe(connection, guids) == observe(direct, guids));
  CHECK_FALSE(session->failure().has_value());
}

TEST_CASE("remote: tickets and non-finite values cross the wire unchanged", "[remote][connection]") {
  using atlantis::world::Light;
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  const auto session = world.attach();
  RuntimeConnection& connection = session->connection();
  const EntityGuid point = scene.entityGuid(kPointNode);
  const access::CommandTicket first =
      connection.submit(set(point, typeOf<Light>(), field("world::Light", "intensity"), std::nanf("")));
  const access::TransactionTicket rest = connection.submitTransaction(
      {set(point, typeOf<Light>(), field("world::Light", "intensity"), 24.0f), access::CreateEntity{newGuid(3)}});
  CHECK(first.value == 1);
  CHECK(rest == access::TransactionTicket{access::CommandTicket{2}, 2});
  (void)world.boundary.applyPending();
  CHECK(connection.drainFailures() ==
        std::vector<access::CommandFailure>{{first, access::AccessError::NonFiniteValue}});
  CHECK(connection.getProperty({point, typeOf<Light>(), field("world::Light", "intensity")}).value() ==
        access::PropertyValue(24.0f));
}

TEST_CASE("remote: failures go back to the remote connection that submitted them", "[remote][connection]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  const auto firstSession = world.attach();
  const auto secondSession = world.attach();
  RuntimeConnection& first = firstSession->connection();
  RuntimeConnection& second = secondSession->connection();
  const EntityGuid camera = scene.entityGuid(kCameraNode);

  const access::CommandTicket mine = first.submit(access::DestroyEntity{newGuid(9)});
  const access::TransactionTicket theirs =
      second.submitTransaction({access::CreateEntity{newGuid(1)}, access::DestroyEntity{camera}});
  (void)world.boundary.submit(access::CreateEntity{EntityGuid{}});  // the owner's own: no connection owns it
  (void)world.boundary.applyPending();

  CHECK(first.drainFailures() == std::vector<access::CommandFailure>{{mine, access::AccessError::UnknownEntity}});
  const std::vector<access::CommandFailure> secondFailures = second.drainFailures();
  REQUIRE(secondFailures.size() == 1);
  CHECK(theirs.contains(secondFailures[0].ticket));
  CHECK(secondFailures[0].error == access::AccessError::ActiveCameraProtected);
  CHECK(first.drainFailures().empty());
  CHECK(second.drainFailures().empty());
}

TEST_CASE("remote: subscriptions are per connection, see only later events, and do not steal",
          "[remote][connection]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  const auto firstSession = world.attach();
  const auto secondSession = world.attach();
  RuntimeConnection& first = firstSession->connection();
  RuntimeConnection& second = secondSession->connection();
  const SubscriptionId early = first.subscribe(EventFilter{});
  CHECK(early.value == 1);  // ids are endpoint-unique, from 1

  first.submit(access::CreateEntity{newGuid(1)});
  (void)world.boundary.applyPending();
  const SubscriptionId late = second.subscribe(EventFilter{});
  CHECK(late.value == 2);
  CHECK(drained(second, late).empty());

  second.submit(access::CreateEntity{newGuid(2)});
  (void)world.boundary.applyPending();
  CHECK(drained(first, early) ==
        std::vector<access::Event>{access::EntityCreated{newGuid(1)}, access::EntityCreated{newGuid(2)}});
  CHECK(drained(second, late) == std::vector<access::Event>{access::EntityCreated{newGuid(2)}});
  CHECK(drained(first, early).empty());
}

TEST_CASE("remote: filters by kind, entity and component", "[remote][connection]") {
  using namespace atlantis::world;
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  const auto session = world.attach();
  RuntimeConnection& connection = session->connection();
  const EntityGuid g = newGuid(1);
  const EntityGuid h = newGuid(2);
  const auto intensity = field("world::Light", "intensity");

  const SubscriptionId changes = connection.subscribe({EventKindSet::only(EventKind::PropertyChanged), {}, {}});
  const SubscriptionId onG = connection.subscribe({EventKindSet::all(), g, {}});
  const SubscriptionId lights = connection.subscribe({EventKindSet::all(), {}, typeOf<Light>()});
  const SubscriptionId created =
      connection.subscribe({EventKindSet::only(EventKind::EntityCreated).with(EventKind::EntityDestroyed), {}, {}});

  for (const EntityGuid& e : {g, h}) {
    connection.submit(access::CreateEntity{e});
    connection.submit(access::AddComponent{e, typeOf<Light>()});
    connection.submit(set(e, typeOf<Light>(), intensity, 4.0f));
    connection.submit(access::AddComponent{e, typeOf<Transform>()});
  }
  REQUIRE(world.boundary.applyPending().failures.empty());

  CHECK(drained(connection, changes) ==
        std::vector<access::Event>{access::PropertyChanged{{g, typeOf<Light>(), intensity}, 4.0f},
                                   access::PropertyChanged{{h, typeOf<Light>(), intensity}, 4.0f}});
  CHECK(drained(connection, onG) ==
        std::vector<access::Event>{access::EntityCreated{g}, access::ComponentAdded{g, typeOf<Light>()},
                                   access::PropertyChanged{{g, typeOf<Light>(), intensity}, 4.0f},
                                   access::ComponentAdded{g, typeOf<Transform>()}});
  CHECK(drained(connection, lights) ==
        std::vector<access::Event>{access::ComponentAdded{g, typeOf<Light>()},
                                   access::PropertyChanged{{g, typeOf<Light>(), intensity}, 4.0f},
                                   access::ComponentAdded{h, typeOf<Light>()},
                                   access::PropertyChanged{{h, typeOf<Light>(), intensity}, 4.0f}});
  CHECK(drained(connection, created) ==
        std::vector<access::Event>{access::EntityCreated{g}, access::EntityCreated{h}});
}

TEST_CASE("remote: an unknown or foreign subscription is UnknownSubscription", "[remote][connection]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  const auto firstSession = world.attach();
  const auto secondSession = world.attach();
  RuntimeConnection& first = firstSession->connection();
  RuntimeConnection& second = secondSession->connection();
  const SubscriptionId mine = first.subscribe(EventFilter{});

  CHECK(second.drainEvents(mine).error() == ConnectionError::UnknownSubscription);
  CHECK(second.unsubscribe(mine).error() == ConnectionError::UnknownSubscription);
  CHECK(first.drainEvents(SubscriptionId{}).error() == ConnectionError::UnknownSubscription);
  CHECK(first.unsubscribe(mine).isOk());
  CHECK(first.unsubscribe(mine).error() == ConnectionError::UnknownSubscription);
  CHECK(first.drainEvents(mine).error() == ConnectionError::UnknownSubscription);
  CHECK_FALSE(firstSession->failure().has_value());  // a refusal is not a transport failure
}

TEST_CASE("remote: a disconnected client's later failures are dropped, the others unaffected",
          "[remote][connection]") {
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  const auto staysSession = world.attach();
  RuntimeConnection& stays = staysSession->connection();
  const access::CommandTicket kept = stays.submit(access::DestroyEntity{newGuid(8)});
  {
    const auto closes = world.attach();
    (void)closes->connection().submit(access::DestroyEntity{newGuid(9)});
  }  // disconnected before its command applies
  world.server->poll();  // the server notices and closes that client's connection
  CHECK(world.server->clientCount() == 1);
  (void)world.boundary.applyPending();
  CHECK(stays.drainFailures() == std::vector<access::CommandFailure>{{kept, access::AccessError::UnknownEntity}});
}

TEST_CASE("remote: batched queries equal one-at-a-time queries", "[remote][connection]") {
  using atlantis::world::Light;
  const ValidatedSceneData scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  const auto session = world.attach();
  const std::vector<EntityGuid> guids = trackedGuids(scene);
  const auto components = session->listComponents(guids);
  REQUIRE(components.size() == guids.size());
  std::vector<access::PropertyAddress> addresses;
  for (std::size_t i = 0; i < guids.size(); ++i) {
    const auto single = session->connection().listComponents(guids[i]);
    REQUIRE(components[i].isOk() == single.isOk());
    if (single.isOk()) {
      CHECK(components[i].value() == single.value());
    } else {
      CHECK(components[i].error() == single.error());
    }
    addresses.push_back({guids[i], typeOf<Light>(), field("world::Light", "intensity")});
  }
  const auto values = session->getProperties(addresses);
  REQUIRE(values.size() == addresses.size());
  for (std::size_t i = 0; i < addresses.size(); ++i) {
    const auto single = session->connection().getProperty(addresses[i]);
    REQUIRE(values[i].isOk() == single.isOk());
    if (single.isOk()) {
      CHECK(values[i].value() == single.value());
    } else {
      CHECK(values[i].error() == single.error());
    }
  }
}
