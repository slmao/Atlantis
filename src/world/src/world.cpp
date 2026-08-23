#include <atlantis/world/world.h>

#include <atlantis/assert.h>

#include <cmath>
#include <cstddef>
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

namespace {

using Mat4 = std::array<float, 16>;

constexpr Mat4 kIdentityMatrix4{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// ADR-0050's own Math contract: column-major, index col*4+row.
// result = a . b, the exact same element formula
// examples/minimal_renderer_demo/main.cpp's own multiply() already uses
// -- reused verbatim as this module's own private implementation, not
// called across the module boundary.
[[nodiscard]] Mat4 multiply(const Mat4& a, const Mat4& b) {
  Mat4 result{};
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) {
        sum += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(col * 4 + k)];
      }
      result[static_cast<std::size_t>(col * 4 + row)] = sum;
    }
  }
  return result;
}

[[nodiscard]] Mat4 translationMatrix(const Vec3& t) {
  Mat4 result = kIdentityMatrix4;
  result[12] = t.x;
  result[13] = t.y;
  result[14] = t.z;
  return result;
}

[[nodiscard]] Mat4 scaleMatrix(const Vec3& s) {
  Mat4 result = kIdentityMatrix4;
  result[0] = s.x;
  result[5] = s.y;
  result[10] = s.z;
  return result;
}

[[nodiscard]] Mat4 rotationX(float theta) {  // rotates Y/Z about +X
  const float c = std::cos(theta), s = std::sin(theta);
  return {1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1};
}

