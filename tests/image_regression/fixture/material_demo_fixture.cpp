#include "material_demo_fixture.h"

#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/runtime/material_realization.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/runtime/scene_load.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <type_traits>
#include <utility>
#include <vector>

// Plan 0018 Milestone 16 (Spec 0018 D12): see material_demo_fixture.h's own
// top-of-file comment -- this file calls Atlantis::RuntimeHost's real
// loadAndInstantiateScene()/computePendingMaterialIds()/
// realizePendingMaterials()/extractCameraMatrices()/resolveMeshAsset()/
// resolveMaterialAsset() directly, never re-implementing any of them.

namespace atlantis::image_regression {

namespace {

using atlantis::renderer::DrawItem;
using atlantis::renderer::Renderer;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Extent2D;
using atlantis::rhi::VertexInputLayout;
using atlantis::runtime::computePendingMaterialIds;
using atlantis::runtime::extractCameraMatrices;
using atlantis::runtime::loadAndInstantiateScene;
using atlantis::runtime::realizePendingMaterials;
using atlantis::runtime::RealizedMaterialCandidate;
using atlantis::runtime::resolveMeshAsset;
using atlantis::runtime::resolveMaterialAsset;
using atlantis::runtime::SceneLoadOutcome;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;

[[nodiscard]] std::optional<std::vector<std::uint32_t>> loadSpirvFile(const char* path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return std::nullopt;

  const std::streamsize sizeBytes = file.tellg();
  if (sizeBytes <= 0 || sizeBytes % 4 != 0) return std::nullopt;
  file.seekg(0);

  std::vector<std::uint32_t> words(static_cast<std::size_t>(sizeBytes) / 4);
  if (!file.read(reinterpret_cast<char*>(words.data()), sizeBytes)) return std::nullopt;
  return words;
}

// Duplicated, not shared -- matches textured_quad_fixture.cpp's own
// identical Vertex schema exactly (Spec 0020's 44-byte position+color+
// UV0+normal mesh artifact layout; color and normal are deliberately
// left unread since textured_quad.slang has neither a color nor a
// normal input).
struct Vertex {
  float position[3];
  float color[3];
  float uv[2];
  float normal[3];
};
static_assert(std::is_standard_layout_v<Vertex>);
static_assert(offsetof(Vertex, position) == atlantis::asset_system::kMeshArtifactPositionOffsetBytes);
static_assert(offsetof(Vertex, color) == atlantis::asset_system::kMeshArtifactColorOffsetBytes);
static_assert(offsetof(Vertex, uv) == atlantis::asset_system::kMeshArtifactUv0OffsetBytes);
static_assert(offsetof(Vertex, normal) == atlantis::asset_system::kMeshArtifactNormalOffsetBytes);
static_assert(sizeof(Vertex) == atlantis::asset_system::kMeshArtifactVertexStrideBytes);

[[nodiscard]] std::optional<VertexInputLayout> unlitTexturedVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// Plan 0019 Section P6: mirrors runtime_application.cpp's own identical
// litTexturedVertexLayout() -- position@0, uv@1, normal@2.
[[nodiscard]] std::optional<VertexInputLayout> litTexturedVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
      MeshVertexAttributeSchema{.location = 2, .offsetBytes = offsetof(Vertex, normal)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// Plan 0023 Milestone 5: realizePendingMaterials()'s own further-widened
// signature requires a real pbrDirectLit* trio too -- byte-identical
// schema to litTexturedVertexLayout() above (pbr_direct_lit.slang's own
// vertex input matches lit_textured.slang's exactly, Milestone 4).
[[nodiscard]] std::optional<VertexInputLayout> pbrDirectLitVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
      MeshVertexAttributeSchema{.location = 2, .offsetBytes = offsetof(Vertex, normal)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// Plan 0024 Milestone 7: the output-transform pass's own fixed vertex
// schema -- NOT sourced from the mesh Vertex struct above, mirrors
// runtime_application.cpp's own identical outputTransformVertexLayout().
[[nodiscard]] std::optional<VertexInputLayout> outputTransformVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = 0},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(float) * 2);
  if (result.isErr()) return std::nullopt;
  return result.value();
}

}  // namespace

