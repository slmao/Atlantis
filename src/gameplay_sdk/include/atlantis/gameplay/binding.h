#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

// Spec 0057 R3/R5/R6, ADR-0111 D2: the vocabulary a generated binding header
// (atlantis/gameplay/generated/*.h, written by atlantis_sdk_codegen) is made
// of -- what each generated type was generated from (TypeBinding), its typed
// field handles, and the per-leaf mapping of its value struct to the
// connection's PropertyValue (Codec). No byte offset or World C++ type
// appears here or in generated code: values cross by copy (ADR-0034).
//
// Inside atlantis::gameplay the name `world` is the generated namespace
// (atlantis::gameplay::world, Plan 0057 J11), so World's access types are
// spelled through the aliases below.
//
// Thread safety: value types, constexpr data and pure functions only.
namespace atlantis::gameplay {

using PropertyValue = ::atlantis::world::access::PropertyValue;
using PropertyAddress = ::atlantis::world::access::PropertyAddress;
using EntityGuid = ::atlantis::asset_system::EntityGuid;
using AssetGuid = ::atlantis::asset_system::AssetGuid;

// One field of a generated type, as the schema described it when the header
// was generated.
struct FieldBinding {
  std::string_view name;
  schema::FieldId id;
  schema::TypeKind kind = schema::TypeKind::Primitive;
  schema::PrimitiveKind primitive = schema::PrimitiveKind::UInt64;  // meaningful when kind == Primitive
  schema::TypeId type;                                              // the referenced struct or enum; {0} otherwise
  bool optional = false;
  bool editable = false;
};

struct EnumConstantBinding {
  std::string_view name;
  std::int64_t value = 0;
};

// A generated type's binding (ADR-0111 D2). zeroIsDeclared (enums only):
// whether 0 -- the value a value-initialized member of this enum holds -- is
// one of its declared constants (Spec 0057 R10).
struct TypeBinding {
  std::string_view name;  // qualified below atlantis::, e.g. "world::Light"
  schema::TypeId id;
  schema::TypeKind kind = schema::TypeKind::Struct;
  std::uint32_t schemaVersion = 1;
  std::span<const FieldBinding> fields;
  std::span<const EnumConstantBinding> constants;
  bool zeroIsDeclared = false;
};

// Every id is the hash of its name -- a generated header static_asserts this
// over its own table, with Core only.
[[nodiscard]] constexpr bool bindingsMatchNames(std::span<const TypeBinding> table) noexcept {
  for (const TypeBinding& type : table) {
    if (schema::typeId(type.name) != type.id) return false;
    for (const FieldBinding& field : type.fields) {
      if (schema::fieldId(type.name, field.name) != field.id) return false;
    }
  }
  return true;
}

// Specialized by generated headers for every generated type: the module's
// binding table and this type's index in it.
template <class T>
struct BindingOf;

// Specialized by generated headers for every component type (a struct no
// other struct nests): its leaves, in the schema's leaf order (nested structs
// expanded in place, Spec 0049 J8 addressing), and their values.
//   static constexpr bool allEditable;
//   static constexpr std::array<schema::FieldId, N> leaves;
//   static void write(const C&, std::span<PropertyValue> out);         // out.size() == N
//   static bool read(std::span<const PropertyValue> in, C& value);     // false: an alternative did not match
template <class C>
struct Codec;

template <class T>
concept Bound = requires {
  { BindingOf<T>::table } -> std::convertible_to<std::span<const TypeBinding>>;
  { BindingOf<T>::index } -> std::convertible_to<std::size_t>;
};

template <class C>
concept Component = Bound<C> && requires {
  { Codec<C>::allEditable } -> std::convertible_to<bool>;
  Codec<C>::leaves;
};

enum class Access : std::uint8_t { ReadWrite, ReadOnly };

// A typed handle on one leaf of component C (ADR-0111 D2): the component's
// TypeId, the leaf's own FieldId and its canonical path. A leaf without the
// Editable flag gets ReadOnlyField, so a typed set on it does not compile.
template <class C, class T, Access A>
struct FieldHandle {
  schema::TypeId component;
  schema::FieldId field;
  std::string_view path;
};

template <class C, class T>
using Field = FieldHandle<C, T, Access::ReadWrite>;
template <class C, class T>
using ReadOnlyField = FieldHandle<C, T, Access::ReadOnly>;

namespace detail {

template <class T>
inline constexpr bool kIsDirect =
    std::is_same_v<T, std::uint64_t> || std::is_same_v<T, float> || std::is_same_v<T, std::array<float, 3>> ||
    std::is_same_v<T, std::array<float, 4>> || std::is_same_v<T, AssetGuid> || std::is_same_v<T, EntityGuid>;

template <class T>
inline constexpr bool kIsEnum = std::is_enum_v<T>;

template <class T>
struct IsOptional : std::false_type {};
template <class T>
struct IsOptional<std::optional<T>> : std::true_type {};

}  // namespace detail

// A leaf's C++ type: one PropertyValue alternative, a generated enum
// (carried as EnumValue), or std::optional of either (absent: Absent).
template <class T>
concept LeafValue = detail::kIsDirect<T> || detail::kIsEnum<T> ||
                    (detail::IsOptional<T>::value &&
                     (detail::kIsDirect<typename T::value_type> || detail::kIsEnum<typename T::value_type>));

template <LeafValue T>
[[nodiscard]] PropertyValue toPropertyValue(const T& value) {
  if constexpr (detail::IsOptional<T>::value) {
    if (!value.has_value()) return ::atlantis::world::access::Absent{};
    return toPropertyValue(*value);
  } else if constexpr (detail::kIsEnum<T>) {
    static_assert(std::is_same_v<std::underlying_type_t<T>, std::int64_t>, "generated enums are int64-based");
    return ::atlantis::world::access::EnumValue{static_cast<std::int64_t>(value)};
  } else {
    return value;
  }
}

// False when `value` holds another alternative than T's (a schema mismatch).
template <LeafValue T>
[[nodiscard]] bool fromPropertyValue(const PropertyValue& value, T& out) {
  if constexpr (detail::IsOptional<T>::value) {
    if (std::holds_alternative<::atlantis::world::access::Absent>(value)) {
      out.reset();
      return true;
    }
    typename T::value_type inner{};
    if (!fromPropertyValue(value, inner)) return false;
    out = inner;
    return true;
  } else if constexpr (detail::kIsEnum<T>) {
    const auto* held = std::get_if<::atlantis::world::access::EnumValue>(&value);
    if (held == nullptr) return false;
    out = static_cast<T>(held->value);
    return true;
  } else {
    const auto* held = std::get_if<T>(&value);
    if (held == nullptr) return false;
    out = *held;
    return true;
  }
}

}  // namespace atlantis::gameplay
