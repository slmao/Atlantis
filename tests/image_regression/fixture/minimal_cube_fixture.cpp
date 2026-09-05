#include "minimal_cube_fixture.h"

#include <atlantis/asset_system/load.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <type_traits>
#include <utility>
#include <vector>

// Duplicates examples/headless_rendering_demo/main.cpp's own fixture
// setup and per-cycle render sequence byte-for-byte (Section 3.1) --
// same Vertex layout, same cube geometry, same fixed camera, factored
// into setUpMinimalCubeFixture()/renderOneFrame() instead of being
// inlined in a demo's own main() loop.

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
// vertex this fixture uploads but are never read by this pipeline
// (confirmed against Device::createPipeline()'s own real
// VkVertexInputAttributeDescription construction, which only names the
// offsets the schema below actually lists).
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

// Identical fixed cube to examples/headless_rendering_demo's own
// fixture -- duplicated, not shared (this file's own top-of-file note).
constexpr Vertex kCubeVertices[8] = {
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 0.0f}}, {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}},   {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},  {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 1.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}},    {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 1.0f}},
};

constexpr std::uint16_t kCubeIndices[36] = {
    0, 1, 2, 2, 3, 0,  // back  (z = -0.5)
    5, 4, 7, 7, 6, 5,  // front (z = +0.5)
    4, 0, 3, 3, 7, 4,  // left  (x = -0.5)
    1, 5, 6, 6, 2, 1,  // right (x = +0.5)
    4, 5, 1, 1, 0, 4,  // bottom (y = -0.5)
    3, 2, 6, 6, 7, 3,  // top    (y = +0.5)
};

using Mat4 = std::array<float, 16>;

[[nodiscard]] Mat4 identityMatrix() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// Fixed camera -- the same eye/look-at examples/headless_rendering_demo
// uses (eyeX=0, eyeY=1.5, eyeZ=2.5, looking at the origin), known to
// frame the cube centered in view.
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

// Plan 0027 Milestone 9 (ADR-0072 D-3): shadow_cast.slang's own real,
// position-only VertexInput -- against this file's own shared Vertex
// struct above, mirrors runtime_application.cpp's own identical
// shadowCastVertexLayout().
[[nodiscard]] std::optional<VertexInputLayout> shadowCastVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
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

