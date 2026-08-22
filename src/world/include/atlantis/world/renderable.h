#pragma once

#include <atlantis/asset_system/asset_id.h>

namespace atlantis::world {

struct Renderable {
  atlantis::asset_system::AssetId meshAsset = 0;
};

}  // namespace atlantis::world
