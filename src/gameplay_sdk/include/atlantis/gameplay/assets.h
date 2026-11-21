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
  [[nodiscard]] connection::assets::StaticGroupResult staticGroup(const asset_system::AssetGuid& guid) const {
    return connection_.staticGroup(guid);
  }
  [[nodiscard]] connection::assets::PrefabRevisionResult prefabRevision(const asset_system::AssetGuid& guid) const {
    return connection_.prefabRevision(guid);
  }
  [[nodiscard]] connection::assets::StaticGroupResult prefab(const asset_system::AssetGuid& guid) const {
    return connection_.prefab(guid);
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
  [[nodiscard]] connection::assets::RenameTextureResult renameTexture(
      const connection::assets::RenameTextureRequest& request) {
    return connection_.renameTexture(request);
  }
  [[nodiscard]] connection::assets::MoveTextureResult moveTexture(
      const connection::assets::MoveTextureRequest& request) {
    return connection_.moveTexture(request);
  }
  [[nodiscard]] connection::assets::TextureRemovalCheckResult checkTextureRemoval(
      const connection::assets::TextureRemovalTarget& target) {
    return connection_.checkTextureRemoval(target);
  }
  [[nodiscard]] connection::assets::TextureRemovalCheckResult textureRemovalCheckStatus(
      connection::assets::TextureRemovalCheckId id) const {
    return connection_.textureRemovalCheckStatus(id);
  }
  [[nodiscard]] connection::assets::RemoveTextureResult removeTexture(
      const connection::assets::TextureRemovalTarget& target, connection::assets::TextureRemovalCheckId id) {
    return connection_.removeTexture(target, id);
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
  [[nodiscard]] connection::assets::TextureImportStartResult normalMapImport(
      std::string_view name, connection::assets::ImportMode mode = connection::assets::ImportMode::Register) {
    return connection_.normalMapImport(name, mode);
  }
  [[nodiscard]] connection::assets::TextureImportStartResult metallicRoughnessImport(
      std::string_view name, connection::assets::ImportMode mode = connection::assets::ImportMode::Register) {
    return connection_.metallicRoughnessImport(name, mode);
  }
  [[nodiscard]] connection::assets::TextureImportStartResult occlusionImport(
      std::string_view name, connection::assets::ImportMode mode = connection::assets::ImportMode::Register) {
    return connection_.occlusionImport(name, mode);
  }
  [[nodiscard]] connection::assets::TextureImportStatusResult normalMapImportStatus(
      std::optional<connection::assets::TextureImportId> id = std::nullopt) const {
    return connection_.normalMapImportStatus(id);
  }
  [[nodiscard]] connection::assets::TextureImportStatusResult metallicRoughnessImportStatus(
      std::optional<connection::assets::TextureImportId> id = std::nullopt) const {
    return connection_.metallicRoughnessImportStatus(id);
  }
  [[nodiscard]] connection::assets::TextureImportStatusResult occlusionImportStatus(
      std::optional<connection::assets::TextureImportId> id = std::nullopt) const {
    return connection_.occlusionImportStatus(id);
  }

  [[nodiscard]] connection::assets::MaterialReadResult materialRead(const asset_system::AssetGuid& guid) const {
    return connection_.materialRead(guid);
  }
  [[nodiscard]] connection::assets::MaterialSaveResult materialSave(const connection::assets::MaterialSave& request) {
    return connection_.materialSave(request);
  }
  [[nodiscard]] connection::assets::MaterialReadResult materialReadWithMaps(const asset_system::AssetGuid& guid) const {
    return connection_.materialReadWithMaps(guid);
  }
  [[nodiscard]] connection::assets::MaterialReadResult materialReadWithMetallicRoughness(const asset_system::AssetGuid& guid) const {
    return connection_.materialReadWithMetallicRoughness(guid);
  }
  [[nodiscard]] connection::assets::MaterialReadResult materialReadWithOcclusion(const asset_system::AssetGuid& guid) const {
    return connection_.materialReadWithOcclusion(guid);
  }
  [[nodiscard]] connection::assets::MaterialSaveResult materialSaveWithMaps(const connection::assets::MaterialSave& request) {
    return connection_.materialSaveWithMaps(request);
  }
  [[nodiscard]] connection::assets::MaterialSaveResult materialSaveWithMetallicRoughness(const connection::assets::MaterialSave& request) {
    return connection_.materialSaveWithMetallicRoughness(request);
  }
  [[nodiscard]] connection::assets::MaterialSaveResult materialSaveWithOcclusion(const connection::assets::MaterialSave& request) {
    return connection_.materialSaveWithOcclusion(request);
  }
  [[nodiscard]] connection::assets::MaterialDraftPreviewResult previewMaterialDraftWithMaps(
      const asset_system::AssetGuid& mesh, const connection::assets::MaterialRecipe& recipe) {
    return connection_.previewMaterialDraftWithMaps(mesh, recipe);
  }
  [[nodiscard]] connection::assets::MaterialDraftPreviewResult previewMaterialDraftWithMetallicRoughness(
      const asset_system::AssetGuid& mesh, const connection::assets::MaterialRecipe& recipe) {
    return connection_.previewMaterialDraftWithMetallicRoughness(mesh, recipe);
  }
  [[nodiscard]] connection::assets::MaterialDraftPreviewResult previewMaterialDraftWithOcclusion(
      const asset_system::AssetGuid& mesh, const connection::assets::MaterialRecipe& recipe) {
    return connection_.previewMaterialDraftWithOcclusion(mesh, recipe);
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