// Plan 0024 Milestone 7 (ADR-0068 D-1/D-3/D-6): this fixture's own HDR
// intermediate, fullscreen-triangle geometry/sampler, and output-
// transform Pipeline -- shared by both setUpMinimalCubeFixture() and
// setUpMinimalCubeFixtureFromAsset() below (identical in every respect
// except where the mesh's own vertex/index bytes come from, this
// file's own top-of-file note), so factored into one file-local helper
// rather than duplicated. Follows the same WORKING_DIRECTORY-relative
// "shaders/output_transform_unorm.{vert,frag}.spv" convention every
// shader load in this file already uses. Returns std::nullopt on
// success; the fixture's own hdrColorTarget/fullscreenTriangle*/
// outputTransformSampler/outputTransformPipeline members are populated
// only on that path.
[[nodiscard]] std::optional<FixtureSetupError> setUpOutputTransformResources(MinimalCubeFixture& fixture,
                                                                              Extent2D extent) {
  const auto outputTransformVertexSpirv = loadSpirvFile("shaders/output_transform_unorm.vert.spv");
  const auto outputTransformFragmentSpirv = loadSpirvFile("shaders/output_transform_unorm.frag.spv");
  if (!outputTransformVertexSpirv.has_value() || !outputTransformFragmentSpirv.has_value()) {
    return FixtureSetupError::ShaderLoadFailed;
  }
  auto outputTransformVertexReflectionResult = loadReflectionMetadata("shaders/output_transform_unorm.vert.refl.json");
  if (outputTransformVertexReflectionResult.isErr()) return FixtureSetupError::ShaderLoadFailed;
  const auto outputTransformVertexInputLayout =
      outputTransformVertexLayout(outputTransformVertexReflectionResult.value());
  if (!outputTransformVertexInputLayout.has_value()) return FixtureSetupError::ShaderLoadFailed;

  auto hdrColorTargetResult = fixture.device->createHdrColorTarget({.extent = extent});
  if (hdrColorTargetResult.isErr()) return FixtureSetupError::ResourceCreationFailed;
  fixture.hdrColorTarget = std::move(hdrColorTargetResult.value());

  const float fullscreenTriangleVertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  auto fullscreenTriangleVertexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  if (fullscreenTriangleVertexBufferResult.isErr()) return FixtureSetupError::ResourceCreationFailed;
  fixture.fullscreenTriangleVertexBuffer = std::move(fullscreenTriangleVertexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleVertexBuffer->mappedData(), fullscreenTriangleVertices,
              sizeof(fullscreenTriangleVertices));

  const std::uint16_t fullscreenTriangleIndices[3] = {0, 1, 2};
  auto fullscreenTriangleIndexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Index, .sizeBytes = sizeof(fullscreenTriangleIndices)});
  if (fullscreenTriangleIndexBufferResult.isErr()) return FixtureSetupError::ResourceCreationFailed;
  fixture.fullscreenTriangleIndexBuffer = std::move(fullscreenTriangleIndexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleIndexBuffer->mappedData(), fullscreenTriangleIndices,
              sizeof(fullscreenTriangleIndices));

  auto outputTransformSamplerResult = fixture.device->createSampler(
      {.filter = atlantis::rhi::Filter::Linear, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  if (outputTransformSamplerResult.isErr()) return FixtureSetupError::ResourceCreationFailed;
  fixture.outputTransformSampler = std::move(outputTransformSamplerResult.value());

  auto outputTransformPipelineResult = fixture.device->createPipeline(
      {.vertexShader = {.spirvWords = outputTransformVertexSpirv->data(),
                         .wordCount = outputTransformVertexSpirv->size()},
       .fragmentShader = {.spirvWords = outputTransformFragmentSpirv->data(),
                           .wordCount = outputTransformFragmentSpirv->size()},
       .vertexInputLayout = *outputTransformVertexInputLayout,
       .colorFormat = kFixtureColorFormat,
       .sampledTextureBindingCount = 1,
       .hasCameraUniformBinding = false,
       .hasDepthAttachment = false});
  if (outputTransformPipelineResult.isErr()) return FixtureSetupError::ResourceCreationFailed;
  fixture.outputTransformPipeline = std::move(outputTransformPipelineResult.value());

  return std::nullopt;
}

// Plan 0027 Milestone 9 (ADR-0072 D-1/P9e): a minimal, always-possible
// real ShadowMap/Sampler/Pipeline/Buffer -- shared by both
// setUpMinimalCubeFixture() and setUpMinimalCubeFixtureFromAsset() below,
// mirroring setUpOutputTransformResources()'s own factoring immediately
// above.
[[nodiscard]] std::optional<FixtureSetupError> setUpShadowCastResources(MinimalCubeFixture& fixture) {
  auto shadowMapResult = fixture.device->createShadowMap({.extent = {1024, 1024}});
  if (shadowMapResult.isErr()) return FixtureSetupError::ResourceCreationFailed;
  fixture.shadowMap = std::move(shadowMapResult.value());

  auto shadowMapSamplerResult = fixture.device->createSampler(
      {.filter = atlantis::rhi::Filter::Nearest, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  if (shadowMapSamplerResult.isErr()) return FixtureSetupError::ResourceCreationFailed;
  fixture.shadowMapSampler = std::move(shadowMapSamplerResult.value());

  const auto shadowCastVertexSpirv = loadSpirvFile("shaders/shadow_cast.vert.spv");
  const auto shadowCastFragmentSpirv = loadSpirvFile("shaders/shadow_cast.frag.spv");
  if (!shadowCastVertexSpirv.has_value() || !shadowCastFragmentSpirv.has_value()) {
    return FixtureSetupError::ShaderLoadFailed;
  }
  auto shadowCastVertexReflectionResult = loadReflectionMetadata("shaders/shadow_cast.vert.refl.json");
  if (shadowCastVertexReflectionResult.isErr()) return FixtureSetupError::ShaderLoadFailed;
  const auto shadowCastVertexInputLayout = shadowCastVertexLayout(shadowCastVertexReflectionResult.value());
  if (!shadowCastVertexInputLayout.has_value()) return FixtureSetupError::ShaderLoadFailed;

  auto shadowCastPipelineResult = fixture.device->createPipeline(
      {.vertexShader = {.spirvWords = shadowCastVertexSpirv->data(), .wordCount = shadowCastVertexSpirv->size()},
       .fragmentShader = {.spirvWords = shadowCastFragmentSpirv->data(),
                           .wordCount = shadowCastFragmentSpirv->size()},
       .vertexInputLayout = *shadowCastVertexInputLayout,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .sampledTextureBindingCount = 0,
       .hasCameraUniformBinding = true,
       .hasDepthAttachment = true,
       .depthWriteEnabled = true,
       .hasColorAttachment = false});
  if (shadowCastPipelineResult.isErr()) return FixtureSetupError::ResourceCreationFailed;
  fixture.shadowCastPipeline = std::move(shadowCastPipelineResult.value());

  auto shadowLightSpaceBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = 128});
  if (shadowLightSpaceBufferResult.isErr()) return FixtureSetupError::ResourceCreationFailed;
  fixture.shadowLightSpaceBuffer = std::move(shadowLightSpaceBufferResult.value());

  return std::nullopt;
}

}  // namespace

