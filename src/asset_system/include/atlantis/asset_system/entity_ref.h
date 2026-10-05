#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/result.h>

#include <array>
#include <compare>
#include <cstddef>
#include <string>
#include <string_view>

namespace atlantis::asset_system {

// Plan 0047 P18 / ADR-0097 D6: a persisted reference to one entity of one
// scene. Pure value type and codecs; all functions are safe for concurrent
// use. Resolving a reference needs a loaded instance of the scene and lives in
// Atlantis Runtime (entity_ref_resolution.h); nothing here depends on World.
// No scene-grammar field stores an EntityRef yet.
struct EntityRef {
  AssetGuid scene;
  EntityGuid entity;
  friend auto operator<=>(const EntityRef&, const EntityRef&) = default;
};

// The scene GUID's 16 bytes, then the entity GUID's 16 bytes.
using EntityRefBytes = std::array<std::byte, 32>;

inline constexpr std::size_t kEntityRefTextLength = 73;  // 36 + '/' + 36

enum class EntityRefParseError {
  WrongLength,       // text is not exactly 73 characters
  MissingSeparator,  // character 36 is not '/'
  MissingHyphen,     // a GUID half has no '-' at offset 8, 13, 18 or 23
  NotLowercaseHex,   // a GUID half has a character outside [0-9a-f]
  NilGuid,           // either GUID is all zero
};

[[nodiscard]] std::string_view toString(EntityRefParseError error) noexcept;

// Canonical text: <scene guid>/<entity guid>, both in canonical GUID text.
[[nodiscard]] atlantis::Result<EntityRef, EntityRefParseError> parseEntityRef(std::string_view text);
[[nodiscard]] std::string toString(const EntityRef& ref);

// Binary form: 32 bytes, scene then entity. Decoding rejects a nil half
// (NilGuid) and nothing else.
[[nodiscard]] EntityRefBytes entityRefToBytes(const EntityRef& ref) noexcept;
[[nodiscard]] atlantis::Result<EntityRef, EntityRefParseError> entityRefFromBytes(const EntityRefBytes& bytes);

}  // namespace atlantis::asset_system
