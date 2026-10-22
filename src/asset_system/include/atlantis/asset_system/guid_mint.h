#pragma once

#include <atlantis/asset_system/asset_guid.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace atlantis::asset_system {

// Plan 0047 P4 / ADR-0097 D1: the only place a GUID is minted. Plan 0065 J8
// (ADR-0130 D1) moved it here from the asset cooker tool so the authoring
// session's CreateNode shares it; it is still the only minting code. Its
// callers are explicit authoring acts only -- the cooker's --kind=mint-guid
// and an authoring CreateNode that names no EntityGuid. No cook, import,
// decode, bake or Runtime path calls it.
//
// One RFC 9562 version-4 GUID from sixteen bytes of `draw` (four 32-bit
// draws); an all-zero draw is retried. Exposed with an injectable source
// only so tests can exercise the retry; mintAssetGuids() is the real entry.
[[nodiscard]] AssetGuid mintAssetGuid(const std::function<std::uint32_t()>& draw);

// `count` GUIDs drawn from one std::random_device owned by this call; safe
// to call concurrently.
[[nodiscard]] std::vector<AssetGuid> mintAssetGuids(std::size_t count);

// Plan 0065 J8: one node identity, minted exactly as mintAssetGuids() mints
// (same draw, same version and variant bits), as an EntityGuid.
[[nodiscard]] EntityGuid mintEntityGuid();

}  // namespace atlantis::asset_system
