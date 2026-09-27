#pragma once

#include <atlantis/asset_system/asset_guid.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace atlantis::tools::asset_cooker {

// Plan 0047 P4 / ADR-0097 D1: the only place a GUID is minted. It lives in
// the cooker tool, never in the Asset System library, so no cook, import or
// Runtime code path can reach it.
//
// One RFC 9562 version-4 GUID from sixteen bytes of `draw` (four 32-bit
// draws); an all-zero draw is retried. Exposed with an injectable source
// only so tests can exercise the retry; mintAssetGuids() is the real entry.
[[nodiscard]] atlantis::asset_system::AssetGuid mintAssetGuid(const std::function<std::uint32_t()>& draw);

// `count` GUIDs drawn from one std::random_device owned by this call; safe
// to call concurrently.
[[nodiscard]] std::vector<atlantis::asset_system::AssetGuid> mintAssetGuids(std::size_t count);

}  // namespace atlantis::tools::asset_cooker