Result<MinimalCubeFixture, FixtureSetupError> setUpMinimalCubeFixture() {
  const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
  const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
  if (!vertexSpirv.has_value() || !fragmentSpirv.has_value()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ShaderLoadFailed);
  }

  auto vertexReflectionResult = loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
  if (vertexReflectionResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ShaderLoadFailed);
  }
  const auto vertexInputLayout = minimalMeshVertexLayout(vertexReflectionResult.value());
  if (!vertexInputLayout.has_value()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ShaderLoadFailed);
  }

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Image Regression Fixture", .enableValidationLayers = true});
  if (deviceResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::DeviceCreationFailed);
  }

  MinimalCubeFixture fixture;
  fixture.device = std::move(deviceResult.value());

  auto meshResult = createMesh(*fixture.device, *vertexInputLayout, kCubeVertices, sizeof(kCubeVertices),
                                kCubeIndices, static_cast<std::uint32_t>(std::size(kCubeIndices)));
  if (meshResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ResourceCreationFailed);
  }
  fixture.mesh = std::move(meshResult.value());

  auto cameraBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  if (cameraBufferResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ResourceCreationFailed);
  }
  fixture.cameraBuffer = std::move(cameraBufferResult.value());

  auto materialResult = createMaterial(
      *fixture.device, {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
                         .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
                         .vertexInputLayout = *vertexInputLayout,
                         // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3): every
                         // geometry Pipeline now renders into the fixed HDR
                         // intermediate, never this fixture's own real
                         // kFixtureColorFormat directly -- mirrors
                         // material_realization.cpp's own identical change.
                         .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
                         .depthFormat = DepthFormat::D32Sfloat,
                         .pushConstantSizeBytes = sizeof(float) * 16});
  if (materialResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ResourceCreationFailed);
  }
  fixture.material = std::move(materialResult.value());

  const Extent2D extent{kFixtureExtentPixels, kFixtureExtentPixels};

  auto depthTextureResult = fixture.device->createTexture({.extent = extent, .format = DepthFormat::D32Sfloat});
  if (depthTextureResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ResourceCreationFailed);
  }
  fixture.depthTexture = std::move(depthTextureResult.value());

  auto offscreenTargetResult =
      fixture.device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = extent, .format = kFixtureColorFormat});
  if (offscreenTargetResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ResourceCreationFailed);
  }
  fixture.offscreenTarget = std::move(offscreenTargetResult.value());

  const std::size_t readbackSizeBytes = static_cast<std::size_t>(kFixtureExtentPixels) * kFixtureExtentPixels * 4;
  auto readbackBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = readbackSizeBytes});
  if (readbackBufferResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ResourceCreationFailed);
  }
  fixture.readbackBuffer = std::move(readbackBufferResult.value());

  if (const auto outputTransformError = setUpOutputTransformResources(fixture, extent);
      outputTransformError.has_value()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(*outputTransformError);
  }

  if (const auto shadowCastError = setUpShadowCastResources(fixture); shadowCastError.has_value()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(*shadowCastError);
  }

  return Result<MinimalCubeFixture, FixtureSetupError>::Ok(std::move(fixture));
}