[[nodiscard]] Mat4 rotationY(float theta) {  // rotates X/Z about +Y
  const float c = std::cos(theta), s = std::sin(theta);
  return {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
}

[[nodiscard]] Mat4 rotationZ(float theta) {  // rotates X/Y about +Z
  const float c = std::cos(theta), s = std::sin(theta);
  return {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// R = Ry(yaw) * Rx(pitch) * Rz(roll) -- ADR-0050's own fixed Euler order.
[[nodiscard]] Mat4 eulerRotation(const Vec3& radians) {
  return multiply(rotationY(radians.y), multiply(rotationX(radians.x), rotationZ(radians.z)));
}

// local = T * R * S.
[[nodiscard]] Mat4 composeLocal(const Transform& t) {
  return multiply(translationMatrix(t.localPosition), multiply(eulerRotation(t.localEulerAnglesRadians),
                                                                  scaleMatrix(t.localScale)));
}

}  // namespace

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

// Memoized, fully iterative traversal (no C++ recursion, no call-stack
// depth tied to hierarchy depth) -- doubles as the defense-in-depth
// cycle guard (setParent()'s own cycle check is the primary
// prevention). Any traversal order satisfying "visit a parent before
// its child" produces identical results; this is one such order, not
// part of World's own public contract.
void World::updateTransforms() {
  ATLANTIS_CHECK_MSG(identity_ != nullptr, "World::updateTransforms() called on a moved-from World");

  for (auto& slot : slots_) slot.visitState = SlotVisitState::NotVisited;
  std::vector<std::uint32_t> path;  // reused scratch buffer -- heap-allocated, not call-stack depth
  for (std::uint32_t i = 0; i < slots_.size(); ++i) {
    if (!slots_[i].alive || slots_[i].visitState == SlotVisitState::Visited) continue;

    // Walk up from i, collecting the not-yet-visited prefix of its own
    // ancestor chain, stopping at a root or at an already-Visited
    // ancestor (whose own cachedWorldMatrix is already correct).
    path.clear();
    std::uint32_t current = i;
    while (true) {
      Slot& s = slots_[current];
      if (s.visitState == SlotVisitState::Visited) break;
      ATLANTIS_CHECK_MSG(s.visitState != SlotVisitState::Visiting,
                          "updateTransforms(): cycle detected -- setParent()'s own prevention has a bug");
      s.visitState = SlotVisitState::Visiting;  // "on the current walk-up path" -- the cycle guard
      path.push_back(current);
      if (s.parent == kInvalidEntityId) break;
      current = s.parent.index();
    }

    // Process outermost-unvisited-ancestor-first, so each entity's own
    // parent world matrix is already known by the time it is computed --
    // an ordinary loop, no recursive call.
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
      Slot& s = slots_[*it];
      const std::array<float, 16> local = composeLocal(s.localTransform);
      s.cachedWorldMatrix =
          (s.parent == kInvalidEntityId) ? local : multiply(slots_[s.parent.index()].cachedWorldMatrix, local);
      s.visitState = SlotVisitState::Visited;
    }
  }
}

atlantis::Result<std::array<float, 16>, WorldError> World::getWorldMatrix(EntityId id) const {
  if (auto r = validate(id); r.isErr()) return atlantis::Result<std::array<float, 16>, WorldError>::Err(r.error());
  return atlantis::Result<std::array<float, 16>, WorldError>::Ok(slots_[id.index()].cachedWorldMatrix);
}

atlantis::Result<std::monostate, WorldError> World::setCamera(EntityId id, Camera camera) {
  if (auto r = validate(id); r.isErr()) return r;
  slots_[id.index()].camera = camera;
  return atlantis::Result<std::monostate, WorldError>::Ok({});
}

atlantis::Result<std::monostate, WorldError> World::removeCamera(EntityId id) {
  if (auto r = validate(id); r.isErr()) return r;
  slots_[id.index()].camera.reset();
  if (activeCamera_.has_value() && *activeCamera_ == id) {
    activeCamera_.reset();
  }
  return atlantis::Result<std::monostate, WorldError>::Ok({});
}

atlantis::Result<Camera, WorldError> World::getCamera(EntityId id) const {
  if (auto r = validate(id); r.isErr()) return atlantis::Result<Camera, WorldError>::Err(r.error());
  const Slot& slot = slots_[id.index()];
  if (!slot.camera.has_value()) return atlantis::Result<Camera, WorldError>::Err(WorldError::NoCameraComponent);
  return atlantis::Result<Camera, WorldError>::Ok(*slot.camera);
}

atlantis::Result<std::monostate, WorldError> World::setActiveCamera(EntityId id) {
  if (auto r = validate(id); r.isErr()) return r;
  if (!slots_[id.index()].camera.has_value()) {
    return atlantis::Result<std::monostate, WorldError>::Err(WorldError::NoCameraComponent);
  }
  activeCamera_ = id;
  return atlantis::Result<std::monostate, WorldError>::Ok({});
}

void World::clearActiveCamera() noexcept {
  ATLANTIS_CHECK_MSG(identity_ != nullptr, "World::clearActiveCamera() called on a moved-from World");
  activeCamera_.reset();
}

std::optional<EntityId> World::activeCamera() const noexcept {
  ATLANTIS_CHECK_MSG(identity_ != nullptr, "World::activeCamera() called on a moved-from World");
  return activeCamera_;
}

atlantis::Result<std::monostate, WorldError> World::setRenderable(EntityId id, Renderable renderable) {
  if (auto r = validate(id); r.isErr()) return r;
  slots_[id.index()].renderable = renderable;
  return atlantis::Result<std::monostate, WorldError>::Ok({});
}

atlantis::Result<std::monostate, WorldError> World::removeRenderable(EntityId id) {
  if (auto r = validate(id); r.isErr()) return r;
  slots_[id.index()].renderable.reset();
  return atlantis::Result<std::monostate, WorldError>::Ok({});
}

atlantis::Result<Renderable, WorldError> World::getRenderable(EntityId id) const {
  if (auto r = validate(id); r.isErr()) return atlantis::Result<Renderable, WorldError>::Err(r.error());
  const Slot& slot = slots_[id.index()];
  if (!slot.renderable.has_value()) {
    return atlantis::Result<Renderable, WorldError>::Err(WorldError::NoRenderableComponent);
  }
  return atlantis::Result<Renderable, WorldError>::Ok(*slot.renderable);
}

std::vector<EntityId> World::renderableEntities() const {
  ATLANTIS_CHECK_MSG(identity_ != nullptr, "World::renderableEntities() called on a moved-from World");
  std::vector<EntityId> result;
  for (std::uint32_t i = 0; i < slots_.size(); ++i) {
    if (slots_[i].alive && slots_[i].renderable.has_value()) {
      result.push_back(EntityId{i, slots_[i].generation, identity_.get()});
    }
  }
  return result;
}

EntityId World::forceGenerationForTesting(EntityId id, std::uint64_t generation) {
  ATLANTIS_CHECK_MSG(identity_ != nullptr, "World::forceGenerationForTesting() called on a moved-from World");
  ATLANTIS_CHECK_MSG(id.index_ < slots_.size() && slots_[id.index_].alive,
                      "forceGenerationForTesting(): id does not name a live slot");
  slots_[id.index_].generation = generation;
  return EntityId{id.index_, generation, identity_.get()};
}

}  // namespace atlantis::world
