#pragma once

#include <cstdint>
#include <limits>

namespace atlantis::world::ecs {

class World;
class EcsWorldIdentity;  // opaque; complete definition private to ecs/world.cpp

// Spec 0050 R1 / ADR-0101 D4 (ruling Q4 (a)): the ECS's own entity handle,
// under ADR-0049's rules unchanged -- an index, a 64-bit generation, and the
// issuing ecs::World's identity token, so a stale, retired or foreign handle
// is detected, never dereferenced. A non-owning, borrowed handle: it must not
// outlive the World that issued it and must never be serialized, persisted or
// used across a process boundary. Only ecs::World constructs a non-default
// one. A plain value; safe for concurrent use.
struct EntityId {
  EntityId() = default;

  [[nodiscard]] std::uint32_t index() const noexcept { return index_; }
  [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

  friend bool operator==(const EntityId&, const EntityId&) = default;

 private:
  friend class World;
  EntityId(std::uint32_t index, std::uint64_t generation, const EcsWorldIdentity* identity)
      : index_(index), generation_(generation), identity_(identity) {}

  std::uint32_t index_ = std::numeric_limits<std::uint32_t>::max();
  std::uint64_t generation_ = 0;
  const EcsWorldIdentity* identity_ = nullptr;
};

inline constexpr EntityId kInvalidEntityId{};

}  // namespace atlantis::world::ecs
