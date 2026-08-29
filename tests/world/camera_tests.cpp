#include <atlantis/world/camera.h>
#include <atlantis/world/entity_id.h>
#include <atlantis/world/light.h>
#include <atlantis/world/world.h>
#include <atlantis/world/world_error.h>

#include <vector>

#include <catch2/catch_test_macros.hpp>

using atlantis::AssertFailureInfo;
using atlantis::world::Camera;
using atlantis::world::EntityId;
using atlantis::world::kInvalidEntityId;
using atlantis::world::World;
using atlantis::world::WorldError;

// V11
TEST_CASE("Camera: setCamera()/getCamera()/removeCamera() round-trip", "[world][camera]") {
  World world;
  const EntityId id = world.createEntity();
  Camera camera{.fovYRadians = 1.0f, .nearZ = 0.1f, .farZ = 100.0f};

  REQUIRE(world.setCamera(id, camera).isOk());
  const auto got = world.getCamera(id);
  REQUIRE(got.isOk());
  REQUIRE(got.value().fovYRadians == camera.fovYRadians);
  REQUIRE(got.value().nearZ == camera.nearZ);
  REQUIRE(got.value().farZ == camera.farZ);

  REQUIRE(world.removeCamera(id).isOk());
  REQUIRE(world.getCamera(id).isErr());
}

TEST_CASE("Camera: setCamera()/getCamera()/removeCamera() correctly report Err(InvalidEntity) on a stale handle",
          "[world][camera]") {
  World world;
  const EntityId stale = world.createEntity();
  REQUIRE(world.destroyEntity(stale).isOk());

  REQUIRE(world.setCamera(stale, Camera{}).error() == WorldError::InvalidEntity);
  REQUIRE(world.getCamera(stale).error() == WorldError::InvalidEntity);
  REQUIRE(world.removeCamera(stale).error() == WorldError::InvalidEntity);
}

TEST_CASE("setActiveCamera() fails Err(NoCameraComponent) against an entity with no Camera", "[world][camera]") {
  World world;
  const EntityId id = world.createEntity();
  const auto result = world.setActiveCamera(id);
  REQUIRE(result.isErr());
  REQUIRE(result.error() == WorldError::NoCameraComponent);
}

TEST_CASE("A fresh World's own activeCamera() starts std::nullopt", "[world][camera]") {
  World world;
  REQUIRE_FALSE(world.activeCamera().has_value());
}

TEST_CASE("Destroying the active camera entity directly clears activeCamera() automatically", "[world][camera]") {
  World world;
  const EntityId camEntity = world.createEntity();
  REQUIRE(world.setCamera(camEntity, Camera{}).isOk());
  REQUIRE(world.setActiveCamera(camEntity).isOk());
  REQUIRE(world.activeCamera().has_value());

  REQUIRE(world.destroyEntity(camEntity).isOk());
  REQUIRE_FALSE(world.activeCamera().has_value());
}

TEST_CASE("Destroying an ancestor transitively clears activeCamera() via cascading destroy", "[world][camera]") {
  World world;
  const EntityId root = world.createEntity();
  const EntityId camEntity = world.createEntity();
  REQUIRE(world.setParent(camEntity, root).isOk());
  REQUIRE(world.setCamera(camEntity, Camera{}).isOk());
  REQUIRE(world.setActiveCamera(camEntity).isOk());

  REQUIRE(world.destroyEntity(root).isOk());
  REQUIRE_FALSE(world.activeCamera().has_value());
}

TEST_CASE("clearActiveCamera() clears an explicitly set active camera", "[world][camera]") {
  World world;
  const EntityId camEntity = world.createEntity();
  REQUIRE(world.setCamera(camEntity, Camera{}).isOk());
  REQUIRE(world.setActiveCamera(camEntity).isOk());

  world.clearActiveCamera();
  REQUIRE_FALSE(world.activeCamera().has_value());
  REQUIRE(world.isValid(camEntity));  // clearing the active reference does not destroy the entity
}

// V24 (remaining methods, deferred from entity_lifecycle_tests.cpp's own
// Step 1 subset -- Camera/getWorldMatrix entry points are not defined
// until Steps 2/3).
TEST_CASE("WrongWorld is reachable from getWorldMatrix()/Camera entry points", "[world][camera]") {
  World a;
  World b;
  const EntityId fromA = a.createEntity();
  REQUIRE(a.setCamera(fromA, Camera{}).isOk());

  REQUIRE(b.getWorldMatrix(fromA).error() == WorldError::WrongWorld);
  REQUIRE(b.setCamera(fromA, Camera{}).error() == WorldError::WrongWorld);
  REQUIRE(b.removeCamera(fromA).error() == WorldError::WrongWorld);
  REQUIRE(b.getCamera(fromA).error() == WorldError::WrongWorld);
  REQUIRE(b.setActiveCamera(fromA).error() == WorldError::WrongWorld);
  REQUIRE(b.setRenderable(fromA, atlantis::world::Renderable{}).error() == WorldError::WrongWorld);
  REQUIRE(b.removeRenderable(fromA).error() == WorldError::WrongWorld);
  REQUIRE(b.getRenderable(fromA).error() == WorldError::WrongWorld);
  REQUIRE(b.setLight(fromA, atlantis::world::Light{}).error() == WorldError::WrongWorld);
  REQUIRE(b.removeLight(fromA).error() == WorldError::WrongWorld);
  REQUIRE(b.getLight(fromA).error() == WorldError::WrongWorld);
}

// V26 (remaining methods, deferred from entity_lifecycle_tests.cpp's own
// Step 1 subset -- updateTransforms()/clearActiveCamera()/activeCamera()
// are not defined until Steps 2/3).
TEST_CASE("Moved-from World: updateTransforms()/clearActiveCamera()/activeCamera() are checked programmer errors",
          "[world][camera]") {
  World original;
  World moved = std::move(original);

  std::vector<AssertFailureInfo> recorded;
  auto previous = atlantis::assertions::setFailureHandler(
      [&recorded](const AssertFailureInfo& info) { recorded.push_back(info); });

  original.updateTransforms();
  REQUIRE(recorded.size() == 1);

  original.clearActiveCamera();
  REQUIRE(recorded.size() == 2);

  static_cast<void>(original.activeCamera());
  REQUIRE(recorded.size() == 3);

  atlantis::assertions::setFailureHandler(std::move(previous));
}
