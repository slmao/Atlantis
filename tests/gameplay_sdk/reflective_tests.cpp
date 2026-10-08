// Plan 0057 M2 (Spec 0057 R2, R4; ADR-0110 D2; ruling Q9; J5, J6): the
// reflective layer over an InProcess connection on the fixture scene, no
// GPU. Every operation is checked against the RuntimeConnection calls it
// made (RecordingConnection): exactly what a client would write by hand, in
// its order, nothing reordered, merged or split.

#include "gameplay_fixture.h"

#include <atlantis/gameplay/query_batch.h>
#include <atlantis/gameplay/transaction.h>
#include <atlantis/gameplay/world.h>

#include <atlantis/connection/text.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace atlantis::gameplay;
using namespace atlantis::gameplay::test;
namespace schema = atlantis::schema;

const schema::TypeId kLight = schema::typeId("world::Light");
const schema::TypeId kWorldMatrix = schema::typeId("world::WorldMatrix");
const schema::FieldId kIntensity = schema::fieldId("world::Light", "intensity");

[[nodiscard]] float asFloat(const PropertyValue& value) {
  REQUIRE(std::holds_alternative<float>(value));
  return std::get<float>(value);
}

}  // namespace

TEST_CASE("get and set by path and by id; one SetProperty, submitted alone", "[gameplay_sdk][reflective]") {
  GameplayFixture f;
  CHECK(asFloat(f.world.get(kSun, "Light.intensity").value()) == 1.2f);
  CHECK(asFloat(f.world.get(kSun, "world::Light.intensity").value()) == 1.2f);  // qualified names too (J5)
  CHECK(asFloat(f.world.get(PropertyAddress{kSun, kLight, kIntensity}).value()) == 1.2f);
  CHECK(f.world.get(kCamera, "Camera.fog.density").isOk());

  const auto ticket = f.world.set(kSun, "Light.intensity", 2.5f);
  REQUIRE(ticket.isOk());
  REQUIRE(f.recording.submissions.size() == 1);
  CHECK_FALSE(f.recording.submissions[0].transaction);
  CHECK(sameCommands(f.recording.submissions[0].commands,
                     {access::SetProperty{{kSun, kLight, kIntensity}, 2.5f}}));
  CHECK(asFloat(f.world.get(kSun, "Light.intensity").value()) == 1.2f);  // not applied until the next frame
  f.frame();
  CHECK(asFloat(f.world.get(kSun, "Light.intensity").value()) == 2.5f);
}

TEST_CASE("name errors are the client's own and submit nothing", "[gameplay_sdk][reflective]") {
  GameplayFixture f;
  CHECK(f.world.get(kSun, "Nope.intensity").error().kind == ErrorKind::UnknownType);
  CHECK(f.world.get(kSun, "Light.nope").error().kind == ErrorKind::UnknownField);
  CHECK(f.world.get(kCamera, "Camera.fog").error().kind == ErrorKind::NotALeaf);
  CHECK(f.world.get(kSun, "Light").error().kind == ErrorKind::NotALeaf);
  CHECK(f.world.set(kSun, "Light.nope", 1.0f).error().kind == ErrorKind::UnknownField);
  CHECK(f.world.entitiesWith({"Light", "Nope"}).error().kind == ErrorKind::UnknownType);
  CHECK(f.world.readComponent(kSun, "LightKind").error().kind == ErrorKind::UnknownType);  // an enum, not a struct
  CHECK(f.recording.submissions.empty());
}

TEST_CASE("the boundary's refusals pass through unchanged (no pre-checks)", "[gameplay_sdk][reflective]") {
  GameplayFixture f;
  // A query refusal is an Error carrying the AccessError.
  const auto unknown = f.world.get(guid("52052052-0099-4052-8052-000000000099"), "Light.intensity");
  REQUIRE(unknown.isErr());
  CHECK(unknown.error().kind == ErrorKind::Refused);
  CHECK(unknown.error().access == access::AccessError::UnknownEntity);
  CHECK(f.world.get(kEmpty, "Light.intensity").error().access == access::AccessError::ComponentMissing);

  // A command the boundary would refuse is submitted as written: the SDK
  // checks no finiteness, kind, limit or enum range (R4).
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const auto ticket = f.world.set(kSun, "Light.intensity", nan);
  const auto kind = f.world.set(kSun, "Light.intensity", std::array<float, 3>{1.0f, 2.0f, 3.0f});
  const auto enumRange = f.world.set(kSun, "Light.kind", access::EnumValue{42});
  REQUIRE(ticket.isOk());
  REQUIRE(kind.isOk());
  REQUIRE(enumRange.isOk());
  CHECK(f.recording.submissions.size() == 3);
  f.frame();
  FailureLog log;
  log.absorb(f.world.drainFailures());
  CHECK(log.refusal(ticket.value())->error == access::AccessError::NonFiniteValue);
  CHECK(log.refusal(kind.value())->error == access::AccessError::KindMismatch);
  CHECK(log.refusal(enumRange.value())->error == access::AccessError::EnumValueOutOfRange);
  CHECK(asFloat(f.world.get(kSun, "Light.intensity").value()) == 1.2f);
}

