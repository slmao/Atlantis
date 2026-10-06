#pragma once

#include <atlantis/result.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/ecs/world_components.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <variant>

// Plan 0052 P3 (ruling Q2, ADR-0099 D5): the accessor layer -- a World
// component's leaf fields read and written through worldSchema()'s
// descriptors. Private to Atlantis World (the World tests include it for the
// coverage checks). Stateless; the tables are immutable static data.
namespace atlantis::world::access::detail {

// A leaf field resolved under its component: its shape and its byte offset
// from the start of the component (nested struct offsets summed).
struct ResolvedField {
  schema::TypeKind kind = schema::TypeKind::Primitive;  // Primitive or Enum
  schema::PrimitiveKind primitive = schema::PrimitiveKind::UInt64;
  schema::TypeId enumType;  // kind == Enum
  schema::FieldFlags flags = schema::FieldFlags::None;
  std::uint32_t byteOffset = 0;
};

// Calls fn(std::type_identity<T>{}) for the World component type T whose
// TypeId is `component`; false, calling nothing, when it is not one of
// ecs::WorldComponentTypes. A compile-time walk: no type-erased ECS entry.
template <typename Fn>
bool visitComponentType(schema::TypeId component, Fn&& fn) {
  bool found = false;
  [&]<typename... Ts>(std::tuple<Ts...>*) {
    // A comma fold: evaluated left to right, one type at a time.
    ((!found && ecs::componentTypeId<Ts>() == component ? (found = true, fn(std::type_identity<Ts>{})) : void()),
     ...);
  }(static_cast<ecs::WorldComponentTypes*>(nullptr));
  return found;
}

// UnknownComponentType unless `component` is a World component type;
// UnknownField unless `field` names exactly one leaf reachable from it
// (Spec 0049 ruling J8).
[[nodiscard]] atlantis::Result<ResolvedField, AccessError> resolveField(schema::TypeId component,
                                                                         schema::FieldId field);

// Editable, then kind (Absent only for an Optional field), then an enum's
// declared constants, then finiteness (ruling Q6's type checks).
[[nodiscard]] atlantis::Result<std::monostate, AccessError> checkValue(const ResolvedField& field,
                                                                        const PropertyValue& value);

// `component` points at a live object of the component type `type`.
[[nodiscard]] PropertyValue readField(const std::byte* component, schema::TypeId type, schema::FieldId field,
                                      const ResolvedField& resolved);
void writeField(std::byte* component, schema::TypeId type, schema::FieldId field, const ResolvedField& resolved,
                const PropertyValue& value);

// Ruling Q2: the one Optional leaf goes through a typed accessor, since
// std::optional's layout is not standard. Every Optional-flagged World field
// must have an entry (a coverage test enforces it).
struct OptionalFieldAccessor {
  schema::TypeId component;
  schema::FieldId field;
  std::uint32_t byteOffset = 0;  // offsetof the member; equal to the descriptor's (tested)
  PropertyValue (*read)(const std::byte* component) = nullptr;
  void (*write)(std::byte* component, const PropertyValue& value) = nullptr;
};
[[nodiscard]] std::span<const OptionalFieldAccessor> optionalFieldAccessors() noexcept;

// Ruling Q2: described enums are stored as std::int32_t, held by
// static_asserts in property_access.cpp. Every Enum-kind World field's enum
// type must be listed here (a coverage test enforces it).
[[nodiscard]] std::span<const schema::TypeId> int32Enums() noexcept;

}  // namespace atlantis::world::access::detail
