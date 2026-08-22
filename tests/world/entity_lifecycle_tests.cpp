#include <atlantis/world/entity_id.h>
#include <atlantis/world/world.h>
#include <atlantis/world/world_error.h>

#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using atlantis::AssertFailureInfo;
using atlantis::world::EntityId;
using atlantis::world::EntityLifecycleTestAccess;
using atlantis::world::kInvalidEntityId;
using atlantis::world::World;
using atlantis::world::WorldError;

// Plan 0014 Deviations / D3: the one narrowly-scoped friend the Plan
// pre-authorized for V4's own generation-retirement boundary test. Only
// this translation unit is named as a friend of World. Declared inside
// atlantis::world to match World's own unqualified `friend struct
// EntityLifecycleTestAccess;` -- an unqualified friend declaration
// resolves (and, if not already declared, implicitly declares) the name
// in the enclosing namespace, not the global namespace.
namespace atlantis::world {
struct EntityLifecycleTestAccess {
  static EntityId forceGeneration(World& world, EntityId id, std::uint64_t generation) {
    return world.forceGenerationForTesting(id, generation);
  }
};
}  // namespace atlantis::world

// V1
TEST_CASE("createEntity() always succeeds and returns a valid handle; the first entity gets index 0, generation 0",
          "[world][entity_lifecycle]") {
  World world;
  const EntityId a = world.createEntity();
  REQUIRE(world.isValid(a));
  REQUIRE(a.index() == 0);
  REQUIRE(a.generation() == 0);
}

// V2 (same-World-instance behavior only; cross-instance behavior is V23-V25)
TEST_CASE("destroyEntity() invalidates the target and every transitive descendant in one call", "[world][entity_lifecycle]") {
  World world;
  const EntityId root = world.createEntity();
  const EntityId child = world.createEntity();
  const EntityId grandchild = world.createEntity();
  const EntityId unrelatedSibling = world.createEntity();
  REQUIRE(world.setParent(child, root).isOk());
  REQUIRE(world.setParent(grandchild, child).isOk());

  REQUIRE(world.destroyEntity(root).isOk());

  REQUIRE_FALSE(world.isValid(root));
  REQUIRE_FALSE(world.isValid(child));
  REQUIRE_FALSE(world.isValid(grandchild));
  REQUIRE(world.isValid(unrelatedSibling));

  REQUIRE(world.destroyEntity(root).isErr());
  REQUIRE(world.destroyEntity(root).error() == WorldError::InvalidEntity);
  REQUIRE(world.destroyEntity(child).error() == WorldError::InvalidEntity);
  REQUIRE(world.destroyEntity(grandchild).error() == WorldError::InvalidEntity);
}

TEST_CASE("destroyEntity() leaves an unrelated ancestor untouched when destroying a descendant",
          "[world][entity_lifecycle]") {
  World world;
  const EntityId root = world.createEntity();
  const EntityId child = world.createEntity();
  REQUIRE(world.setParent(child, root).isOk());

  REQUIRE(world.destroyEntity(child).isOk());

  REQUIRE(world.isValid(root));
  REQUIRE_FALSE(world.isValid(child));
}

// V3
TEST_CASE("Slot reuse: destroying and recreating produces a different generation() at the same index(), "
          "and the LIFO free list is directly observed",
          "[world][entity_lifecycle]") {
  World world;
  const EntityId a = world.createEntity();
  const EntityId b = world.createEntity();
  REQUIRE(world.destroyEntity(a).isOk());
  REQUIRE(world.destroyEntity(b).isOk());  // free list, LIFO: [a, b] -> next reuse is b, then a

  const EntityId reusedFirst = world.createEntity();
  REQUIRE(reusedFirst.index() == b.index());
  REQUIRE(reusedFirst.generation() != b.generation());

  const EntityId reusedSecond = world.createEntity();
  REQUIRE(reusedSecond.index() == a.index());
  REQUIRE(reusedSecond.generation() != a.generation());

  REQUIRE_FALSE(world.isValid(a));
  REQUIRE_FALSE(world.isValid(b));
}

