#pragma once

#include <atlantis/connection/asset_connection.h>

namespace atlantis::gameplay {
// Spec 0081: transport-neutral read-only facade. Not thread-safe; caller's
// connection thread. Borrows the connection, which must outlive it; owns no GPU
// or file state. Results own their values; getAsset requires a non-nil GUID.
// A Remote caller checks its session failure before interpreting any sentinel.
class Assets {
 public:
  explicit Assets(connection::AssetConnection& connection) : connection_(connection) {}
  [[nodiscard]] Result<std::vector<connection::assets::AssetSummary>, connection::assets::AssetError> listAssets() const {
    return connection_.listAssets();
  }
  [[nodiscard]] Result<connection::assets::AssetDetails, connection::assets::AssetError> getAsset(
      const asset_system::AssetGuid& guid) const {
    return connection_.getAsset(guid);
  }
 private:
  connection::AssetConnection& connection_;
};
}  // namespace atlantis::gameplay
