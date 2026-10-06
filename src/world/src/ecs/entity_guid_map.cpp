#include <atlantis/world/ecs/entity_guid_map.h>

#include <algorithm>

#include <atlantis/world/ecs/world.h>

namespace atlantis::world::ecs {

using atlantis::asset_system::EntityGuid;

namespace detail {

// The one place allowed to build an EntityGuidMap and to ask a World whether
// it is iterating (world.h's friend).
struct GuidBinding {
  static atlantis::Result<EntityGuidMap, EcsError> create(World& world, std::span<const EntityGuid> guids) {
    using ResultT = atlantis::Result<EntityGuidMap, EcsError>;
    if (world.refuseIfIterating()) return ResultT::Err(EcsError::StructuralChangeDuringQuery);

    std::vector<EntityGuid> sorted(guids.begin(), guids.end());
    std::sort(sorted.begin(), sorted.end());
    for (const EntityGuid& guid : sorted) {
      if (guid == EntityGuid{}) return ResultT::Err(EcsError::NilGuid);
    }
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
      return ResultT::Err(EcsError::DuplicateGuid);
    }

    EntityGuidMap map;
    map.entries_.reserve(guids.size());
    for (const EntityGuid& guid : guids) map.entries_.emplace_back(guid, world.createEntity());
    std::sort(map.entries_.begin(), map.entries_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return ResultT::Ok(std::move(map));
  }
};

}  // namespace detail

std::optional<EntityId> EntityGuidMap::find(const EntityGuid& guid) const {
  const auto found = std::lower_bound(entries_.begin(), entries_.end(), guid,
                                      [](const auto& entry, const EntityGuid& key) { return entry.first < key; });
  if (found == entries_.end() || found->first != guid) return std::nullopt;
  return found->second;
}

atlantis::Result<EntityGuidMap, EcsError> createEntities(World& world, std::span<const EntityGuid> guids) {
  return detail::GuidBinding::create(world, guids);
}

}  // namespace atlantis::world::ecs
