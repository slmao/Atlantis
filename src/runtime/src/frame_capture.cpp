#include "frame_capture.h"

// Plan 0055 P1/P8 (ADR-0106 D5): the PNG encoder for captures. Static
// implementation (STB_IMAGE_WRITE_STATIC): every stb_image_write symbol has
// internal linkage in this translation unit, so it can never collide with
// another implementation TU linked into the same binary (the image-regression
// support library's png_codec.cpp, the asset cooker's).
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
// Third-party code, not warning-clean under /W4 /WX: suppressed for this
// include only, as tests/image_regression/support/png_codec.cpp does.
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/runtime/environment_realization.h>
#include <atlantis/runtime/material_realization.h>
#include <atlantis/runtime/runtime_application.h>
#include <atlantis/runtime/scene_extraction.h>

#include <algorithm>
#include <array>
#include <optional>
#include <cstring>
#include <span>
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

bool writePng(const std::string& path, std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> pixels,
              bool bgra) {
  std::vector<std::uint8_t> rgba(pixels.begin(), pixels.end());
  if (bgra) {
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) std::swap(rgba[i], rgba[i + 2]);
  }
  for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 0xFF;  // the swapchain's alpha is not meaningful
  return stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data(),
                        static_cast<int>(width) * 4) != 0;
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

