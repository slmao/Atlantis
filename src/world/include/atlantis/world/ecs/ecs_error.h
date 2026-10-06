#pragma once

#include <string_view>

namespace atlantis::world::ecs {

// Plan 0050 P4 (Spec 0050 R1, R2, R6-R8): every recoverable ECS failure.
enum class EcsError {
  InvalidEntity,                // stale, retired, foreign-instance, or the sentinel
  ComponentMissing,             // get/set/remove of a component the entity lacks
  ComponentAlreadyPresent,      // add of a component the entity has
  StructuralChangeDuringQuery,  // a structural operation while a query runs (Plan 0050 J1)
  NilGuid,                      // createEntities(): a nil GUID
  DuplicateGuid,                // createEntities(): a GUID twice in one batch
};

[[nodiscard]] constexpr std::string_view toString(EcsError error) noexcept {
  switch (error) {
    case EcsError::InvalidEntity: return "InvalidEntity";
    case EcsError::ComponentMissing: return "ComponentMissing";
    case EcsError::ComponentAlreadyPresent: return "ComponentAlreadyPresent";
    case EcsError::StructuralChangeDuringQuery: return "StructuralChangeDuringQuery";
    case EcsError::NilGuid: return "NilGuid";
    case EcsError::DuplicateGuid: return "DuplicateGuid";
  }
  return "Unknown";
}

}  // namespace atlantis::world::ecs
