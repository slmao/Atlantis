#include <atlantis/world/camera.h>
#include <atlantis/world/entity_id.h>
#include <atlantis/world/renderable.h>
#include <atlantis/world/world.h>
#include <atlantis/world/world_error.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::world::Camera;
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
  REQUIRE(world.getRenderable(id).error() == WorldError::NoRenderableComponent);
}

TEST_CASE("Renderable: setRenderable()/getRenderable()/removeRenderable() report Err(InvalidEntity) on a stale "
          "handle",
          "[world][renderable]") {
  World world;
  const EntityId stale = world.createEntity();
  REQUIRE(world.destroyEntity(stale).isOk());

  // stale never had a Renderable before being destroyed -- if
  // component-absence were checked before handle validity, this would
  // incorrectly report NoRenderableComponent instead. V28.
  REQUIRE(world.setRenderable(stale, Renderable{}).error() == WorldError::InvalidEntity);
  REQUIRE(world.getRenderable(stale).error() == WorldError::InvalidEntity);
  REQUIRE(world.removeRenderable(stale).error() == WorldError::InvalidEntity);
}

// V28(a)/(b): NoRenderableComponent and NoCameraComponent are distinct,
// non-interchangeable errors, proven on the very same entity in the same
// test -- not merely "each individually returns some error."
TEST_CASE("getRenderable() and setActiveCamera() report distinct component-absence errors for the same entity",
          "[world][renderable][camera]") {
  World world;
  const EntityId id = world.createEntity();  // no Camera, no Renderable

  REQUIRE(world.getRenderable(id).error() == WorldError::NoRenderableComponent);
  REQUIRE(world.setActiveCamera(id).error() == WorldError::NoCameraComponent);

  // Attaching only a Renderable clears getRenderable()'s own error but
  // leaves setActiveCamera()'s Camera-absence error unaffected.
  REQUIRE(world.setRenderable(id, Renderable{}).isOk());
  REQUIRE(world.getRenderable(id).isOk());
  REQUIRE(world.setActiveCamera(id).error() == WorldError::NoCameraComponent);

  // Attaching only a Camera to a second, otherwise-componentless entity
  // clears setActiveCamera()'s own error but leaves getRenderable()'s
  // Renderable-absence error unaffected.
  const EntityId cameraOnly = world.createEntity();
  REQUIRE(world.setCamera(cameraOnly, Camera{}).isOk());
  REQUIRE(world.setActiveCamera(cameraOnly).isOk());
  REQUIRE(world.getRenderable(cameraOnly).error() == WorldError::NoRenderableComponent);
}

// V28(d), widened by Plan 0019 P1: every WorldError-producing
// switch/mapping in this module must remain exhaustive over all six
// enumerators (NoLightComponent is the sixth, Spec 0019 D2), with no
// `default:` case masking a missing one. This repository has no
// production WorldError-consuming switch (world.cpp only ever produces
// a WorldError, via if-chains), so this canary switch is the "explicit
// enumeration in the test itself" V28 names as the alternative to a
// production compile-time check -- matching Plan 0013's own established
// C4062 precedent (error_classification.cpp), scoped here to
// atlantis_world_tests via CMakeLists.txt's own /w14062. Adding a
// seventh WorldError enumerator without updating this switch fails to
// compile under this target's /w14062 /WX.
TEST_CASE("WorldError's six enumerators are exhaustively covered by a no-default switch", "[world][renderable]") {
  const auto describe = [](WorldError error) -> int {
    switch (error) {
      case WorldError::InvalidEntity:
        return 0;
      case WorldError::WouldCreateCycle:
        return 1;
      case WorldError::NoCameraComponent:
        return 2;
      case WorldError::WrongWorld:
        return 3;
      case WorldError::NoRenderableComponent:
        return 4;
      case WorldError::NoLightComponent:
        return 5;
    }
    return -1;  // unreachable if the switch above is exhaustive
  };

  REQUIRE(describe(WorldError::InvalidEntity) == 0);
  REQUIRE(describe(WorldError::WouldCreateCycle) == 1);
  REQUIRE(describe(WorldError::NoCameraComponent) == 2);
  REQUIRE(describe(WorldError::WrongWorld) == 3);
  REQUIRE(describe(WorldError::NoRenderableComponent) == 4);
  REQUIRE(describe(WorldError::NoLightComponent) == 5);
}
