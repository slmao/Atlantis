#include "frame_capture.h"

#include <atlantis/renderer/draw_item.h>
#include <atlantis/runtime/runtime_application.h>
#include <atlantis/runtime/scene_extraction.h>

#include <algorithm>
#include <vector>

namespace atlantis::runtime {

namespace connection = atlantis::connection;
using atlantis::renderer::DrawItem;

namespace detail {

connection::FrameData toFrameData(const FrameLightingData& lighting, const CameraMatrices& camera,
                                  std::size_t drawItemCount) {
  connection::FrameData data;
  for (std::uint32_t i = 0; i < lighting.directionalLightCount && i < 1; ++i) {
    const auto& gpu = lighting.directionalLights[i];
    data.directionalLights.push_back(connection::FrameDirectionalLight{
        {gpu.direction[0], gpu.direction[1], gpu.direction[2]}, {gpu.color[0], gpu.color[1], gpu.color[2]},
        gpu.intensity});
  }
  for (std::uint32_t i = 0; i < lighting.pointLightCount && i < kMaxPointLights; ++i) {
    const auto& gpu = lighting.pointLights[i];
    data.pointLights.push_back(connection::FramePointLight{{gpu.position[0], gpu.position[1], gpu.position[2]},
                                                           {gpu.color[0], gpu.color[1], gpu.color[2]},
                                                           gpu.intensity, gpu.range});
  }
  data.view = camera.view;
  data.projection = camera.projection;
  data.drawItemCount = drawItemCount;
  return data;
}

}  // namespace detail

// Plan 0055 P8: the DrawItems runFrame() hands drawFrame() -- the same
// resolution, in the same order, against the same resources -- rebuilt
// between frames. This duplicates runFrame()'s DrawItem walk rather than
// refactoring it out (runFrame() changes in exactly one line, P7); the frame
// data equality and image determinism tests pin the two together, and a
// change to one must be made to both. Between frames every material the last
// frame realized is published, so there are no per-frame candidates; and
// nothing is logged (the frame already did).
std::vector<DrawItem> RuntimeApplication::assembleCaptureDrawItems(
    const std::vector<RenderableExtractionInput>& renderables) const {
  std::vector<atlantis::asset_system::AssetId> knownMeshAssetIds;
  knownMeshAssetIds.reserve(meshResourceMap_.size());
  for (const auto& [assetId, mesh] : meshResourceMap_) knownMeshAssetIds.push_back(assetId);
  std::vector<atlantis::asset_system::AssetId> knownMaterialIds;
  knownMaterialIds.reserve(materialResourceMap_.size());
  for (const auto& [assetId, material] : materialResourceMap_) knownMaterialIds.push_back(assetId);

  std::vector<DrawItem> drawItems;
  for (const RenderableExtractionInput& renderable : renderables) {
    if (resolveMeshAsset(renderable.renderable.meshAsset, knownMeshAssetIds).isErr()) continue;
    const atlantis::renderer::Material* resolvedMaterial = fallbackMaterial_.get();
    if (const auto& materialAsset = renderable.renderable.materialAsset; materialAsset.has_value()) {
      if (resolveMaterialAsset(*materialAsset, knownMaterialIds).isErr()) continue;
      const auto materialDataIt = materialDataMap_.find(*materialAsset);
      if (materialDataIt == materialDataMap_.end()) continue;  // runFrame() CHECKs this never happens
      if ((materialDataIt->second.kind == atlantis::asset_system::MaterialKind::LitTextured ||
           materialDataIt->second.kind == atlantis::asset_system::MaterialKind::PbrDirectLit) &&
          checkConformalTransform(renderable.worldMatrix).isErr()) {
        continue;
      }
      const auto it = materialResourceMap_.find(*materialAsset);
      if (it == materialResourceMap_.end()) continue;
      resolvedMaterial = it->second.get();
    }
    DrawItem item;
    item.mesh = &meshResourceMap_.at(renderable.renderable.meshAsset);
    item.material = resolvedMaterial;
    item.objectToWorld = renderable.worldMatrix;
    drawItems.push_back(item);
  }
  return drawItems;
}

atlantis::Result<connection::FrameData, connection::ControlError> RuntimeApplication::captureFrameData() {
  using ResultT = atlantis::Result<connection::FrameData, connection::ControlError>;
  // A frame has drawn only once the window-sized targets exist.
  if (!scene_ || !lastSeenExtent_.has_value() || !fallbackMaterial_) {
    return ResultT::Err(connection::ControlError::NotRendering);
  }
  const std::optional<ActiveCameraInput> activeCamera = collectActiveCamera(*scene_);
  if (!activeCamera.has_value()) return ResultT::Err(connection::ControlError::NotRendering);
  const atlantis::rhi::Extent2D extent = *lastSeenExtent_;
  const float aspect =
      extent.height != 0 ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
  const auto camera = extractCameraMatrices(activeCamera->worldMatrix, activeCamera->camera.fovYRadians,
                                            activeCamera->camera.nearZ, activeCamera->camera.farZ, aspect);
  const auto lighting = extractFrameLightingData(collectLights(*scene_));
  if (camera.isErr() || lighting.isErr()) return ResultT::Err(connection::ControlError::NotRendering);
  const std::vector<DrawItem> drawItems = assembleCaptureDrawItems(collectRenderables(*scene_));
  return ResultT::Ok(detail::toFrameData(lighting.value(), camera.value(), drawItems.size()));
}

}  // namespace atlantis::runtime