// V4
TEST_CASE("Generation retirement at the real boundary: reaching the tombstone permanently retires the index",
          "[world][entity_lifecycle]") {
  World world;
  const EntityId originallyCreated = world.createEntity();
  const EntityId keepAlive = world.createEntity();  // occupies a different index throughout

  constexpr std::uint64_t kTombstone = std::numeric_limits<std::uint64_t>::max();
  // forceGenerationForTesting() returns a fresh handle reflecting the
  // forced generation -- originallyCreated itself is an immutable value
  // snapshot and does not change.
  const EntityId staleAtOldGeneration = originallyCreated;  // generation 0, now stale relative to the slot
  const EntityId id = EntityLifecycleTestAccess::forceGeneration(world, originallyCreated, kTombstone - 1);
  REQUIRE(world.destroyEntity(id).isOk());

  // (a) the slot's own generation is now the tombstone value: confirmed
  // indirectly -- an EntityId minted at (index, kTombstone) would be the
  // only value that could ever validate again, and createEntity() never
  // produces one (see (b)).
  REQUIRE_FALSE(world.isValid(id));

  // (b) exhausting every other free slot never reuses the retired index.
  const EntityId other1 = world.createEntity();  // reuses id's own index only if NOT retired
  REQUIRE(other1.index() != id.index());
  REQUIRE(world.isValid(keepAlive));

  // (c) the stale, pre-retirement handle still correctly reports
  // Err(InvalidEntity) via the same, unmodified check every other
  // stale-handle case already uses.
  const auto destroyStale = world.destroyEntity(staleAtOldGeneration);
  REQUIRE(destroyStale.isErr());
  REQUIRE(destroyStale.error() == WorldError::InvalidEntity);
}

// V6 (entity-lifecycle half)
TEST_CASE("destroyEntity()/setLocalTransform() on an invalid handle leaves every other entity's state unchanged",
          "[world][entity_lifecycle]") {
  World world;
  const EntityId a = world.createEntity();
  const EntityId stale = world.createEntity();
  REQUIRE(world.destroyEntity(stale).isOk());

  REQUIRE(world.destroyEntity(stale).isErr());
  REQUIRE(world.isValid(a));

  atlantis::world::Transform t;
  t.localPosition.x = 5.0f;
  REQUIRE(world.setLocalTransform(stale, t).isErr());
  // a's own transform (default-constructed) is unaffected by the failed
  // call against a completely different, stale handle.
  REQUIRE(world.getLocalTransform(a).value().localPosition.x == 0.0f);
}

// V22
namespace atlantis::world {
static_assert(std::is_move_constructible_v<World>);
static_assert(!std::is_copy_constructible_v<World>);
static_assert(!std::is_move_assignable_v<World>);
static_assert(!std::is_copy_assignable_v<World>);
}  // namespace atlantis::world

// V13 (by-value access): every public getter returns a plain value, never a reference/pointer.
static_assert(!std::is_reference_v<decltype(std::declval<const World&>().getLocalTransform(kInvalidEntityId))> &&
              !std::is_pointer_v<decltype(std::declval<const World&>().getLocalTransform(kInvalidEntityId))>);
static_assert(!std::is_reference_v<decltype(std::declval<const World&>().getWorldMatrix(kInvalidEntityId))> &&
              !std::is_pointer_v<decltype(std::declval<const World&>().getWorldMatrix(kInvalidEntityId))>);

