#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace atlantis::asset_system {

// Plan 0047 (ADR-0097 D2): the 64-bit key of an asset's persistent
// AssetGuid -- assetKey() (asset_guid.h), FNV-1a-64 over the GUID's 16
// bytes. Logical paths no longer participate in identity; 0 is reserved
// ("none").
using AssetId = std::uint64_t;

// Fixed-width, lowercase, 16-hex-digit form -- the exact text this
// module's metadata sidecar records (ADR-0044).
[[nodiscard]] std::string toHexString(AssetId id);

// Explicit little-endian 8-byte serialization (ADR-0045) -- never a
// memcpy of the host std::uint64_t representation, so the on-disk value
// is independent of host endianness.
[[nodiscard]] std::array<std::byte, 8> toLittleEndianBytes(AssetId id) noexcept;
[[nodiscard]] AssetId fromLittleEndianBytes(const std::array<std::byte, 8>& bytes) noexcept;

}  // namespace atlantis::asset_system
