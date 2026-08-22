#include <atlantis/world/world.h>

#include <atlantis/assert.h>

#include <limits>
#include <utility>

namespace atlantis::world {

// Opaque, no data, no behavior -- only its own heap address matters, as
// this World instance's unique, address-stable token (ADR-0049's own
// Accepted Amendment). class, matching the forward declarations in
// entity_id.h/world.h -- MSVC treats a struct/class tag mismatch as a
// warning (C4099), promoted to a hard error by this repository's own
// /WX.
class WorldIdentity {};

// Transient, updateTransforms() only (Step 2) -- declared here because
// Slot's own visitState field needs it.
enum class SlotVisitState : std::uint8_t { NotVisited, Visiting, Visited };

struct Slot {
  bool alive = false;
  std::uint64_t generation = 0;
  EntityId parent = kInvalidEntityId;
  Transform localTransform;
  std::array<float, 16> cachedWorldMatrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // valid only after updateTransforms()
  std::optional<Camera> camera;
  std::optional<Renderable> renderable;
  SlotVisitState visitState = SlotVisitState::NotVisited;  // reset at the start of every updateTransforms() call
};

World::World() : identity_(std::make_unique<WorldIdentity>()) {}

World::~World() = default;

World::World(World&&) noexcept = default;

atlantis::Result<std::monostate, WorldError> World::validate(EntityId id) const {
  ATLANTIS_CHECK_MSG(identity_ != nullptr, "World::validate() called on a moved-from World");
  // World is EntityId's own friend -- reads the three private fields
  // directly.
  if (id.worldIdentity_ != nullptr && id.worldIdentity_ != identity_.get()) {
    return atlantis::Result<std::monostate, WorldError>::Err(WorldError::WrongWorld);
  }
  if (id.index_ >= slots_.size() || !slots_[id.index_].alive || slots_[id.index_].generation != id.generation_) {
    return atlantis::Result<std::monostate, WorldError>::Err(WorldError::InvalidEntity);
  }
  return atlantis::Result<std::monostate, WorldError>::Ok({});
}

EntityId World::createEntity() {
  ATLANTIS_CHECK_MSG(identity_ != nullptr, "World::createEntity() called on a moved-from World");

  std::uint32_t index;
  if (!freeList_.empty()) {
    index = freeList_.back();
    freeList_.pop_back();
  } else {
    index = static_cast<std::uint32_t>(slots_.size());
    slots_.emplace_back();
  }

  Slot& slot = slots_[index];
  slot.alive = true;
  slot.parent = kInvalidEntityId;
  slot.localTransform = Transform{};
  slot.camera.reset();
  slot.renderable.reset();
  // slot.generation is NOT touched here -- it was already advanced by
  // the destroyEntity() call that freed this index (or is still 0, for
  // a never-before-used index).

  return EntityId{index, slot.generation, identity_.get()};
}

atlantis::Result<std::monostate, WorldError> World::destroyEntity(EntityId id) {
  if (auto r = validate(id); r.isErr()) return r;

  // Collection phase: build the full transitive-descendant set before
  // mutating anything. See plans/0014-world-scene-foundation.md D3 for
  // the full correctness argument (no missed descendant, no double-
  // processing, no slot-reuse-mid-scan hazard).
  std::vector<EntityId> toDestroy{id};
  for (std::size_t i = 0; i < toDestroy.size(); ++i) {
    const EntityId current = toDestroy[i];
    for (std::uint32_t j = 0; j < slots_.size(); ++j) {
      if (slots_[j].alive && slots_[j].parent == current) {
        toDestroy.push_back(EntityId{j, slots_[j].generation, identity_.get()});
      }
    }
  }

  // Mutation phase: only after the entire collection above has finished.
  for (const EntityId& entry : toDestroy) {
    const std::uint32_t index = entry.index();
    if (activeCamera_.has_value() && *activeCamera_ == entry) {
      activeCamera_.reset();
    }
    Slot& slot = slots_[index];
    slot.alive = false;
    ++slot.generation;
    if (slot.generation != std::numeric_limits<std::uint64_t>::max()) {
      freeList_.push_back(index);
    }
    // If the tombstone value was just reached, the index is permanently
    // retired -- never pushed to freeList_ again (ADR-0049's own
    // Decision).
  }

  return atlantis::Result<std::monostate, WorldError>::Ok({});
}

bool World::isValid(EntityId id) const noexcept {
  ATLANTIS_CHECK_MSG(identity_ != nullptr, "World::isValid() called on a moved-from World");
  return (id.worldIdentity_ == nullptr || id.worldIdentity_ == identity_.get()) && id.index_ < slots_.size() &&
         slots_[id.index_].alive && slots_[id.index_].generation == id.generation_;
}

atlantis::Result<std::monostate, WorldError> World::setParent(EntityId child, EntityId parent) {
  if (auto r = validate(child); r.isErr()) return r;
  if (parent != kInvalidEntityId) {
    if (auto r = validate(parent); r.isErr()) return r;
    // Walk parent's own ancestor chain, including parent itself as the
    // zeroth step -- this single loop covers both "parent == child" (the
    // degenerate one-entity cycle) and every longer transitive cycle
    // with one algorithm, not two special cases.
    EntityId ancestor = parent;
    while (ancestor != kInvalidEntityId) {
      if (ancestor == child) {
        return atlantis::Result<std::monostate, WorldError>::Err(WorldError::WouldCreateCycle);
      }
      ancestor = slots_[ancestor.index()].parent;
    }
  }
  slots_[child.index()].parent = parent;
  return atlantis::Result<std::monostate, WorldError>::Ok({});
}

atlantis::Result<EntityId, WorldError> World::getParent(EntityId child) const {
  if (auto r = validate(child); r.isErr()) return atlantis::Result<EntityId, WorldError>::Err(r.error());
  return atlantis::Result<EntityId, WorldError>::Ok(slots_[child.index()].parent);
}

atlantis::Result<std::monostate, WorldError> World::setLocalTransform(EntityId id, Transform transform) {
  if (auto r = validate(id); r.isErr()) return r;
  slots_[id.index()].localTransform = transform;
  return atlantis::Result<std::monostate, WorldError>::Ok({});
}

atlantis::Result<Transform, WorldError> World::getLocalTransform(EntityId id) const {
  if (auto r = validate(id); r.isErr()) return atlantis::Result<Transform, WorldError>::Err(r.error());
  return atlantis::Result<Transform, WorldError>::Ok(slots_[id.index()].localTransform);
}

EntityId World::forceGenerationForTesting(EntityId id, std::uint64_t generation) {
  ATLANTIS_CHECK_MSG(identity_ != nullptr, "World::forceGenerationForTesting() called on a moved-from World");
  ATLANTIS_CHECK_MSG(id.index_ < slots_.size() && slots_[id.index_].alive,
                      "forceGenerationForTesting(): id does not name a live slot");
  slots_[id.index_].generation = generation;
  return EntityId{id.index_, generation, identity_.get()};
}

}  // namespace atlantis::world
