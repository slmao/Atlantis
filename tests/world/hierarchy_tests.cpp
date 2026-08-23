#include <atlantis/world/entity_id.h>
#include <atlantis/world/world.h>
#include <atlantis/world/world_error.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::world::EntityId;
using atlantis::world::kInvalidEntityId;
using atlantis::world::World;
using atlantis::world::WorldError;

// V5: setParent() success and cycle prevention.
TEST_CASE("setParent() succeeds for a valid, non-cycle-forming request", "[world][hierarchy]") {
  World world;
  const EntityId parent = world.createEntity();
  const EntityId child = world.createEntity();

  REQUIRE(world.setParent(child, parent).isOk());
  const auto result = world.getParent(child);
  REQUIRE(result.isOk());
  REQUIRE(result.value() == parent);
}

TEST_CASE("setParent() rejects a direct self-parent, leaving the hierarchy unchanged", "[world][hierarchy]") {
  World world;
  const EntityId a = world.createEntity();

  const auto result = world.setParent(a, a);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == WorldError::WouldCreateCycle);

  const auto parentResult = world.getParent(a);
  REQUIRE(parentResult.isOk());
  REQUIRE(parentResult.value() == kInvalidEntityId);
}

TEST_CASE("setParent() rejects a two-hop cycle, leaving the hierarchy unchanged", "[world][hierarchy]") {
  World world;
  const EntityId a = world.createEntity();
  const EntityId b = world.createEntity();
  REQUIRE(world.setParent(b, a).isOk());  // b's parent = a

  const auto result = world.setParent(a, b);  // would make a's parent = b -> a -> b -> a
  REQUIRE(result.isErr());
  REQUIRE(result.error() == WorldError::WouldCreateCycle);

  REQUIRE(world.getParent(a).value() == kInvalidEntityId);
  REQUIRE(world.getParent(b).value() == a);
}

TEST_CASE("setParent() rejects a four-hop transitive cycle, leaving the hierarchy unchanged", "[world][hierarchy]") {
  World world;
  const EntityId a = world.createEntity();
  const EntityId b = world.createEntity();
  const EntityId c = world.createEntity();
  const EntityId d = world.createEntity();
  REQUIRE(world.setParent(b, a).isOk());  // b -> a
  REQUIRE(world.setParent(c, b).isOk());  // c -> b
  REQUIRE(world.setParent(d, c).isOk());  // d -> c

  const auto result = world.setParent(a, d);  // would make a -> d -> c -> b -> a
  REQUIRE(result.isErr());
  REQUIRE(result.error() == WorldError::WouldCreateCycle);

  REQUIRE(world.getParent(a).value() == kInvalidEntityId);
  REQUIRE(world.getParent(b).value() == a);
  REQUIRE(world.getParent(c).value() == b);
  REQUIRE(world.getParent(d).value() == c);
}

TEST_CASE("setParent()/getParent() reject a stale handle with Err(InvalidEntity), checked before any cycle walk",
          "[world][hierarchy]") {
  World world;
  const EntityId a = world.createEntity();
  const EntityId stale = world.createEntity();
  REQUIRE(world.destroyEntity(stale).isOk());

  const auto childStale = world.setParent(stale, a);
  REQUIRE(childStale.isErr());
  REQUIRE(childStale.error() == WorldError::InvalidEntity);

  const auto parentStale = world.setParent(a, stale);
  REQUIRE(parentStale.isErr());
  REQUIRE(parentStale.error() == WorldError::InvalidEntity);

  REQUIRE(world.getParent(stale).isErr());
  REQUIRE(world.getParent(stale).error() == WorldError::InvalidEntity);
}

// V6 (hierarchy half): every Err path leaves every observable World
// state byte-identical to immediately before the call.
TEST_CASE("setParent()'s Err path leaves every entity's own parent link untouched", "[world][hierarchy]") {
  World world;
  const EntityId a = world.createEntity();
  const EntityId b = world.createEntity();
  const EntityId sibling = world.createEntity();
  REQUIRE(world.setParent(b, a).isOk());
  REQUIRE(world.setParent(sibling, a).isOk());

  REQUIRE(world.setParent(a, b).isErr());  // would-be cycle

  REQUIRE(world.getParent(a).value() == kInvalidEntityId);
  REQUIRE(world.getParent(b).value() == a);
  REQUIRE(world.getParent(sibling).value() == a);  // unrelated sibling untouched
}

// V24 (hierarchy portion): WrongWorld reachable from setParent()/getParent(),
// checked before any index/generation-dependent behavior (e.g. the cycle
// walk never runs for a wrong-world handle).
TEST_CASE("setParent()/getParent() reject a handle from a different, live World with Err(WrongWorld)",
          "[world][hierarchy]") {
  World worldA;
  World worldB;
  const EntityId fromA = worldA.createEntity();
  const EntityId inB = worldB.createEntity();

  const auto setParentChildWrong = worldB.setParent(fromA, inB);
  REQUIRE(setParentChildWrong.isErr());
  REQUIRE(setParentChildWrong.error() == WorldError::WrongWorld);

  const auto setParentParentWrong = worldB.setParent(inB, fromA);
  REQUIRE(setParentParentWrong.isErr());
  REQUIRE(setParentParentWrong.error() == WorldError::WrongWorld);

  const auto getParentWrong = worldB.getParent(fromA);
  REQUIRE(getParentWrong.isErr());
  REQUIRE(getParentWrong.error() == WorldError::WrongWorld);

  // kInvalidEntityId itself still reports InvalidEntity, never WrongWorld.
  const auto invalidResult = worldB.getParent(kInvalidEntityId);
  REQUIRE(invalidResult.isErr());
  REQUIRE(invalidResult.error() == WorldError::InvalidEntity);
}
