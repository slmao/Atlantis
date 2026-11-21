#pragma once

#include <atlantis/connection/asset_connection.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/asset_system/static_mesh_asset_data.h>
#include <atlantis/asset_system/texture_types.h>

#include <functional>
#include <memory>
#include <string>

namespace atlantis::asset_system { class AssetCatalog; }

namespace atlantis::runtime {
class AssetRebuildHost;
// Spec 0081 R3: executable-private, frame-thread-only host. Construction and
// connect do no I/O. The first query fixes Ready/Unavailable until destruction.
// Connections borrow this host; destroy them first. The optional preview
// adapter borrows the caller's Runtime composition on the same thread.
class AssetHost {
 public:
  struct PreviewProvider {
    std::function<bool()> available;
    std::function<connection::assets::MeshPreviewResult(const asset_system::AssetGuid&,
                                                        asset_system::StaticMeshAssetData)> render;
    std::function<connection::assets::MeshMaterialPreviewResult(
        const asset_system::AssetGuid&, const asset_system::AssetGuid&,
        const asset_system::StaticMeshAssetData&, const asset_system::MaterialAssetData&,
        const asset_system::TextureAssetData&, const asset_system::TextureAssetData*,
        const asset_system::TextureAssetData*, const asset_system::TextureAssetData*,
        const asset_system::TextureAssetData*, bool)> renderPair{};
  };
  explicit AssetHost(std::string catalogPath, AssetRebuildHost* rebuild = nullptr, PreviewProvider preview = {});
  ~AssetHost();
  AssetHost(const AssetHost&) = delete;
  AssetHost& operator=(const AssetHost&) = delete;
  [[nodiscard]] std::unique_ptr<connection::AssetConnection> connect();
  [[nodiscard]] std::shared_ptr<const asset_system::AssetCatalog> catalog() const;
  struct FrozenPrefab {
    connection::assets::StaticGroup group;
    std::string source;
    std::string revision;
  };
  // Borrows the immutable verified definition until the host is destroyed.
  [[nodiscard]] const FrozenPrefab* frozenPrefab(const asset_system::AssetGuid& guid) const;
  // Call once for the combined Remote poll and --exec dispatch, if enabled.
  void beginServiceInterval() noexcept;
 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace atlantis::runtime
