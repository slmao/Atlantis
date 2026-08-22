#include <atlantis/world/entity_id.h>
#include <atlantis/world/renderable.h>
#include <atlantis/world/world.h>
#include <atlantis/world/world_error.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::world::EntityId;
using atlantis::world::Renderable;
using atlantis::world::World;
using atlantis::world::WorldError;

// V12
TEST_CASE("Renderable: setRenderable()/getRenderable()/removeRenderable() round-trip by value", "[world][renderable]") {
  World world;
  const EntityId id = world.createEntity();
  Renderable renderable{.meshAsset = 42};

  REQUIRE(world.setRenderable(id, renderable).isOk());

  // Mutating the caller's own local copy after passing it in does not
  // affect what getRenderable() later returns -- by-value semantics.
  renderable.meshAsset = 999;

  const auto got = world.getRenderable(id);
  REQUIRE(got.isOk());
  REQUIRE(got.value().meshAsset == 42);

  REQUIRE(world.removeRenderable(id).isOk());
  REQUIRE(world.getRenderable(id).isErr());
}

TEST_CASE("Renderable: setRenderable()/getRenderable()/removeRenderable() report Err(InvalidEntity) on a stale "
          "handle",
          "[world][renderable]") {
  World world;
  const EntityId stale = world.createEntity();
  REQUIRE(world.destroyEntity(stale).isOk());

  REQUIRE(world.setRenderable(stale, Renderable{}).error() == WorldError::InvalidEntity);
  REQUIRE(world.getRenderable(stale).error() == WorldError::InvalidEntity);
  REQUIRE(world.removeRenderable(stale).error() == WorldError::InvalidEntity);
}
