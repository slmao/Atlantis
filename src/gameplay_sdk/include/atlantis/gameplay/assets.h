#pragma once

#include <atlantis/connection/asset_connection.h>

namespace atlantis::gameplay {
// Specs 0081/0085/0091/0092/0094: transport-neutral asset facade. Not thread-safe; caller's
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
  // Both GUIDs must be non-nil.
  [[nodiscard]] connection::assets::MeshMaterialPreviewResult previewMeshMaterial(
      const asset_system::AssetGuid& meshGuid, const asset_system::AssetGuid& materialGuid) {
    return connection_.previewMeshMaterial(meshGuid, materialGuid);
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
  [[nodiscard]] connection::assets::TextureImportStartResult textureImport(
      std::string_view name, connection::assets::ImportMode mode = connection::assets::ImportMode::Register) {
    return connection_.textureImport(name, mode);
  }
  [[nodiscard]] connection::assets::TextureImportStatusResult textureImportStatus(
      std::optional<connection::assets::TextureImportId> id = std::nullopt) const {
    return connection_.textureImportStatus(id);
  }

  [[nodiscard]] connection::assets::MaterialReadResult materialRead(const asset_system::AssetGuid& guid) const {
    return connection_.materialRead(guid);
  }
  [[nodiscard]] connection::assets::MaterialSaveResult materialSave(const connection::assets::MaterialSave& request) {
    return connection_.materialSave(request);
  }
  [[nodiscard]] connection::assets::MaterialStatusResult materialStatus(
      std::optional<connection::assets::MaterialJobId> id = std::nullopt) const {
    return connection_.materialStatus(id);
  }
  [[nodiscard]] connection::assets::MaterialDraftPreviewResult previewMaterialDraft(
      const asset_system::AssetGuid& mesh, const connection::assets::MaterialRecipe& recipe) {
    return connection_.previewMaterialDraft(mesh, recipe);
  }

 private:
  connection::AssetConnection& connection_;
};
}  // namespace atlantis::gameplay
