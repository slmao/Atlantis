#include <atlantis/world/ecs/world.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include <atlantis/world/ecs/world_components.h>

#include "ecs_test_access.h"

// Plan 0050 M2 (Spec 0050 R4-R6; rulings J2, J5, J6): archetype identity,
// moves between archetypes, swap-remove, chunk overflow and release, errors.

namespace {

namespace ecs = atlantis::world::ecs;
using atlantis::world::Camera;
using atlantis::world::Light;
using atlantis::world::LightKind;
using atlantis::world::Renderable;
using atlantis::world::Transform;
using atlantis::world::Vec3;

[[nodiscard]] Transform at(float x) {
  Transform t;
  t.localPosition = Vec3{x, 0.0f, 0.0f};
  return t;
}

}  // namespace

TEST_CASE("ecs storage: archetype identity is the component set, not the add order", "[world][ecs][storage]") {
  ecs::World world;
  CHECK(ecs::EcsTestAccess::archetypeCount(world) == 1);  // the empty archetype
  const ecs::EntityId a = world.createEntity();
  const ecs::EntityId b = world.createEntity();
  REQUIRE(world.add<Transform>(a).isOk());
  REQUIRE(world.add<Renderable>(a).isOk());  // {} -> {T} -> {T,R}
  REQUIRE(world.add<Renderable>(b).isOk());
  REQUIRE(world.add<Transform>(b).isOk());  // {} -> {R} -> {R,T} == {T,R}
  CHECK(ecs::EcsTestAccess::archetypeCount(world) == 4);  // {}, {T}, {T,R}, {R}
  CHECK(ecs::EcsTestAccess::chunkCount(
            world, {ecs::componentTypeId<Transform>(), ecs::componentTypeId<Renderable>()}) == 1);
}

TEST_CASE("ecs storage: values survive add and remove moves", "[world][ecs][storage]") {
  ecs::World world;
  const ecs::EntityId e = world.createEntity();
  REQUIRE(world.add<Transform>(e, at(3.0f)).isOk());
  Light light;
  light.kind = LightKind::Point;
  light.range = 5.0f;
  REQUIRE(world.add<Light>(e, light).isOk());
  REQUIRE(world.add<Renderable>(e, Renderable{42, 7}).isOk());
  REQUIRE(world.remove<Light>(e).isOk());

  CHECK(world.get<Transform>(e).value().localPosition.x == 3.0f);
  CHECK(world.get<Renderable>(e).value().meshAsset == 42);
  CHECK(world.get<Renderable>(e).value().materialAsset == 7u);
  CHECK_FALSE(world.has<Light>(e).value());
  CHECK(world.has<Transform>(e).value());
}

TEST_CASE("ecs storage: swap-remove keeps every other entity's data", "[world][ecs][storage]") {
  ecs::World world;
  std::vector<ecs::EntityId> ids;
  for (int i = 0; i < 5; ++i) {
    ids.push_back(world.createEntity());
    REQUIRE(world.add<Transform>(ids.back(), at(static_cast<float>(i))).isOk());
  }
  REQUIRE(world.destroyEntity(ids[1]).isOk());     // the last row (4) fills row 1
  REQUIRE(world.remove<Transform>(ids[0]).isOk());  // a move out of the archetype
  for (int i = 2; i < 5; ++i) CHECK(world.get<Transform>(ids[i]).value().localPosition.x == static_cast<float>(i));
  CHECK_FALSE(world.has<Transform>(ids[0]).value());
}

TEST_CASE("ecs storage: an archetype overflows into a second chunk and releases it", "[world][ecs][storage]") {
  ecs::World world;
  const std::vector<atlantis::schema::TypeId> types{ecs::componentTypeId<Transform>()};
  std::vector<ecs::EntityId> ids;
  ids.push_back(world.createEntity());
  REQUIRE(world.add<Transform>(ids.back(), at(0.0f)).isOk());
  const std::uint32_t capacity = ecs::EcsTestAccess::capacity(world, types);
  REQUIRE(capacity > 1);
  // Plan 0050 P5 (J5): 16 KiB holds 16384 / (24 + 36) = 273 {Transform} rows.
  CHECK(capacity == 273);

  while (ids.size() < capacity + 1) {
    ids.push_back(world.createEntity());
    REQUIRE(world.add<Transform>(ids.back(), at(static_cast<float>(ids.size() - 1))).isOk());
  }
  CHECK(ecs::EcsTestAccess::chunkCount(world, types) == 2);
  for (std::size_t i = 0; i < ids.size(); ++i) {
    CHECK(world.get<Transform>(ids[i]).value().localPosition.x == static_cast<float>(i));
  }

  REQUIRE(world.destroyEntity(ids.front()).isOk());  // the second chunk's only row moves into chunk 0
  CHECK(ecs::EcsTestAccess::chunkCount(world, types) == 1);
  for (std::size_t i = 1; i < ids.size(); ++i) {
    CHECK(world.get<Transform>(ids[i]).value().localPosition.x == static_cast<float>(i));
  }
}

TEST_CASE("ecs storage: add, get, set, remove and has report their errors (J6)", "[world][ecs][storage]") {
  ecs::World world;
  const ecs::EntityId e = world.createEntity();
  REQUIRE(world.add<Transform>(e, at(1.0f)).isOk());

  CHECK(world.add<Transform>(e, at(9.0f)).error() == ecs::EcsError::ComponentAlreadyPresent);
  CHECK(world.get<Transform>(e).value().localPosition.x == 1.0f);  // not overwritten
  CHECK(world.set<Camera>(e, Camera{}).error() == ecs::EcsError::ComponentMissing);
  CHECK_FALSE(world.has<Camera>(e).value());                    // not added
  CHECK(world.get<Camera>(e).error() == ecs::EcsError::ComponentMissing);
  CHECK(world.remove<Camera>(e).error() == ecs::EcsError::ComponentMissing);

  REQUIRE(world.set<Transform>(e, at(2.0f)).isOk());
  CHECK(world.get<Transform>(e).value().localPosition.x == 2.0f);

  REQUIRE(world.destroyEntity(e).isOk());
  CHECK(world.add<Camera>(e).error() == ecs::EcsError::InvalidEntity);
  CHECK(world.get<Transform>(e).error() == ecs::EcsError::InvalidEntity);
  CHECK(world.set<Transform>(e, at(3.0f)).error() == ecs::EcsError::InvalidEntity);
  CHECK(world.remove<Transform>(e).error() == ecs::EcsError::InvalidEntity);
  CHECK(world.has<Transform>(e).error() == ecs::EcsError::InvalidEntity);
}
