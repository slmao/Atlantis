#include "pbr_sheen_demo_fixture.h"

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

// Plan 0035 Milestone 3: see pbr_sheen_demo_fixture.h's own top-of-file
// comment -- this file calls Atlantis::RuntimeHost's real
// loadAndInstantiateScene()/computePendingMaterialIds()/
// realizePendingMaterials()/extractCameraMatrices()/
// extractCameraWorldPosition()/extractFrameLightingData()/
// resolveMeshAsset()/resolveMaterialAsset()/checkConformalTransform()
// directly, never re-implementing any of them. Duplicated, not shared,
// from pbr_clearcoat_demo_fixture.cpp (that file's own top-of-file
// comment establishes this repository's convention for from-scratch
// composition roots).

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

// Duplicated, not shared -- matches every sibling fixture's own identical
// Vertex schema (Spec 0020's 44-byte position+color+UV0+normal+tangent
// mesh artifact layout, which pbr_sphere.mesh.txt also uses).
struct Vertex {
  float position[3];
  float color[3];
  float uv[2];
  float normal[3];
  float tangent[4];
};
static_assert(std::is_standard_layout_v<Vertex>);
static_assert(offsetof(Vertex, position) == atlantis::asset_system::kMeshArtifactPositionOffsetBytes);
static_assert(offsetof(Vertex, color) == atlantis::asset_system::kMeshArtifactColorOffsetBytes);
static_assert(offsetof(Vertex, uv) == atlantis::asset_system::kMeshArtifactUv0OffsetBytes);
static_assert(offsetof(Vertex, normal) == atlantis::asset_system::kMeshArtifactNormalOffsetBytes);
static_assert(offsetof(Vertex, tangent) == atlantis::asset_system::kMeshArtifactTangentOffsetBytes);
static_assert(sizeof(Vertex) == atlantis::asset_system::kMeshArtifactVertexStrideBytes);

// Plan 0027 Milestone 9 (ADR-0072 D-1/P5): the no-directional-light
// light-space sentinel -- this fixture never configures a real
// shadow-casting occluder, matching every sibling fixture's own identical
// reasoning.
constexpr float kIdentityMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

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

// Plan 0029 Section P13/Plan 0035: pbrDirectLitVertexLayout()'s own
// schema above plus a trailing tangent@3, matching
// pbr_ibl_normal_map.slang/pbr_sheen_ibl_normal_map.slang's own
// VertexInput exactly -- mirrors runtime_application.cpp's own identical
// pbrNormalMapVertexLayout().
[[nodiscard]] std::optional<VertexInputLayout> pbrNormalMapVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
      MeshVertexAttributeSchema{.location = 2, .offsetBytes = offsetof(Vertex, normal)},
      MeshVertexAttributeSchema{.location = 3, .offsetBytes = offsetof(Vertex, tangent)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

[[nodiscard]] std::optional<VertexInputLayout> shadowCastVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

[[nodiscard]] std::optional<VertexInputLayout> outputTransformVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = 0},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(float) * 2);
  if (result.isErr()) return std::nullopt;
  return result.value();
}

}  // namespace

