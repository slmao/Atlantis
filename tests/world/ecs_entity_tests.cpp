#include <atlantis/world/ecs/world.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <utility>

#include <atlantis/world/ecs/world_components.h>

#include "ecs_test_access.h"

// Plan 0050 M2 (Spec 0050 R1; ruling Q4 (a)): ecs::EntityId under ADR-0049's
// rules -- generation, retirement, per-instance identity, the sentinel.

namespace {

namespace ecs = atlantis::world::ecs;
using atlantis::world::Transform;

}  // namespace

TEST_CASE("ecs entity: createEntity returns a valid handle; the first gets index 0, generation 0",
          "[world][ecs][entity]") {
  ecs::World world;
  const ecs::EntityId a = world.createEntity();
  CHECK(world.isValid(a));
  CHECK(a.index() == 0);
  CHECK(a.generation() == 0);
  CHECK(a != ecs::kInvalidEntityId);
}

TEST_CASE("ecs entity: destroy invalidates; the index is reused with a bumped generation", "[world][ecs][entity]") {
  ecs::World world;
  const ecs::EntityId a = world.createEntity();
  REQUIRE(world.destroyEntity(a).isOk());
  CHECK_FALSE(world.isValid(a));
  const auto again = world.destroyEntity(a);
  REQUIRE(again.isErr());
  CHECK(again.error() == ecs::EcsError::InvalidEntity);

  const ecs::EntityId b = world.createEntity();
  CHECK(b.index() == a.index());
  CHECK(b.generation() == a.generation() + 1);
  CHECK(world.isValid(b));
  CHECK_FALSE(world.isValid(a));
  CHECK(world.get<Transform>(a).error() == ecs::EcsError::InvalidEntity);
}

TEST_CASE("ecs entity: reaching the tombstone generation retires the index for good", "[world][ecs][entity]") {
  ecs::World world;
  constexpr std::uint64_t kTombstone = std::numeric_limits<std::uint64_t>::max();
  ecs::EntityId id = world.createEntity();
  id = ecs::EcsTestAccess::forceGeneration(world, id, kTombstone - 1);
  REQUIRE(world.isValid(id));
  REQUIRE(world.destroyEntity(id).isOk());

  // The retired index is never handed out again.
  for (int i = 0; i < 4; ++i) CHECK(world.createEntity().index() != id.index());
  CHECK_FALSE(world.isValid(id));
}

TEST_CASE("ecs entity: a handle from another World instance is rejected", "[world][ecs][entity]") {
  ecs::World first;
  ecs::World second;
  const ecs::EntityId a = first.createEntity();
  const ecs::EntityId b = second.createEntity();
  CHECK(a.index() == b.index());
  CHECK(a.generation() == b.generation());
  CHECK_FALSE(second.isValid(a));
  CHECK(second.add<Transform>(a).error() == ecs::EcsError::InvalidEntity);
  CHECK(second.destroyEntity(a).error() == ecs::EcsError::InvalidEntity);
}

TEST_CASE("ecs entity: the sentinel is never valid", "[world][ecs][entity]") {
  ecs::World world;
  (void)world.createEntity();
  CHECK_FALSE(world.isValid(ecs::kInvalidEntityId));
  CHECK_FALSE(world.isValid(ecs::EntityId{}));
  CHECK(world.has<Transform>(ecs::kInvalidEntityId).error() == ecs::EcsError::InvalidEntity);
}

TEST_CASE("ecs entity: handles stay valid across a World move", "[world][ecs][entity]") {
  ecs::World original;
  const ecs::EntityId a = original.createEntity();
  REQUIRE(original.add<Transform>(a).isOk());
  ecs::World moved{std::move(original)};
  CHECK(moved.isValid(a));
  CHECK(moved.has<Transform>(a).value());
}
