#include "property_access.h"

#include <atlantis/assert.h>
#include <atlantis/world/light.h>
#include <atlantis/world/renderable.h>
#include <atlantis/world/vec3.h>
#include <atlantis/world/world_schema.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace atlantis::world::access::detail {

namespace {

using atlantis::asset_system::AssetGuid;
using atlantis::asset_system::EntityGuid;
using schema::FieldDescriptor;
using schema::FieldFlags;
using schema::PrimitiveKind;
using schema::TypeDescriptor;
using schema::TypeKind;

// Ruling Q2: the enum convention, compiled by MSVC and by the Android NDK's
// Clang (this file is in both builds), so both toolchains hold it. A new
// described enum gets its own assertion here and an entry in kInt32Enums.
static_assert(std::is_same_v<std::underlying_type_t<LightKind>, std::int32_t>,
              "Plan 0052 P3: described enums are stored as std::int32_t");
static_assert(sizeof(LightKind) == sizeof(std::int32_t));

// PropertyValue's vector alternatives have the shape the descriptor kinds
// describe (three or four contiguous floats).
static_assert(sizeof(std::array<float, 3>) == schema::primitiveSize(PrimitiveKind::Vec3Float32));
static_assert(sizeof(Vec3) == schema::primitiveSize(PrimitiveKind::Vec3Float32));
static_assert(sizeof(std::array<float, 4>) == schema::primitiveSize(PrimitiveKind::Vec4Float32));
static_assert(sizeof(AssetGuid) == schema::primitiveSize(PrimitiveKind::AssetGuid));
static_assert(sizeof(EntityGuid) == schema::primitiveSize(PrimitiveKind::EntityGuid));

constexpr std::array<schema::TypeId, 1> kInt32Enums{schema::typeId("world::LightKind")};

PropertyValue readMaterialAsset(const std::byte* component) {
  const auto& renderable = *reinterpret_cast<const Renderable*>(component);
  if (!renderable.materialAsset.has_value()) return Absent{};
  return std::uint64_t{*renderable.materialAsset};
}

void writeMaterialAsset(std::byte* component, const PropertyValue& value) {
  auto& renderable = *reinterpret_cast<Renderable*>(component);
  if (std::holds_alternative<Absent>(value)) {
    renderable.materialAsset.reset();
  } else {
    renderable.materialAsset = std::get<std::uint64_t>(value);
  }
}

const std::array<OptionalFieldAccessor, 1> kOptionalFields{
    OptionalFieldAccessor{schema::typeId("world::Renderable"), schema::fieldId("world::Renderable", "materialAsset"),
                          static_cast<std::uint32_t>(offsetof(Renderable, materialAsset)), &readMaterialAsset,
                          &writeMaterialAsset},
};

[[nodiscard]] const TypeDescriptor* findWorldType(schema::TypeId id) {
  for (const TypeDescriptor& type : worldSchema()) {
    if (type.id == id) return &type;
  }
  return nullptr;
}

[[nodiscard]] const OptionalFieldAccessor* findOptional(schema::TypeId component, schema::FieldId field) {
  for (const OptionalFieldAccessor& entry : kOptionalFields) {
    if (entry.component == component && entry.field == field) return &entry;
  }
  return nullptr;
}

void findLeaf(const TypeDescriptor& type, schema::FieldId field, std::uint32_t base, int& matches,
              ResolvedField& out) {
  for (const FieldDescriptor& candidate : type.fields) {
    if (candidate.kind == TypeKind::Struct) {
      const TypeDescriptor* nested = findWorldType(candidate.type);
      ATLANTIS_CHECK_MSG(nested != nullptr, "resolveField(): a Struct field's type is not in worldSchema()");
      findLeaf(*nested, field, base + candidate.byteOffset, matches, out);
    } else if (candidate.id == field) {
      ++matches;
      out = ResolvedField{candidate.kind, candidate.primitive, candidate.type, candidate.flags,
                          base + candidate.byteOffset};
    }
  }
}

[[nodiscard]] std::size_t expectedAlternative(const ResolvedField& field) {
  if (field.kind == TypeKind::Enum) return 6;  // EnumValue
  switch (field.primitive) {
    case PrimitiveKind::UInt64: return 0;
    case PrimitiveKind::Float32: return 1;
    case PrimitiveKind::Vec3Float32: return 2;
    case PrimitiveKind::Vec4Float32: return 3;
    case PrimitiveKind::AssetGuid: return 4;
    case PrimitiveKind::EntityGuid: return 5;
  }
  ATLANTIS_CHECK_MSG(false, "expectedAlternative(): unhandled PrimitiveKind");
  return 0;
}

template <std::size_t N>
[[nodiscard]] bool allFinite(const std::array<float, N>& values) {
  return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
}

}  // namespace