Result<PixelBuffer, FixtureRenderError> renderOneFrame(MinimalCubeFixture& fixture) {
  namespace rhi = atlantis::rhi;
  namespace render_graph = atlantis::render_graph;

  auto acquireResult = fixture.offscreenTarget->acquireTarget();
  if (acquireResult.isErr()) {
    return Result<PixelBuffer, FixtureRenderError>::Err(FixtureRenderError::AcquireFailed);
  }
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

  const Mat4 view = lookAt(0.0f, 1.5f, 2.5f, 0.0f, 0.0f, 0.0f);
  const Mat4 projection = perspective(60.0f * 3.14159265f / 180.0f, 1.0f, 0.1f, 100.0f);

  auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = view[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = projection[i];

  DrawItem item;
  item.mesh = &*fixture.mesh;
  item.material = &*fixture.material;
  item.objectToWorld = identityMatrix();
  const std::array<DrawItem, 1> drawItems{item};

  auto commandListResult = fixture.device->createCommandList();
  if (commandListResult.isErr()) {
    return Result<PixelBuffer, FixtureRenderError>::Err(FixtureRenderError::CommandListCreationFailed);
  }
  std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

  Renderer renderer;
  renderer.drawFrame(*commandList, *target, *fixture.depthTexture, *fixture.cameraBuffer, drawItems,
                      rhi::ResourceState::TransferSource, *fixture.hdrColorTarget,
                      *fixture.fullscreenTriangleVertexBuffer, *fixture.fullscreenTriangleIndexBuffer,
                      *fixture.outputTransformPipeline, *fixture.outputTransformSampler, nullptr, nullptr,
                      *fixture.shadowMap, *fixture.shadowMapSampler, *fixture.shadowCastPipeline,
                      *fixture.shadowLightSpaceBuffer, {});

  render_graph::RenderGraphBuilder copyBuilder;
  const auto copyResource = copyBuilder.declareResource("color-copy");
  const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
  copyBuilder.writes(copyPass, copyResource, rhi::ResourceState::TransferSource);
  copyBuilder.setExecute(copyPass, [&target, &fixture](rhi::CommandList& cmd) {
    cmd.copyRenderTargetToBuffer(*target, *fixture.readbackBuffer);
  });
  auto copyCompileResult = copyBuilder.compile();
  if (copyCompileResult.isErr()) {
    return Result<PixelBuffer, FixtureRenderError>::Err(FixtureRenderError::CommandListCreationFailed);
  }
  const std::vector<render_graph::ResourceBinding> copyBindings{{.resource = copyCompileResult.value().resourceAt(0),
                                                                   .target = target.get(),
                                                                   .incomingState = rhi::ResourceState::TransferSource}};
  render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  auto submitResult = fixture.device->submit(std::move(commandList), *target);
  if (submitResult.isErr()) {
    return Result<PixelBuffer, FixtureRenderError>::Err(FixtureRenderError::SubmitFailed);
  }

  auto waitResult = fixture.device->waitIdle();
  if (waitResult.isErr()) {
    return Result<PixelBuffer, FixtureRenderError>::Err(FixtureRenderError::WaitIdleFailed);
  }

  PixelBuffer result;
  result.width = kFixtureExtentPixels;
  result.height = kFixtureExtentPixels;
  const std::size_t byteCount = static_cast<std::size_t>(kFixtureExtentPixels) * kFixtureExtentPixels * 4;
  const auto* readbackData = static_cast<const std::uint8_t*>(fixture.readbackBuffer->mappedData());
  result.rgba8.assign(readbackData, readbackData + byteCount);

  target.reset();  // Ends this cycle's borrow (RAII, ADR-0038) -- no release()/consume() call.

  return Result<PixelBuffer, FixtureRenderError>::Ok(std::move(result));
}

// Plan 0012 Step 6: identical to setUpMinimalCubeFixture() above except
// for exactly one step -- where the mesh's vertex/index bytes come
// from. This function is the composition root: it loads Asset System's
// CPU-side StaticMeshAssetData, resolves the VertexInputLayout via the
// same minimalMeshVertexLayout() helper the hand-authored path already
// uses, and calls the existing, unmodified createMesh() itself, passing
// the loaded data's own bytes straight through with no intermediate
// copy or transformation -- Asset System's own code never touches RHI
// or calls createMesh().
Result<MinimalCubeFixture, FixtureSetupError> setUpMinimalCubeFixtureFromAsset(const char* artifactPath,
                                                                                const char* metadataPath) {
  const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
  const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
  if (!vertexSpirv.has_value() || !fragmentSpirv.has_value()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ShaderLoadFailed);
  }

  auto vertexReflectionResult = loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
  if (vertexReflectionResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ShaderLoadFailed);
  }
  const auto vertexInputLayout = minimalMeshVertexLayout(vertexReflectionResult.value());
  if (!vertexInputLayout.has_value()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ShaderLoadFailed);
  }

  auto assetResult = atlantis::asset_system::loadStaticMeshAsset(artifactPath, metadataPath);
  if (assetResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::AssetLoadFailed);
  }
  const atlantis::asset_system::StaticMeshAssetData& meshData = assetResult.value();

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Image Regression Fixture (Asset)", .enableValidationLayers = true});
  if (deviceResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::DeviceCreationFailed);
  }

  MinimalCubeFixture fixture;
  fixture.device = std::move(deviceResult.value());

  auto meshResult = createMesh(*fixture.device, *vertexInputLayout, meshData.vertexBytes().data(),
                                meshData.vertexBytes().size(), meshData.indices().data(),
                                static_cast<std::uint32_t>(meshData.indices().size()));
  if (meshResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ResourceCreationFailed);
  }
  fixture.mesh = std::move(meshResult.value());

  auto cameraBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  if (cameraBufferResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ResourceCreationFailed);
  }
  fixture.cameraBuffer = std::move(cameraBufferResult.value());

  auto materialResult = createMaterial(
      *fixture.device, {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
                         .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
                         .vertexInputLayout = *vertexInputLayout,
                         // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3): every
                         // geometry Pipeline now renders into the fixed HDR
                         // intermediate, never this fixture's own real
                         // kFixtureColorFormat directly -- mirrors
                         // material_realization.cpp's own identical change.
                         .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
                         .depthFormat = DepthFormat::D32Sfloat,
                         .pushConstantSizeBytes = sizeof(float) * 16});
  if (materialResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ResourceCreationFailed);
  }
  fixture.material = std::move(materialResult.value());

  const Extent2D extent{kFixtureExtentPixels, kFixtureExtentPixels};

  auto depthTextureResult = fixture.device->createTexture({.extent = extent, .format = DepthFormat::D32Sfloat});
  if (depthTextureResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ResourceCreationFailed);
  }
  fixture.depthTexture = std::move(depthTextureResult.value());

  auto offscreenTargetResult =
      fixture.device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = extent, .format = kFixtureColorFormat});
  if (offscreenTargetResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ResourceCreationFailed);
  }
  fixture.offscreenTarget = std::move(offscreenTargetResult.value());

  const std::size_t readbackSizeBytes = static_cast<std::size_t>(kFixtureExtentPixels) * kFixtureExtentPixels * 4;
  auto readbackBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = readbackSizeBytes});
  if (readbackBufferResult.isErr()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(FixtureSetupError::ResourceCreationFailed);
  }
  fixture.readbackBuffer = std::move(readbackBufferResult.value());

  if (const auto outputTransformError = setUpOutputTransformResources(fixture, extent);
      outputTransformError.has_value()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(*outputTransformError);
  }

  if (const auto shadowCastError = setUpShadowCastResources(fixture); shadowCastError.has_value()) {
    return Result<MinimalCubeFixture, FixtureSetupError>::Err(*shadowCastError);
  }

  return Result<MinimalCubeFixture, FixtureSetupError>::Ok(std::move(fixture));
}

}  // namespace atlantis::image_regression
