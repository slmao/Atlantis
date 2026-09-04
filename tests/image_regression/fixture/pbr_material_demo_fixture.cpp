#include "pbr_material_demo_fixture.h"

#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/load_environment.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/runtime/material_realization.h>
#include <atlantis/runtime/environment_realization.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/runtime/scene_load.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

// Plan 0023 Milestone 8: see pbr_material_demo_fixture.h's own top-of-
// file comment -- this file calls Atlantis::RuntimeHost's real
// loadAndInstantiateScene()/computePendingMaterialIds()/
// realizePendingMaterials()/extractCameraMatrices()/
// extractCameraWorldPosition()/extractFrameLightingData()/
// resolveMeshAsset()/resolveMaterialAsset()/checkConformalTransform()
// directly, never re-implementing any of them.

namespace atlantis::image_regression {

namespace {

using atlantis::renderer::DrawItem;
using atlantis::renderer::Renderer;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Extent2D;
using atlantis::rhi::VertexInputLayout;
using atlantis::runtime::CameraWorldPositionData;
using atlantis::runtime::checkConformalTransform;
using atlantis::runtime::computePendingMaterialIds;
using atlantis::runtime::extractCameraMatrices;
using atlantis::runtime::extractCameraWorldPosition;
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

// Duplicated, not shared -- matches lighting_demo_fixture.cpp's/
// material_demo_fixture.cpp's own identical Vertex schema exactly
// (Spec 0020's 44-byte position+color+UV0+normal mesh artifact layout,
// which pbr_sphere.mesh.txt also uses, Milestone 6).
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

atlantis::Result<PbrMaterialDemoFixture, PbrMaterialDemoSetupError> setUpPbrMaterialDemoFixture(
    const atlantis::runtime::BootstrapConfig& config) {
  using ResultT = atlantis::Result<PbrMaterialDemoFixture, PbrMaterialDemoSetupError>;

  auto vertexSpirv = loadSpirvFile(config.unlitTexturedVertexShaderSpirvPath.c_str());
  auto fragmentSpirv = loadSpirvFile(config.unlitTexturedFragmentShaderSpirvPath.c_str());
  if (!vertexSpirv.has_value() || !fragmentSpirv.has_value()) {
    return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);
  }
  auto vertexReflectionResult = loadReflectionMetadata(config.unlitTexturedVertexShaderReflectionPath.c_str());
  if (vertexReflectionResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);
  const auto vertexInputLayout = unlitTexturedVertexLayout(vertexReflectionResult.value());
  if (!vertexInputLayout.has_value()) return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);

