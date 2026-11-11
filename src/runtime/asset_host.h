#pragma once

#include <atlantis/connection/asset_connection.h>
#include <atlantis/asset_system/static_mesh_asset_data.h>

#include <functional>
#include <memory>
#include <string>

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
  };
  explicit AssetHost(std::string catalogPath, AssetRebuildHost* rebuild = nullptr, PreviewProvider preview = {});
  ~AssetHost();
  AssetHost(const AssetHost&) = delete;
  AssetHost& operator=(const AssetHost&) = delete;
  [[nodiscard]] std::unique_ptr<connection::AssetConnection> connect();
  // Call once for the combined Remote poll and --exec dispatch, if enabled.
  void beginServiceInterval() noexcept;
 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace atlantis::runtime
