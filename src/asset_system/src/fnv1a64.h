#pragma once

#include <atlantis/asset_system/asset_id.h>

#include <cstddef>
#include <span>

namespace atlantis::asset_system::detail {

// Plan 0047 ruling I1: 64-bit FNV-1a (offset basis 0xcbf29ce484222325,
// prime 0x100000001b3) over raw bytes -- the core assetKey() hashes a
// GUID's 16 bytes with. Internal: no public entry point hashes a path.
[[nodiscard]] constexpr AssetId fnv1a64(std::span<const std::byte> bytes) noexcept {
  AssetId hash = 0xcbf29ce484222325ULL;
  for (const std::byte byte : bytes) {
    hash ^= static_cast<AssetId>(byte);
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

}  // namespace atlantis::asset_system::detail
