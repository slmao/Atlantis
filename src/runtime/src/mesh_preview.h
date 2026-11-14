#pragma once

#include <atlantis/asset_system/static_mesh_asset_data.h>
#include <atlantis/connection/asset_connection.h>
#include <atlantis/rhi/device.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/runtime_application.h>
#include <atlantis/runtime/scene_extraction.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace atlantis::runtime::detail {

struct FramedMeshPreview {
  std::vector<std::byte> vertexBytes;
  CameraMatrices camera;
  Vec3 eye{};
};

[[nodiscard]] std::optional<FramedMeshPreview> frameMeshPreview(const asset_system::StaticMeshAssetData& mesh);

struct MeshPreviewResources;
struct MeshPreviewExecution {
  connection::assets::MeshPreviewResult result;
  bool fatal = false;
};
struct MeshMaterialPreviewExecution {
  connection::assets::MeshMaterialPreviewResult result;
  bool fatal = false;
};

// Already initialized and immutable for the lifetime of a windowed Runtime.
// Headless callers can leave this absent and let the request own equivalents.
struct MeshPreviewReadOnly {
  rhi::Buffer& fullscreenVertices;
  rhi::Buffer& fullscreenIndices;
  rhi::Sampler& outputSampler;
  rhi::Sampler& shadowSampler;
};

// Spec 0091 R2/R3: the production render path also used on a headless test
// device. Caller owns the device and invokes this between frames. On fatal
// completion failure, the caller must stop submission and keep the returned
// bundle alive until its shutdown drain, then release it before the device.
[[nodiscard]] MeshPreviewExecution renderMeshPreview(
    rhi::Device& device, const BootstrapConfig& config, const asset_system::StaticMeshAssetData& mesh,
    const asset_system::AssetGuid& guid, std::shared_ptr<MeshPreviewResources>& incompleteSubmission,
    PreviewCompletion complete = nullptr, const MeshPreviewReadOnly* readOnly = nullptr);

// Spec 0092 R3. The same frame and lifetime path with a validated opaque
// PBR material and its validated cooked textures.
[[nodiscard]] MeshMaterialPreviewExecution renderMeshMaterialPreview(
    rhi::Device& device, const BootstrapConfig& config, const asset_system::StaticMeshAssetData& mesh,
    const asset_system::MaterialAssetData& material, const asset_system::TextureAssetData& texture,
    const asset_system::AssetGuid& meshGuid, const asset_system::AssetGuid& materialGuid,
    std::shared_ptr<MeshPreviewResources>& incompleteSubmission,
    PreviewCompletion complete = nullptr, const MeshPreviewReadOnly* readOnly = nullptr,
    const asset_system::TextureAssetData* normal = nullptr, const asset_system::TextureAssetData* emissive = nullptr);

}  // namespace atlantis::runtime::detail
