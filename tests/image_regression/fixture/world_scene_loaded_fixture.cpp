#include "world_scene_loaded_fixture.h"

#include "minimal_cube_fixture.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/load.h>
#include <atlantis/asset_system/logical_path.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>
#include <atlantis/world/scene_instantiation.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Plan 0015 Section D11/Step 9: duplicates src/runtime/scene_load.cpp's
// own D10 steps (a)-(g) independently -- not a shared function -- and
// duplicates src/runtime/scene_extraction.cpp's own camera-math/
// extraction logic independently too, matching this directory's own
// already-established "duplicated, not shared" precedent
// (world_scene_fixture.cpp's own identical top-of-file comment). The
// manifest reader here is deliberately minimal (not a re-implementation
// of SceneManifestError's own full duplicate/collision/mismatch
// validation graph, already exhaustively tested by
// tests/runtime/scene_manifest_tests.cpp) -- this fixture's own job is
// producing a rendered frame from the real, already-cooked scene
// asset, not re-verifying Plan 0015's own manifest contract.

namespace atlantis::image_regression {

namespace {

using atlantis::renderer::createMaterial;
using atlantis::renderer::createMesh;
using atlantis::renderer::DrawItem;
using atlantis::renderer::Renderer;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Extent2D;
using atlantis::rhi::OffscreenTargetCreateParams;
using atlantis::rhi::VertexInputLayout;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;

using ResultT = atlantis::Result<WorldSceneLoadedFixture, WorldSceneLoadedFixtureSetupError>;

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

// Plan 0017 Section D5/ADR-0058: gains a trailing UV0 field to match
// the mesh artifact's own new 32-byte layout. Plan 0020 Section P1/P4/
// ADR-0063: gains a further trailing normal field, matching the
// artifact's own new 44-byte layout. minimal_mesh.slang declares
// neither a UV nor a normal input, so the schema below stays unchanged
// (position@0, color@1) -- these trailing bytes are present in every
// vertex this fixture uploads but are never read by this pipeline.
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

[[nodiscard]] std::optional<VertexInputLayout> minimalMeshVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, color)},
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

using Mat4 = std::array<float, 16>;

[[nodiscard]] Mat4 identityMatrix() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

[[nodiscard]] Mat4 lookAt(float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ) {
  float fx = centerX - eyeX, fy = centerY - eyeY, fz = centerZ - eyeZ;
  const float fLen = std::sqrt(fx * fx + fy * fy + fz * fz);
  fx /= fLen;
  fy /= fLen;
  fz /= fLen;

  const float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
  float sx = fy * upZ - fz * upY;
  float sy = fz * upX - fx * upZ;
  float sz = fx * upY - fy * upX;
  const float sLen = std::sqrt(sx * sx + sy * sy + sz * sz);
  sx /= sLen;
  sy /= sLen;
  sz /= sLen;

  const float ux = sy * fz - sz * fy;
  const float uy = sz * fx - sx * fz;
  const float uz = sx * fy - sy * fx;

  Mat4 result = identityMatrix();
  result[0] = sx;
  result[4] = sy;
  result[8] = sz;
  result[1] = ux;
  result[5] = uy;
  result[9] = uz;
  result[2] = -fx;
  result[6] = -fy;
  result[10] = -fz;
  result[12] = -(sx * eyeX + sy * eyeY + sz * eyeZ);
  result[13] = -(ux * eyeX + uy * eyeY + uz * eyeZ);
  result[14] = fx * eyeX + fy * eyeY + fz * eyeZ;
  return result;
}

[[nodiscard]] Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
  const float f = 1.0f / std::tan(fovYRadians * 0.5f);
  Mat4 result{};
  result[0] = f / aspect;
  result[5] = -f;
  result[10] = farZ / (nearZ - farZ);
  result[11] = -1.0f;
  result[14] = (nearZ * farZ) / (nearZ - farZ);
  return result;
}

constexpr float kDegenerateLengthEpsilon = 1e-6f;

struct V3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;
};

