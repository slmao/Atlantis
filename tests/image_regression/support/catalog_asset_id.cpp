#include "catalog_asset_id.h"

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_catalog_source.h>
#include <atlantis/asset_system/asset_guid.h>

#include <fstream>
#include <iterator>
#include <string>

namespace atlantis::image_regression {

atlantis::asset_system::AssetId catalogAssetId(std::string_view logicalPath) {
  std::ifstream in(ATLANTIS_ASSET_CATALOG_SOURCE_PATH, std::ios::binary);
  ATLANTIS_CHECK_MSG(in.is_open(), "catalogAssetId(): the catalog source is unreadable");
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto catalog = atlantis::asset_system::parseAssetCatalogSource(text);
  ATLANTIS_CHECK_MSG(catalog.isOk(), "catalogAssetId(): the catalog source does not parse");
  const atlantis::asset_system::CatalogSourceEntry* entry =
      catalog.value().find(atlantis::asset_system::CatalogRoot::Assets, logicalPath);
  ATLANTIS_CHECK_MSG(entry != nullptr, "catalogAssetId(): no catalog-source entry for this logical path");
  return atlantis::asset_system::assetKey(entry->guid);
}

}  // namespace atlantis::image_regression
