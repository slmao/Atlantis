#include <atlantis/world/access/runtime_world_access.h>

#include <atlantis/assert.h>

namespace atlantis::world::access {

std::string_view toString(AccessError error) noexcept {
  switch (error) {
    case AccessError::UnknownEntity: return "UnknownEntity";
    case AccessError::NilGuid: return "NilGuid";
    case AccessError::DuplicateGuid: return "DuplicateGuid";
    case AccessError::UnknownComponentType: return "UnknownComponentType";
    case AccessError::ComponentMissing: return "ComponentMissing";
    case AccessError::ComponentAlreadyPresent: return "ComponentAlreadyPresent";
    case AccessError::UnknownField: return "UnknownField";
    case AccessError::FieldNotEditable: return "FieldNotEditable";
    case AccessError::KindMismatch: return "KindMismatch";
    case AccessError::EnumValueOutOfRange: return "EnumValueOutOfRange";
    case AccessError::NonFiniteValue: return "NonFiniteValue";
    case AccessError::LightLimitExceeded: return "LightLimitExceeded";
    case AccessError::ActiveCameraProtected: return "ActiveCameraProtected";
  }
  ATLANTIS_CHECK_MSG(false, "toString(AccessError): unhandled enumerator");
  return "(unrecognized AccessError)";
}

}  // namespace atlantis::world::access
