#pragma once

#include <string_view>

namespace atlantis::world::access {

// Spec 0052 R3/R4/R8 (Plan 0052 P5): why a query or a command was refused.
// A refused command has no effect (ruling Q9). Plain enumeration; no state.
enum class AccessError {
  UnknownEntity,            // no live entity with that GUID
  NilGuid,                  // CreateEntity with the nil GUID
  DuplicateGuid,            // CreateEntity with a GUID already in use
  UnknownComponentType,     // the TypeId is not a World component type
  ComponentMissing,         // the entity has no such component
  ComponentAlreadyPresent,  // AddComponent of a component the entity has
  UnknownField,             // the FieldId names no leaf reachable from the component
  FieldNotEditable,         // the field is not flagged Editable
  KindMismatch,             // the value's kind is not the field's (Absent only for Optional)
  EnumValueOutOfRange,      // an enum value that is not one of its declared constants
  NonFiniteValue,           // a NaN or infinite float
  LightLimitExceeded,       // > 1 Directional or > 64 Point lights (ruling Q6; Plan 0052 J1)
  ActiveCameraProtected,    // removing the active camera or its Camera/WorldMatrix (Plan 0052 J3)
};

[[nodiscard]] std::string_view toString(AccessError error) noexcept;

}  // namespace atlantis::world::access
