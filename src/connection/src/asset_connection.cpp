#include <atlantis/connection/asset_connection.h>

#include <atlantis/assert.h>

namespace atlantis::connection::assets {
std::string_view toString(AssetType type) noexcept {
  switch (type) {
    case AssetType::Mesh: return "mesh";
    case AssetType::Texture: return "texture";
    case AssetType::Material: return "material";
    case AssetType::Scene: return "scene";
    case AssetType::Environment: return "environment";
  }
  ATLANTIS_CHECK(false);
  return {};
}
std::string_view toString(AssetError error) noexcept {
  switch (error) {
    case AssetError::CatalogUnavailable: return "CatalogUnavailable";
    case AssetError::UnknownAsset: return "UnknownAsset";
  }
  ATLANTIS_CHECK(false);
  return {};
}
}  // namespace atlantis::connection::assets