atlantis::Result<ResolvedField, AccessError> resolveField(schema::TypeId component, schema::FieldId field) {
  using ResultT = atlantis::Result<ResolvedField, AccessError>;
  if (!visitComponentType(component, [](auto) {})) return ResultT::Err(AccessError::UnknownComponentType);
  const TypeDescriptor* type = findWorldType(component);
  ATLANTIS_CHECK_MSG(type != nullptr, "resolveField(): a World component type is not described in worldSchema()");
  int matches = 0;
  ResolvedField resolved;
  findLeaf(*type, field, 0, matches, resolved);
  if (matches != 1) return ResultT::Err(AccessError::UnknownField);
  return ResultT::Ok(resolved);
}

atlantis::Result<std::monostate, AccessError> checkValue(const ResolvedField& field, const PropertyValue& value) {
  using ResultT = atlantis::Result<std::monostate, AccessError>;
  if (!schema::hasFlags(field.flags, FieldFlags::Editable)) return ResultT::Err(AccessError::FieldNotEditable);
  if (std::holds_alternative<Absent>(value)) {
    return schema::hasFlags(field.flags, FieldFlags::Optional) ? ResultT::Ok({})
                                                               : ResultT::Err(AccessError::KindMismatch);
  }
  if (value.index() != expectedAlternative(field)) return ResultT::Err(AccessError::KindMismatch);
  if (const auto* enumValue = std::get_if<EnumValue>(&value)) {
    const TypeDescriptor* enumType = findWorldType(field.enumType);
    ATLANTIS_CHECK_MSG(enumType != nullptr, "checkValue(): an Enum field's type is not in worldSchema()");
    const bool declared = std::any_of(enumType->constants.begin(), enumType->constants.end(),
                                      [&](const auto& constant) { return constant.value == enumValue->value; });
    if (!declared) return ResultT::Err(AccessError::EnumValueOutOfRange);
  }
  bool finite = true;
  if (const auto* f = std::get_if<float>(&value)) finite = std::isfinite(*f);
  if (const auto* v3 = std::get_if<std::array<float, 3>>(&value)) finite = allFinite(*v3);
  if (const auto* v4 = std::get_if<std::array<float, 4>>(&value)) finite = allFinite(*v4);
  if (!finite) return ResultT::Err(AccessError::NonFiniteValue);
  return ResultT::Ok({});
}

PropertyValue readField(const std::byte* component, schema::TypeId type, schema::FieldId field,
                        const ResolvedField& resolved) {
  if (const OptionalFieldAccessor* optional = findOptional(type, field)) return optional->read(component);
  ATLANTIS_CHECK_MSG(!schema::hasFlags(resolved.flags, FieldFlags::Optional),
                      "readField(): an Optional field has no typed accessor (Plan 0052 P3)");
  const std::byte* at = component + resolved.byteOffset;
  if (resolved.kind == TypeKind::Enum) {
    std::int32_t raw = 0;
    std::memcpy(&raw, at, sizeof(raw));
    return EnumValue{raw};
  }
  switch (resolved.primitive) {
    case PrimitiveKind::UInt64: {
      std::uint64_t v = 0;
      std::memcpy(&v, at, sizeof(v));
      return v;
    }
    case PrimitiveKind::Float32: {
      float v = 0.0f;
      std::memcpy(&v, at, sizeof(v));
      return v;
    }
    case PrimitiveKind::Vec3Float32: {
      std::array<float, 3> v{};
      std::memcpy(v.data(), at, sizeof(v));
      return v;
    }
    case PrimitiveKind::Vec4Float32: {
      std::array<float, 4> v{};
      std::memcpy(v.data(), at, sizeof(v));
      return v;
    }
    case PrimitiveKind::AssetGuid: {
      AssetGuid v;
      std::memcpy(&v, at, sizeof(v));
      return v;
    }
    case PrimitiveKind::EntityGuid: {
      EntityGuid v;
      std::memcpy(&v, at, sizeof(v));
      return v;
    }
  }
  ATLANTIS_CHECK_MSG(false, "readField(): unhandled PrimitiveKind");
  return Absent{};
}

void writeField(std::byte* component, schema::TypeId type, schema::FieldId field, const ResolvedField& resolved,
                const PropertyValue& value) {
  if (const OptionalFieldAccessor* optional = findOptional(type, field)) {
    optional->write(component, value);
    return;
  }
  ATLANTIS_CHECK_MSG(!schema::hasFlags(resolved.flags, FieldFlags::Optional),
                      "writeField(): an Optional field has no typed accessor (Plan 0052 P3)");
  std::byte* at = component + resolved.byteOffset;
  std::visit(
      [&](const auto& v) {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, EnumValue>) {
          const auto raw = static_cast<std::int32_t>(v.value);
          std::memcpy(at, &raw, sizeof(raw));
        } else if constexpr (std::is_same_v<V, Absent>) {
          ATLANTIS_CHECK_MSG(false, "writeField(): Absent reached a non-Optional field");
        } else {
          std::memcpy(at, &v, sizeof(V));
        }
      },
      value);
}

std::span<const OptionalFieldAccessor> optionalFieldAccessors() noexcept { return kOptionalFields; }

std::span<const schema::TypeId> int32Enums() noexcept { return kInt32Enums; }

}  // namespace atlantis::world::access::detail
