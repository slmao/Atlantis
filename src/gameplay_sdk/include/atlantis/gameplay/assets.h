#pragma once

#include <atlantis/connection/asset_connection.h>

namespace atlantis::gameplay {
// Specs 0081/0085/0091: transport-neutral asset facade. Not thread-safe; caller's
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
  [[nodiscard]] connection::assets::MeshPreviewResult previewMesh(const asset_system::AssetGuid& guid) {
    return connection_.previewMesh(guid);
  }
  [[nodiscard]] connection::assets::RebuildStartResult rebuild(const asset_system::AssetGuid& guid) {
    return connection_.rebuild(guid);
  }
  [[nodiscard]] connection::assets::RebuildStatusResult rebuildStatus(
      std::optional<connection::assets::RebuildId> id = std::nullopt) const {
    return connection_.rebuildStatus(id);
  }
  [[nodiscard]] connection::assets::ImportStartResult importAsset(
      std::string_view name, connection::assets::ImportMode mode = connection::assets::ImportMode::Register) {
    return connection_.importAsset(name, mode);
  }
  [[nodiscard]] connection::assets::ImportStatusResult importStatus(
      std::optional<connection::assets::ImportId> id = std::nullopt) const {
    return connection_.importStatus(id);
  }

 private:
  connection::AssetConnection& connection_;
};
}  // namespace atlantis::gameplay
