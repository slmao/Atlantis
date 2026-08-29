#include <atlantis/world/entity_id.h>
#include <atlantis/world/light.h>
#include <atlantis/world/world.h>
#include <atlantis/world/world_error.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::world::EntityId;
using atlantis::world::Light;
using atlantis::world::LightKind;
using atlantis::world::World;
using atlantis::world::WorldError;

// Spec 0019 D2 / plans/0019-lighting-foundation.md P1, V1: mirrors
// camera_tests.cpp's own exact shape for setCamera()/getCamera()/
// removeCamera() -- Light follows the identical set/get/remove
// contract.
TEST_CASE("Light: setLight()/getLight()/removeLight() round-trip by value", "[world][light]") {
  World world;
  const EntityId id = world.createEntity();
  Light light{.kind = LightKind::Point, .color = {0.5f, 0.25f, 0.75f}, .intensity = 2.0f, .range = 10.0f};

  REQUIRE(world.setLight(id, light).isOk());

  // Mutating the caller's own local copy after passing it in does not
  // affect what getLight() later returns -- by-value semantics, matching
  // Renderable's own identical proof.
  light.intensity = 999.0f;

  const auto got = world.getLight(id);
  REQUIRE(got.isOk());
  REQUIRE(got.value().kind == LightKind::Point);
  REQUIRE(got.value().color.x == 0.5f);
  REQUIRE(got.value().color.y == 0.25f);
  REQUIRE(got.value().color.z == 0.75f);
  REQUIRE(got.value().intensity == 2.0f);
  REQUIRE(got.value().range == 10.0f);

  REQUIRE(world.removeLight(id).isOk());
  REQUIRE(world.getLight(id).error() == WorldError::NoLightComponent);
}

TEST_CASE("Light: a fresh entity has no Light -- getLight() reports Err(NoLightComponent)", "[world][light]") {
  World world;
  const EntityId id = world.createEntity();
  REQUIRE(world.getLight(id).error() == WorldError::NoLightComponent);
}

// V1: error precedence -- WrongWorld first, InvalidEntity second,
// NoLightComponent third -- matching World::validate()'s own real,
// current code and getCamera()/getRenderable()'s own identical shape.
TEST_CASE("Light: setLight()/getLight()/removeLight() report Err(InvalidEntity) on a stale handle",
          "[world][light]") {
  World world;
  const EntityId stale = world.createEntity();
  REQUIRE(world.destroyEntity(stale).isOk());

  // stale never had a Light before being destroyed -- if component-absence
  // were checked before handle validity, this would incorrectly report
  // NoLightComponent instead, matching renderable_tests.cpp's own
  // identical proof for NoRenderableComponent.
  REQUIRE(world.setLight(stale, Light{}).error() == WorldError::InvalidEntity);
  REQUIRE(world.getLight(stale).error() == WorldError::InvalidEntity);
  REQUIRE(world.removeLight(stale).error() == WorldError::InvalidEntity);
}

TEST_CASE("Light: WrongWorld is reachable from every Light entry point, checked before InvalidEntity",
          "[world][light]") {
  World a;
  World b;
  const EntityId fromA = a.createEntity();
  REQUIRE(a.setLight(fromA, Light{}).isOk());

  REQUIRE(b.setLight(fromA, Light{}).error() == WorldError::WrongWorld);
  REQUIRE(b.getLight(fromA).error() == WorldError::WrongWorld);
  REQUIRE(b.removeLight(fromA).error() == WorldError::WrongWorld);
}

// At most one Light per entity -- the same fixed-slot storage Camera uses:
// a second setLight() call replaces, never errors.
TEST_CASE("Light: a second setLight() call replaces the first, never errors", "[world][light]") {
  World world;
  const EntityId id = world.createEntity();
  REQUIRE(world.setLight(id, Light{.kind = LightKind::Directional}).isOk());
  REQUIRE(world.setLight(id, Light{.kind = LightKind::Point}).isOk());

  const auto got = world.getLight(id);
  REQUIRE(got.isOk());
  REQUIRE(got.value().kind == LightKind::Point);
}

// V1: lightEntities() determinism -- ascending slot-index order, a fresh
// snapshot per call, mirroring renderableEntities()'s own exact contract
// and test shape.
TEST_CASE("Light: lightEntities() returns light-bearing entities in ascending slot-index order", "[world][light]") {
  World world;
  const EntityId first = world.createEntity();
  const EntityId second = world.createEntity();
  const EntityId third = world.createEntity();  // no Light -- must be excluded

  // Deliberately set in a different order than slot index, to prove the
  // returned order is slot-index-driven, not insertion-order-driven.
  REQUIRE(world.setLight(second, Light{}).isOk());
  REQUIRE(world.setLight(first, Light{}).isOk());

  const auto entities = world.lightEntities();
  REQUIRE(entities.size() == 2);
  REQUIRE(entities[0] == first);
  REQUIRE(entities[1] == second);

  static_cast<void>(third);
}

TEST_CASE("Light: lightEntities() is a fresh snapshot -- a later mutation does not affect an already-returned vector",
          "[world][light]") {
  World world;
  const EntityId id = world.createEntity();
  REQUIRE(world.setLight(id, Light{}).isOk());

  const auto before = world.lightEntities();
  REQUIRE(before.size() == 1);

  const EntityId second = world.createEntity();
  REQUIRE(world.setLight(second, Light{}).isOk());

  // The snapshot taken before the second setLight() call is unaffected --
  // proving lightEntities() returns a fresh std::vector, not a live view.
  REQUIRE(before.size() == 1);
  REQUIRE(world.lightEntities().size() == 2);
}

// Entity destroy/cascade: a Light component is destroyed exactly like
// Camera/Renderable already are -- no new logic, no special-casing.
// Mirrors camera_tests.cpp's own "cascading destroy" test shape exactly.
TEST_CASE("Light: destroying an entity makes its own Light unreachable", "[world][light]") {
  World world;
  const EntityId id = world.createEntity();
  REQUIRE(world.setLight(id, Light{}).isOk());
  REQUIRE(world.destroyEntity(id).isOk());

  REQUIRE(world.getLight(id).error() == WorldError::InvalidEntity);
}

TEST_CASE("Light: destroying an ancestor transitively removes a descendant's own Light from lightEntities()",
          "[world][light]") {
  World world;
  const EntityId root = world.createEntity();
  const EntityId lightEntity = world.createEntity();
  REQUIRE(world.setParent(lightEntity, root).isOk());
  REQUIRE(world.setLight(lightEntity, Light{}).isOk());
  REQUIRE(world.lightEntities().size() == 1);

  REQUIRE(world.destroyEntity(root).isOk());
  REQUIRE(world.lightEntities().empty());
}
