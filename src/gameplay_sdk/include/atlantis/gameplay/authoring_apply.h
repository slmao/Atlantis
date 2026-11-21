#pragma once
#include <atlantis/connection/authoring_connection.h>
namespace atlantis::gameplay {
// Borrows a caller-thread connection, which must outlive this facade. Results own
// values. A Remote caller checks session failure before interpreting a sentinel.
class AuthoringApply {
 public:
  explicit AuthoringApply(connection::AuthoringConnection& connection) : connection_(connection) {}
  [[nodiscard]] Result<connection::authoring::ApplyReceipt, connection::authoring::ApplyError> applyTracked() {
    return connection_.applyTracked();
  }
  [[nodiscard]] Result<connection::authoring::ApplySnapshot, connection::authoring::ApplyLookupError> applyStatus(
      std::optional<connection::authoring::ApplyId> id = std::nullopt) const {
    return connection_.applyStatus(id);
  }
  [[nodiscard]] Result<connection::authoring::CreatedScene, connection::authoring::CreateSceneError> createScene(
      std::string_view name) { return connection_.createScene(name); }

  [[nodiscard]] Result<connection::authoring::SavedScene, connection::authoring::SaveAsError> saveAs(std::string_view name) {
    return connection_.saveAs(name);
  }
  [[nodiscard]] connection::authoring::PrefabUpdateResult updatePrefab(const asset_system::AssetGuid& prefab,
      std::string_view revision, const asset_system::EntityGuid& donor,
      const connection::authoring::DocumentToken& document) {
    return connection_.updatePrefab(prefab, revision, donor, document);
  }
  [[nodiscard]] connection::authoring::PrefabEditResult syncPrefab(const asset_system::EntityGuid& root,
      std::string_view revision, const connection::authoring::DocumentToken& document,
      std::optional<asset_system::AssetGuid> instantiate = std::nullopt) {
    return connection_.syncPrefab(root, revision, document, instantiate);
  }
  [[nodiscard]] connection::authoring::PrefabEditResult replayPrefab(const asset_system::EntityGuid& root,
      const connection::authoring::PrefabSubtree& before, const connection::authoring::PrefabSubtree& after,
      const connection::authoring::DocumentToken& document) {
    return connection_.replayPrefab(root, before, after, document);
  }
  [[nodiscard]] Result<connection::authoring::PrefabRegistration, connection::authoring::SaveAsError> createPrefab(
      std::string_view name, const connection::authoring::EntityGuid& node,
      const connection::authoring::DocumentToken& token) {
    return connection_.createPrefab(name, node, token);
  }
 private:
  connection::AuthoringConnection& connection_;
};
}  // namespace atlantis::gameplay