[[nodiscard]] float length(const V3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
[[nodiscard]] V3 cross(const V3& a, const V3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] std::optional<Mat4> extractView(const Mat4& cameraWorldMatrix) {
  const V3 negColumn2{-cameraWorldMatrix[8], -cameraWorldMatrix[9], -cameraWorldMatrix[10]};
  const float forwardLen = length(negColumn2);
  if (forwardLen < kDegenerateLengthEpsilon) return std::nullopt;
  const V3 forward{negColumn2.x / forwardLen, negColumn2.y / forwardLen, negColumn2.z / forwardLen};

  const V3 eye{cameraWorldMatrix[12], cameraWorldMatrix[13], cameraWorldMatrix[14]};

  const V3 worldUp{0.0f, 1.0f, 0.0f};
  if (length(cross(forward, worldUp)) < kDegenerateLengthEpsilon) return std::nullopt;

  return lookAt(eye.x, eye.y, eye.z, eye.x + forward.x, eye.y + forward.y, eye.z + forward.z);
}

struct ManifestEntry {
  atlantis::asset_system::AssetId assetId = 0;
  std::string artifactPath;
  std::string metadataPath;
};

// Deliberately minimal -- see this file's own top-of-file comment.
// Tolerates a trailing '\r' per line (CMake's own file(GENERATE)
// writes this toolchain's native \r\n line ending on Windows,
// empirically confirmed by Plan 0015 Step 5's own
// scene_asset_cmake_declaration_tests.cpp).
[[nodiscard]] std::optional<std::vector<ManifestEntry>> readManifest(const char* manifestPath) {
  std::ifstream file(manifestPath, std::ios::binary);
  if (!file.is_open()) return std::nullopt;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  std::string text = buffer.str();

  std::vector<ManifestEntry> entries;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t newline = text.find('\n', start);
    std::string_view line(text.data() + start, (newline == std::string::npos ? text.size() : newline) - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!line.empty()) {
      const std::size_t firstTab = line.find('\t');
      const std::size_t secondTab = firstTab == std::string_view::npos ? std::string_view::npos
                                                                        : line.find('\t', firstTab + 1);
      if (firstTab == std::string_view::npos || secondTab == std::string_view::npos) return std::nullopt;
      const std::string_view logicalPath = line.substr(0, firstTab);
      const std::string_view artifactPath = line.substr(firstTab + 1, secondTab - firstTab - 1);
      const std::string_view metadataPath = line.substr(secondTab + 1);
      const auto normalizedResult = atlantis::asset_system::normalizeLogicalPath(logicalPath);
      if (normalizedResult.isErr()) return std::nullopt;
      entries.push_back(ManifestEntry{atlantis::asset_system::computeAssetId(normalizedResult.value()),
                                       std::string(artifactPath), std::string(metadataPath)});
    }
    if (newline == std::string::npos) break;
    start = newline + 1;
  }
  return entries;
}

[[nodiscard]] const ManifestEntry* findManifestEntry(const std::vector<ManifestEntry>& entries,
                                                      atlantis::asset_system::AssetId id) {
  const auto it = std::find_if(entries.begin(), entries.end(),
                                [id](const ManifestEntry& entry) { return entry.assetId == id; });
  return it == entries.end() ? nullptr : &*it;
}

}  // namespace

