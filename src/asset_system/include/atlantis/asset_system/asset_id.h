#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace atlantis::asset_system {

// Plan 0012 Section D9 / ADR-0044: a deterministic, path-derived Asset
// ID -- not rename/move-durable (see ADR-0044's own Decision). Computed
// via computeAssetId() over an already-normalized logical path (see
// logical_path.h); this module never derives it any other way, so every
// caller sees the same value for the same normalized path.
using AssetId = std::uint64_t;

// 64-bit FNV-1a over the normalized logical path's own UTF-8/ASCII
// bytes.
[[nodiscard]] AssetId computeAssetId(std::string_view normalizedLogicalPath) noexcept;

// Fixed-width, lowercase, 16-hex-digit form -- the exact text this
// module's metadata sidecar records (ADR-0044).
[[nodiscard]] std::string toHexString(AssetId id);

// Explicit little-endian 8-byte serialization (ADR-0045) -- never a
// memcpy of the host std::uint64_t representation, so the on-disk value
// is independent of host endianness.
[[nodiscard]] std::array<std::byte, 8> toLittleEndianBytes(AssetId id) noexcept;
[[nodiscard]] AssetId fromLittleEndianBytes(const std::array<std::byte, 8>& bytes) noexcept;

}  // namespace atlantis::asset_system
