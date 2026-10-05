#include <atlantis/schema.h>

namespace atlantis::schema {

std::string_view toString(TypeKind kind) noexcept {
  switch (kind) {
    case TypeKind::Primitive: return "Primitive";
    case TypeKind::Struct: return "Struct";
    case TypeKind::Enum: return "Enum";
  }
  return "Unknown";
}

std::string_view toString(PrimitiveKind kind) noexcept {
  switch (kind) {
    case PrimitiveKind::UInt64: return "UInt64";
    case PrimitiveKind::Float32: return "Float32";
    case PrimitiveKind::Vec3Float32: return "Vec3Float32";
    case PrimitiveKind::Vec4Float32: return "Vec4Float32";
    case PrimitiveKind::AssetGuid: return "AssetGuid";
    case PrimitiveKind::EntityGuid: return "EntityGuid";
  }
  return "Unknown";
}

}  // namespace atlantis::schema
