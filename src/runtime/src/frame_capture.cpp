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

#include <atlantis/profile.h>
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
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <cstring>
#include <span>
#include <unordered_set>
#include <vector>

namespace atlantis::runtime {

namespace connection = atlantis::connection;
using atlantis::renderer::DrawItem;

namespace detail {

struct RenderLookResources {
  std::unique_ptr<rhi::Buffer> camera;
  std::unique_ptr<rhi::Buffer> shadowSpace;
  std::unique_ptr<rhi::ShadowMap> shadow;
  std::unique_ptr<rhi::Texture> depth;
  std::unique_ptr<rhi::HdrColorTarget> hdr;
  std::optional<renderer::BloomTargets> bloom;
  std::array<std::unique_ptr<rhi::Pipeline>, renderer::kBloomPipelineCount> bloomPipelines;
  std::unique_ptr<rhi::Buffer> readback;
  std::unique_ptr<rhi::OffscreenTarget> output;
  std::unique_ptr<rhi::RenderTarget> target;
};

connection::FrameData toFrameData(const FrameLightingData& lighting, const CameraMatrices& camera,
                                  std::size_t drawItemCount, float exposureCompensationEv) {
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
  if (std::isfinite(exposureCompensationEv)) data.exposureCompensationEv = exposureCompensationEv;
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

std::optional<std::vector<std::uint8_t>> encodeCapturePng(
    std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba, std::size_t cap) {
  struct Sink {
    std::size_t cap;
    std::vector<std::uint8_t> bytes;
    bool overrun = false;
  } sink{cap, {}};
  const auto write = [](void* context, void* data, int size) {
    auto& s = *static_cast<Sink*>(context);
    const auto count = static_cast<std::size_t>(size);
    if (s.overrun || count > s.cap - s.bytes.size()) {
      s.overrun = true;
      return;
    }
    const auto* first = static_cast<const std::uint8_t*>(data);
    s.bytes.insert(s.bytes.end(), first, first + count);
  };
  if (stbi_write_png_to_func(write, &sink, static_cast<int>(width), static_cast<int>(height), 4,
                             rgba.data(), static_cast<int>(width) * 4) == 0 || sink.overrun) {
    return std::nullopt;
  }
  return std::move(sink.bytes);
}

}  // namespace detail

namespace {

constexpr rhi::Extent2D kLookExtent{connection::kRenderLookCaptureWidth, connection::kRenderLookCaptureHeight};
constexpr std::size_t kLookBytes = static_cast<std::size_t>(kLookExtent.width) * kLookExtent.height * 4;

bool recordLookReadback(rhi::CommandList& commands, rhi::RenderTarget& target, rhi::Buffer& readback) {
  render_graph::RenderGraphBuilder graph;
  const auto image = graph.declareResource("look-output");
  const auto copy = graph.declarePass("look-readback");
  graph.writes(copy, image, rhi::ResourceState::TransferSource);
  graph.setExecute(copy, [&](rhi::CommandList& cmd) { cmd.copyRenderTargetToBuffer(target, readback); });
  const auto compiled = graph.compile();
  if (compiled.isErr()) return false;
  const std::vector bindings{render_graph::ResourceBinding{
      .resource = compiled.value().resourceAt(0), .target = &target,
      .incomingState = rhi::ResourceState::TransferSource, .finalState = std::nullopt}};
  render_graph::execute(compiled.value(), bindings, commands);
  return commands.recordingValid();
}

std::FILE* openFresh(const std::string& path) {
#if defined(_WIN32)
  std::FILE* file = nullptr;
  return fopen_s(&file, path.c_str(), "wbx") == 0 ? file : nullptr;
#else
  return std::fopen(path.c_str(), "wbx");
#endif
}

bool saveLookPair(const std::array<std::string, 2>& paths,
                  const std::array<std::vector<std::uint8_t>, 2>& pngs) {
  std::FILE* first = openFresh(paths[0]);
  if (first == nullptr) return false;
  std::FILE* second = openFresh(paths[1]);
  const bool opened = second != nullptr;
  bool written = opened;
  if (opened) {
    written = std::fwrite(pngs[0].data(), 1, pngs[0].size(), first) == pngs[0].size() &&
              std::fwrite(pngs[1].data(), 1, pngs[1].size(), second) == pngs[1].size();
    if (std::fclose(second) != 0) written = false;
  }
  if (std::fclose(first) != 0) written = false;
  if (!written) {
    std::error_code ignored;
    std::filesystem::remove(paths[0], ignored);
    if (opened) std::filesystem::remove(paths[1], ignored);
  }
  return written;
}

void writeLookUniforms(detail::RenderLookResources& scratch, const CameraMatrices& camera,
                       const FrameLightingData& lights, const ActiveCameraInput& input,
                       const std::array<float, 36>* irradiance) {
  auto* data = static_cast<std::byte*>(scratch.camera->mappedData());
  const auto position = extractCameraWorldPosition(input.worldMatrix);
  std::memcpy(data, &camera, sizeof(camera));
  std::memcpy(data + kCameraUniformLightingOffsetBytes, &lights, sizeof(lights));
  std::memcpy(data + kCameraUniformWorldPositionOffsetBytes, &position, sizeof(position));
  auto* sh = reinterpret_cast<float*>(data + kCameraUniformIrradianceShOffsetBytes);
  writeEnvironmentIrradianceSh(std::span<float, 36>(sh, 36), irradiance);
  CameraMatrices shadow{identityMatrix(), identityMatrix()};
  if (lights.directionalLightCount != 0) {
    const auto& dir = lights.directionalLights[0].direction;
    shadow = computeShadowLightSpaceMatrices(Vec3{dir[0], dir[1], dir[2]});
  }
  std::memcpy(data + kCameraUniformLightSpaceOffsetBytes, &shadow, sizeof(shadow));
  std::memcpy(scratch.shadowSpace->mappedData(), &shadow, sizeof(shadow));
  const auto fog = extractFogData(input.camera.fog);
  const auto clusters = buildClusterLightData(lights, camera);
  std::memcpy(data + kCameraUniformFogOffsetBytes, &fog, sizeof(fog));
  std::memcpy(data + kCameraUniformClusterOffsetBytes, &clusters, sizeof(clusters));
}

}  // namespace

connection::RenderLookCaptureResult RuntimeApplication::captureRenderLook(
    const connection::RenderLookCaptureRequest& request,
    const std::array<asset_system::ValidatedSceneData, 2>& looks, const BootstrapConfig& config) {
  using E = connection::RenderLookCaptureError;
  using R = connection::RenderLookCaptureResult;
  if (lifecycle_.state() != RuntimeLifecycleState::Running || !device_ || !lastSeenFormat_ || environmentData_)
    return R::Err(E::Unavailable);
  if (incompleteRenderLook_) return R::Err(E::Busy);
  if (request.generation != sceneGeneration()) return R::Err(E::StaleProjection);
  if (request.paths[0].empty() || request.paths[1].empty() || request.paths[0] == request.paths[1] ||
      std::ranges::any_of(request.paths, [](const std::string& path) {
        return path.size() > 2048 || path.find('\0') != std::string::npos || !std::filesystem::path(path).is_absolute();
      })) return R::Err(E::InvalidRequest);
  for (const auto& path : request.paths) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec) || ec) return R::Err(E::InvalidRequest);
  }
  auto& output = isSrgbFormat(*lastSeenFormat_) ? outputTransformSrgbPipeline_ : outputTransformUnormPipeline_;
  if (!output || !shadowCastPipeline_ || !shadowMapSampler_ || !fallbackMaterial_)
    return R::Err(E::Unavailable);
  const auto camera = extractCameraMatrices(request.camera.worldMatrix, request.camera.fovYRadians,
                                             request.camera.nearZ, request.camera.farZ, 16.0f / 9.0f);
  if (camera.isErr()) return R::Err(E::InvalidRequest);

  struct Variant {
    ActiveCameraInput camera;
    FrameLightingData lighting;
    std::vector<DrawItem> items;
    std::vector<rhi::Pipeline*> pipelines;
  };
  std::array<Variant, 2> variants{};
  bool wantsBloom = false;
  for (std::size_t i = 0; i < variants.size(); ++i) {
    auto baked = world::bakeScene(looks[i]);
    const auto active = collectActiveCamera(baked);
    const auto lighting = extractFrameLightingData(collectLights(baked));
    if (!active || lighting.isErr()) return R::Err(E::InvalidRequest);
    const auto renderables = collectRenderables(baked);
    for (const auto& item : renderables) {
      if (!meshResourceMap_.contains(item.renderable.meshAsset) ||
          (item.renderable.materialAsset && !materialResourceMap_.contains(*item.renderable.materialAsset)))
        return R::Err(E::Unavailable);
    }
    auto& variant = variants[i];
    variant.camera = *active;
    variant.camera.worldMatrix = request.camera.worldMatrix;
    variant.lighting = lighting.value();
    variant.items = assembleCaptureDrawItems(renderables);
    wantsBloom |= active->camera.bloom.strength > 0.0f;
  }

  auto scratch = std::make_shared<detail::RenderLookResources>();
  if (wantsBloom && !bloomPipelines_.front()) {
    auto localPipelines = createRenderLookBloomPipelines(config);
    if (!localPipelines) return R::Err(E::Unavailable);
    scratch->bloomPipelines = std::move(*localPipelines);
  }
  const auto& bloomPipelines = scratch->bloomPipelines.front() ? scratch->bloomPipelines : bloomPipelines_;
  for (auto& variant : variants) {
    std::unordered_set<rhi::Pipeline*> distinct;
    const auto add = [&](rhi::Pipeline* p) {
      if (p && distinct.insert(p).second) variant.pipelines.push_back(p);
    };
    add(output.get());
    add(shadowCastPipeline_.get());
    add(skyPipeline_.get());
    for (const auto& item : variant.items) add(&item.material->pipeline());
    if (variant.camera.camera.bloom.strength > 0.0f)
      for (const auto& p : bloomPipelines) add(p.get());
    const auto checked = device_->checkIsolatedBindings(variant.pipelines);
    if (checked.isErr())
      return R::Err(checked.error() == rhi::CommandListCreateError::IsolatedBindingBudgetExceeded
                        ? E::BudgetExceeded : E::CaptureFailed);
  }
  if (device_->waitIdle().isErr()) {
    lifecycle_.markFailed();
    return R::Err(E::CaptureFailed);
  }
  auto uniform = device_->createBuffer({.purpose = rhi::BufferPurpose::Uniform, .sizeBytes = kCameraUniformBufferSizeBytes});
  auto shadowUniform = device_->createBuffer({.purpose = rhi::BufferPurpose::Uniform, .sizeBytes = sizeof(CameraMatrices)});
  auto shadow = device_->createShadowMap({.extent = {1024, 1024}});
  auto depth = device_->createTexture({.extent = kLookExtent, .format = rhi::DepthFormat::D32Sfloat});
  auto hdr = device_->createHdrColorTarget({.extent = kLookExtent});
  auto readback = device_->createBuffer({.purpose = rhi::BufferPurpose::Readback, .sizeBytes = kLookBytes});
  auto target = device_->createOffscreenTarget({.extent = kLookExtent, .format = *lastSeenFormat_});
  if (uniform.isErr() || shadowUniform.isErr() || shadow.isErr() || depth.isErr() || hdr.isErr() ||
      readback.isErr() || target.isErr()) return R::Err(E::CaptureFailed);
  scratch->camera = std::move(uniform.value());
  scratch->shadowSpace = std::move(shadowUniform.value());
  scratch->shadow = std::move(shadow.value());
  scratch->depth = std::move(depth.value());
  scratch->hdr = std::move(hdr.value());
  scratch->readback = std::move(readback.value());
  scratch->output = std::move(target.value());
  if (wantsBloom) {
    auto bloom = renderer::createBloomTargets(*device_, kLookExtent);
    if (bloom.isErr()) return R::Err(E::CaptureFailed);
    scratch->bloom.emplace(std::move(bloom.value()));
  }
  auto borrow = scratch->output->acquireTarget();
  if (borrow.isErr()) return R::Err(E::CaptureFailed);
  scratch->target = std::move(borrow.value());

  std::optional<renderer::EnvironmentLighting> environment;
  if (environmentLightingResources_) environment.emplace(environmentLightingResources_->borrowedView());
  connection::RenderLookCapturedPair result{request.document, request.apply, request.generation,
      request.basis.activeCamera, {}, {}, request.looks};
  std::array<std::vector<std::uint8_t>, 2> pngs;
  for (std::size_t i = 0; i < variants.size(); ++i) {
    const auto& variant = variants[i];
    const auto& active = variant.camera;
    writeLookUniforms(*scratch, camera.value(), variant.lighting, active,
        environmentLightingResources_ ? &environmentLightingResources_->irradianceSh : nullptr);
    auto commands = device_->createIsolatedCommandList(variant.pipelines);
    if (commands.isErr()) return R::Err(E::CaptureFailed);
    std::optional<renderer::BloomInput> bloom;
    if (active.camera.bloom.strength > 0.0f) {
      bloom.emplace(renderer::BloomInput{.targets = *scratch->bloom,
          .strength = active.camera.bloom.strength, .threshold = active.camera.bloom.threshold});
      for (std::size_t p = 0; p < bloomPipelines.size(); ++p) bloom->pipelines[p] = bloomPipelines[p].get();
    }
    const auto position = extractCameraWorldPosition(active.worldMatrix);
    const auto casters = variant.lighting.directionalLightCount == 0 ? std::span<const DrawItem>{} : std::span(variant.items);
    renderer_.drawFrame(*commands.value(), *scratch->target, *scratch->depth, *scratch->camera, variant.items,
        rhi::ResourceState::TransferSource, *scratch->hdr, *fullscreenTriangleVertexBuffer_,
        *fullscreenTriangleIndexBuffer_, *output, *outputTransformSampler_, active.camera.exposureCompensationEv,
        environment ? &*environment : nullptr, skyPipeline_.get(), *scratch->shadow,
        *shadowMapSampler_, *shadowCastPipeline_, *scratch->shadowSpace, casters,
        std::array{position.x, position.y, position.z}, bloom ? &*bloom : nullptr);
    if (!recordLookReadback(*commands.value(), *scratch->target, *scratch->readback))
      return R::Err(E::CaptureFailed);
    const auto submitted = device_->submit(std::move(commands.value()), *scratch->target);
    if (submitted.isErr()) {
      if (submitted.error() == rhi::SubmitError::DeviceLost) lifecycle_.markFailed();
      return R::Err(E::CaptureFailed);
    }
    if (device_->waitIdle().isErr()) {
      incompleteRenderLook_ = std::move(scratch);
      lifecycle_.markFailed();
      return R::Err(E::CaptureFailed);
    }
    const auto* pixels = static_cast<const std::uint8_t*>(scratch->readback->mappedData());
    std::vector<std::uint8_t> rgba(pixels, pixels + kLookBytes);
    const bool bgra = *lastSeenFormat_ == rhi::Format::Bgra8Srgb || *lastSeenFormat_ == rhi::Format::Bgra8Unorm;
    for (std::size_t p = 0; p < rgba.size(); p += 4) {
      if (bgra) std::swap(rgba[p], rgba[p + 2]);
      rgba[p + 3] = 255;
    }
    auto png = detail::encodeCapturePng(kLookExtent.width, kLookExtent.height, rgba, kLookBytes * 2);
    if (!png) return R::Err(E::CaptureFailed);
    pngs[i] = std::move(*png);
    result.frames[i] = detail::toFrameData(variant.lighting, camera.value(), variant.items.size(),
                                           active.camera.exposureCompensationEv);
    result.images[i] = {request.paths[i], kLookExtent.width, kLookExtent.height};
  }
  if (!saveLookPair(request.paths, pngs)) return R::Err(E::CaptureFailed);
  return R::Ok(std::move(result));
}

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
          checkInvertibleNormalTransform(renderable.worldMatrix).isErr()) {
        continue;  // runFrame()'s skip (ADR-0134 D2)
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
  std::optional<ActiveCameraInput> activeCamera = collectActiveCamera(*scene_);
  if (!activeCamera.has_value()) return ResultT::Err(connection::ControlError::NotRendering);
  // Plan 0062 P4 (Spec 0062 ruling Q3, F-a): the camera the frame drew.
  if (overlay_ || controlCamera_) activeCamera = viewportFrameCamera(*activeCamera);
  const atlantis::rhi::Extent2D extent = *lastSeenExtent_;
  const float aspect =
      extent.height != 0 ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
  const auto camera = extractCameraMatrices(activeCamera->worldMatrix, activeCamera->camera.fovYRadians,
                                            activeCamera->camera.nearZ, activeCamera->camera.farZ, aspect);
  const auto lighting = extractFrameLightingData(collectLights(*scene_));
  if (camera.isErr() || lighting.isErr()) return ResultT::Err(connection::ControlError::NotRendering);
  const std::vector<DrawItem> drawItems = assembleCaptureDrawItems(collectRenderables(*scene_));
  return ResultT::Ok(detail::toFrameData(lighting.value(), camera.value(), drawItems.size(),
                                        activeCamera->camera.exposureCompensationEv));
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
    const std::string& path, const std::optional<connection::ViewportOverlay>& overlay) {
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
  std::optional<ActiveCameraInput> activeCamera = collectActiveCamera(*scene_);
  if (!activeCamera.has_value()) return ResultT::Err(connection::ControlError::NotRendering);
  // Plan 0062 P4 (Spec 0062 ruling Q3, F-a): the camera the frame drew.
  if (overlay_ || controlCamera_) activeCamera = viewportFrameCamera(*activeCamera);
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
  // Plan 0061 Stage 2: the cluster region, as runFrame() writes it.
  const ClusterLightData clusterLightData =
      buildClusterLightData(lighting.value(), camera.value());
  std::memcpy(cameraData + kCameraUniformClusterOffsetBytes / sizeof(float), &clusterLightData, sizeof(clusterLightData));

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

  auto offscreen = device_->createOffscreenTarget(
      {.extent = extent, .format = *lastSeenFormat_, .debugName = ATL_DEBUG_NAME("capture_target")});
  auto readback = device_->createBuffer({.purpose = atlantis::rhi::BufferPurpose::Readback,
                                         .sizeBytes = static_cast<std::size_t>(extent.width) * extent.height * 4,
                                         .debugName = ATL_DEBUG_NAME("capture_readback")});
  auto commandList = device_->createCommandList();
  if (offscreen.isErr() || readback.isErr() || commandList.isErr()) {
    return ResultT::Err(connection::ControlError::CaptureFailed);
  }
  auto acquired = offscreen.value()->acquireTarget();
  if (acquired.isErr()) return ResultT::Err(connection::ControlError::CaptureFailed);
  std::unique_ptr<atlantis::rhi::RenderTarget> target = std::move(acquired.value());

  // Plan 0066 P4 (Spec 0066 R5; ADR-0132 D2): the requester's own overlay,
  // when its step asked -- never the window's union -- drawn as runFrame()
  // draws one; without it, the capture is exactly as before.
  const std::optional<atlantis::renderer::ViewportOverlayDrawList> overlayList =
      overlay.has_value() ? viewportOverlayDrawList(*overlay, *lastSeenFormat_) : std::nullopt;
  const atlantis::renderer::FrameEndStates sceneEnd = renderer_.drawFrame(*commandList.value(), *target, *depthTexture_, *cameraBuffer_, drawItems,
                      overlayList ? ResourceState::ColorAttachmentOutput : ResourceState::TransferSource, *hdrColorTarget_, *fullscreenTriangleVertexBuffer_,
                      *fullscreenTriangleIndexBuffer_, *outputTransformPipeline, *outputTransformSampler_,
                      activeCamera->camera.exposureCompensationEv,
                      environmentLightingView.has_value() ? &*environmentLightingView : nullptr, skyPipeline_.get(),
                      *shadowMap_, *shadowMapSampler_, *shadowCastPipeline_, *shadowLightSpaceBuffer_,
                      hasDirectionalLight ? std::span<const DrawItem>(drawItems) : std::span<const DrawItem>(),
                      cameraWorldPosition, bloomInput.has_value() ? &*bloomInput : nullptr);
  if (overlayList) {
    static_cast<void>(drawViewportOverlayInto(*commandList.value(), *target, sceneEnd, ResourceState::TransferSource,
                                              camera.value(), *overlayList));
  }

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
