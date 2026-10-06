#pragma once

#include <atlantis/schema.h>

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace atlantis::world::ecs {

// Spec 0050 R3 / ADR-0101 D3 (rulings Q3, Q7): a component's identity is its
// Spec 0048 schema::TypeId. A C++ type becomes a component through an
// explicit, non-intrusive specialization naming its schema name:
//
//   template <> struct ComponentType<world::Transform> {
//     static constexpr std::string_view kName = "world::Transform";
//   };
//
// The mapped TypeId must be in the owning module's schema table (checked by
// test, Plan 0050 J4). The primary template has no kName, so an unmapped type
// is not a Component and every constrained operation rejects it at compile
// time. Components are plain values: trivially copyable, trivially
// destructible and standard-layout. Everything here is compile-time; there is
// no shared state.
template <typename T>
struct ComponentType {};

template <typename T>
concept Component = requires {
  { ComponentType<T>::kName } -> std::convertible_to<std::string_view>;
} && std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T> && std::is_standard_layout_v<T>;

template <Component T>
[[nodiscard]] constexpr schema::TypeId componentTypeId() noexcept {
  return schema::typeId(ComponentType<T>::kName);
}

// What the type-erased storage needs to know about a component.
struct ComponentInfo {
  schema::TypeId id;
  std::uint32_t size = 0;
  std::uint32_t alignment = 0;
  friend bool operator==(const ComponentInfo&, const ComponentInfo&) = default;
};

template <Component T>
[[nodiscard]] constexpr ComponentInfo componentInfo() noexcept {
  return {componentTypeId<T>(), static_cast<std::uint32_t>(sizeof(T)), static_cast<std::uint32_t>(alignof(T))};
}

}  // namespace atlantis::world::ecs
