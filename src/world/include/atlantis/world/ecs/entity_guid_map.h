#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/result.h>
#include <atlantis/world/ecs/ecs_error.h>
#include <atlantis/world/ecs/entity_id.h>

#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace atlantis::world::ecs {

class World;

namespace detail {
struct GuidBinding;
}

// Spec 0050 R2 / ADR-0101 D4 (ruling Q4 (G1), (P-a)): EntityGuid, the asset
// layer's persistent identity (ADR-0097 D5), meets the ECS only here, at
// creation. createEntities() makes one new entity per GUID and returns this
// caller-owned, immutable snapshot; the ECS stores no GUID. Liveness stays
// the World's: after a destroy, find() still returns the old handle, which
// isValid() then rejects. A plain value; safe for concurrent reads.
class EntityGuidMap {
 public:
  [[nodiscard]] std::optional<EntityId> find(const atlantis::asset_system::EntityGuid& guid) const;
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

 private:
  friend struct detail::GuidBinding;
  std::vector<std::pair<atlantis::asset_system::EntityGuid, EntityId>> entries_;  // sorted by GUID
};

// All or nothing (Plan 0050 P6): a nil GUID (NilGuid) or one given twice
// (DuplicateGuid) fails before any entity is created, as does a call during
// a query (StructuralChangeDuringQuery, Plan 0050 J1). Otherwise one entity
// per GUID, with no components, in span order. Not thread-safe (ADR-0004).
[[nodiscard]] atlantis::Result<EntityGuidMap, EcsError> createEntities(
    World& world, std::span<const atlantis::asset_system::EntityGuid> guids);

}  // namespace atlantis::world::ecs