// V23
TEST_CASE("Two simultaneously live World instances' own first entities correctly cross-reject via Err(WrongWorld)",
          "[world][entity_lifecycle]") {
  World a;
  World b;
  const EntityId fromA = a.createEntity();
  const EntityId fromB = b.createEntity();

  // Both are {index=0, generation=0} -- differing only in their private identity.
  REQUIRE(fromA.index() == fromB.index());
  REQUIRE(fromA.generation() == fromB.generation());

  const auto bRejectsA = b.destroyEntity(fromA);
  REQUIRE(bRejectsA.isErr());
  REQUIRE(bRejectsA.error() == WorldError::WrongWorld);

  const auto aRejectsB = a.destroyEntity(fromB);
  REQUIRE(aRejectsB.isErr());
  REQUIRE(aRejectsB.error() == WorldError::WrongWorld);

  // Neither World actually mutated the other's entity.
  REQUIRE(a.isValid(fromA));
  REQUIRE(b.isValid(fromB));
}

// V24 (entity-lifecycle portion, Step 1 subset: setParent()/getParent()
// covered in hierarchy_tests.cpp; getWorldMatrix()/Camera/Renderable
// entry points are not defined until Steps 2/3 -- see camera_tests.cpp's
// own "V24 (remaining methods)" case added in Step 3).
TEST_CASE("WrongWorld is reachable from destroyEntity()/setLocalTransform()/getLocalTransform()",
          "[world][entity_lifecycle]") {
  World a;
  World b;
  const EntityId fromA = a.createEntity();

  REQUIRE(b.isValid(fromA) == false);
  REQUIRE(b.destroyEntity(fromA).error() == WorldError::WrongWorld);
  REQUIRE(b.setLocalTransform(fromA, atlantis::world::Transform{}).error() == WorldError::WrongWorld);
  REQUIRE(b.getLocalTransform(fromA).error() == WorldError::WrongWorld);

  // kInvalidEntityId is never treated as WrongWorld.
  REQUIRE(b.destroyEntity(kInvalidEntityId).error() == WorldError::InvalidEntity);
}

// V25
TEST_CASE("EntityId validity survives World move-construction", "[world][entity_lifecycle]") {
  World original;
  const EntityId root = original.createEntity();
  const EntityId child = original.createEntity();
  REQUIRE(original.setParent(child, root).isOk());

  World moved = std::move(original);

  REQUIRE(moved.isValid(root));
  REQUIRE(moved.isValid(child));
  REQUIRE(moved.getParent(child).value() == root);
}

// V26 (Step 1 portion: createEntity() and an EntityId-accepting method
// via validate()/isValid() -- updateTransforms()/clearActiveCamera()/
// activeCamera() are not defined until Steps 2/3; the remaining portion
// of this same guard is exercised once they exist, see
// camera_tests.cpp's own "V26 (remaining methods)" case added in Step 3).
TEST_CASE("Moved-from World: createEntity() and isValid() are checked programmer errors",
          "[world][entity_lifecycle]") {
  World original;
  World moved = std::move(original);  // original is now moved-from

  std::vector<AssertFailureInfo> recorded;
  auto previous = atlantis::assertions::setFailureHandler(
      [&recorded](const AssertFailureInfo& info) { recorded.push_back(info); });

  static_cast<void>(original.createEntity());
  REQUIRE(recorded.size() == 1);

  static_cast<void>(original.isValid(kInvalidEntityId));
  REQUIRE(recorded.size() == 2);

  atlantis::assertions::setFailureHandler(std::move(previous));
}

// V27
TEST_CASE("EntityId's full encapsulation: trivially copyable, equality covers all three fields, "
          "no fixed sizeof promised",
          "[world][entity_lifecycle]") {
  static_assert(std::is_trivially_copyable_v<EntityId>);

  World a;
  World b;
  const EntityId x = a.createEntity();
  const EntityId y = a.createEntity();

  REQUIRE(x != y);  // index differs
  REQUIRE(x == x);

  const EntityId xInB = b.createEntity();  // same index/generation as x, different identity
  REQUIRE(x != xInB);

  REQUIRE(kInvalidEntityId == kInvalidEntityId);
  REQUIRE(x != kInvalidEntityId);
}
