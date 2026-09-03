#include "lighting_demo_fixture.h"

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

// Plan 0019 Section P10/Milestone 10 (Spec 0019 D10): see
// lighting_demo_fixture.h's own top-of-file comment -- this file calls
// Atlantis::RuntimeHost's real loadAndInstantiateScene()/
// computePendingMaterialIds()/realizePendingMaterials()/
// extractCameraMatrices()/resolveMeshAsset()/resolveMaterialAsset()/
// extractFrameLightingData()/checkConformalTransform() directly, never
// re-implementing any of them.

namespace atlantis::image_regression {

namespace {

using atlantis::renderer::DrawItem;
using atlantis::renderer::Renderer;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Extent2D;
using atlantis::rhi::VertexInputLayout;
using atlantis::runtime::checkConformalTransform;
using atlantis::runtime::computePendingMaterialIds;
using atlantis::runtime::extractCameraMatrices;
using atlantis::runtime::extractFrameLightingData;
using atlantis::runtime::FrameLightingData;
using atlantis::runtime::LightExtractionInput;
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

// Duplicated, not shared -- matches material_demo_fixture.cpp's own
// identical Vertex schema exactly (Spec 0020's 44-byte position+color+
// UV0+normal mesh artifact layout).
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

// Plan 0019 Section P15: position@0, uv@1, normal@2 -- matches
// runtime_application.cpp's/material_demo_fixture.cpp's own identical
// litTexturedVertexLayout().
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

atlantis::Result<LightingDemoFixture, LightingDemoSetupError> setUpLightingDemoFixture(
    const atlantis::runtime::BootstrapConfig& config) {
  using ResultT = atlantis::Result<LightingDemoFixture, LightingDemoSetupError>;

  auto vertexSpirv = loadSpirvFile(config.unlitTexturedVertexShaderSpirvPath.c_str());
  auto fragmentSpirv = loadSpirvFile(config.unlitTexturedFragmentShaderSpirvPath.c_str());
  if (!vertexSpirv.has_value() || !fragmentSpirv.has_value()) {
    return ResultT::Err(LightingDemoSetupError::ShaderLoadFailed);
  }
  auto vertexReflectionResult = loadReflectionMetadata(config.unlitTexturedVertexShaderReflectionPath.c_str());
  if (vertexReflectionResult.isErr()) return ResultT::Err(LightingDemoSetupError::ShaderLoadFailed);
  const auto vertexInputLayout = unlitTexturedVertexLayout(vertexReflectionResult.value());
  if (!vertexInputLayout.has_value()) return ResultT::Err(LightingDemoSetupError::ShaderLoadFailed);

  auto litVertexSpirv = loadSpirvFile(config.litTexturedVertexShaderSpirvPath.c_str());
  auto litFragmentSpirv = loadSpirvFile(config.litTexturedFragmentShaderSpirvPath.c_str());
  if (!litVertexSpirv.has_value() || !litFragmentSpirv.has_value()) {
    return ResultT::Err(LightingDemoSetupError::ShaderLoadFailed);
  }
  auto litVertexReflectionResult = loadReflectionMetadata(config.litTexturedVertexShaderReflectionPath.c_str());
  if (litVertexReflectionResult.isErr()) return ResultT::Err(LightingDemoSetupError::ShaderLoadFailed);
  const auto litVertexInputLayout = litTexturedVertexLayout(litVertexReflectionResult.value());
  if (!litVertexInputLayout.has_value()) return ResultT::Err(LightingDemoSetupError::ShaderLoadFailed);

  auto pbrVertexSpirv = loadSpirvFile(config.pbrDirectLitVertexShaderSpirvPath.c_str());
  auto pbrFragmentSpirv = loadSpirvFile(config.pbrDirectLitFragmentShaderSpirvPath.c_str());
  if (!pbrVertexSpirv.has_value() || !pbrFragmentSpirv.has_value()) {
    return ResultT::Err(LightingDemoSetupError::ShaderLoadFailed);
  }
  auto pbrVertexReflectionResult = loadReflectionMetadata(config.pbrDirectLitVertexShaderReflectionPath.c_str());
  if (pbrVertexReflectionResult.isErr()) return ResultT::Err(LightingDemoSetupError::ShaderLoadFailed);
  const auto pbrVertexInputLayout = pbrDirectLitVertexLayout(pbrVertexReflectionResult.value());
  if (!pbrVertexInputLayout.has_value()) return ResultT::Err(LightingDemoSetupError::ShaderLoadFailed);

  // Plan 0024 Milestone 7: the output-transform-unorm shader pair --
  // this fixture's own colorFormat is fixed Rgba8Unorm, so only this
  // one variant is ever loaded.
  auto outputTransformVertexSpirv = loadSpirvFile(config.outputTransformUnormVertexShaderSpirvPath.c_str());
  auto outputTransformFragmentSpirv = loadSpirvFile(config.outputTransformUnormFragmentShaderSpirvPath.c_str());
  if (!outputTransformVertexSpirv.has_value() || !outputTransformFragmentSpirv.has_value()) {
    return ResultT::Err(LightingDemoSetupError::ShaderLoadFailed);
  }
  auto outputTransformVertexReflectionResult =
      loadReflectionMetadata(config.outputTransformUnormVertexShaderReflectionPath.c_str());
  if (outputTransformVertexReflectionResult.isErr()) return ResultT::Err(LightingDemoSetupError::ShaderLoadFailed);
  const auto outputTransformVertexInputLayout =
      outputTransformVertexLayout(outputTransformVertexReflectionResult.value());
  if (!outputTransformVertexInputLayout.has_value()) return ResultT::Err(LightingDemoSetupError::ShaderLoadFailed);

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Image Regression Fixture (Lighting Demo)", .enableValidationLayers = true});
  if (deviceResult.isErr()) return ResultT::Err(LightingDemoSetupError::DeviceCreationFailed);

  LightingDemoFixture fixture;
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
  // never duplicated here.
  auto sceneLoadResult = loadAndInstantiateScene(config, fixture.device.get(), *vertexInputLayout);
  if (sceneLoadResult.isErr()) return ResultT::Err(LightingDemoSetupError::SceneLoadFailed);
  SceneLoadOutcome outcome = std::move(sceneLoadResult.value());
  fixture.world.emplace(std::move(outcome.world));
  fixture.meshResourceMap = std::move(outcome.meshResourceMap);
  fixture.materialDataMap = std::move(outcome.materialDataMap);
  fixture.textureDataMap = std::move(outcome.textureDataMap);

  // Plan 0019 Section P7: 32 floats (camera view+projection) + the
  // 176-byte FrameLightingData appended immediately after -- matches
  // runtime_application.cpp's own identical widened sizing exactly.
  auto cameraBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32 + sizeof(FrameLightingData)});
  if (cameraBufferResult.isErr()) return ResultT::Err(LightingDemoSetupError::ResourceCreationFailed);
  fixture.cameraBuffer = std::move(cameraBufferResult.value());