TEST_CASE("readComponent: every leaf in leavesOf order, in one batched getProperties", "[gameplay_sdk][reflective]") {
  GameplayFixture f;
  const auto light = f.world.readComponent(kSun, "Light");
  REQUIRE(light.isOk());
  CHECK(light.value().type == kLight);
  CHECK(light.value().name == "world::Light");
  const auto leaves = atlantis::connection::text::leavesOf(f.world.schema(), kLight);
  REQUIRE(light.value().leaves.size() == leaves.size());
  for (std::size_t i = 0; i < leaves.size(); ++i) {
    CHECK(light.value().leaves[i].path == leaves[i].path);
    CHECK(light.value().leaves[i].field == leaves[i].leaf->id);
    CHECK(light.value().leaves[i].value == f.world.get(kSun, leaves[i].path).value());
  }
  CHECK(f.batch.getPropertiesBatches == std::vector<std::size_t>{4});

  const auto camera = f.world.readComponent(kCamera, "Camera");
  REQUIRE(camera.isOk());
  CHECK(camera.value().leaves.size() == 11);  // nested fog and bloom leaves expanded in place
  CHECK(camera.value().leaves[4].path == "Camera.fog.color");
  CHECK(f.batch.getPropertiesBatches == std::vector<std::size_t>{4, 11});

  const auto missing = f.world.readComponent(kEmpty, "Light");
  REQUIRE(missing.isErr());
  CHECK(missing.error().access == access::AccessError::ComponentMissing);
}

TEST_CASE("entitiesWith: one listEntities and one batched listComponents, filtered client-side",
          "[gameplay_sdk][reflective]") {
  GameplayFixture f;
  const auto lights = f.world.entitiesWith({"Light"});
  REQUIRE(lights.isOk());
  CHECK(lights.value() == std::vector<EntityGuid>{kSun, kLamp});
  const std::size_t listed = f.world.entities().size();
  CHECK(f.batch.listComponentsBatches == std::vector<std::size_t>{listed});
  CHECK(f.world.entitiesWith({"Light", "Renderable"}).value().empty());
  CHECK(f.world.entitiesWith({"Renderable"}).value() == std::vector<EntityGuid>{kMesh});
  const std::array ids{kLight, kWorldMatrix};
  CHECK(f.world.entitiesWith(std::span<const schema::TypeId>(ids)).value() == std::vector<EntityGuid>{kSun, kLamp});
  CHECK(f.batch.listComponentsBatches.size() == 4);  // one batch per filter
}

TEST_CASE("a transaction is one submitTransaction of its operations, in order; create, add, remove, destroy",
          "[gameplay_sdk][reflective]") {
  GameplayFixture f;
  const EntityGuid beacon = guid("57005700-0000-4000-8000-0000000000b1");
  Subscription events = f.world.subscribe(atlantis::connection::EventFilter{
      atlantis::connection::EventKindSet::all(), beacon, std::nullopt});
  Transaction tx;
  tx.create(beacon)
      .add(beacon, "Light", {{"kind", access::EnumValue{1}}, {"intensity", 4.0f}, {"range", 3.0f}})
      .add(beacon, "WorldMatrix")
      .set(beacon, "WorldMatrix.column3", std::array<float, 4>{1.0f, 2.0f, 0.0f, 1.0f});
  const auto ticket = f.world.submit(tx);
  REQUIRE(ticket.isOk());
  CHECK(ticket.value().count == 7);
  REQUIRE(f.recording.submissions.size() == 1);
  CHECK(f.recording.submissions[0].transaction);
  const schema::FieldId column3 = schema::fieldId("world::WorldMatrix", "column3");
  const std::vector<access::Command> handWritten{
      access::CreateEntity{beacon},
      access::AddComponent{beacon, kLight},
      access::SetProperty{{beacon, kLight, schema::fieldId("world::Light", "kind")}, access::EnumValue{1}},
      access::SetProperty{{beacon, kLight, kIntensity}, 4.0f},
      access::SetProperty{{beacon, kLight, schema::fieldId("world::Light", "range")}, 3.0f},
      access::AddComponent{beacon, kWorldMatrix},
      access::SetProperty{{beacon, kWorldMatrix, column3}, std::array<float, 4>{1.0f, 2.0f, 0.0f, 1.0f}},
  };
  CHECK(sameCommands(f.recording.submissions[0].commands, handWritten));  // R4 parity, order kept
  f.frame();
  CHECK(f.world.drainFailures().empty());
  CHECK(f.world.exists(beacon));
  CHECK(f.world.components(beacon).value() == std::vector<schema::TypeId>{std::min(kLight, kWorldMatrix),
                                                                          std::max(kLight, kWorldMatrix)});
  const auto created = events.drain();
  REQUIRE(created.isOk());
  CHECK(created.value().size() == 7);  // one event per applied command
  CHECK(created.value().front() == access::Event{access::EntityCreated{beacon}});

  Transaction teardown;
  teardown.remove(beacon, "WorldMatrix").remove(beacon, kLight).destroy(beacon);
  REQUIRE(f.world.submit(teardown).isOk());
  CHECK(sameCommands(f.recording.submissions.back().commands,
                     {access::RemoveComponent{beacon, kWorldMatrix}, access::RemoveComponent{beacon, kLight},
                      access::DestroyEntity{beacon}}));
  f.frame();
  CHECK_FALSE(f.world.exists(beacon));
  CHECK(events.drain().value().back() == access::Event{access::EntityDestroyed{beacon}});
}