atlantis::Result<PbrSheenDemoFixture, PbrSheenDemoSetupError> setUpPbrSheenDemoFixture(
    const atlantis::runtime::BootstrapConfig& config) {
  using ResultT = atlantis::Result<PbrSheenDemoFixture, PbrSheenDemoSetupError>;

  auto vertexSpirv = loadSpirvFile(config.unlitTexturedVertexShaderSpirvPath.c_str());
  auto fragmentSpirv = loadSpirvFile(config.unlitTexturedFragmentShaderSpirvPath.c_str());
  if (!vertexSpirv.has_value() || !fragmentSpirv.has_value()) {
    return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  }
  auto vertexReflectionResult = loadReflectionMetadata(config.unlitTexturedVertexShaderReflectionPath.c_str());
  if (vertexReflectionResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  const auto vertexInputLayout = unlitTexturedVertexLayout(vertexReflectionResult.value());
  if (!vertexInputLayout.has_value()) return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);

  auto litVertexSpirv = loadSpirvFile(config.litTexturedVertexShaderSpirvPath.c_str());
  auto litFragmentSpirv = loadSpirvFile(config.litTexturedFragmentShaderSpirvPath.c_str());
  if (!litVertexSpirv.has_value() || !litFragmentSpirv.has_value()) {
    return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  }
  auto litVertexReflectionResult = loadReflectionMetadata(config.litTexturedVertexShaderReflectionPath.c_str());
  if (litVertexReflectionResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  const auto litVertexInputLayout = litTexturedVertexLayout(litVertexReflectionResult.value());
  if (!litVertexInputLayout.has_value()) return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);

  auto pbrVertexSpirv = loadSpirvFile(config.pbrDirectLitVertexShaderSpirvPath.c_str());
  auto pbrFragmentSpirv = loadSpirvFile(config.pbrDirectLitFragmentShaderSpirvPath.c_str());
  if (!pbrVertexSpirv.has_value() || !pbrFragmentSpirv.has_value()) {
    return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  }
  auto pbrVertexReflectionResult = loadReflectionMetadata(config.pbrDirectLitVertexShaderReflectionPath.c_str());
  if (pbrVertexReflectionResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  const auto pbrVertexInputLayout = pbrDirectLitVertexLayout(pbrVertexReflectionResult.value());
  if (!pbrVertexInputLayout.has_value()) return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);

  auto shadowCastVertexSpirv = loadSpirvFile(config.shadowCastVertexShaderSpirvPath.c_str());
  auto shadowCastFragmentSpirv = loadSpirvFile(config.shadowCastFragmentShaderSpirvPath.c_str());
  if (!shadowCastVertexSpirv.has_value() || !shadowCastFragmentSpirv.has_value()) {
    return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  }
  auto shadowCastVertexReflectionResult = loadReflectionMetadata(config.shadowCastVertexShaderReflectionPath.c_str());
  if (shadowCastVertexReflectionResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  const auto shadowCastVertexInputLayout = shadowCastVertexLayout(shadowCastVertexReflectionResult.value());
  if (!shadowCastVertexInputLayout.has_value()) return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);

  // Plan 0035 Milestone 3 (ADR-0081): this fixture's own scenes always
  // configure an environment (selectShaderPair() requires
  // environmentEnabled for MaterialKind::PbrSheen), so pbrIbl/sky/
  // pbrSheenIbl*/pbrSheenIblNormalMap* are all loaded unconditionally
  // inside this block, mirroring setUpPbrClearcoatDemoFixture()'s own
  // identical "environment always configured" convention.
  if (atlantis::runtime::validateEnvironmentBootstrapConfig(config).isErr()) {
    return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  }
  auto pbrIblVertexSpirv = loadSpirvFile(config.pbrIblVertexShaderSpirvPath.c_str());
  auto pbrIblFragmentSpirv = loadSpirvFile(config.pbrIblFragmentShaderSpirvPath.c_str());
  auto pbrIblReflection = loadReflectionMetadata(config.pbrIblVertexShaderReflectionPath.c_str());
  if (!pbrIblVertexSpirv.has_value() || !pbrIblFragmentSpirv.has_value() || pbrIblReflection.isErr()) {
    return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  }
  auto pbrIblVertexInputLayout = pbrDirectLitVertexLayout(pbrIblReflection.value());
  if (!pbrIblVertexInputLayout.has_value()) return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);

  auto skyVertexSpirv = loadSpirvFile(config.skyVertexShaderSpirvPath.c_str());
  auto skyFragmentSpirv = loadSpirvFile(config.skyFragmentShaderSpirvPath.c_str());
  auto skyReflection = loadReflectionMetadata(config.skyVertexShaderReflectionPath.c_str());
  if (!skyVertexSpirv.has_value() || !skyFragmentSpirv.has_value() || skyReflection.isErr()) {
    return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  }
  auto skyVertexInputLayout = outputTransformVertexLayout(skyReflection.value());
  if (!skyVertexInputLayout.has_value()) return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);

  // Plan 0035 Milestone 3 (ADR-0081): the two real sheen shader trios --
  // this fixture's own point of difference from every other sibling
  // fixture (which all pass dead-path filler for these two trios).
  auto pbrSheenIblVertexSpirv = loadSpirvFile(config.pbrSheenIblVertexShaderSpirvPath.c_str());
  auto pbrSheenIblFragmentSpirv = loadSpirvFile(config.pbrSheenIblFragmentShaderSpirvPath.c_str());
  auto pbrSheenIblReflection = loadReflectionMetadata(config.pbrSheenIblVertexShaderReflectionPath.c_str());
  if (!pbrSheenIblVertexSpirv.has_value() || !pbrSheenIblFragmentSpirv.has_value() ||
      pbrSheenIblReflection.isErr()) {
    return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  }
  auto pbrSheenIblVertexInputLayout = pbrDirectLitVertexLayout(pbrSheenIblReflection.value());
  if (!pbrSheenIblVertexInputLayout.has_value()) {
    return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  }

  auto pbrSheenIblNormalMapVertexSpirv = loadSpirvFile(config.pbrSheenIblNormalMapVertexShaderSpirvPath.c_str());
  auto pbrSheenIblNormalMapFragmentSpirv =
      loadSpirvFile(config.pbrSheenIblNormalMapFragmentShaderSpirvPath.c_str());
  auto pbrSheenIblNormalMapReflection =
      loadReflectionMetadata(config.pbrSheenIblNormalMapVertexShaderReflectionPath.c_str());
  if (!pbrSheenIblNormalMapVertexSpirv.has_value() || !pbrSheenIblNormalMapFragmentSpirv.has_value() ||
      pbrSheenIblNormalMapReflection.isErr()) {
    return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  }
  auto pbrSheenIblNormalMapVertexInputLayout = pbrNormalMapVertexLayout(pbrSheenIblNormalMapReflection.value());
  if (!pbrSheenIblNormalMapVertexInputLayout.has_value()) {
    return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  }

  auto outputTransformVertexSpirv = loadSpirvFile(config.outputTransformUnormVertexShaderSpirvPath.c_str());
  auto outputTransformFragmentSpirv = loadSpirvFile(config.outputTransformUnormFragmentShaderSpirvPath.c_str());
  if (!outputTransformVertexSpirv.has_value() || !outputTransformFragmentSpirv.has_value()) {
    return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  }
  auto outputTransformVertexReflectionResult =
      loadReflectionMetadata(config.outputTransformUnormVertexShaderReflectionPath.c_str());
  if (outputTransformVertexReflectionResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);
  const auto outputTransformVertexInputLayout =
      outputTransformVertexLayout(outputTransformVertexReflectionResult.value());
  if (!outputTransformVertexInputLayout.has_value()) return ResultT::Err(PbrSheenDemoSetupError::ShaderLoadFailed);

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Image Regression Fixture (PBR Sheen Demo)", .enableValidationLayers = true});
  if (deviceResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::DeviceCreationFailed);

  PbrSheenDemoFixture fixture;
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
  fixture.shadowCastVertexInputLayout = *shadowCastVertexInputLayout;
  fixture.shadowCastVertexSpirv = std::move(*shadowCastVertexSpirv);
  fixture.shadowCastFragmentSpirv = std::move(*shadowCastFragmentSpirv);
  fixture.pbrIblVertexInputLayout = std::move(*pbrIblVertexInputLayout);
  fixture.pbrIblVertexSpirv = std::move(*pbrIblVertexSpirv);
  fixture.pbrIblFragmentSpirv = std::move(*pbrIblFragmentSpirv);
  fixture.skyVertexInputLayout = std::move(*skyVertexInputLayout);
  fixture.skyVertexSpirv = std::move(*skyVertexSpirv);
  fixture.skyFragmentSpirv = std::move(*skyFragmentSpirv);
  fixture.pbrSheenIblVertexInputLayout = std::move(*pbrSheenIblVertexInputLayout);
  fixture.pbrSheenIblVertexSpirv = std::move(*pbrSheenIblVertexSpirv);
  fixture.pbrSheenIblFragmentSpirv = std::move(*pbrSheenIblFragmentSpirv);
  fixture.pbrSheenIblNormalMapVertexInputLayout = std::move(*pbrSheenIblNormalMapVertexInputLayout);
  fixture.pbrSheenIblNormalMapVertexSpirv = std::move(*pbrSheenIblNormalMapVertexSpirv);
  fixture.pbrSheenIblNormalMapFragmentSpirv = std::move(*pbrSheenIblNormalMapFragmentSpirv);
  fixture.outputTransformUnormVertexInputLayout = *outputTransformVertexInputLayout;
  fixture.outputTransformUnormVertexSpirv = std::move(*outputTransformVertexSpirv);
  fixture.outputTransformUnormFragmentSpirv = std::move(*outputTransformFragmentSpirv);

  // Phase 1: the real, Runtime-private CPU load/instantiate pipeline --
  // never duplicated here.
  auto sceneLoadResult = loadAndInstantiateScene(config, fixture.device.get(), *vertexInputLayout);
  if (sceneLoadResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::SceneLoadFailed);
  SceneLoadOutcome outcome = std::move(sceneLoadResult.value());
  fixture.world.emplace(std::move(outcome.world));
  fixture.meshResourceMap = std::move(outcome.meshResourceMap);
  fixture.materialDataMap = std::move(outcome.materialDataMap);
  fixture.textureDataMap = std::move(outcome.textureDataMap);
  auto environmentResult =
      atlantis::asset_system::loadEnvironmentAsset(config.environmentArtifactPath, config.environmentMetadataPath);
  if (environmentResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::SceneLoadFailed);
  fixture.environmentData.emplace(std::move(environmentResult.value()));

  // Plan 0023 Milestone 2/8 / Plan 0027 Milestone 9 (ADR-0072 D-9/P9d):
  // this fixture's own independent 592-byte Camera/Lighting/
  // CameraWorldPosition/light-space buffer -- matches every sibling
  // fixture's own identical, current sizing.
  auto cameraBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Uniform,
       .sizeBytes = atlantis::runtime::kCameraUniformBufferSizeBytes});
  if (cameraBufferResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.cameraBuffer = std::move(cameraBufferResult.value());

  const Extent2D extent{kPbrSheenDemoExtentPixels, kPbrSheenDemoExtentPixels};

  auto depthTextureResult = fixture.device->createTexture({.extent = extent, .format = DepthFormat::D32Sfloat});
  if (depthTextureResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.depthTexture = std::move(depthTextureResult.value());

  auto offscreenTargetResult =
      fixture.device->createOffscreenTarget({.extent = extent, .format = kPbrSheenDemoColorFormat});
  if (offscreenTargetResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.offscreenTarget = std::move(offscreenTargetResult.value());

  const std::size_t readbackSizeBytes =
      static_cast<std::size_t>(kPbrSheenDemoExtentPixels) * kPbrSheenDemoExtentPixels * 4;
  auto readbackBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = readbackSizeBytes});
  if (readbackBufferResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.readbackBuffer = std::move(readbackBufferResult.value());

  auto hdrColorTargetResult = fixture.device->createHdrColorTarget({.extent = extent});
  if (hdrColorTargetResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.hdrColorTarget = std::move(hdrColorTargetResult.value());

  const float fullscreenTriangleVertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  auto fullscreenTriangleVertexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  if (fullscreenTriangleVertexBufferResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.fullscreenTriangleVertexBuffer = std::move(fullscreenTriangleVertexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleVertexBuffer->mappedData(), fullscreenTriangleVertices,
              sizeof(fullscreenTriangleVertices));

  const std::uint16_t fullscreenTriangleIndices[3] = {0, 1, 2};
  auto fullscreenTriangleIndexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Index, .sizeBytes = sizeof(fullscreenTriangleIndices)});
  if (fullscreenTriangleIndexBufferResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.fullscreenTriangleIndexBuffer = std::move(fullscreenTriangleIndexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleIndexBuffer->mappedData(), fullscreenTriangleIndices,
              sizeof(fullscreenTriangleIndices));

  auto outputTransformSamplerResult = fixture.device->createSampler(
      {.filter = atlantis::rhi::Filter::Linear, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  if (outputTransformSamplerResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.outputTransformSampler = std::move(outputTransformSamplerResult.value());

  auto outputTransformPipelineResult = fixture.device->createPipeline(
      {.vertexShader = {.spirvWords = fixture.outputTransformUnormVertexSpirv.data(),
                         .wordCount = fixture.outputTransformUnormVertexSpirv.size()},
       .fragmentShader = {.spirvWords = fixture.outputTransformUnormFragmentSpirv.data(),
                           .wordCount = fixture.outputTransformUnormFragmentSpirv.size()},
       .vertexInputLayout = fixture.outputTransformUnormVertexInputLayout,
       .colorFormat = kPbrSheenDemoColorFormat,
       .pushConstantSizeBytes = 4,  // Plan 0031
       .sampledTextureBindingCount = 1,
       .hasCameraUniformBinding = false,
       .hasDepthAttachment = false});
  if (outputTransformPipelineResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.outputTransformPipeline = std::move(outputTransformPipelineResult.value());

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
  if (skyPipelineResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.skyPipeline = std::move(skyPipelineResult.value());

  auto shadowMapResult = fixture.device->createShadowMap({.extent = {1024, 1024}});
  if (shadowMapResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.shadowMap = std::move(shadowMapResult.value());

  auto shadowMapSamplerResult = fixture.device->createSampler(
      {.filter = atlantis::rhi::Filter::Nearest, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  if (shadowMapSamplerResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.shadowMapSampler = std::move(shadowMapSamplerResult.value());

  auto shadowCastPipelineResult = fixture.device->createPipeline(
      {.vertexShader = {.spirvWords = fixture.shadowCastVertexSpirv.data(),
                         .wordCount = fixture.shadowCastVertexSpirv.size()},
       .fragmentShader = {.spirvWords = fixture.shadowCastFragmentSpirv.data(),
                           .wordCount = fixture.shadowCastFragmentSpirv.size()},
       .vertexInputLayout = fixture.shadowCastVertexInputLayout,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .sampledTextureBindingCount = 0,
       .hasCameraUniformBinding = true,
       .hasDepthAttachment = true,
       .depthWriteEnabled = true,
       .hasColorAttachment = false});
  if (shadowCastPipelineResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.shadowCastPipeline = std::move(shadowCastPipelineResult.value());

  auto shadowLightSpaceBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = 128});
  if (shadowLightSpaceBufferResult.isErr()) return ResultT::Err(PbrSheenDemoSetupError::ResourceCreationFailed);
  fixture.shadowLightSpaceBuffer = std::move(shadowLightSpaceBufferResult.value());

  return ResultT::Ok(std::move(fixture));
}

atlantis::Result<PixelBuffer, PbrSheenDemoRenderError> renderPbrSheenDemoFrame(PbrSheenDemoFixture& fixture) {
  namespace rhi = atlantis::rhi;
  namespace render_graph = atlantis::render_graph;
  using ResultT = atlantis::Result<PixelBuffer, PbrSheenDemoRenderError>;

  auto acquireResult = fixture.offscreenTarget->acquireTarget();
  if (acquireResult.isErr()) return ResultT::Err(PbrSheenDemoRenderError::AcquireFailed);
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

  fixture.world->updateTransforms();

  const auto activeCamera = fixture.world->activeCamera();
  if (!activeCamera.has_value()) return ResultT::Err(PbrSheenDemoRenderError::NoActiveCamera);
  const auto cameraWorldMatrixResult = fixture.world->getWorldMatrix(*activeCamera);
  const auto cameraComponentResult = fixture.world->getCamera(*activeCamera);
  if (cameraWorldMatrixResult.isErr() || cameraComponentResult.isErr()) {
    return ResultT::Err(PbrSheenDemoRenderError::ExtractionFailed);
  }
  const atlantis::world::Camera cameraComponent = cameraComponentResult.value();
  const auto extractionResult = extractCameraMatrices(cameraWorldMatrixResult.value(), cameraComponent.fovYRadians,
                                                        cameraComponent.nearZ, cameraComponent.farZ, 1.0f);
  if (extractionResult.isErr()) return ResultT::Err(PbrSheenDemoRenderError::ExtractionFailed);

  auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = extractionResult.value().view[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = extractionResult.value().projection[i];

  std::vector<LightExtractionInput> lightInputs;
  for (const atlantis::world::EntityId& id : fixture.world->lightEntities()) {
    const auto lightResult = fixture.world->getLight(id);
    const auto lightWorldMatrixResult = fixture.world->getWorldMatrix(id);
    if (lightResult.isErr() || lightWorldMatrixResult.isErr()) {
      return ResultT::Err(PbrSheenDemoRenderError::LightExtractionFailed);
    }
    lightInputs.push_back({lightResult.value(), lightWorldMatrixResult.value()});
  }
  const auto lightingResult = extractFrameLightingData(lightInputs);
  if (lightingResult.isErr()) return ResultT::Err(PbrSheenDemoRenderError::LightExtractionFailed);
  auto* lightingData = reinterpret_cast<FrameLightingData*>(cameraData + 32);
  *lightingData = lightingResult.value();

  auto* cameraWorldPositionData = reinterpret_cast<CameraWorldPositionData*>(
      cameraData + atlantis::runtime::kCameraUniformWorldPositionOffsetBytes / sizeof(float));
  *cameraWorldPositionData = extractCameraWorldPosition(cameraWorldMatrixResult.value());
  const std::array<float, 36>* irradianceShSource = nullptr;
  if (fixture.environmentData.has_value()) {
    irradianceShSource = &fixture.environmentData->irradianceSh;
  } else if (fixture.environmentLightingResources.has_value()) {
    irradianceShSource = &fixture.environmentLightingResources->irradianceSh;
  }
  atlantis::runtime::writeEnvironmentIrradianceSh(
      std::span<float, 36>(cameraData + atlantis::runtime::kCameraUniformIrradianceShOffsetBytes / sizeof(float), 36),
      irradianceShSource);

  std::memcpy(cameraData + atlantis::runtime::kCameraUniformLightSpaceOffsetBytes / sizeof(float), kIdentityMatrix,
              sizeof(float) * 16);
  std::memcpy(cameraData + atlantis::runtime::kCameraUniformLightSpaceOffsetBytes / sizeof(float) + 16, kIdentityMatrix,
              sizeof(float) * 16);

  // Plan 0043 P5 (Spec 0043 R5): the FogData tail, from the World's active
  // camera, written unconditionally every frame like the Runtime's --
  // density 0 (off) unless the camera carries fog.
  *reinterpret_cast<atlantis::runtime::FogData*>(cameraData + atlantis::runtime::kCameraUniformFogOffsetBytes /
                                                                   sizeof(float)) =
      atlantis::runtime::extractFogData(cameraComponent.fog);

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
  if (commandListResult.isErr()) return ResultT::Err(PbrSheenDemoRenderError::CommandListCreationFailed);
  std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

  std::optional<atlantis::runtime::EnvironmentLightingCandidate> environmentCandidate;
  if (fixture.environmentData.has_value() && !fixture.environmentLightingResources.has_value()) {
    auto result = atlantis::runtime::realizeEnvironmentCandidate(*fixture.device, *fixture.environmentData);
    if (result.isErr()) return ResultT::Err(PbrSheenDemoRenderError::CommandListCreationFailed);
    environmentCandidate.emplace(std::move(result.value()));
    atlantis::runtime::recordEnvironmentUploads(*commandList, *environmentCandidate);
  }
  const bool environmentEnabled = fixture.environmentData.has_value() || fixture.environmentLightingResources.has_value();

  std::unordered_map<atlantis::asset_system::AssetId, RealizedMaterialCandidate> realizedCandidates =
      // Plan 0035 Milestone 3 (ADR-0081): unlike every other sibling
      // fixture, the two trailing trios below are the REAL, loaded
      // pbrSheenIbl*/pbrSheenIblNormalMap* shader data -- this
      // fixture's own scenes reference MaterialKind::PbrSheen
      // materials, so selectShaderPair() (material_realization.cpp)
      // must see genuine shader trios here, never dead-path filler. The
      // two clearcoat trailing trios are dead-path filler, reusing
      // pbrDirectLit* -- this fixture never realizes a PbrClearcoat
      // material.
      realizePendingMaterials(*fixture.device, *commandList, fixture.unlitTexturedVertexInputLayout,
                               fixture.unlitTexturedVertexSpirv, fixture.unlitTexturedFragmentSpirv,
                               fixture.litTexturedVertexInputLayout,
                               fixture.litTexturedVertexSpirv, fixture.litTexturedFragmentSpirv,
                               fixture.pbrDirectLitVertexInputLayout, fixture.pbrDirectLitVertexSpirv,
                               fixture.pbrDirectLitFragmentSpirv, fixture.pbrIblVertexInputLayout,
                               fixture.pbrIblVertexSpirv, fixture.pbrIblFragmentSpirv,
                               fixture.pbrDirectLitVertexInputLayout, fixture.pbrDirectLitVertexSpirv,
                               fixture.pbrDirectLitFragmentSpirv, fixture.pbrIblVertexInputLayout,
                               fixture.pbrIblVertexSpirv, fixture.pbrIblFragmentSpirv,
                               fixture.pbrDirectLitVertexInputLayout, fixture.pbrDirectLitVertexSpirv,
                               fixture.pbrDirectLitFragmentSpirv, fixture.pbrDirectLitVertexInputLayout,
                               fixture.pbrDirectLitVertexSpirv, fixture.pbrDirectLitFragmentSpirv,
                               fixture.pbrSheenIblVertexInputLayout, fixture.pbrSheenIblVertexSpirv,
                               fixture.pbrSheenIblFragmentSpirv, fixture.pbrSheenIblNormalMapVertexInputLayout,
                               fixture.pbrSheenIblNormalMapVertexSpirv,
                               fixture.pbrSheenIblNormalMapFragmentSpirv,
                               // Plan 0035 Milestone 4 (ADR-0081): this
                               // fixture's own scenes never realize a
                               // PbrAnisotropic material, so the two new
                               // trailing trios are dead-path filler,
                               // reusing this fixture's own already-
                               // loaded pbrDirectLit* values -- mirrors
                               // the pre-existing pbrIbl*/pbrDirectLit*
                               // reuse pattern above.
                               fixture.pbrDirectLitVertexInputLayout, fixture.pbrDirectLitVertexSpirv,
                               fixture.pbrDirectLitFragmentSpirv, fixture.pbrDirectLitVertexInputLayout,
                               fixture.pbrDirectLitVertexSpirv, fixture.pbrDirectLitFragmentSpirv, environmentEnabled,
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
    // Plan 0035 Milestone 3: PbrSheen shares PbrDirectLit's own exact
    // vertex-normal-transform shape, so it needs this same gate too.
    const auto materialDataIt = fixture.materialDataMap.find(*materialAsset);
    if (materialDataIt == fixture.materialDataMap.end()) continue;  // resolveMaterialAsset() already confirmed membership; defensive only
    if (materialDataIt->second.kind == atlantis::asset_system::MaterialKind::LitTextured ||
        materialDataIt->second.kind == atlantis::asset_system::MaterialKind::PbrDirectLit ||
        materialDataIt->second.kind == atlantis::asset_system::MaterialKind::PbrSheen) {
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
                      *fixture.outputTransformPipeline, *fixture.outputTransformSampler, 0.0f,
                      environmentLightingView.has_value() ? &*environmentLightingView : nullptr,
                      fixture.skyPipeline.get(), *fixture.shadowMap, *fixture.shadowMapSampler,
                      *fixture.shadowCastPipeline, *fixture.shadowLightSpaceBuffer, {});

  render_graph::RenderGraphBuilder copyBuilder;
  const auto copyResource = copyBuilder.declareResource("color-copy");
  const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
  copyBuilder.writes(copyPass, copyResource, rhi::ResourceState::TransferSource);
  copyBuilder.setExecute(copyPass, [&target, &fixture](rhi::CommandList& cmd) {
    cmd.copyRenderTargetToBuffer(*target, *fixture.readbackBuffer);
  });
  auto copyCompileResult = copyBuilder.compile();
  if (copyCompileResult.isErr()) return ResultT::Err(PbrSheenDemoRenderError::CommandListCreationFailed);
  const std::vector<render_graph::ResourceBinding> copyBindings{{.resource = copyCompileResult.value().resourceAt(0),
                                                                   .target = target.get(),
                                                                   .incomingState = rhi::ResourceState::TransferSource}};
  render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  auto submitResult = fixture.device->submit(std::move(commandList), *target);
  if (submitResult.isErr()) return ResultT::Err(PbrSheenDemoRenderError::SubmitFailed);

  auto waitResult = fixture.device->waitIdle();
  if (waitResult.isErr()) return ResultT::Err(PbrSheenDemoRenderError::WaitIdleFailed);

  for (auto& [assetId, candidate] : realizedCandidates) {
    if (candidate.newSampledTexture) {
      fixture.sampledTextureResourceMap.emplace(candidate.textureAssetId, std::move(candidate.newSampledTexture));
    }
    // Plan 0035 Milestone 3 (fix-0): mirrors runtime_application.cpp's
    // own real, production commit (Step 3) -- normalMapTextureAssetId/
    // newNormalMapTexture is a second, independent texture slot on
    // RealizedMaterialCandidate (material_realization.h), same shape as
    // textureAssetId/newSampledTexture immediately above, and equally
    // needs a persistent home; this fixture's own normal-mapped scene
    // (pbr_sheen_normal_map_demo) genuinely realizes such a material, so
    // omitting this would dangle on this fixture's very next
    // renderPbrSheenDemoFrame() call.
    if (candidate.newNormalMapTexture) {
      fixture.sampledTextureResourceMap.emplace(candidate.normalMapTextureAssetId,
                                                  std::move(candidate.newNormalMapTexture));
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
  result.width = kPbrSheenDemoExtentPixels;
  result.height = kPbrSheenDemoExtentPixels;
  const std::size_t byteCount =
      static_cast<std::size_t>(kPbrSheenDemoExtentPixels) * kPbrSheenDemoExtentPixels * 4;
  const auto* readbackData = static_cast<const std::uint8_t*>(fixture.readbackBuffer->mappedData());
  result.rgba8.assign(readbackData, readbackData + byteCount);

  target.reset();  // Ends this cycle's borrow (RAII, ADR-0038) -- no release()/consume() call.

  return ResultT::Ok(std::move(result));
}

}  // namespace atlantis::image_regression
