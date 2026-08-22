#include <atlantis/world/entity_id.h>
#include <atlantis/world/transform.h>
#include <atlantis/world/world.h>

#include <array>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

using atlantis::world::EntityId;
using atlantis::world::Transform;
using atlantis::world::Vec3;
using atlantis::world::World;

namespace {
constexpr float kEpsilon = 1e-4f;

[[nodiscard]] bool matricesClose(const std::array<float, 16>& a, const std::array<float, 16>& b) {
  for (std::size_t i = 0; i < 16; ++i) {
    if (std::abs(a[i] - b[i]) >= kEpsilon) return false;
  }
  return true;
}
}  // namespace

// V9: moving/rotating a parent changes a child's own getWorldMatrix()
// result in exactly the way composing the new parent matrix with the
// child's unchanged local matrix predicts.
TEST_CASE("Moving a parent changes the child's world matrix by exactly the new parent-to-old-parent delta",
          "[world][update_transforms]") {
  World world;
  const EntityId parent = world.createEntity();
  const EntityId child = world.createEntity();
  REQUIRE(world.setParent(child, parent).isOk());

  Transform childTransform;
  childTransform.localPosition = Vec3{1.0f, 0.0f, 0.0f};
  REQUIRE(world.setLocalTransform(child, childTransform).isOk());

  world.updateTransforms();
  const auto childWorldBefore = world.getWorldMatrix(child);
  REQUIRE(childWorldBefore.isOk());
  // Parent starts at identity -- child's world position should equal its
  // own local position.
  REQUIRE(std::abs(childWorldBefore.value()[12] - 1.0f) < kEpsilon);

  Transform newParentTransform;
  newParentTransform.localPosition = Vec3{5.0f, 2.0f, -3.0f};
  REQUIRE(world.setLocalTransform(parent, newParentTransform).isOk());
  world.updateTransforms();

  const auto childWorldAfter = world.getWorldMatrix(child);
  REQUIRE(childWorldAfter.isOk());
  // Predicted: parent's new translation composed with the child's own
  // unchanged local translation (no rotation/scale involved on either
  // side here, so this reduces to simple vector addition).
  REQUIRE(std::abs(childWorldAfter.value()[12] - 6.0f) < kEpsilon);
  REQUIRE(std::abs(childWorldAfter.value()[13] - 2.0f) < kEpsilon);
  REQUIRE(std::abs(childWorldAfter.value()[14] - (-3.0f)) < kEpsilon);
}

// V10: setParent() preserves the child's own getLocalTransform()
// byte-for-byte across a reparent; its getWorldMatrix() (after
// updateTransforms()) changes when, and only when, the old and new
// parent's own world matrices actually differ.
TEST_CASE("setParent() preserves the child's local transform, and world matrix changes iff the parent's does",
          "[world][update_transforms]") {
  World world;
  const EntityId oldParent = world.createEntity();
  const EntityId newParentSameTransform = world.createEntity();
  const EntityId newParentDifferentTransform = world.createEntity();
  const EntityId child = world.createEntity();

  // oldParent and newParentSameTransform share the identity transform;
  // newParentDifferentTransform does not.
  Transform differentTransform;
  differentTransform.localPosition = Vec3{10.0f, 0.0f, 0.0f};
  REQUIRE(world.setLocalTransform(newParentDifferentTransform, differentTransform).isOk());

  Transform childTransform;
  childTransform.localPosition = Vec3{1.0f, 2.0f, 3.0f};
  REQUIRE(world.setLocalTransform(child, childTransform).isOk());
  REQUIRE(world.setParent(child, oldParent).isOk());
  world.updateTransforms();
  const auto worldBefore = world.getWorldMatrix(child);
  REQUIRE(worldBefore.isOk());

  // Reparent to a parent with the SAME (identity) world transform:
  // getLocalTransform() must be byte-for-byte unchanged, and the world
  // matrix must not change either.
  REQUIRE(world.setParent(child, newParentSameTransform).isOk());
  const auto localAfterSameParent = world.getLocalTransform(child);
  REQUIRE(localAfterSameParent.isOk());
  REQUIRE(localAfterSameParent.value().localPosition.x == childTransform.localPosition.x);
  REQUIRE(localAfterSameParent.value().localPosition.y == childTransform.localPosition.y);
  REQUIRE(localAfterSameParent.value().localPosition.z == childTransform.localPosition.z);

  world.updateTransforms();
  const auto worldAfterSameParent = world.getWorldMatrix(child);
  REQUIRE(worldAfterSameParent.isOk());
  REQUIRE(matricesClose(worldBefore.value(), worldAfterSameParent.value()));

  // Reparent to a parent with a DIFFERENT world transform: local
  // transform still byte-for-byte unchanged, but the world matrix now
  // differs.
  REQUIRE(world.setParent(child, newParentDifferentTransform).isOk());
  const auto localAfterDifferentParent = world.getLocalTransform(child);
  REQUIRE(localAfterDifferentParent.isOk());
  REQUIRE(localAfterDifferentParent.value().localPosition.x == childTransform.localPosition.x);
  REQUIRE(localAfterDifferentParent.value().localPosition.y == childTransform.localPosition.y);
  REQUIRE(localAfterDifferentParent.value().localPosition.z == childTransform.localPosition.z);

  world.updateTransforms();
  const auto worldAfterDifferentParent = world.getWorldMatrix(child);
  REQUIRE(worldAfterDifferentParent.isOk());
  REQUIRE_FALSE(matricesClose(worldBefore.value(), worldAfterDifferentParent.value()));
}
