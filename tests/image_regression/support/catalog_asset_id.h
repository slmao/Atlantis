#pragma once

#include <atlantis/asset_system/asset_id.h>

#include <string_view>

namespace atlantis::image_regression {

// Plan 0047 M3 (ADR-0097 D2, ADR-0098 D1): the Asset ID a committed asset
// cooks under -- assetKey() of the GUID the committed catalog source records
// for assets:<logicalPath>. Replaces hashing the path, which no longer names
// the asset. Aborts (ATLANTIS_CHECK) when the catalog source is unreadable or
// has no such entry: a test naming an unknown asset is a programmer error.
[[nodiscard]] atlantis::asset_system::AssetId catalogAssetId(std::string_view logicalPath);

}  // namespace atlantis::image_regression
