#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/result.h>
#include <atlantis/schema.h>

#include <compare>
#include <cstddef>
#include <string>
#include <string_view>

namespace atlantis::asset_system::scene {

// Spec 0049 R7 / ADR-0100 D7 (ruling Q4): the canonical address of a
// property inside a scene document -- the node's EntityGuid, the node
// component's TypeId, and the leaf field's FieldId. A nested field (fog
// density) is addressed under its node component (Camera) by its leaf
// FieldId (CameraFog.density), valid while the leaf is reachable through
// exactly one field chain (Plan 0049 ruling J8). A value type only: nothing
// resolves or applies an address yet. Safe for concurrent use.
struct PropertyAddress {
  EntityGuid node;
  schema::TypeId component;
  schema::FieldId field;
  friend auto operator<=>(const PropertyAddress&, const PropertyAddress&) = default;
};

// <entity guid>/<component TypeId>/<field FieldId>, each id 16 lowercase hex
// digits (Plan 0049 ruling J10): 36 + 1 + 16 + 1 + 16 characters.
inline constexpr std::size_t kPropertyAddressTextLength = 70;

enum class PropertyAddressParseError {
  WrongLength,       // not exactly kPropertyAddressTextLength characters
  MissingSeparator,  // no '/' at offset 36 or 53
  MalformedGuid,     // the node part is not canonical GUID text
  NilGuid,           // the node GUID is all zero
  MalformedId,       // an id part has a character outside [0-9a-f]
  ZeroId,            // an id is zero
};

[[nodiscard]] std::string_view toString(PropertyAddressParseError error) noexcept;
[[nodiscard]] std::string toString(const PropertyAddress& address);
[[nodiscard]] atlantis::Result<PropertyAddress, PropertyAddressParseError> parsePropertyAddress(
    std::string_view text);

}  // namespace atlantis::asset_system::scene