// Plan 0055 P8 (ADR-0106 D5, ruling Q2 C2): the world as the last frame drew
// it, rendered once more -- into an OffscreenTarget of the presentation's
// extent and format, so the existing output-transform Pipeline and the
// window-sized depth, HDR and bloom targets are reused as they are -- then
// read back (ADR-0040) and written as PNG. Between frames, after waitIdle();
// one extra render, on capture only. The camera uniform is rewritten as
// runFrame() writes it (a frame that drew nothing -- minimized -- leaves it
// stale); the draw arguments are assembled as runFrame() assembles them.
// Duplicated, not refactored out of runFrame() (P7): the frame-data equality
// and image determinism tests pin the two together.
atlantis::Result<connection::CapturedImage, connection::ControlError> RuntimeApplication::captureImage(
    const std::string& path) {
  using ResultT = atlantis::Result<connection::CapturedImage, connection::ControlError>;
  using atlantis::rhi::ResourceState;
  if (!scene_ || !device_ || !presentation_ || !lastSeenFormat_.has_value() || !lastSeenExtent_.has_value() ||
      !depthTexture_ || !hdrColorTarget_ || !fallbackMaterial_ || environmentData_.has_value()) {
    return ResultT::Err(connection::ControlError::NotRendering);
  }
  atlantis::rhi::Pipeline* outputTransformPipeline =
      isSrgbFormat(*lastSeenFormat_) ? outputTransformSrgbPipeline_.get() : outputTransformUnormPipeline_.get();
  if (outputTransformPipeline == nullptr) return ResultT::Err(connection::ControlError::NotRendering);
  const atlantis::rhi::Extent2D extent = *lastSeenExtent_;

  // The camera uniform, as runFrame() writes it (the SH9 tail is the
  // realized environment's, unchanged since its first frame).
  const std::optional<ActiveCameraInput> activeCamera = collectActiveCamera(*scene_);
  if (!activeCamera.has_value()) return ResultT::Err(connection::ControlError::NotRendering);
  const float aspect =
      extent.height != 0 ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
  const auto camera = extractCameraMatrices(activeCamera->worldMatrix, activeCamera->camera.fovYRadians,
                                            activeCamera->camera.nearZ, activeCamera->camera.farZ, aspect);
  const auto lighting = extractFrameLightingData(collectLights(*scene_));
  if (camera.isErr() || lighting.isErr()) return ResultT::Err(connection::ControlError::NotRendering);

  if (device_->waitIdle().isErr()) return ResultT::Err(connection::ControlError::CaptureFailed);
  auto* cameraData = static_cast<float*>(cameraBuffer_->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = camera.value().view[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = camera.value().projection[i];
  *reinterpret_cast<FrameLightingData*>(cameraData + 32) = lighting.value();
  auto* cameraWorldPositionData =
      reinterpret_cast<CameraWorldPositionData*>(cameraData + kCameraUniformWorldPositionOffsetBytes / sizeof(float));
  *cameraWorldPositionData = extractCameraWorldPosition(activeCamera->worldMatrix);
  const std::array<float, 3> cameraWorldPosition{cameraWorldPositionData->x, cameraWorldPositionData->y,
                                                 cameraWorldPositionData->z};
  const bool hasDirectionalLight = lighting.value().directionalLightCount > 0;
  Mat4 lightSpaceView = identityMatrix();
  Mat4 lightSpaceProjection = identityMatrix();
  if (hasDirectionalLight) {
    const auto& gpuDirection = lighting.value().directionalLights[0].direction;
    const CameraMatrices lightSpace =
        computeShadowLightSpaceMatrices(Vec3{gpuDirection[0], gpuDirection[1], gpuDirection[2]});
    lightSpaceView = lightSpace.view;
    lightSpaceProjection = lightSpace.projection;
  }
  float* lightSpaceTail = cameraData + kCameraUniformLightSpaceOffsetBytes / sizeof(float);
  std::memcpy(lightSpaceTail, lightSpaceView.data(), sizeof(float) * 16);
  std::memcpy(lightSpaceTail + 16, lightSpaceProjection.data(), sizeof(float) * 16);
  auto* shadowLightSpaceData = static_cast<float*>(shadowLightSpaceBuffer_->mappedData());
  std::memcpy(shadowLightSpaceData, lightSpaceView.data(), sizeof(float) * 16);
  std::memcpy(shadowLightSpaceData + 16, lightSpaceProjection.data(), sizeof(float) * 16);
  *reinterpret_cast<FogData*>(cameraData + kCameraUniformFogOffsetBytes / sizeof(float)) =
      extractFogData(activeCamera->camera.fog);

  // The draw arguments, as runFrame() assembles them.
  const std::vector<DrawItem> drawItems = assembleCaptureDrawItems(collectRenderables(*scene_));
  std::optional<atlantis::renderer::EnvironmentLighting> environmentLightingView;
  if (environmentLightingResources_.has_value()) {
    environmentLightingView.emplace(environmentLightingResources_->borrowedView());
  }
  std::optional<atlantis::renderer::BloomInput> bloomInput;
  if (activeCamera->camera.bloom.strength > 0.0f && bloomTargets_.has_value() && bloomPipelines_[0]) {
    bloomInput.emplace(atlantis::renderer::BloomInput{.targets = *bloomTargets_,
                                                      .strength = activeCamera->camera.bloom.strength,
                                                      .threshold = activeCamera->camera.bloom.threshold});
    for (std::size_t i = 0; i < bloomPipelines_.size(); ++i) bloomInput->pipelines[i] = bloomPipelines_[i].get();
  }

  auto offscreen = device_->createOffscreenTarget({.extent = extent, .format = *lastSeenFormat_});
  auto readback = device_->createBuffer({.purpose = atlantis::rhi::BufferPurpose::Readback,
                                         .sizeBytes = static_cast<std::size_t>(extent.width) * extent.height * 4});
  auto commandList = device_->createCommandList();
  if (offscreen.isErr() || readback.isErr() || commandList.isErr()) {
    return ResultT::Err(connection::ControlError::CaptureFailed);
  }
  auto acquired = offscreen.value()->acquireTarget();
  if (acquired.isErr()) return ResultT::Err(connection::ControlError::CaptureFailed);
  std::unique_ptr<atlantis::rhi::RenderTarget> target = std::move(acquired.value());

  renderer_.drawFrame(*commandList.value(), *target, *depthTexture_, *cameraBuffer_, drawItems,
                      ResourceState::TransferSource, *hdrColorTarget_, *fullscreenTriangleVertexBuffer_,
                      *fullscreenTriangleIndexBuffer_, *outputTransformPipeline, *outputTransformSampler_,
                      activeCamera->camera.exposureCompensationEv,
                      environmentLightingView.has_value() ? &*environmentLightingView : nullptr, skyPipeline_.get(),
                      *shadowMap_, *shadowMapSampler_, *shadowCastPipeline_, *shadowLightSpaceBuffer_,
                      hasDirectionalLight ? std::span<const DrawItem>(drawItems) : std::span<const DrawItem>(),
                      cameraWorldPosition, bloomInput.has_value() ? &*bloomInput : nullptr);

  // The readback copy, as a RenderGraph pass (the image-regression fixtures'
  // shape): the target is already in TransferSource.
  atlantis::render_graph::RenderGraphBuilder copyBuilder;
  const auto copyResource = copyBuilder.declareResource("capture-color");
  const auto copyPass = copyBuilder.declarePass("capture-copy-to-buffer");
  copyBuilder.writes(copyPass, copyResource, ResourceState::TransferSource);
  atlantis::rhi::Buffer& readbackBuffer = *readback.value();
  copyBuilder.setExecute(copyPass, [&target, &readbackBuffer](atlantis::rhi::CommandList& cmd) {
    cmd.copyRenderTargetToBuffer(*target, readbackBuffer);
  });
  auto compiled = copyBuilder.compile();
  if (compiled.isErr()) return ResultT::Err(connection::ControlError::CaptureFailed);
  const std::vector<atlantis::render_graph::ResourceBinding> bindings{
      {.resource = compiled.value().resourceAt(0),
       .target = target.get(),
       .incomingState = ResourceState::TransferSource,
       .finalState = std::nullopt}};
  atlantis::render_graph::execute(compiled.value(), bindings, *commandList.value());

  auto submitted = device_->submit(std::move(commandList.value()), *target);
  if (submitted.isErr() || device_->waitIdle().isErr()) return ResultT::Err(connection::ControlError::CaptureFailed);
  target.reset();  // ends the borrow (ADR-0038)

  const auto* pixels = static_cast<const std::uint8_t*>(readbackBuffer.mappedData());
  const bool bgra =
      *lastSeenFormat_ == atlantis::rhi::Format::Bgra8Unorm || *lastSeenFormat_ == atlantis::rhi::Format::Bgra8Srgb;
  if (!detail::writePng(path, extent.width, extent.height,
                        std::span<const std::uint8_t>(pixels, static_cast<std::size_t>(extent.width) * extent.height * 4),
                        bgra)) {
    return ResultT::Err(connection::ControlError::CaptureFailed);
  }
  return ResultT::Ok(connection::CapturedImage{path, extent.width, extent.height});
}

}  // namespace atlantis::runtime
