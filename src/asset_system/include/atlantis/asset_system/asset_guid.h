#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/result.h>

#include <array>
#include <compare>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace atlantis::asset_system {

// Plan 0047 P1-P3 / ADR-0097 D1, D2, D4: persistent 128-bit identities.
// Every function here is pure; all are safe for concurrent use.

// The 16 bytes in canonical-text order (a byte string, no endianness).
using GuidBytes = std::array<std::byte, 16>;

// A default-constructed value is the nil GUID, which is invalid everywhere
// (ADR-0097 D1): only parseAssetGuid()/assetGuidFromBytes() and the
// derivation functions below produce values a caller may rely on.
struct AssetGuid {
  GuidBytes bytes{};
  friend auto operator<=>(const AssetGuid&, const AssetGuid&) = default;
};

// Same representation as AssetGuid, deliberately a distinct type so an
// entity identity can never be passed where an asset identity is expected.
struct EntityGuid {
  GuidBytes bytes{};
  friend auto operator<=>(const EntityGuid&, const EntityGuid&) = default;
};

enum class GuidParseError {
  WrongLength,      // text is not exactly 36 characters
  MissingHyphen,    // no '-' at offset 8, 13, 18 or 23
  NotLowercaseHex,  // any other character outside [0-9a-f]; uppercase is rejected
  NilGuid,          // all sixteen bytes are zero
};

// Canonical text: lowercase RFC 9562 form, 8-4-4-4-12.
[[nodiscard]] atlantis::Result<AssetGuid, GuidParseError> parseAssetGuid(std::string_view text);
[[nodiscard]] atlantis::Result<EntityGuid, GuidParseError> parseEntityGuid(std::string_view text);
[[nodiscard]] std::string toString(const AssetGuid& guid);
[[nodiscard]] std::string toString(const EntityGuid& guid);

// Binary form: the 16 bytes as stored. Rejects only the nil GUID.
[[nodiscard]] atlantis::Result<AssetGuid, GuidParseError> assetGuidFromBytes(const GuidBytes& bytes);
[[nodiscard]] atlantis::Result<EntityGuid, GuidParseError> entityGuidFromBytes(const GuidBytes& bytes);

// FNV-1a-128 (offset basis 6c62272e07bb014262b821756295c58d, prime
// 2^88 + 0x13b); the 128-bit result is returned big-endian.
[[nodiscard]] GuidBytes fnv1a128(std::span<const std::byte> data) noexcept;

// ADR-0097 D4: v8(FNV-1a-128(owner bytes ‖ sub-key bytes)) -- version
// nibble 8 in byte 6, variant 0b10 in byte 8, so the result is never nil.
// subKey is hashed byte for byte; callers pass ASCII sub-keys (Plan 0047
// P5), and this function does not re-check that.
[[nodiscard]] AssetGuid deriveAssetGuid(const AssetGuid& owner, std::string_view subKey) noexcept;
[[nodiscard]] EntityGuid deriveEntityGuid(const AssetGuid& scene, std::string_view subKey) noexcept;

// ADR-0097 D2: the 64-bit reference key, FNV-1a-64 over the 16 bytes.
// Not unique by construction -- uniqueness is enforced by catalog assembly
// (ADR-0098 D2).
[[nodiscard]] AssetId assetKey(const AssetGuid& guid) noexcept;

}  // namespace atlantis::asset_system