  const Extent2D extent{kLightingDemoExtentPixels, kLightingDemoExtentPixels};

  auto depthTextureResult = fixture.device->createTexture({.extent = extent, .format = DepthFormat::D32Sfloat});
  if (depthTextureResult.isErr()) return ResultT::Err(LightingDemoSetupError::ResourceCreationFailed);
  fixture.depthTexture = std::move(depthTextureResult.value());

  auto offscreenTargetResult =
      fixture.device->createOffscreenTarget({.extent = extent, .format = kLightingDemoColorFormat});
  if (offscreenTargetResult.isErr()) return ResultT::Err(LightingDemoSetupError::ResourceCreationFailed);
  fixture.offscreenTarget = std::move(offscreenTargetResult.value());

  const std::size_t readbackSizeBytes =
      static_cast<std::size_t>(kLightingDemoExtentPixels) * kLightingDemoExtentPixels * 4;
  auto readbackBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = readbackSizeBytes});
  if (readbackBufferResult.isErr()) return ResultT::Err(LightingDemoSetupError::ResourceCreationFailed);
  fixture.readbackBuffer = std::move(readbackBufferResult.value());

  // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3/D-6): this fixture's own
  // HDR intermediate, fullscreen-triangle geometry/sampler, and output-
  // transform Pipeline.
  auto hdrColorTargetResult = fixture.device->createHdrColorTarget({.extent = extent});
  if (hdrColorTargetResult.isErr()) return ResultT::Err(LightingDemoSetupError::ResourceCreationFailed);
  fixture.hdrColorTarget = std::move(hdrColorTargetResult.value());

  const float fullscreenTriangleVertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  auto fullscreenTriangleVertexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  if (fullscreenTriangleVertexBufferResult.isErr()) return ResultT::Err(LightingDemoSetupError::ResourceCreationFailed);
  fixture.fullscreenTriangleVertexBuffer = std::move(fullscreenTriangleVertexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleVertexBuffer->mappedData(), fullscreenTriangleVertices,
              sizeof(fullscreenTriangleVertices));

  const std::uint16_t fullscreenTriangleIndices[3] = {0, 1, 2};
  auto fullscreenTriangleIndexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Index, .sizeBytes = sizeof(fullscreenTriangleIndices)});
  if (fullscreenTriangleIndexBufferResult.isErr()) return ResultT::Err(LightingDemoSetupError::ResourceCreationFailed);
  fixture.fullscreenTriangleIndexBuffer = std::move(fullscreenTriangleIndexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleIndexBuffer->mappedData(), fullscreenTriangleIndices,
              sizeof(fullscreenTriangleIndices));

  auto outputTransformSamplerResult = fixture.device->createSampler(
      {.filter = atlantis::rhi::Filter::Linear, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  if (outputTransformSamplerResult.isErr()) return ResultT::Err(LightingDemoSetupError::ResourceCreationFailed);
  fixture.outputTransformSampler = std::move(outputTransformSamplerResult.value());

  auto outputTransformPipelineResult = fixture.device->createPipeline(
      {.vertexShader = {.spirvWords = fixture.outputTransformUnormVertexSpirv.data(),
                         .wordCount = fixture.outputTransformUnormVertexSpirv.size()},
       .fragmentShader = {.spirvWords = fixture.outputTransformUnormFragmentSpirv.data(),
                           .wordCount = fixture.outputTransformUnormFragmentSpirv.size()},
       .vertexInputLayout = fixture.outputTransformUnormVertexInputLayout,
       .colorFormat = kLightingDemoColorFormat,
       .sampledTextureBindingCount = 1,
       .hasCameraUniformBinding = false,
       .hasDepthAttachment = false});
  if (outputTransformPipelineResult.isErr()) return ResultT::Err(LightingDemoSetupError::ResourceCreationFailed);
  fixture.outputTransformPipeline = std::move(outputTransformPipelineResult.value());

  return ResultT::Ok(std::move(fixture));
}

atlantis::Result<PixelBuffer, LightingDemoRenderError> renderLightingDemoFrame(LightingDemoFixture& fixture) {
  namespace rhi = atlantis::rhi;
  namespace render_graph = atlantis::render_graph;
  using ResultT = atlantis::Result<PixelBuffer, LightingDemoRenderError>;

  auto acquireResult = fixture.offscreenTarget->acquireTarget();
  if (acquireResult.isErr()) return ResultT::Err(LightingDemoRenderError::AcquireFailed);
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

  fixture.world->updateTransforms();

  const auto activeCamera = fixture.world->activeCamera();
  if (!activeCamera.has_value()) return ResultT::Err(LightingDemoRenderError::NoActiveCamera);
  const auto cameraWorldMatrixResult = fixture.world->getWorldMatrix(*activeCamera);
  const auto cameraComponentResult = fixture.world->getCamera(*activeCamera);
  if (cameraWorldMatrixResult.isErr() || cameraComponentResult.isErr()) {
    return ResultT::Err(LightingDemoRenderError::ExtractionFailed);
  }
  const atlantis::world::Camera cameraComponent = cameraComponentResult.value();
  const auto extractionResult = extractCameraMatrices(cameraWorldMatrixResult.value(), cameraComponent.fovYRadians,
                                                        cameraComponent.nearZ, cameraComponent.farZ, 1.0f);
  if (extractionResult.isErr()) return ResultT::Err(LightingDemoRenderError::ExtractionFailed);

  auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = extractionResult.value().view[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = extractionResult.value().projection[i];

  // Plan 0022 Section M2: re-extracts and republishes the complete
  // FrameLightingData from World's live state on every call -- the
  // fixture-local direct analog of runtime_application.cpp's own
  // identical, now-unconditional block. Safe because the *previous*
  // call against this same fixture (if any) already called
  // fixture.device->waitIdle() (below) before returning -- this call's
  // own CPU writes below never race that previous call's GPU work.
  std::vector<LightExtractionInput> lightInputs;
  for (const atlantis::world::EntityId& id : fixture.world->lightEntities()) {
    const auto lightResult = fixture.world->getLight(id);
    const auto lightWorldMatrixResult = fixture.world->getWorldMatrix(id);
    if (lightResult.isErr() || lightWorldMatrixResult.isErr()) {
      return ResultT::Err(LightingDemoRenderError::LightExtractionFailed);
    }
    lightInputs.push_back({lightResult.value(), lightWorldMatrixResult.value()});
  }
  const auto lightingResult = extractFrameLightingData(lightInputs);
  if (lightingResult.isErr()) return ResultT::Err(LightingDemoRenderError::LightExtractionFailed);
  auto* lightingData = reinterpret_cast<FrameLightingData*>(cameraData + 32);
  *lightingData = lightingResult.value();

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
  if (commandListResult.isErr()) return ResultT::Err(LightingDemoRenderError::CommandListCreationFailed);
  std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

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

    const auto& materialAsset = renderableResult.value().materialAsset;
    if (!materialAsset.has_value()) continue;
    if (resolveMaterialAsset(*materialAsset, knownMaterialIds).isErr()) continue;

    // Plan 0019 Section P15: gated on this entity's OWN
    // MaterialAssetData.kind -- an UnlitTextured-bound entity never
    // calls checkConformalTransform().
    const auto materialDataIt = fixture.materialDataMap.find(*materialAsset);
    if (materialDataIt == fixture.materialDataMap.end()) continue;  // resolveMaterialAsset() already confirmed membership; defensive only
    if (materialDataIt->second.kind == atlantis::asset_system::MaterialKind::LitTextured) {
      if (checkConformalTransform(worldMatrixResult.value()).isErr()) continue;  // skip this entity for this frame only
    }

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
  if (copyCompileResult.isErr()) return ResultT::Err(LightingDemoRenderError::CommandListCreationFailed);
  const std::vector<render_graph::ResourceBinding> copyBindings{{.resource = copyCompileResult.value().resourceAt(0),
                                                                   .target = target.get(),
                                                                   .incomingState = rhi::ResourceState::TransferSource}};
  render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  auto submitResult = fixture.device->submit(std::move(commandList), *target);
  if (submitResult.isErr()) return ResultT::Err(LightingDemoRenderError::SubmitFailed);

  auto waitResult = fixture.device->waitIdle();
  if (waitResult.isErr()) return ResultT::Err(LightingDemoRenderError::WaitIdleFailed);

  for (auto& [assetId, candidate] : realizedCandidates) {
    if (candidate.newSampledTexture) {
      fixture.sampledTextureResourceMap.emplace(candidate.textureAssetId, std::move(candidate.newSampledTexture));
    }
    fixture.samplerResourceMap.emplace(assetId, std::move(candidate.sampler));
    fixture.materialResourceMap.emplace(assetId, std::move(candidate.material));
  }

  PixelBuffer result;
  result.width = kLightingDemoExtentPixels;
  result.height = kLightingDemoExtentPixels;
  const std::size_t byteCount = static_cast<std::size_t>(kLightingDemoExtentPixels) * kLightingDemoExtentPixels * 4;
  const auto* readbackData = static_cast<const std::uint8_t*>(fixture.readbackBuffer->mappedData());
  result.rgba8.assign(readbackData, readbackData + byteCount);

  target.reset();  // Ends this cycle's borrow (RAII, ADR-0038) -- no release()/consume() call.

  return ResultT::Ok(std::move(result));
}

}  // namespace atlantis::image_regression