  auto litVertexSpirv = loadSpirvFile(config.litTexturedVertexShaderSpirvPath.c_str());
  auto litFragmentSpirv = loadSpirvFile(config.litTexturedFragmentShaderSpirvPath.c_str());
  if (!litVertexSpirv.has_value() || !litFragmentSpirv.has_value()) {
    return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);
  }
  auto litVertexReflectionResult = loadReflectionMetadata(config.litTexturedVertexShaderReflectionPath.c_str());
  if (litVertexReflectionResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);
  const auto litVertexInputLayout = litTexturedVertexLayout(litVertexReflectionResult.value());
  if (!litVertexInputLayout.has_value()) return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);

  auto pbrVertexSpirv = loadSpirvFile(config.pbrDirectLitVertexShaderSpirvPath.c_str());
  auto pbrFragmentSpirv = loadSpirvFile(config.pbrDirectLitFragmentShaderSpirvPath.c_str());
  if (!pbrVertexSpirv.has_value() || !pbrFragmentSpirv.has_value()) {
    return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);
  }
  auto pbrVertexReflectionResult = loadReflectionMetadata(config.pbrDirectLitVertexShaderReflectionPath.c_str());
  if (pbrVertexReflectionResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);
  const auto pbrVertexInputLayout = pbrDirectLitVertexLayout(pbrVertexReflectionResult.value());
  if (!pbrVertexInputLayout.has_value()) return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);

  std::optional<std::vector<std::uint32_t>> pbrIblVertexSpirv;
  std::optional<std::vector<std::uint32_t>> pbrIblFragmentSpirv;
  std::optional<VertexInputLayout> pbrIblVertexInputLayout;
  std::optional<std::vector<std::uint32_t>> skyVertexSpirv;
  std::optional<std::vector<std::uint32_t>> skyFragmentSpirv;
  std::optional<VertexInputLayout> skyVertexInputLayout;
  if (!config.environmentArtifactPath.empty()) {
    if (atlantis::runtime::validateEnvironmentBootstrapConfig(config).isErr()) {
      return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);
    }
    pbrIblVertexSpirv = loadSpirvFile(config.pbrIblVertexShaderSpirvPath.c_str());
    pbrIblFragmentSpirv = loadSpirvFile(config.pbrIblFragmentShaderSpirvPath.c_str());
    auto pbrIblReflection = loadReflectionMetadata(config.pbrIblVertexShaderReflectionPath.c_str());
    if (!pbrIblVertexSpirv.has_value() || !pbrIblFragmentSpirv.has_value() || pbrIblReflection.isErr()) {
      return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);
    }
    pbrIblVertexInputLayout = pbrDirectLitVertexLayout(pbrIblReflection.value());
    if (!pbrIblVertexInputLayout.has_value()) return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);

    // Plan 0026 Milestone 5 (ADR-0071): the sky shader pair -- same gate,
    // same shape as the pbrIbl load immediately above. Its own vertex
    // layout is resolved via the existing outputTransformVertexLayout()
    // (P1's reused fullscreen schema), not a new vertex-layout function.
    skyVertexSpirv = loadSpirvFile(config.skyVertexShaderSpirvPath.c_str());
    skyFragmentSpirv = loadSpirvFile(config.skyFragmentShaderSpirvPath.c_str());
    auto skyReflection = loadReflectionMetadata(config.skyVertexShaderReflectionPath.c_str());
    if (!skyVertexSpirv.has_value() || !skyFragmentSpirv.has_value() || skyReflection.isErr()) {
      return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);
    }
    skyVertexInputLayout = outputTransformVertexLayout(skyReflection.value());
    if (!skyVertexInputLayout.has_value()) return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);
  }

  // Plan 0024 Milestone 7: the output-transform-unorm shader pair --
  // this fixture's own colorFormat is fixed Rgba8Unorm, so only this
  // one variant is ever loaded.
  auto outputTransformVertexSpirv = loadSpirvFile(config.outputTransformUnormVertexShaderSpirvPath.c_str());
  auto outputTransformFragmentSpirv = loadSpirvFile(config.outputTransformUnormFragmentShaderSpirvPath.c_str());
  if (!outputTransformVertexSpirv.has_value() || !outputTransformFragmentSpirv.has_value()) {
    return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);
  }
  auto outputTransformVertexReflectionResult =
      loadReflectionMetadata(config.outputTransformUnormVertexShaderReflectionPath.c_str());
  if (outputTransformVertexReflectionResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);
  const auto outputTransformVertexInputLayout =
      outputTransformVertexLayout(outputTransformVertexReflectionResult.value());
  if (!outputTransformVertexInputLayout.has_value()) return ResultT::Err(PbrMaterialDemoSetupError::ShaderLoadFailed);

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Image Regression Fixture (PBR Material Demo)", .enableValidationLayers = true});
  if (deviceResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::DeviceCreationFailed);

  PbrMaterialDemoFixture fixture;
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
  if (pbrIblVertexSpirv.has_value()) {
    fixture.pbrIblVertexInputLayout = std::move(*pbrIblVertexInputLayout);
    fixture.pbrIblVertexSpirv = std::move(*pbrIblVertexSpirv);
    fixture.pbrIblFragmentSpirv = std::move(*pbrIblFragmentSpirv);
  }
  if (skyVertexSpirv.has_value()) {
    fixture.skyVertexInputLayout = std::move(*skyVertexInputLayout);
    fixture.skyVertexSpirv = std::move(*skyVertexSpirv);
    fixture.skyFragmentSpirv = std::move(*skyFragmentSpirv);
  }
  fixture.outputTransformUnormVertexInputLayout = *outputTransformVertexInputLayout;
  fixture.outputTransformUnormVertexSpirv = std::move(*outputTransformVertexSpirv);
  fixture.outputTransformUnormFragmentSpirv = std::move(*outputTransformFragmentSpirv);

  // Phase 1: the real, Runtime-private CPU load/instantiate pipeline --
  // never duplicated here.
  auto sceneLoadResult = loadAndInstantiateScene(config, fixture.device.get(), *vertexInputLayout);
  if (sceneLoadResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::SceneLoadFailed);
  SceneLoadOutcome outcome = std::move(sceneLoadResult.value());
  fixture.world.emplace(std::move(outcome.world));
  fixture.meshResourceMap = std::move(outcome.meshResourceMap);
  fixture.materialDataMap = std::move(outcome.materialDataMap);
  fixture.textureDataMap = std::move(outcome.textureDataMap);
  if (!config.environmentArtifactPath.empty()) {
    auto environmentResult = atlantis::asset_system::loadEnvironmentAsset(config.environmentArtifactPath,
                                                                           config.environmentMetadataPath);
    if (environmentResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::SceneLoadFailed);
    fixture.environmentData.emplace(std::move(environmentResult.value()));
  }

  // Plan 0023 Milestone 2/8: this fixture's own independent 320-byte
  // Camera/Lighting/CameraWorldPosition buffer -- 32 floats (view+
  // projection) + FrameLightingData (176 bytes) + CameraWorldPositionData
  // (16 bytes), matching runtime_application.cpp's own real, current
  // sizing exactly (never LightingDemoFixture's own pre-Milestone-2
  // 304-byte one).
  auto cameraBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Uniform,
       .sizeBytes = 464});
  if (cameraBufferResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ResourceCreationFailed);
  fixture.cameraBuffer = std::move(cameraBufferResult.value());

  const Extent2D extent{kPbrMaterialDemoExtentPixels, kPbrMaterialDemoExtentPixels};

  auto depthTextureResult = fixture.device->createTexture({.extent = extent, .format = DepthFormat::D32Sfloat});
  if (depthTextureResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ResourceCreationFailed);
  fixture.depthTexture = std::move(depthTextureResult.value());

  auto offscreenTargetResult =
      fixture.device->createOffscreenTarget({.extent = extent, .format = kPbrMaterialDemoColorFormat});
  if (offscreenTargetResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ResourceCreationFailed);
  fixture.offscreenTarget = std::move(offscreenTargetResult.value());

  const std::size_t readbackSizeBytes =
      static_cast<std::size_t>(kPbrMaterialDemoExtentPixels) * kPbrMaterialDemoExtentPixels * 4;
  auto readbackBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = readbackSizeBytes});
  if (readbackBufferResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ResourceCreationFailed);
  fixture.readbackBuffer = std::move(readbackBufferResult.value());

  // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3/D-6): this fixture's own
  // HDR intermediate, fullscreen-triangle geometry/sampler, and output-
  // transform Pipeline -- mirrors runtime_application.cpp's own Step
  // 4b/first-format-change-event construction, but created once, here,
  // unconditionally (no resize/format-change path exists in any
  // fixture).
  auto hdrColorTargetResult = fixture.device->createHdrColorTarget({.extent = extent});
  if (hdrColorTargetResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ResourceCreationFailed);
  fixture.hdrColorTarget = std::move(hdrColorTargetResult.value());

  const float fullscreenTriangleVertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  auto fullscreenTriangleVertexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  if (fullscreenTriangleVertexBufferResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ResourceCreationFailed);
  fixture.fullscreenTriangleVertexBuffer = std::move(fullscreenTriangleVertexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleVertexBuffer->mappedData(), fullscreenTriangleVertices,
              sizeof(fullscreenTriangleVertices));

  const std::uint16_t fullscreenTriangleIndices[3] = {0, 1, 2};
  auto fullscreenTriangleIndexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Index, .sizeBytes = sizeof(fullscreenTriangleIndices)});
  if (fullscreenTriangleIndexBufferResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ResourceCreationFailed);
  fixture.fullscreenTriangleIndexBuffer = std::move(fullscreenTriangleIndexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleIndexBuffer->mappedData(), fullscreenTriangleIndices,
              sizeof(fullscreenTriangleIndices));

  auto outputTransformSamplerResult = fixture.device->createSampler(
      {.filter = atlantis::rhi::Filter::Linear, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  if (outputTransformSamplerResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ResourceCreationFailed);
  fixture.outputTransformSampler = std::move(outputTransformSamplerResult.value());

  auto outputTransformPipelineResult = fixture.device->createPipeline(
      {.vertexShader = {.spirvWords = fixture.outputTransformUnormVertexSpirv.data(),
                         .wordCount = fixture.outputTransformUnormVertexSpirv.size()},
       .fragmentShader = {.spirvWords = fixture.outputTransformUnormFragmentSpirv.data(),
                           .wordCount = fixture.outputTransformUnormFragmentSpirv.size()},
       .vertexInputLayout = fixture.outputTransformUnormVertexInputLayout,
       .colorFormat = kPbrMaterialDemoColorFormat,
       .sampledTextureBindingCount = 1,
       .hasCameraUniformBinding = false,
       .hasDepthAttachment = false});
  if (outputTransformPipelineResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ResourceCreationFailed);
  fixture.outputTransformPipeline = std::move(outputTransformPipelineResult.value());

  // Plan 0026 Milestone 5 (ADR-0071 P3/P5): the sky Pipeline -- created
  // once, here, only when an environment is configured, mirroring
  // outputTransformPipeline's own "no real Format/extent dependency"
  // reasoning immediately above (sky targets the fixed
  // HdrFormat::Rgba16Float, unlike outputTransformPipeline).
  if (skyVertexSpirv.has_value()) {
    auto skyPipelineResult = fixture.device->createPipeline(
        {.vertexShader = {.spirvWords = fixture.skyVertexSpirv.data(), .wordCount = fixture.skyVertexSpirv.size()},
         .fragmentShader = {.spirvWords = fixture.skyFragmentSpirv.data(),
                             .wordCount = fixture.skyFragmentSpirv.size()},
         .vertexInputLayout = fixture.skyVertexInputLayout,
         .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
         .depthFormat = DepthFormat::D32Sfloat,
         .sampledTextureBindingCount = 1,
         .hasDepthAttachment = true,
         .depthWriteEnabled = false});
    if (skyPipelineResult.isErr()) return ResultT::Err(PbrMaterialDemoSetupError::ResourceCreationFailed);
    fixture.skyPipeline = std::move(skyPipelineResult.value());
  }

  return ResultT::Ok(std::move(fixture));
}

atlantis::Result<PixelBuffer, PbrMaterialDemoRenderError> renderPbrMaterialDemoFrame(PbrMaterialDemoFixture& fixture) {
  namespace rhi = atlantis::rhi;
  namespace render_graph = atlantis::render_graph;
  using ResultT = atlantis::Result<PixelBuffer, PbrMaterialDemoRenderError>;

  auto acquireResult = fixture.offscreenTarget->acquireTarget();
  if (acquireResult.isErr()) return ResultT::Err(PbrMaterialDemoRenderError::AcquireFailed);
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

  fixture.world->updateTransforms();

  const auto activeCamera = fixture.world->activeCamera();
  if (!activeCamera.has_value()) return ResultT::Err(PbrMaterialDemoRenderError::NoActiveCamera);
  const auto cameraWorldMatrixResult = fixture.world->getWorldMatrix(*activeCamera);
  const auto cameraComponentResult = fixture.world->getCamera(*activeCamera);
  if (cameraWorldMatrixResult.isErr() || cameraComponentResult.isErr()) {
    return ResultT::Err(PbrMaterialDemoRenderError::ExtractionFailed);
  }
  const atlantis::world::Camera cameraComponent = cameraComponentResult.value();
  const auto extractionResult = extractCameraMatrices(cameraWorldMatrixResult.value(), cameraComponent.fovYRadians,
                                                        cameraComponent.nearZ, cameraComponent.farZ, 1.0f);
  if (extractionResult.isErr()) return ResultT::Err(PbrMaterialDemoRenderError::ExtractionFailed);

  auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = extractionResult.value().view[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = extractionResult.value().projection[i];

  // Re-extracts and republishes the complete FrameLightingData from
  // World's live state on every call -- matching
  // runtime_application.cpp's/lighting_demo_fixture.cpp's own identical,
  // unconditional block (Plan 0022's own corrected dynamic contract).
  std::vector<LightExtractionInput> lightInputs;
  for (const atlantis::world::EntityId& id : fixture.world->lightEntities()) {
    const auto lightResult = fixture.world->getLight(id);
    const auto lightWorldMatrixResult = fixture.world->getWorldMatrix(id);
    if (lightResult.isErr() || lightWorldMatrixResult.isErr()) {
      return ResultT::Err(PbrMaterialDemoRenderError::LightExtractionFailed);
    }
    lightInputs.push_back({lightResult.value(), lightWorldMatrixResult.value()});
  }
  const auto lightingResult = extractFrameLightingData(lightInputs);
  if (lightingResult.isErr()) return ResultT::Err(PbrMaterialDemoRenderError::LightExtractionFailed);
  auto* lightingData = reinterpret_cast<FrameLightingData*>(cameraData + 32);
  *lightingData = lightingResult.value();

  // Plan 0023 Milestone 2/8: tail-only CameraWorldPositionData write,
  // unconditional every frame like the camera-matrix and lighting
  // writes above -- matches runtime_application.cpp's own identical
  // write exactly (byte offset 304 / float offset 76 = 32 + 44).
  auto* cameraWorldPositionData = reinterpret_cast<CameraWorldPositionData*>(cameraData + 32 + 44);
  *cameraWorldPositionData = extractCameraWorldPosition(cameraWorldMatrixResult.value());
  const std::array<float, 36>* irradianceShSource = nullptr;
  if (fixture.environmentData.has_value()) {
    irradianceShSource = &fixture.environmentData->irradianceSh;
  } else if (fixture.environmentLightingResources.has_value()) {
    irradianceShSource = &fixture.environmentLightingResources->irradianceSh;
  }
  atlantis::runtime::writeEnvironmentIrradianceSh(std::span<float, 36>(cameraData + 80, 36), irradianceShSource);

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
  if (commandListResult.isErr()) return ResultT::Err(PbrMaterialDemoRenderError::CommandListCreationFailed);
  std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

  std::optional<atlantis::runtime::EnvironmentLightingCandidate> environmentCandidate;
  if (fixture.environmentData.has_value() && !fixture.environmentLightingResources.has_value()) {
    auto result = atlantis::runtime::realizeEnvironmentCandidate(*fixture.device, *fixture.environmentData);
    if (result.isErr()) return ResultT::Err(PbrMaterialDemoRenderError::CommandListCreationFailed);
    environmentCandidate.emplace(std::move(result.value()));
    atlantis::runtime::recordEnvironmentUploads(*commandList, *environmentCandidate);
  }
  const bool environmentEnabled = fixture.environmentData.has_value() || fixture.environmentLightingResources.has_value();

  std::unordered_map<atlantis::asset_system::AssetId, RealizedMaterialCandidate> realizedCandidates =
      realizePendingMaterials(*fixture.device, *commandList, fixture.unlitTexturedVertexInputLayout,
                               fixture.unlitTexturedVertexSpirv, fixture.unlitTexturedFragmentSpirv,
                               fixture.litTexturedVertexInputLayout,
                               fixture.litTexturedVertexSpirv, fixture.litTexturedFragmentSpirv,
                               fixture.pbrDirectLitVertexInputLayout, fixture.pbrDirectLitVertexSpirv,
                               fixture.pbrDirectLitFragmentSpirv, fixture.pbrIblVertexInputLayout,
                               fixture.pbrIblVertexSpirv, fixture.pbrIblFragmentSpirv, environmentEnabled,
                               pendingMaterialIds,
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

    // Gated on this entity's OWN MaterialAssetData.kind -- an
    // UnlitTextured-bound entity never calls checkConformalTransform().
    // Plan 0023 Milestone 8 (a fix found during this Milestone's own
    // implementation, applying an already-Approved rule -- see
    // runtime_application.cpp's own identical fix and comment):
    // PbrDirectLit shares LitTextured's own exact vertex-normal-
    // transform shape (Milestone 4), so it needs this same gate.
    const auto materialDataIt = fixture.materialDataMap.find(*materialAsset);
    if (materialDataIt == fixture.materialDataMap.end()) continue;  // resolveMaterialAsset() already confirmed membership; defensive only
    if (materialDataIt->second.kind == atlantis::asset_system::MaterialKind::LitTextured ||
        materialDataIt->second.kind == atlantis::asset_system::MaterialKind::PbrDirectLit) {
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
  std::optional<atlantis::renderer::EnvironmentLighting> environmentLightingView;
  if (environmentCandidate.has_value()) {
    environmentLightingView.emplace(environmentCandidate->resources.borrowedView());
  } else if (fixture.environmentLightingResources.has_value()) {
    environmentLightingView.emplace(fixture.environmentLightingResources->borrowedView());
  }
  renderer.drawFrame(*commandList, *target, *fixture.depthTexture, *fixture.cameraBuffer, drawItems,
                      rhi::ResourceState::TransferSource, *fixture.hdrColorTarget,
                      *fixture.fullscreenTriangleVertexBuffer, *fixture.fullscreenTriangleIndexBuffer,
                      *fixture.outputTransformPipeline, *fixture.outputTransformSampler,
                      environmentLightingView.has_value() ? &*environmentLightingView : nullptr,
                      fixture.skyPipeline.get());

  render_graph::RenderGraphBuilder copyBuilder;
  const auto copyResource = copyBuilder.declareResource("color-copy");
  const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
  copyBuilder.writes(copyPass, copyResource, rhi::ResourceState::TransferSource);
  copyBuilder.setExecute(copyPass, [&target, &fixture](rhi::CommandList& cmd) {
    cmd.copyRenderTargetToBuffer(*target, *fixture.readbackBuffer);
  });
  auto copyCompileResult = copyBuilder.compile();
  if (copyCompileResult.isErr()) return ResultT::Err(PbrMaterialDemoRenderError::CommandListCreationFailed);
  const std::vector<render_graph::ResourceBinding> copyBindings{{.resource = copyCompileResult.value().resourceAt(0),
                                                                   .target = target.get(),
                                                                   .incomingState = rhi::ResourceState::TransferSource}};
  render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  auto submitResult = fixture.device->submit(std::move(commandList), *target);
  if (submitResult.isErr()) return ResultT::Err(PbrMaterialDemoRenderError::SubmitFailed);

  auto waitResult = fixture.device->waitIdle();
  if (waitResult.isErr()) return ResultT::Err(PbrMaterialDemoRenderError::WaitIdleFailed);

  for (auto& [assetId, candidate] : realizedCandidates) {
    if (candidate.newSampledTexture) {
      fixture.sampledTextureResourceMap.emplace(candidate.textureAssetId, std::move(candidate.newSampledTexture));
    }
    fixture.samplerResourceMap.emplace(assetId, std::move(candidate.sampler));
    fixture.materialResourceMap.emplace(assetId, std::move(candidate.material));
  }
  if (environmentCandidate.has_value()) {
    fixture.environmentLightingResources.emplace(std::move(environmentCandidate->resources));
    fixture.environmentData.reset();
    ++fixture.environmentUploadCount;
  }

  PixelBuffer result;
  result.width = kPbrMaterialDemoExtentPixels;
  result.height = kPbrMaterialDemoExtentPixels;
  const std::size_t byteCount =
      static_cast<std::size_t>(kPbrMaterialDemoExtentPixels) * kPbrMaterialDemoExtentPixels * 4;
  const auto* readbackData = static_cast<const std::uint8_t*>(fixture.readbackBuffer->mappedData());
  result.rgba8.assign(readbackData, readbackData + byteCount);

  target.reset();  // Ends this cycle's borrow (RAII, ADR-0038) -- no release()/consume() call.

  return ResultT::Ok(std::move(result));
}

}  // namespace atlantis::image_regression
