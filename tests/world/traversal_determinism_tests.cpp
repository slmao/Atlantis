#include <atlantis/world/entity_id.h>
#include <atlantis/world/renderable.h>
#include <atlantis/world/world.h>

#include <vector>

#include <catch2/catch_test_macros.hpp>

using atlantis::world::EntityId;
using atlantis::world::Renderable;
using atlantis::world::World;

namespace {

// A fixed sequence of createEntity()/destroyEntity() calls exercising
// the LIFO free list, returning the ascending-slot-index EntityId list
// renderableEntities() reports.
[[nodiscard]] std::vector<EntityId> runFixedSequence() {
  World world;
  const EntityId a = world.createEntity();
  const EntityId b = world.createEntity();
  const EntityId c = world.createEntity();
  static_cast<void>(world.destroyEntity(b));  // frees b's index
  const EntityId d = world.createEntity();    // reuses b's own index (LIFO)
  const EntityId e = world.createEntity();

  static_cast<void>(world.setRenderable(a, Renderable{.meshAsset = 1}));
  static_cast<void>(world.setRenderable(c, Renderable{.meshAsset = 3}));
  static_cast<void>(world.setRenderable(d, Renderable{.meshAsset = 4}));
  static_cast<void>(world.setRenderable(e, Renderable{.meshAsset = 5}));
  // b was destroyed and never re-added as Renderable -- its own reused
  // slot (now d) is a distinct entity with its own EntityId.

  return world.renderableEntities();
}

}  // namespace

// V14
TEST_CASE("renderableEntities() produces the exact same ascending-slot-index ordering across repeated, "
          "independent runs of the same fixed sequence",
          "[world][traversal_determinism]") {
  const std::vector<EntityId> first = runFixedSequence();
  const std::vector<EntityId> second = runFixedSequence();
  const std::vector<EntityId> third = runFixedSequence();

  REQUIRE(first.size() == 4);
  for (std::size_t i = 0; i + 1 < first.size(); ++i) {
    REQUIRE(first[i].index() < first[i + 1].index());  // strictly ascending
  }

  REQUIRE(first.size() == second.size());
  REQUIRE(first.size() == third.size());
  for (std::size_t i = 0; i < first.size(); ++i) {
    REQUIRE(first[i].index() == second[i].index());
    REQUIRE(first[i].generation() == second[i].generation());
    REQUIRE(first[i].index() == third[i].index());
    REQUIRE(first[i].generation() == third[i].generation());
  }
}
