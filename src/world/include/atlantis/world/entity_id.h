#pragma once

#include <cstdint>
#include <limits>

namespace atlantis::world {

class World;          // forward declaration (friend)
class WorldIdentity;  // opaque forward declaration only -- full definition
                       // private to world.cpp; EntityId never dereferences it

// See specs/0014-world-scene-foundation.md and
// adr/0049-entity-identity-and-handle-invalidation.md (including its own
// Accepted Amendment -- stable World identity token). A non-owning,
// borrowed handle: it must not outlive the World instance that issued
// it, and must never be serialized, persisted, or used across a process
// boundary. All three fields are private, caller-immutable state -- only
// World may construct a non-default EntityId or read any of them, via
// the friend relationship below. Callers get default construction (the
// invalid sentinel), value equality, and -- where a real call site needs
// it -- the two read-only accessors; no accessor of any kind exists for
// the identity field, and no mutator of any kind exists for any field.
struct EntityId {
  EntityId() = default;

  [[nodiscard]] std::uint32_t index() const noexcept { return index_; }
  [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

  friend bool operator==(const EntityId&, const EntityId&) = default;

 private:
  friend class World;  // only World may construct a non-default EntityId
                        // or read any of its three private fields
  EntityId(std::uint32_t idx, std::uint64_t gen, const WorldIdentity* identity)
      : index_(idx), generation_(gen), worldIdentity_(identity) {}

  std::uint32_t index_ = std::numeric_limits<std::uint32_t>::max();
  std::uint64_t generation_ = 0;
  const WorldIdentity* worldIdentity_ = nullptr;
};

inline constexpr EntityId kInvalidEntityId{};  // index_ == max, generation_ == 0, worldIdentity_ == nullptr

}  // namespace atlantis::world
