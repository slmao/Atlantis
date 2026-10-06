#include <atlantis/world/ecs/world.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include <atlantis/assert.h>
#include <atlantis/world/ecs/world_components.h>

#include "ecs_test_access.h"

// Plan 0050 M3 (Spec 0050 R7; rulings Q6, J1, J8): all-of matching, the
// deterministic order, access through the callback's references, and the
// refusal of structural changes while a query runs.

namespace {

namespace ecs = atlantis::world::ecs;
using atlantis::world::Light;
using atlantis::world::Renderable;
using atlantis::world::Transform;
using atlantis::world::Vec3;

[[nodiscard]] Transform at(float x) {
  Transform t;
  t.localPosition = Vec3{x, 0.0f, 0.0f};
  return t;
}

// Records failed assertions instead of aborting, for the test's lifetime.
struct RecordAssertions {
  std::vector<std::string> messages;
  atlantis::AssertFailureHandler previous;
  RecordAssertions() {
    previous = atlantis::assertions::setFailureHandler(
        [this](const atlantis::AssertFailureInfo& info) { messages.emplace_back(info.message); });
  }
  ~RecordAssertions() { atlantis::assertions::setFailureHandler(std::move(previous)); }
  RecordAssertions(const RecordAssertions&) = delete;
  RecordAssertions& operator=(const RecordAssertions&) = delete;
};

}  // namespace

TEST_CASE("ecs query: matches every archetype that contains all requested types", "[world][ecs][query]") {
  ecs::World world;
  const ecs::EntityId transformOnly = world.createEntity();
  const ecs::EntityId both = world.createEntity();
  const ecs::EntityId renderableOnly = world.createEntity();
  const ecs::EntityId all = world.createEntity();
  REQUIRE(world.add<Transform>(transformOnly).isOk());
  REQUIRE(world.add<Transform>(both).isOk());
  REQUIRE(world.add<Renderable>(both).isOk());
  REQUIRE(world.add<Renderable>(renderableOnly).isOk());
  REQUIRE(world.add<Transform>(all).isOk());
  REQUIRE(world.add<Renderable>(all).isOk());
  REQUIRE(world.add<Light>(all).isOk());

  std::vector<ecs::EntityId> seen;
  world.query<Transform, Renderable>([&](ecs::EntityId e, Transform&, Renderable&) { seen.push_back(e); });
  CHECK(seen == std::vector<ecs::EntityId>{both, all});

  std::size_t everything = 0;
  world.query<>([&](ecs::EntityId) { ++everything; });
  CHECK(everything == 4);
}

TEST_CASE("ecs query: order is archetype creation, then chunk, then row", "[world][ecs][query]") {
  ecs::World world;
  // Archetype {T} first, filled past one chunk; then {T,L}; then {T,R}.
  std::vector<ecs::EntityId> expected;
  const std::uint32_t capacity = [&] {
    const ecs::EntityId probe = world.createEntity();
    REQUIRE(world.add<Transform>(probe).isOk());
    expected.push_back(probe);
    return ecs::EcsTestAccess::capacity(world, {ecs::componentTypeId<Transform>()});
  }();
  while (expected.size() < capacity + 3) {
    expected.push_back(world.createEntity());
    REQUIRE(world.add<Transform>(expected.back()).isOk());
  }
  REQUIRE(ecs::EcsTestAccess::chunkCount(world, {ecs::componentTypeId<Transform>()}) == 2);
  const ecs::EntityId lit = world.createEntity();
  REQUIRE(world.add<Transform>(lit).isOk());  // passes through {T} briefly, then moves on
  REQUIRE(world.add<Light>(lit).isOk());
  const ecs::EntityId drawn = world.createEntity();
  REQUIRE(world.add<Transform>(drawn).isOk());
  REQUIRE(world.add<Renderable>(drawn).isOk());
  expected.push_back(lit);
  expected.push_back(drawn);

  std::vector<ecs::EntityId> seen;
  world.query<Transform>([&](ecs::EntityId e, Transform&) { seen.push_back(e); });
  CHECK(seen == expected);

  std::vector<ecs::EntityId> again;
  world.query<const Transform>([&](ecs::EntityId e, const Transform&) { again.push_back(e); });
  CHECK(again == seen);  // repeatable
}

TEST_CASE("ecs query: mutation through the references is visible to get", "[world][ecs][query]") {
  ecs::World world;
  const ecs::EntityId a = world.createEntity();
  const ecs::EntityId b = world.createEntity();
  REQUIRE(world.add<Transform>(a, at(1.0f)).isOk());
  REQUIRE(world.add<Transform>(b, at(2.0f)).isOk());
  world.query<Transform>([](ecs::EntityId, Transform& t) { t.localPosition.x *= 10.0f; });
  CHECK(world.get<Transform>(a).value().localPosition.x == 10.0f);
  CHECK(world.get<Transform>(b).value().localPosition.x == 20.0f);

  float sum = 0.0f;
  world.query<const Transform>([&](ecs::EntityId, const Transform& t) { sum += t.localPosition.x; });
  CHECK(sum == 30.0f);
}

TEST_CASE("ecs query: nested queries and get/set/has inside a callback are allowed (J8)", "[world][ecs][query]") {
  ecs::World world;
  const ecs::EntityId a = world.createEntity();
  const ecs::EntityId b = world.createEntity();
  REQUIRE(world.add<Transform>(a, at(1.0f)).isOk());
  REQUIRE(world.add<Transform>(b, at(2.0f)).isOk());
  REQUIRE(world.add<Renderable>(b).isOk());

  std::size_t pairs = 0;
  world.query<const Transform>([&](ecs::EntityId outer, const Transform&) {
    world.query<const Transform>([&](ecs::EntityId, const Transform&) { ++pairs; });
    CHECK(world.has<Transform>(outer).value());
    REQUIRE(world.set<Transform>(b, at(5.0f)).isOk());  // another entity, no row moves
    CHECK(world.get<Transform>(b).value().localPosition.x == 5.0f);
  });
  CHECK(pairs == 4);
}

TEST_CASE("ecs query: structural changes inside a callback are refused and change nothing (J1)",
          "[world][ecs][query]") {
  ecs::World world;
  const ecs::EntityId a = world.createEntity();
  REQUIRE(world.add<Transform>(a, at(1.0f)).isOk());
  const ecs::EntityId b = world.createEntity();

  RecordAssertions recorded;
  world.query<Transform>([&](ecs::EntityId e, Transform&) {
    CHECK(world.add<Renderable>(e).error() == ecs::EcsError::StructuralChangeDuringQuery);
    CHECK(world.remove<Transform>(e).error() == ecs::EcsError::StructuralChangeDuringQuery);
    CHECK(world.destroyEntity(b).error() == ecs::EcsError::StructuralChangeDuringQuery);
    CHECK(world.createEntity() == ecs::kInvalidEntityId);
  });

#if !defined(NDEBUG)
  CHECK(recorded.messages.size() == 4);  // ATLANTIS_ASSERT_MSG fires once per refused call (Debug only)
#else
  CHECK(recorded.messages.empty());
#endif
  CHECK(world.isValid(b));
  CHECK_FALSE(world.has<Renderable>(a).value());
  CHECK(world.get<Transform>(a).value().localPosition.x == 1.0f);
  CHECK(ecs::EcsTestAccess::archetypeCount(world) == 2);  // {} and {T}: nothing new
  // After the query, the same operations succeed.
  CHECK(world.add<Renderable>(a).isOk());
}
