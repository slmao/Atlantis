#include <atlantis/world/ecs/world.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

#include <atlantis/world/ecs/command_buffer.h>
#include <atlantis/world/ecs/world_components.h>

// Plan 0050 M6 (Spec 0050 R10): the maintainer's acceptance north star, on
// the engine's real component types -- Atlantis has no MeshRenderer, so
// world::Renderable stands in for it:
//
//   EntityId e = world.createEntity();
//   world.add<Transform>(e);
//   world.add<MeshRenderer>(e);
//   world.query<Transform, MeshRenderer>([](EntityId e, Transform& t, MeshRenderer& r) { ... });

using atlantis::world::Light;
using atlantis::world::Renderable;
using atlantis::world::Transform;
using atlantis::world::ecs::CommandBuffer;
using atlantis::world::ecs::EntityId;

TEST_CASE("ecs north star: createEntity, add, query on Transform and Renderable", "[world][ecs][north_star]") {
  atlantis::world::ecs::World world;

  EntityId e = world.createEntity();
  REQUIRE(world.add<Transform>(e).isOk());
  REQUIRE(world.add<Renderable>(e, Renderable{1001, 2002}).isOk());

  EntityId lamp = world.createEntity();  // not drawn: excluded from the query
  REQUIRE(world.add<Transform>(lamp).isOk());
  REQUIRE(world.add<Light>(lamp).isOk());

  std::vector<EntityId> visited;
  world.query<Transform, Renderable>([&](EntityId id, Transform& t, Renderable& r) {
    visited.push_back(id);
    t.localPosition.x += 1.5f;
    r.meshAsset += 1;
  });
  REQUIRE(visited == std::vector<EntityId>{e});
  CHECK(world.get<Transform>(e).value().localPosition.x == 1.5f);
  CHECK(world.get<Renderable>(e).value().meshAsset == 1002);
  CHECK(world.get<Transform>(lamp).value().localPosition.x == 0.0f);

  // An archetype move: no longer drawn, still transformed.
  REQUIRE(world.remove<Renderable>(e).isOk());
  visited.clear();
  world.query<Transform, Renderable>([&](EntityId id, Transform&, Renderable&) { visited.push_back(id); });
  CHECK(visited.empty());
  CHECK(world.get<Transform>(e).value().localPosition.x == 1.5f);

  // Structural changes decided inside a query, deferred through a CommandBuffer.
  CommandBuffer buffer;
  world.query<const Transform>([&](EntityId id, const Transform&) { buffer.add<Renderable>(id, Renderable{7, {}}); });
  REQUIRE(buffer.apply(world).failures.empty());
  std::size_t drawn = 0;
  world.query<Transform, Renderable>([&](EntityId, Transform&, Renderable& r) {
    CHECK(r.meshAsset == 7);
    ++drawn;
  });
  CHECK(drawn == 2);
}