atlantis::Result<MaterialDemoFixture, MaterialDemoSetupError> setUpMaterialDemoFixture(
    const atlantis::runtime::BootstrapConfig& config) {
  using ResultT = atlantis::Result<MaterialDemoFixture, MaterialDemoSetupError>;

  auto vertexSpirv = loadSpirvFile(config.unlitTexturedVertexShaderSpirvPath.c_str());
  auto fragmentSpirv = loadSpirvFile(config.unlitTexturedFragmentShaderSpirvPath.c_str());
  if (!vertexSpirv.has_value() || !fragmentSpirv.has_value()) {
    return ResultT::Err(MaterialDemoSetupError::ShaderLoadFailed);
  }

  auto vertexReflectionResult = loadReflectionMetadata(config.unlitTexturedVertexShaderReflectionPath.c_str());
  if (vertexReflectionResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ShaderLoadFailed);
  const auto vertexInputLayout = unlitTexturedVertexLayout(vertexReflectionResult.value());
  if (!vertexInputLayout.has_value()) return ResultT::Err(MaterialDemoSetupError::ShaderLoadFailed);

  // Plan 0019 Section P6: the litTextured* trio, loaded the same way as
  // unlitTextured* above -- see MaterialDemoFixture's own field comment
  // for why this fixture needs real (if functionally unused) data here.
  auto litVertexSpirv = loadSpirvFile(config.litTexturedVertexShaderSpirvPath.c_str());
  auto litFragmentSpirv = loadSpirvFile(config.litTexturedFragmentShaderSpirvPath.c_str());
  if (!litVertexSpirv.has_value() || !litFragmentSpirv.has_value()) {
    return ResultT::Err(MaterialDemoSetupError::ShaderLoadFailed);
  }
  auto litVertexReflectionResult = loadReflectionMetadata(config.litTexturedVertexShaderReflectionPath.c_str());
  if (litVertexReflectionResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ShaderLoadFailed);
  const auto litVertexInputLayout = litTexturedVertexLayout(litVertexReflectionResult.value());
  if (!litVertexInputLayout.has_value()) return ResultT::Err(MaterialDemoSetupError::ShaderLoadFailed);

  // Plan 0023 Milestone 5: the pbrDirectLit* trio, loaded the same way
  // as litTextured* above -- see MaterialDemoFixture's own field
  // comment for why this fixture needs real (if functionally unused)
  // data here.
  auto pbrVertexSpirv = loadSpirvFile(config.pbrDirectLitVertexShaderSpirvPath.c_str());
  auto pbrFragmentSpirv = loadSpirvFile(config.pbrDirectLitFragmentShaderSpirvPath.c_str());
  if (!pbrVertexSpirv.has_value() || !pbrFragmentSpirv.has_value()) {
    return ResultT::Err(MaterialDemoSetupError::ShaderLoadFailed);
  }
  auto pbrVertexReflectionResult = loadReflectionMetadata(config.pbrDirectLitVertexShaderReflectionPath.c_str());
  if (pbrVertexReflectionResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ShaderLoadFailed);
  const auto pbrVertexInputLayout = pbrDirectLitVertexLayout(pbrVertexReflectionResult.value());
  if (!pbrVertexInputLayout.has_value()) return ResultT::Err(MaterialDemoSetupError::ShaderLoadFailed);

  // Plan 0024 Milestone 7: the output-transform-unorm shader pair --
  // this fixture's own colorFormat is fixed Rgba8Unorm, so only this
  // one variant is ever loaded.
  auto outputTransformVertexSpirv = loadSpirvFile(config.outputTransformUnormVertexShaderSpirvPath.c_str());
  auto outputTransformFragmentSpirv = loadSpirvFile(config.outputTransformUnormFragmentShaderSpirvPath.c_str());
  if (!outputTransformVertexSpirv.has_value() || !outputTransformFragmentSpirv.has_value()) {
    return ResultT::Err(MaterialDemoSetupError::ShaderLoadFailed);
  }
  auto outputTransformVertexReflectionResult =
      loadReflectionMetadata(config.outputTransformUnormVertexShaderReflectionPath.c_str());
  if (outputTransformVertexReflectionResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ShaderLoadFailed);
  const auto outputTransformVertexInputLayout =
      outputTransformVertexLayout(outputTransformVertexReflectionResult.value());
  if (!outputTransformVertexInputLayout.has_value()) return ResultT::Err(MaterialDemoSetupError::ShaderLoadFailed);

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Image Regression Fixture (Material Demo)", .enableValidationLayers = true});
  if (deviceResult.isErr()) return ResultT::Err(MaterialDemoSetupError::DeviceCreationFailed);

  MaterialDemoFixture fixture;
  fixture.device = std::move(deviceResult.value());
  fixture.unlitTexturedVertexInputLayout = *vertexInputLayout;
  fixture.unlitTexturedVertexSpirv = std::move(*vertexSpirv);
  fixture.unlitTexturedFragmentSpirv = std::move(*fragmentSpirv);
  fixture.litTexturedVertexInputLayout = *litVertexInputLayout;
  fixture.litTexturedVertexSpirv = std::move(*litVertexSpirv);
  fixture.litTexturedFragmentSpirv = std::move(*litFragmentSpirv);
  fixture.pbrDirectLitVertexInputLayout = *pbrVertexInputLayout;
  fixture.pbrDirectLitVertexSpirv = std::move(*pbrVertexSpirv);
  fixture.pbrDirectLitFragmentSpirv = std::move(*pbrFragmentSpirv);
  fixture.outputTransformUnormVertexInputLayout = *outputTransformVertexInputLayout;
  fixture.outputTransformUnormVertexSpirv = std::move(*outputTransformVertexSpirv);
  fixture.outputTransformUnormFragmentSpirv = std::move(*outputTransformFragmentSpirv);

  // Phase 1: the real, Runtime-private CPU load/instantiate pipeline --
  // never duplicated here. loadAndInstantiateScene()'s own vertexInputLayout
  // parameter is used only by createMesh(), which never actually inspects
  // it (mesh.cpp's own "layout is not otherwise inspected" comment) -- so
  // passing the unlitTextured layout here (rather than loading a second,
  // unused minimal_mesh shader pair just to build a fallback layout this
  // scene never references) is correct, not a shortcut.
  auto sceneLoadResult = loadAndInstantiateScene(config, fixture.device.get(), *vertexInputLayout);
  if (sceneLoadResult.isErr()) return ResultT::Err(MaterialDemoSetupError::SceneLoadFailed);
  SceneLoadOutcome outcome = std::move(sceneLoadResult.value());
  fixture.world.emplace(std::move(outcome.world));
  fixture.meshResourceMap = std::move(outcome.meshResourceMap);
  fixture.materialDataMap = std::move(outcome.materialDataMap);
  fixture.textureDataMap = std::move(outcome.textureDataMap);

  // Plan 0019 Section P7: widened to match every other camera-buffer-
  // creating composition root, even though this fixture's own material
  // is UnlitTextured-only and never reads the appended tail bytes.
  auto cameraBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Uniform,
       .sizeBytes = sizeof(float) * 32 + sizeof(atlantis::runtime::FrameLightingData)});
  if (cameraBufferResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ResourceCreationFailed);
  fixture.cameraBuffer = std::move(cameraBufferResult.value());

  const Extent2D extent{kMaterialDemoExtentPixels, kMaterialDemoExtentPixels};

  auto depthTextureResult = fixture.device->createTexture({.extent = extent, .format = DepthFormat::D32Sfloat});
  if (depthTextureResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ResourceCreationFailed);
  fixture.depthTexture = std::move(depthTextureResult.value());

  auto offscreenTargetResult = fixture.device->createOffscreenTarget(
      {.extent = extent, .format = kMaterialDemoColorFormat});
  if (offscreenTargetResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ResourceCreationFailed);
  fixture.offscreenTarget = std::move(offscreenTargetResult.value());

  const std::size_t readbackSizeBytes = static_cast<std::size_t>(kMaterialDemoExtentPixels) *
                                         kMaterialDemoExtentPixels * 4;
  auto readbackBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = readbackSizeBytes});
  if (readbackBufferResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ResourceCreationFailed);
  fixture.readbackBuffer = std::move(readbackBufferResult.value());

  // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3/D-6): this fixture's own
  // HDR intermediate, fullscreen-triangle geometry/sampler, and output-
  // transform Pipeline.
  auto hdrColorTargetResult = fixture.device->createHdrColorTarget({.extent = extent});
  if (hdrColorTargetResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ResourceCreationFailed);
  fixture.hdrColorTarget = std::move(hdrColorTargetResult.value());

  const float fullscreenTriangleVertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  auto fullscreenTriangleVertexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  if (fullscreenTriangleVertexBufferResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ResourceCreationFailed);
  fixture.fullscreenTriangleVertexBuffer = std::move(fullscreenTriangleVertexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleVertexBuffer->mappedData(), fullscreenTriangleVertices,
              sizeof(fullscreenTriangleVertices));

  const std::uint16_t fullscreenTriangleIndices[3] = {0, 1, 2};
  auto fullscreenTriangleIndexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Index, .sizeBytes = sizeof(fullscreenTriangleIndices)});
  if (fullscreenTriangleIndexBufferResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ResourceCreationFailed);
  fixture.fullscreenTriangleIndexBuffer = std::move(fullscreenTriangleIndexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleIndexBuffer->mappedData(), fullscreenTriangleIndices,
              sizeof(fullscreenTriangleIndices));

  auto outputTransformSamplerResult = fixture.device->createSampler(
      {.filter = atlantis::rhi::Filter::Linear, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  if (outputTransformSamplerResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ResourceCreationFailed);
  fixture.outputTransformSampler = std::move(outputTransformSamplerResult.value());

  auto outputTransformPipelineResult = fixture.device->createPipeline(
      {.vertexShader = {.spirvWords = fixture.outputTransformUnormVertexSpirv.data(),
                         .wordCount = fixture.outputTransformUnormVertexSpirv.size()},
       .fragmentShader = {.spirvWords = fixture.outputTransformUnormFragmentSpirv.data(),
                           .wordCount = fixture.outputTransformUnormFragmentSpirv.size()},
       .vertexInputLayout = fixture.outputTransformUnormVertexInputLayout,
       .colorFormat = kMaterialDemoColorFormat,
       .hasSampledTextureBinding = true,
       .hasCameraUniformBinding = false,
       .hasDepthAttachment = false});
  if (outputTransformPipelineResult.isErr()) return ResultT::Err(MaterialDemoSetupError::ResourceCreationFailed);
  fixture.outputTransformPipeline = std::move(outputTransformPipelineResult.value());

  return ResultT::Ok(std::move(fixture));
}

atlantis::Result<PixelBuffer, MaterialDemoRenderError> renderMaterialDemoFrame(MaterialDemoFixture& fixture) {
  namespace rhi = atlantis::rhi;
  namespace render_graph = atlantis::render_graph;
  using ResultT = atlantis::Result<PixelBuffer, MaterialDemoRenderError>;

  auto acquireResult = fixture.offscreenTarget->acquireTarget();
  if (acquireResult.isErr()) return ResultT::Err(MaterialDemoRenderError::AcquireFailed);
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

  fixture.world->updateTransforms();

  const auto activeCamera = fixture.world->activeCamera();
  if (!activeCamera.has_value()) return ResultT::Err(MaterialDemoRenderError::NoActiveCamera);
  const auto cameraWorldMatrixResult = fixture.world->getWorldMatrix(*activeCamera);
  const auto cameraComponentResult = fixture.world->getCamera(*activeCamera);
  if (cameraWorldMatrixResult.isErr() || cameraComponentResult.isErr()) {
    return ResultT::Err(MaterialDemoRenderError::ExtractionFailed);
  }
  const atlantis::world::Camera cameraComponent = cameraComponentResult.value();
  const auto extractionResult = extractCameraMatrices(cameraWorldMatrixResult.value(), cameraComponent.fovYRadians,
                                                        cameraComponent.nearZ, cameraComponent.farZ, 1.0f);
  if (extractionResult.isErr()) return ResultT::Err(MaterialDemoRenderError::ExtractionFailed);

  auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = extractionResult.value().view[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = extractionResult.value().projection[i];

  // Spec 0018 D8 step 1 (Human Review Approval item 3's own determinism
  // requirement): referencedMaterialIds is collected from World's own
  // already-deterministic renderableEntities() iteration, exactly mirroring
  // runFrame()'s own identical collection loop.
  std::vector<atlantis::asset_system::AssetId> referencedMaterialIds;
  for (const auto& id : fixture.world->renderableEntities()) {
    const auto renderableResult = fixture.world->getRenderable(id);
    if (renderableResult.isErr()) continue;
    if (const auto& materialAsset = renderableResult.value().materialAsset; materialAsset.has_value()) {
      if (std::find(referencedMaterialIds.begin(), referencedMaterialIds.end(), *materialAsset) ==
          referencedMaterialIds.end()) {
        referencedMaterialIds.push_back(*materialAsset);
      }
    }
  }
  std::vector<atlantis::asset_system::AssetId> alreadyRealizedMaterialIds;
  alreadyRealizedMaterialIds.reserve(fixture.materialResourceMap.size());
  for (const auto& [assetId, material] : fixture.materialResourceMap) alreadyRealizedMaterialIds.push_back(assetId);
  const std::vector<atlantis::asset_system::AssetId> pendingMaterialIds =
      computePendingMaterialIds(referencedMaterialIds, alreadyRealizedMaterialIds);

  auto commandListResult = fixture.device->createCommandList();
  if (commandListResult.isErr()) return ResultT::Err(MaterialDemoRenderError::CommandListCreationFailed);
  std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

  // Phase 2 (Spec 0018 D8 steps 2-3): the real realization call -- records
  // any pending materials' own upload passes into commandList before the
  // draw graph below, never duplicated here.
  std::unordered_map<atlantis::asset_system::AssetId, RealizedMaterialCandidate> realizedCandidates =
      realizePendingMaterials(*fixture.device, *commandList, fixture.unlitTexturedVertexInputLayout,
                               fixture.unlitTexturedVertexSpirv, fixture.unlitTexturedFragmentSpirv,
                               fixture.litTexturedVertexInputLayout,
                               fixture.litTexturedVertexSpirv, fixture.litTexturedFragmentSpirv,
                               fixture.pbrDirectLitVertexInputLayout, fixture.pbrDirectLitVertexSpirv,
                               fixture.pbrDirectLitFragmentSpirv, pendingMaterialIds,
                               fixture.sampledTextureResourceMap, fixture.materialDataMap, fixture.textureDataMap);

  std::vector<atlantis::asset_system::AssetId> knownMaterialIds = alreadyRealizedMaterialIds;
  for (const auto& [assetId, candidate] : realizedCandidates) knownMaterialIds.push_back(assetId);

  std::vector<atlantis::asset_system::AssetId> knownMeshAssetIds;
  knownMeshAssetIds.reserve(fixture.meshResourceMap.size());
  for (const auto& [assetId, mesh] : fixture.meshResourceMap) knownMeshAssetIds.push_back(assetId);

  std::vector<DrawItem> drawItems;
  for (const auto& id : fixture.world->renderableEntities()) {
    const auto renderableResult = fixture.world->getRenderable(id);
    if (renderableResult.isErr()) continue;
    if (resolveMeshAsset(renderableResult.value().meshAsset, knownMeshAssetIds).isErr()) continue;
    const auto worldMatrixResult = fixture.world->getWorldMatrix(id);
    if (worldMatrixResult.isErr()) continue;

    // Spec 0018 D4 case 3: present-but-unresolvable is skipped for this
    // entity, never silently drawn with a fallback -- this scene declares
    // no absent-material entity at all (material_demo.scene.txt, both
    // nodes reference the same real material), so there is no fallback
    // Material anywhere in this fixture.
    const auto& materialAsset = renderableResult.value().materialAsset;
    if (!materialAsset.has_value()) continue;
    if (resolveMaterialAsset(*materialAsset, knownMaterialIds).isErr()) continue;

    const atlantis::renderer::Material* resolvedMaterial = nullptr;
    if (const auto it = realizedCandidates.find(*materialAsset); it != realizedCandidates.end()) {
      resolvedMaterial = it->second.material.get();
    } else if (const auto mapIt = fixture.materialResourceMap.find(*materialAsset);
               mapIt != fixture.materialResourceMap.end()) {
      resolvedMaterial = mapIt->second.get();
    }
    if (!resolvedMaterial) continue;

    DrawItem item;
    item.mesh = &fixture.meshResourceMap.at(renderableResult.value().meshAsset);
    item.material = resolvedMaterial;
    item.objectToWorld = worldMatrixResult.value();
    drawItems.push_back(item);
  }

  Renderer renderer;
  renderer.drawFrame(*commandList, *target, *fixture.depthTexture, *fixture.cameraBuffer, drawItems,
                      rhi::ResourceState::TransferSource, *fixture.hdrColorTarget,
                      *fixture.fullscreenTriangleVertexBuffer, *fixture.fullscreenTriangleIndexBuffer,
                      *fixture.outputTransformPipeline, *fixture.outputTransformSampler);

  render_graph::RenderGraphBuilder copyBuilder;
  const auto copyResource = copyBuilder.declareResource("color-copy");
  const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
  copyBuilder.writes(copyPass, copyResource, rhi::ResourceState::TransferSource);
  copyBuilder.setExecute(copyPass, [&target, &fixture](rhi::CommandList& cmd) {
    cmd.copyRenderTargetToBuffer(*target, *fixture.readbackBuffer);
  });
  auto copyCompileResult = copyBuilder.compile();
  if (copyCompileResult.isErr()) return ResultT::Err(MaterialDemoRenderError::CommandListCreationFailed);
  const std::vector<render_graph::ResourceBinding> copyBindings{{.resource = copyCompileResult.value().resourceAt(0),
                                                                   .target = target.get(),
                                                                   .incomingState = rhi::ResourceState::TransferSource}};
  render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  auto submitResult = fixture.device->submit(std::move(commandList), *target);
  if (submitResult.isErr()) return ResultT::Err(MaterialDemoRenderError::SubmitFailed);

  auto waitResult = fixture.device->waitIdle();
  if (waitResult.isErr()) return ResultT::Err(MaterialDemoRenderError::WaitIdleFailed);

  // Only now, after waitIdle() has confirmed this frame's own upload+draw
  // work has finished, are the newly-realized candidates published into
  // the persistent maps -- matching runFrame()'s own identical ordering,
  // unconditionally here since this fixture always waitIdle()s (it always
  // needs the readback Buffer's contents CPU-visible immediately, unlike
  // RuntimeApplication's own steady-state windowed loop).
  for (auto& [assetId, candidate] : realizedCandidates) {
    if (candidate.newSampledTexture) {
      fixture.sampledTextureResourceMap.emplace(candidate.textureAssetId, std::move(candidate.newSampledTexture));
    }
    fixture.samplerResourceMap.emplace(assetId, std::move(candidate.sampler));
    fixture.materialResourceMap.emplace(assetId, std::move(candidate.material));
  }

  PixelBuffer result;
  result.width = kMaterialDemoExtentPixels;
  result.height = kMaterialDemoExtentPixels;
  const std::size_t byteCount = static_cast<std::size_t>(kMaterialDemoExtentPixels) * kMaterialDemoExtentPixels * 4;
  const auto* readbackData = static_cast<const std::uint8_t*>(fixture.readbackBuffer->mappedData());
  result.rgba8.assign(readbackData, readbackData + byteCount);

  target.reset();  // Ends this cycle's borrow (RAII, ADR-0038) -- no release()/consume() call.

  return ResultT::Ok(std::move(result));
}

}  // namespace atlantis::image_regression
