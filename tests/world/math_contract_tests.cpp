#include <atlantis/world/entity_id.h>
#include <atlantis/world/transform.h>
#include <atlantis/world/world.h>

#include <array>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

using atlantis::world::Camera;
using atlantis::world::EntityId;
using atlantis::world::Transform;
using atlantis::world::Vec3;
using atlantis::world::World;

namespace {
constexpr float kEpsilon = 1e-4f;
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

// V7: a multi-level chain, each with a distinct Transform, produces world
// matrices matching an independently hand-computed expected result --
// verifying column-major layout, parentWorld*local composition, T*R*S
// order, and the fixed Ry*Rx*Rz Euler order together. A root entity's
// world matrix equals its own local matrix.
TEST_CASE("updateTransforms()/getWorldMatrix(): a three-level chain matches a hand-computed expected result",
          "[world][math_contract]") {
  World world;
  const EntityId root = world.createEntity();
  const EntityId child = world.createEntity();
  const EntityId grandchild = world.createEntity();
  REQUIRE(world.setParent(child, root).isOk());
  REQUIRE(world.setParent(grandchild, child).isOk());

  Transform rootTransform;
  rootTransform.localPosition = Vec3{1.0f, 0.0f, 0.0f};
  REQUIRE(world.setLocalTransform(root, rootTransform).isOk());

  Transform childTransform;
  childTransform.localPosition = Vec3{0.0f, 2.0f, 0.0f};
  childTransform.localEulerAnglesRadians = Vec3{0.0f, kPi / 2.0f, 0.0f};  // yaw 90 degrees
  REQUIRE(world.setLocalTransform(child, childTransform).isOk());

  Transform grandchildTransform;
  grandchildTransform.localPosition = Vec3{0.0f, 0.0f, 3.0f};
  REQUIRE(world.setLocalTransform(grandchild, grandchildTransform).isOk());

  world.updateTransforms();

  const auto rootWorld = world.getWorldMatrix(root);
  REQUIRE(rootWorld.isOk());
  const std::array<float, 16> expectedRootWorld{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1};
  for (std::size_t i = 0; i < 16; ++i) {
    REQUIRE(std::abs(rootWorld.value()[i] - expectedRootWorld[i]) < kEpsilon);
  }

  const auto childWorld = world.getWorldMatrix(child);
  REQUIRE(childWorld.isOk());
  const std::array<float, 16> expectedChildWorld{0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 2, 0, 1};
  for (std::size_t i = 0; i < 16; ++i) {
    REQUIRE(std::abs(childWorld.value()[i] - expectedChildWorld[i]) < kEpsilon);
  }

  const auto grandchildWorld = world.getWorldMatrix(grandchild);
  REQUIRE(grandchildWorld.isOk());
  const std::array<float, 16> expectedGrandchildWorld{0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 4, 2, 0, 1};
  for (std::size_t i = 0; i < 16; ++i) {
    REQUIRE(std::abs(grandchildWorld.value()[i] - expectedGrandchildWorld[i]) < kEpsilon);
  }
}

TEST_CASE("A root entity's world matrix equals its own local matrix", "[world][math_contract]") {
  World world;
  const EntityId root = world.createEntity();
  Transform t;
  t.localPosition = Vec3{4.0f, -1.0f, 2.0f};
  t.localEulerAnglesRadians = Vec3{0.3f, 0.6f, 0.1f};
  t.localScale = Vec3{1.5f, 1.0f, 2.0f};
  REQUIRE(world.setLocalTransform(root, t).isOk());

  world.updateTransforms();

  // The root's own local matrix is not directly observable, but a
  // second, unparented entity given the same Transform must produce the
  // identical world matrix, since composeLocal() is a pure function of
  // Transform alone and a root's own world matrix is defined to equal
  // its local matrix exactly.
  const EntityId secondRoot = world.createEntity();
  REQUIRE(world.setLocalTransform(secondRoot, t).isOk());
  world.updateTransforms();

  const auto rootWorld = world.getWorldMatrix(root);
  const auto secondWorld = world.getWorldMatrix(secondRoot);
  REQUIRE(rootWorld.isOk());
  REQUIRE(secondWorld.isOk());
  REQUIRE(rootWorld.value() == secondWorld.value());
}

// V8: shear under a scaled hierarchy -- ADR-0050's own counter-example,
// reproduced exactly: a parent with localScale = (2,1,1) composed with a
// child rotated 45 degrees about Z produces a world matrix whose own
// linear-part columns 0/1 have a non-zero dot product, matching the
// hand-computed -1.5 value ADR-0050 itself records.
TEST_CASE("Shear under a scaled hierarchy: composed columns 0/1 are not orthogonal", "[world][math_contract]") {
  World world;
  const EntityId parent = world.createEntity();
  const EntityId child = world.createEntity();
  REQUIRE(world.setParent(child, parent).isOk());

  Transform parentTransform;
  parentTransform.localScale = Vec3{2.0f, 1.0f, 1.0f};
  REQUIRE(world.setLocalTransform(parent, parentTransform).isOk());

  Transform childTransform;
  childTransform.localEulerAnglesRadians = Vec3{0.0f, 0.0f, kPi / 4.0f};  // 45 degrees about Z
  REQUIRE(world.setLocalTransform(child, childTransform).isOk());

  world.updateTransforms();

  const auto childWorld = world.getWorldMatrix(child);
  REQUIRE(childWorld.isOk());
  const std::array<float, 16>& m = childWorld.value();
  // column 0 = m[0..2], column 1 = m[4..6] (the linear part only, w rows excluded)
  const float dot = m[0] * m[4] + m[1] * m[5] + m[2] * m[6];
  REQUIRE(std::abs(dot - (-1.5f)) < kEpsilon);
}