TEST_CASE("a transaction is all or nothing: one failure by ticket, nothing applied", "[gameplay_sdk][reflective]") {
  GameplayFixture f;
  Transaction tx;
  tx.set(kSun, "Light.intensity", 9.0f).set(kSun, "Light.color", std::array<float, 3>{
                                                                  std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f});
  const auto ticket = f.world.submit(tx);
  REQUIRE(ticket.isOk());
  f.frame();
  FailureLog log;
  log.absorb(f.world.drainFailures());
  const auto refusal = log.refusal(ticket.value());
  REQUIRE(refusal.has_value());
  CHECK(refusal->error == access::AccessError::NonFiniteValue);
  CHECK(refusal->ticket.value == ticket.value().first.value + 1);  // aborted at the second command
  CHECK(asFloat(f.world.get(kSun, "Light.intensity").value()) == 1.2f);  // the valid half did not apply
}

TEST_CASE("a transaction with an unresolvable name is refused whole: nothing submitted, no ticket (J6)",
          "[gameplay_sdk][reflective]") {
  GameplayFixture f;
  Transaction tx;
  tx.set(kSun, "Light.intensity", 9.0f).set(kSun, "Light.nope", 1.0f);
  const auto refused = f.world.submit(tx);
  REQUIRE(refused.isErr());
  CHECK(refused.error().kind == ErrorKind::UnknownField);
  CHECK(refused.error().subject == "Light.nope");
  Transaction badType;
  badType.create(guid("57005700-0000-4000-8000-0000000000b2")).add(guid("57005700-0000-4000-8000-0000000000b2"), "Nope");
  CHECK(f.world.submit(badType).error().kind == ErrorKind::UnknownType);
  CHECK(f.recording.submissions.empty());
  // An empty transaction passes through: no commands, no ticket (Spec 0053 Q4).
  const auto empty = f.world.submit(Transaction{});
  REQUIRE(empty.isOk());
  CHECK(empty.value().count == 0);
}

TEST_CASE("a subscription unsubscribes when destroyed, and moves", "[gameplay_sdk][reflective]") {
  GameplayFixture f;
  atlantis::connection::SubscriptionId id;
  {
    Subscription first = f.world.subscribe();
    id = first.id();
    Subscription moved = std::move(first);
    REQUIRE(f.world.set(kSun, "Light.intensity", 2.0f).isOk());
    f.frame();
    CHECK(moved.drain().value().size() == 1);
    CHECK(first.drain().error().kind == ErrorKind::Connection);  // moved from: no connection
    CHECK(f.recording.unsubscribeCalls == 0);
  }
  CHECK(f.recording.unsubscribeCalls == 1);
  CHECK(f.recording.drainEvents(id).error() == atlantis::connection::ConnectionError::UnknownSubscription);
}

TEST_CASE("SequentialQueryBatch answers exactly the one-at-a-time calls, in input order", "[gameplay_sdk][batch]") {
  GameplayFixture f;
  SequentialQueryBatch sequential(f.recording);
  const std::vector<EntityGuid> entities{kSun, guid("52052052-0099-4052-8052-000000000099"), kMesh};
  const auto listed = sequential.listComponents(entities);
  REQUIRE(listed.size() == 3);
  for (std::size_t i = 0; i < entities.size(); ++i) {
    const auto direct = f.recording.listComponents(entities[i]);
    CHECK(listed[i].isOk() == direct.isOk());
    if (direct.isOk()) CHECK(listed[i].value() == direct.value());
    if (direct.isErr()) CHECK(listed[i].error() == direct.error());
  }
  const std::vector<PropertyAddress> addresses{{kSun, kLight, kIntensity}, {kEmpty, kLight, kIntensity},
                                               {kLamp, kLight, kIntensity}};
  const auto values = sequential.getProperties(addresses);
  REQUIRE(values.size() == 3);
  CHECK(asFloat(values[0].value()) == 1.2f);
  CHECK(values[1].error() == access::AccessError::ComponentMissing);
  CHECK(asFloat(values[2].value()) == 3.0f);
}