atlantis::Result<WorldSceneLoadedFixture, WorldSceneLoadedFixtureSetupError> setUpWorldSceneLoadedFixture(
    const char* sceneArtifactPath, const char* sceneMetadataPath, const char* sceneManifestPath) {
  const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
  const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
  if (!vertexSpirv.has_value() || !fragmentSpirv.has_value()) {
    return ResultT::Err(WorldSceneLoadedFixtureSetupError::ShaderLoadFailed);
  }

  auto vertexReflectionResult = loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
  if (vertexReflectionResult.isErr()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::ShaderLoadFailed);
  const auto vertexInputLayout = minimalMeshVertexLayout(vertexReflectionResult.value());
  if (!vertexInputLayout.has_value()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::ShaderLoadFailed);

  // (a) Manifest.
  const auto manifestEntries = readManifest(sceneManifestPath);
  if (!manifestEntries.has_value()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::ManifestLoadFailed);

  // (b) Decode the scene artifact.
  auto sceneResult = atlantis::asset_system::decodeScene(sceneArtifactPath, sceneMetadataPath);
  if (sceneResult.isErr()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::SceneArtifactLoadFailed);
  const atlantis::asset_system::ValidatedSceneData& scene = sceneResult.value();

  // (c) Distinct AssetIds, ascending first-reference order.
  std::vector<atlantis::asset_system::AssetId> distinctIds;
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    if (const auto& node = scene.node(i); node.renderable.has_value()) {
      const auto id = node.renderable->meshAsset;
      if (std::find(distinctIds.begin(), distinctIds.end(), id) == distinctIds.end()) distinctIds.push_back(id);
    }
  }

  // (d) Resolve.
  std::vector<const ManifestEntry*> resolvedEntries;
  for (const auto id : distinctIds) {
    const auto* entry = findManifestEntry(*manifestEntries, id);
    if (!entry) return ResultT::Err(WorldSceneLoadedFixtureSetupError::SceneDependencyUnresolved);
    resolvedEntries.push_back(entry);
  }

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Image Regression Fixture (World Scene, Loaded)", .enableValidationLayers = true});
  if (deviceResult.isErr()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::DeviceCreationFailed);

  WorldSceneLoadedFixture fixture;
  fixture.device = std::move(deviceResult.value());

  // (e) Load, same order as (c)/(d).
  for (std::size_t i = 0; i < distinctIds.size(); ++i) {
    auto meshAssetResult =
        atlantis::asset_system::loadStaticMeshAsset(resolvedEntries[i]->artifactPath, resolvedEntries[i]->metadataPath);
    if (meshAssetResult.isErr()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::SceneDependencyLoadFailed);
    const atlantis::asset_system::StaticMeshAssetData& meshData = meshAssetResult.value();
    auto meshResult = createMesh(*fixture.device, *vertexInputLayout, meshData.vertexBytes().data(),
                                  meshData.vertexBytes().size(), meshData.indices().data(),
                                  static_cast<std::uint32_t>(meshData.indices().size()));
    if (meshResult.isErr()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::SceneDependencyLoadFailed);
    fixture.meshResourceMap.emplace(distinctIds[i], std::move(meshResult.value()));
  }

  auto cameraBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  if (cameraBufferResult.isErr()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::ResourceCreationFailed);
  fixture.cameraBuffer = std::move(cameraBufferResult.value());

  auto materialResult = createMaterial(
      *fixture.device, {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
                         .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
                         .vertexInputLayout = *vertexInputLayout,
                         // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3): every
                         // geometry Pipeline now renders into the fixed HDR
                         // intermediate, never this fixture's own real
                         // kFixtureColorFormat directly.
                         .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
                         .depthFormat = DepthFormat::D32Sfloat,
                         .pushConstantSizeBytes = sizeof(float) * 16});
  if (materialResult.isErr()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::ResourceCreationFailed);
  fixture.material = std::move(materialResult.value());

  const Extent2D extent{kFixtureExtentPixels, kFixtureExtentPixels};

  auto depthTextureResult = fixture.device->createTexture({.extent = extent, .format = DepthFormat::D32Sfloat});
  if (depthTextureResult.isErr()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::ResourceCreationFailed);
  fixture.depthTexture = std::move(depthTextureResult.value());

  auto offscreenTargetResult =
      fixture.device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = extent, .format = kFixtureColorFormat});
  if (offscreenTargetResult.isErr()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::ResourceCreationFailed);
  fixture.offscreenTarget = std::move(offscreenTargetResult.value());

  const std::size_t readbackSizeBytes = static_cast<std::size_t>(kFixtureExtentPixels) * kFixtureExtentPixels * 4;
  auto readbackBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = readbackSizeBytes});
  if (readbackBufferResult.isErr()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::ResourceCreationFailed);
  fixture.readbackBuffer = std::move(readbackBufferResult.value());

  // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3/D-6): this fixture's own
  // HDR intermediate, fullscreen-triangle geometry/sampler, and output-
  // transform Pipeline.
  const auto outputTransformVertexSpirv = loadSpirvFile("shaders/output_transform_unorm.vert.spv");
  const auto outputTransformFragmentSpirv = loadSpirvFile("shaders/output_transform_unorm.frag.spv");
  if (!outputTransformVertexSpirv.has_value() || !outputTransformFragmentSpirv.has_value()) {
    return ResultT::Err(WorldSceneLoadedFixtureSetupError::ShaderLoadFailed);
  }
  auto outputTransformVertexReflectionResult = loadReflectionMetadata("shaders/output_transform_unorm.vert.refl.json");
  if (outputTransformVertexReflectionResult.isErr()) {
    return ResultT::Err(WorldSceneLoadedFixtureSetupError::ShaderLoadFailed);
  }
  const auto outputTransformVertexInputLayout =
      outputTransformVertexLayout(outputTransformVertexReflectionResult.value());
  if (!outputTransformVertexInputLayout.has_value()) {
    return ResultT::Err(WorldSceneLoadedFixtureSetupError::ShaderLoadFailed);
  }

  auto hdrColorTargetResult = fixture.device->createHdrColorTarget({.extent = extent});
  if (hdrColorTargetResult.isErr()) return ResultT::Err(WorldSceneLoadedFixtureSetupError::ResourceCreationFailed);
  fixture.hdrColorTarget = std::move(hdrColorTargetResult.value());

  const float fullscreenTriangleVertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  auto fullscreenTriangleVertexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  if (fullscreenTriangleVertexBufferResult.isErr()) {
    return ResultT::Err(WorldSceneLoadedFixtureSetupError::ResourceCreationFailed);
  }
  fixture.fullscreenTriangleVertexBuffer = std::move(fullscreenTriangleVertexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleVertexBuffer->mappedData(), fullscreenTriangleVertices,
              sizeof(fullscreenTriangleVertices));

  const std::uint16_t fullscreenTriangleIndices[3] = {0, 1, 2};
  auto fullscreenTriangleIndexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Index, .sizeBytes = sizeof(fullscreenTriangleIndices)});
  if (fullscreenTriangleIndexBufferResult.isErr()) {
    return ResultT::Err(WorldSceneLoadedFixtureSetupError::ResourceCreationFailed);
  }
  fixture.fullscreenTriangleIndexBuffer = std::move(fullscreenTriangleIndexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleIndexBuffer->mappedData(), fullscreenTriangleIndices,
              sizeof(fullscreenTriangleIndices));

  auto outputTransformSamplerResult = fixture.device->createSampler(
      {.filter = atlantis::rhi::Filter::Linear, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  if (outputTransformSamplerResult.isErr()) {
    return ResultT::Err(WorldSceneLoadedFixtureSetupError::ResourceCreationFailed);
  }
  fixture.outputTransformSampler = std::move(outputTransformSamplerResult.value());

  auto outputTransformPipelineResult = fixture.device->createPipeline(
      {.vertexShader = {.spirvWords = outputTransformVertexSpirv->data(),
                         .wordCount = outputTransformVertexSpirv->size()},
       .fragmentShader = {.spirvWords = outputTransformFragmentSpirv->data(),
                           .wordCount = outputTransformFragmentSpirv->size()},
       .vertexInputLayout = *outputTransformVertexInputLayout,
       .colorFormat = kFixtureColorFormat,
       .hasSampledTextureBinding = true,
       .hasCameraUniformBinding = false,
       .hasDepthAttachment = false});
  if (outputTransformPipelineResult.isErr()) {
    return ResultT::Err(WorldSceneLoadedFixtureSetupError::ResourceCreationFailed);
  }
  fixture.outputTransformPipeline = std::move(outputTransformPipelineResult.value());

  // (f)/(g) Instantiate -- infallible.
  fixture.world.emplace(atlantis::world::fromValidatedSceneData(scene));

  return ResultT::Ok(std::move(fixture));
}

atlantis::Result<PixelBuffer, WorldSceneLoadedFixtureRenderError> renderOneWorldSceneLoadedFrame(
    WorldSceneLoadedFixture& fixture) {
  namespace rhi = atlantis::rhi;
  namespace render_graph = atlantis::render_graph;

  auto acquireResult = fixture.offscreenTarget->acquireTarget();
  if (acquireResult.isErr()) {
    return atlantis::Result<PixelBuffer, WorldSceneLoadedFixtureRenderError>::Err(
        WorldSceneLoadedFixtureRenderError::AcquireFailed);
  }
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

  fixture.world->updateTransforms();

  const auto activeCamera = fixture.world->activeCamera();
  if (!activeCamera.has_value()) {
    return atlantis::Result<PixelBuffer, WorldSceneLoadedFixtureRenderError>::Err(
        WorldSceneLoadedFixtureRenderError::NoActiveCamera);
  }
  const auto cameraWorldMatrixResult = fixture.world->getWorldMatrix(*activeCamera);
  const auto cameraComponentResult = fixture.world->getCamera(*activeCamera);
  if (cameraWorldMatrixResult.isErr() || cameraComponentResult.isErr()) {
    return atlantis::Result<PixelBuffer, WorldSceneLoadedFixtureRenderError>::Err(
        WorldSceneLoadedFixtureRenderError::ExtractionFailed);
  }
  const auto view = extractView(cameraWorldMatrixResult.value());
  if (!view.has_value()) {
    return atlantis::Result<PixelBuffer, WorldSceneLoadedFixtureRenderError>::Err(
        WorldSceneLoadedFixtureRenderError::ExtractionFailed);
  }
  const atlantis::world::Camera cameraComponent = cameraComponentResult.value();
  const Mat4 projection = perspective(cameraComponent.fovYRadians, 1.0f, cameraComponent.nearZ, cameraComponent.farZ);

  auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = (*view)[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = projection[i];

  std::vector<DrawItem> drawItems;
  for (const auto& id : fixture.world->renderableEntities()) {
    const auto renderableResult = fixture.world->getRenderable(id);
    if (renderableResult.isErr()) continue;
    const auto meshIt = fixture.meshResourceMap.find(renderableResult.value().meshAsset);
    if (meshIt == fixture.meshResourceMap.end()) continue;
    const auto worldMatrixResult = fixture.world->getWorldMatrix(id);
    if (worldMatrixResult.isErr()) continue;

    DrawItem item;
    item.mesh = &meshIt->second;
    item.material = &*fixture.material;
    item.objectToWorld = worldMatrixResult.value();
    drawItems.push_back(item);
  }

  auto commandListResult = fixture.device->createCommandList();
  if (commandListResult.isErr()) {
    return atlantis::Result<PixelBuffer, WorldSceneLoadedFixtureRenderError>::Err(
        WorldSceneLoadedFixtureRenderError::CommandListCreationFailed);
  }
  std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

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
  if (copyCompileResult.isErr()) {
    return atlantis::Result<PixelBuffer, WorldSceneLoadedFixtureRenderError>::Err(
        WorldSceneLoadedFixtureRenderError::CommandListCreationFailed);
  }
  const std::vector<render_graph::ResourceBinding> copyBindings{{.resource = copyCompileResult.value().resourceAt(0),
                                                                   .target = target.get(),
                                                                   .incomingState = rhi::ResourceState::TransferSource}};
  render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  auto submitResult = fixture.device->submit(std::move(commandList), *target);
  if (submitResult.isErr()) {
    return atlantis::Result<PixelBuffer, WorldSceneLoadedFixtureRenderError>::Err(
        WorldSceneLoadedFixtureRenderError::SubmitFailed);
  }

  auto waitResult = fixture.device->waitIdle();
  if (waitResult.isErr()) {
    return atlantis::Result<PixelBuffer, WorldSceneLoadedFixtureRenderError>::Err(
        WorldSceneLoadedFixtureRenderError::WaitIdleFailed);
  }

  PixelBuffer result;
  result.width = kFixtureExtentPixels;
  result.height = kFixtureExtentPixels;
  const std::size_t byteCount = static_cast<std::size_t>(kFixtureExtentPixels) * kFixtureExtentPixels * 4;
  const auto* readbackData = static_cast<const std::uint8_t*>(fixture.readbackBuffer->mappedData());
  result.rgba8.assign(readbackData, readbackData + byteCount);

  target.reset();  // Ends this cycle's borrow (RAII, ADR-0038) -- no release()/consume() call.

  return atlantis::Result<PixelBuffer, WorldSceneLoadedFixtureRenderError>::Ok(std::move(result));
}

}  // namespace atlantis::image_regression
