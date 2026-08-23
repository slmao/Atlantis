#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/result.h>
#include <atlantis/world/camera.h>
#include <atlantis/world/entity_id.h>
#include <atlantis/world/renderable.h>
#include <atlantis/world/transform.h>
#include <atlantis/world/world_error.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace atlantis::world {

class WorldIdentity;  // opaque; complete definition private to world.cpp
struct Slot;          // complete definition private to world.cpp

// See specs/0014-world-scene-foundation.md and
// adr/0048-0051-*.md. World owns every entity and component outright --
// no reference or pointer into its own internal storage ever crosses
// this public API; every accessor returns by value. Move-constructible,
// not copyable, not move-assignable (see world.cpp's own constructor/
// destructor for why -- the identity token below is the reason).
//
// A moved-from World guarantees only that it remains destructible or may
// be move-constructed from again; every other call is a checked
// programmer error (ADR-0049's own Accepted Amendment), not a Result.
class World {
 public:
  World();   // defined in world.cpp -- allocates the identity token,
             // which requires WorldIdentity's complete definition
  ~World();  // defined in world.cpp for the identical reason

  World(const World&) = delete;
  World& operator=(const World&) = delete;
  // Declared here, defined (= default) in world.cpp -- not inlined.
  // Slot (like WorldIdentity) is complete only there: MSVC's own
  // std::vector<Slot> generates its defaulted move constructor's
  // exception-unwind path against Slot's own destructor, which requires
  // Slot to be complete at the point this special member is compiled.
  World(World&&) noexcept;
  World& operator=(World&&) = delete;

  [[nodiscard]] EntityId createEntity();
  [[nodiscard]] atlantis::Result<std::monostate, WorldError> destroyEntity(EntityId id);
  [[nodiscard]] bool isValid(EntityId id) const noexcept;

  [[nodiscard]] atlantis::Result<std::monostate, WorldError> setParent(EntityId child, EntityId parent);
  [[nodiscard]] atlantis::Result<EntityId, WorldError> getParent(EntityId child) const;

  [[nodiscard]] atlantis::Result<std::monostate, WorldError> setLocalTransform(EntityId id, Transform transform);
  [[nodiscard]] atlantis::Result<Transform, WorldError> getLocalTransform(EntityId id) const;

  void updateTransforms();
  [[nodiscard]] atlantis::Result<std::array<float, 16>, WorldError> getWorldMatrix(EntityId id) const;

  [[nodiscard]] atlantis::Result<std::monostate, WorldError> setCamera(EntityId id, Camera camera);
  [[nodiscard]] atlantis::Result<std::monostate, WorldError> removeCamera(EntityId id);
  [[nodiscard]] atlantis::Result<Camera, WorldError> getCamera(EntityId id) const;

  [[nodiscard]] atlantis::Result<std::monostate, WorldError> setActiveCamera(EntityId id);
  void clearActiveCamera() noexcept;
  [[nodiscard]] std::optional<EntityId> activeCamera() const noexcept;

  [[nodiscard]] atlantis::Result<std::monostate, WorldError> setRenderable(EntityId id, Renderable renderable);
  [[nodiscard]] atlantis::Result<std::monostate, WorldError> removeRenderable(EntityId id);
  [[nodiscard]] atlantis::Result<Renderable, WorldError> getRenderable(EntityId id) const;

  // Ascending slot-index order -- a fresh std::vector snapshot each call,
  // valid as of the call, not a live iterator held across a subsequent
  // World mutation.
  [[nodiscard]] std::vector<EntityId> renderableEntities() const;

 private:
  // Identity checked before slot/generation, moved-from state checked
  // before either -- every EntityId-accepting method routes through
  // this first.
  [[nodiscard]] atlantis::Result<std::monostate, WorldError> validate(EntityId id) const;

  // Plan 0014 Deviations: "a documented, narrowly-scoped friend
  // declaration naming the one test translation unit that needs it" for
  // V4's own generation-retirement boundary test -- the capability
  // (direct, test-only generation mutation) is Plan-fixed; this
  // declaration is the Implementation-time C++ spelling of it. Forces
  // the alive slot id names to the given generation, for exercising the
  // tombstone boundary without waiting for 2^64 real destroy/reuse
  // cycles.
  friend struct EntityLifecycleTestAccess;
  // Returns a fresh EntityId reflecting the forced generation -- id
  // itself is an immutable value snapshot, so forcing the underlying
  // slot's generation does not retroactively change what id.generation()
  // reports; the caller needs the returned handle to exercise the new
  // generation.
  [[nodiscard]] EntityId forceGenerationForTesting(EntityId id, std::uint64_t generation);

  std::unique_ptr<WorldIdentity> identity_;  // this instance's own stable, address-stable token
  std::vector<Slot> slots_;
  std::vector<std::uint32_t> freeList_;  // LIFO stack: push_back()/pop_back() only
  std::optional<EntityId> activeCamera_;
};

}  // namespace atlantis::world
