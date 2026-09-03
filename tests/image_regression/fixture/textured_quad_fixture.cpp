#include "textured_quad_fixture.h"

#include <atlantis/asset_system/load.h>
#include <atlantis/asset_system/load_texture.h>
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
#include <cstddef>
#include <cstring>
#include <fstream>
#include <type_traits>
#include <utility>
#include <vector>

// Duplicates minimal_cube_fixture.cpp's own exact call-ordering template
// (Plan 0016 Pre-draft verification) -- same shader-load/device-create/
// resource-create sequence shape, extended with two SampledTexture
// uploads and two Materials/Meshes instead of one.

namespace atlantis::image_regression {

namespace {

using atlantis::renderer::createMaterial;
using atlantis::renderer::createMesh;
using atlantis::renderer::DrawItem;
using atlantis::renderer::Renderer;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Extent2D;
using atlantis::rhi::Filter;
using atlantis::rhi::AddressMode;
using atlantis::rhi::OffscreenTargetCreateParams;
using atlantis::rhi::SampledTextureCreateParams;
using atlantis::rhi::SampledTextureFormat;
using atlantis::rhi::SamplerCreateParams;
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

// Plan 0017 Section D8/ADR-0058, extended by Plan 0020 Section P1/P4/
// ADR-0063: the real mesh artifact's own 44-byte vertex layout is
// position(3)+color(3)+UV0(2)+normal(3) -- this local Vertex exists
// only so offsetof() can compute the real byte offsets below (position
// at 0, UV0 at 24); the color and normal regions are deliberately left
// undeclared in the schema passed to toVertexInputLayout() below, since
// textured_quad.slang has neither a color nor a normal input and never
// reads either (Device::createPipeline() only ever constructs a
// VkVertexInputAttributeDescription for an attribute the caller's own
// schema names). strideBytes is the mesh's own real, loaded
// StaticMeshAssetData::vertexStrideBytes() -- never a hardcoded
// sizeof() -- so this layout stays correct even if the artifact's own
// stride ever changes independently of this file; the static_asserts
// below are still real, useful confirmation that this local struct's
// own shape genuinely matches the artifact's own real layout, even
// though this file's own runtime code path does not depend on
// sizeof(Vertex) for correctness the way the other five composition
// roots do.
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

[[nodiscard]] std::optional<VertexInputLayout> texturedQuadVertexLayout(const ReflectionMetadata& vertexMetadata,
                                                                         std::uint32_t strideBytes) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, strideBytes);
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

// Spec 0016/D4: a named pass-builder, not an anonymous lambda hiding its
// own inputs -- stagingBuffer/destination are explicit, required
// parameters. Declares SampledTexture as the one tracked resource, with
// only a single TransferDestination usage on this pass; the trailing
// TransferDestination -> ShaderRead transition is reached via the
// caller's own ResourceBinding::finalState (execution.cpp's own trailing-
// transition loop), not a second usage on this same pass -- see
// tests/vulkan_backend/texture_upload_gpu_tests.cpp's own identical,
// already-verified helper for why. Independently duplicated here rather
// than shared (Plan 0016/D4's own disclosed scope: this test target and
// tests/vulkan_backend/ share no existing private-header dependency).
void buildTextureUploadPass(atlantis::render_graph::RenderGraphBuilder& builder, atlantis::rhi::Buffer& stagingBuffer,
                             atlantis::rhi::SampledTexture& destination) {
  const auto resource = builder.declareResource("texture-upload");
  const auto pass = builder.declarePass("TextureUpload");
  builder.writes(pass, resource, atlantis::rhi::ResourceState::TransferDestination);
  builder.setExecute(pass, [&stagingBuffer, &destination](atlantis::rhi::CommandList& cmd) {
    cmd.copyBufferToTexture(stagingBuffer, destination);
  });
}

}  // namespace

Result<TexturedQuadFixture, TexturedQuadSetupError> setUpTexturedQuadFixture(const char* unormArtifactPath,
                                                                              const char* unormMetadataPath,
                                                                              const char* srgbArtifactPath,
                                                                              const char* srgbMetadataPath,
                                                                              const char* leftMeshArtifactPath,
                                                                              const char* leftMeshMetadataPath,
                                                                              const char* rightMeshArtifactPath,
                                                                              const char* rightMeshMetadataPath) {
  using ResultT = Result<TexturedQuadFixture, TexturedQuadSetupError>;

  const auto vertexSpirv = loadSpirvFile("shaders/textured_quad.vert.spv");
  const auto fragmentSpirv = loadSpirvFile("shaders/textured_quad.frag.spv");
  if (!vertexSpirv.has_value() || !fragmentSpirv.has_value()) {
    return ResultT::Err(TexturedQuadSetupError::ShaderLoadFailed);
  }

  auto vertexReflectionResult = loadReflectionMetadata("shaders/textured_quad.vert.refl.json");
  if (vertexReflectionResult.isErr()) {
    return ResultT::Err(TexturedQuadSetupError::ShaderLoadFailed);
  }

  // Plan 0017 Milestone 3: both quads are now Asset-System-sourced --
  // no hand-authored vertex/UV fallback of any kind. Loaded before the
  // vertex layout below so the layout's own strideBytes comes from the
  // real, loaded StaticMeshAssetData, never a hardcoded sizeof().
  auto leftMeshResult = atlantis::asset_system::loadStaticMeshAsset(leftMeshArtifactPath, leftMeshMetadataPath);
  if (leftMeshResult.isErr()) return ResultT::Err(TexturedQuadSetupError::AssetLoadFailed);
  auto rightMeshResult = atlantis::asset_system::loadStaticMeshAsset(rightMeshArtifactPath, rightMeshMetadataPath);
  if (rightMeshResult.isErr()) return ResultT::Err(TexturedQuadSetupError::AssetLoadFailed);
  const atlantis::asset_system::StaticMeshAssetData& leftMeshData = leftMeshResult.value();
  const atlantis::asset_system::StaticMeshAssetData& rightMeshData = rightMeshResult.value();

  const auto vertexInputLayout =
      texturedQuadVertexLayout(vertexReflectionResult.value(), leftMeshData.vertexStrideBytes());
  if (!vertexInputLayout.has_value()) {
    return ResultT::Err(TexturedQuadSetupError::ShaderLoadFailed);
  }

  auto unormAssetResult = atlantis::asset_system::loadTextureAsset(unormArtifactPath, unormMetadataPath);
  if (unormAssetResult.isErr()) return ResultT::Err(TexturedQuadSetupError::AssetLoadFailed);
  auto srgbAssetResult = atlantis::asset_system::loadTextureAsset(srgbArtifactPath, srgbMetadataPath);
  if (srgbAssetResult.isErr()) return ResultT::Err(TexturedQuadSetupError::AssetLoadFailed);
  const atlantis::asset_system::TextureAssetData& unormData = unormAssetResult.value();
  const atlantis::asset_system::TextureAssetData& srgbData = srgbAssetResult.value();

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Image Regression Fixture (Textured)", .enableValidationLayers = true});
  if (deviceResult.isErr()) return ResultT::Err(TexturedQuadSetupError::DeviceCreationFailed);

  TexturedQuadFixture fixture;
  fixture.device = std::move(deviceResult.value());

  // Composition root: this fixture (never Asset System) translates
  // TextureColorSpace -> SampledTextureFormat (Spec 0016/D8's own
  // module-boundary note).
  auto unormTextureResult = fixture.device->createSampledTexture(
      SampledTextureCreateParams{.extent = Extent2D{unormData.width, unormData.height},
                                  .format = SampledTextureFormat::Rgba8Unorm});
  if (unormTextureResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.sampledTextureUnorm = std::move(unormTextureResult.value());

  auto srgbTextureResult = fixture.device->createSampledTexture(
      SampledTextureCreateParams{.extent = Extent2D{srgbData.width, srgbData.height},
                                  .format = SampledTextureFormat::Rgba8Srgb});
  if (srgbTextureResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.sampledTextureSrgb = std::move(srgbTextureResult.value());

  fixture.unormPixelBytes = unormData.pixelBytes;
  fixture.srgbPixelBytes = srgbData.pixelBytes;

  // Filter::Nearest keeps the checkerboard's own block edges crisp in
  // the captured golden, avoiding filtering-interpolation ambiguity at
  // block boundaries (Spec 0016/D11).
  auto samplerResult = fixture.device->createSampler(
      SamplerCreateParams{.filter = Filter::Nearest, .addressMode = AddressMode::ClampToEdge});
  if (samplerResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.sampler = std::move(samplerResult.value());

  auto cameraBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  if (cameraBufferResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.cameraBuffer = std::move(cameraBufferResult.value());

  auto materialUnormResult = createMaterial(
      *fixture.device,
      {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
       .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
       .vertexInputLayout = *vertexInputLayout,
       // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3): every geometry
       // Pipeline now renders into the fixed HDR intermediate, never
       // this fixture's own real kTexturedQuadColorFormat directly.
       .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .sampledTextureBindingCount = 1},
      fixture.sampledTextureUnorm.get(), fixture.sampler.get());
  if (materialUnormResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.materialUnorm = std::move(materialUnormResult.value());

  auto materialSrgbResult = createMaterial(
      *fixture.device,
      {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
       .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
       .vertexInputLayout = *vertexInputLayout,
       // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3): every geometry
       // Pipeline now renders into the fixed HDR intermediate, never
       // this fixture's own real kTexturedQuadColorFormat directly.
       .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .sampledTextureBindingCount = 1},
      fixture.sampledTextureSrgb.get(), fixture.sampler.get());
  if (materialSrgbResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.materialSrgb = std::move(materialSrgbResult.value());

  auto meshLeftResult =
      createMesh(*fixture.device, *vertexInputLayout, leftMeshData.vertexBytes().data(),
                 leftMeshData.vertexBytes().size(), leftMeshData.indices().data(),
                 static_cast<std::uint32_t>(leftMeshData.indices().size()));
  if (meshLeftResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.meshLeft = std::move(meshLeftResult.value());

  auto meshRightResult =
      createMesh(*fixture.device, *vertexInputLayout, rightMeshData.vertexBytes().data(),
                 rightMeshData.vertexBytes().size(), rightMeshData.indices().data(),
                 static_cast<std::uint32_t>(rightMeshData.indices().size()));
  if (meshRightResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.meshRight = std::move(meshRightResult.value());

  const Extent2D extent{kTexturedQuadExtentPixels, kTexturedQuadExtentPixels};

  auto depthTextureResult = fixture.device->createTexture({.extent = extent, .format = DepthFormat::D32Sfloat});
  if (depthTextureResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.depthTexture = std::move(depthTextureResult.value());

  auto offscreenTargetResult = fixture.device->createOffscreenTarget(
      OffscreenTargetCreateParams{.extent = extent, .format = kTexturedQuadColorFormat});
  if (offscreenTargetResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.offscreenTarget = std::move(offscreenTargetResult.value());

  const std::size_t readbackSizeBytes = static_cast<std::size_t>(kTexturedQuadExtentPixels) *
                                         kTexturedQuadExtentPixels * 4;
  auto readbackBufferResult =
      fixture.device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = readbackSizeBytes});
  if (readbackBufferResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.readbackBuffer = std::move(readbackBufferResult.value());

  // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3/D-6): this fixture's own
  // HDR intermediate, fullscreen-triangle geometry/sampler, and output-
  // transform Pipeline.
  const auto outputTransformVertexSpirv = loadSpirvFile("shaders/output_transform_unorm.vert.spv");
  const auto outputTransformFragmentSpirv = loadSpirvFile("shaders/output_transform_unorm.frag.spv");
  if (!outputTransformVertexSpirv.has_value() || !outputTransformFragmentSpirv.has_value()) {
    return ResultT::Err(TexturedQuadSetupError::ShaderLoadFailed);
  }
  auto outputTransformVertexReflectionResult = loadReflectionMetadata("shaders/output_transform_unorm.vert.refl.json");
  if (outputTransformVertexReflectionResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ShaderLoadFailed);
  const auto outputTransformVertexInputLayout =
      outputTransformVertexLayout(outputTransformVertexReflectionResult.value());
  if (!outputTransformVertexInputLayout.has_value()) return ResultT::Err(TexturedQuadSetupError::ShaderLoadFailed);

  auto hdrColorTargetResult = fixture.device->createHdrColorTarget({.extent = extent});
  if (hdrColorTargetResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.hdrColorTarget = std::move(hdrColorTargetResult.value());

  const float fullscreenTriangleVertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  auto fullscreenTriangleVertexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  if (fullscreenTriangleVertexBufferResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.fullscreenTriangleVertexBuffer = std::move(fullscreenTriangleVertexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleVertexBuffer->mappedData(), fullscreenTriangleVertices,
              sizeof(fullscreenTriangleVertices));

  const std::uint16_t fullscreenTriangleIndices[3] = {0, 1, 2};
  auto fullscreenTriangleIndexBufferResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Index, .sizeBytes = sizeof(fullscreenTriangleIndices)});
  if (fullscreenTriangleIndexBufferResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.fullscreenTriangleIndexBuffer = std::move(fullscreenTriangleIndexBufferResult.value());
  std::memcpy(fixture.fullscreenTriangleIndexBuffer->mappedData(), fullscreenTriangleIndices,
              sizeof(fullscreenTriangleIndices));

  auto outputTransformSamplerResult =
      fixture.device->createSampler(SamplerCreateParams{.filter = Filter::Linear, .addressMode = AddressMode::ClampToEdge});
  if (outputTransformSamplerResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.outputTransformSampler = std::move(outputTransformSamplerResult.value());

  auto outputTransformPipelineResult = fixture.device->createPipeline(
      {.vertexShader = {.spirvWords = outputTransformVertexSpirv->data(),
                         .wordCount = outputTransformVertexSpirv->size()},
       .fragmentShader = {.spirvWords = outputTransformFragmentSpirv->data(),
                           .wordCount = outputTransformFragmentSpirv->size()},
       .vertexInputLayout = *outputTransformVertexInputLayout,
       .colorFormat = kTexturedQuadColorFormat,
       .sampledTextureBindingCount = 1,
       .hasCameraUniformBinding = false,
       .hasDepthAttachment = false});
  if (outputTransformPipelineResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.outputTransformPipeline = std::move(outputTransformPipelineResult.value());

  return ResultT::Ok(std::move(fixture));
}

Result<PixelBuffer, TexturedQuadRenderError> renderTexturedQuadFrame(TexturedQuadFixture& fixture) {
  namespace rhi = atlantis::rhi;
  namespace render_graph = atlantis::render_graph;
  using ResultT = Result<PixelBuffer, TexturedQuadRenderError>;

  // Spec 0016/D5a case 1: every step below, up to and including
  // Device::submit(), is safe to abandon via a plain early return --
  // ordinary RAII on target/stagingBufferUnorm/stagingBufferSrgb
  // (whichever already exist at that point) is sufficient, since no GPU
  // work referencing them has been submitted yet.
  auto acquireResult = fixture.offscreenTarget->acquireTarget();
  if (acquireResult.isErr()) return ResultT::Err(TexturedQuadRenderError::AcquireFailed);
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

  const auto unormExtent = fixture.sampledTextureUnorm->extent();
  const std::size_t unormStagingBytes = static_cast<std::size_t>(unormExtent.width) * unormExtent.height * 4;
  auto stagingUnormResult =
      fixture.device->createBuffer({.purpose = rhi::BufferPurpose::Staging, .sizeBytes = unormStagingBytes});
  if (stagingUnormResult.isErr()) return ResultT::Err(TexturedQuadRenderError::StagingBufferCreationFailed);
  std::unique_ptr<rhi::Buffer> stagingBufferUnorm = std::move(stagingUnormResult.value());
  std::memcpy(stagingBufferUnorm->mappedData(), fixture.unormPixelBytes.data(), unormStagingBytes);

  const auto srgbExtent = fixture.sampledTextureSrgb->extent();
  const std::size_t srgbStagingBytes = static_cast<std::size_t>(srgbExtent.width) * srgbExtent.height * 4;
  auto stagingSrgbResult =
      fixture.device->createBuffer({.purpose = rhi::BufferPurpose::Staging, .sizeBytes = srgbStagingBytes});
  if (stagingSrgbResult.isErr()) return ResultT::Err(TexturedQuadRenderError::StagingBufferCreationFailed);
  std::unique_ptr<rhi::Buffer> stagingBufferSrgb = std::move(stagingSrgbResult.value());
  std::memcpy(stagingBufferSrgb->mappedData(), fixture.srgbPixelBytes.data(), srgbStagingBytes);

  const Mat4 identity = identityMatrix();
  auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = identity[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = identity[i];

  auto commandListResult = fixture.device->createCommandList();
  if (commandListResult.isErr()) return ResultT::Err(TexturedQuadRenderError::CommandListCreationFailed);
  std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

  // Step 1-2: two independent texture uploads, one RenderGraphBuilder,
  // recorded into commandList first.
  render_graph::RenderGraphBuilder uploadBuilder;
  buildTextureUploadPass(uploadBuilder, *stagingBufferUnorm, *fixture.sampledTextureUnorm);
  buildTextureUploadPass(uploadBuilder, *stagingBufferSrgb, *fixture.sampledTextureSrgb);
  auto uploadCompileResult = uploadBuilder.compile();
  if (uploadCompileResult.isErr()) return ResultT::Err(TexturedQuadRenderError::CommandListCreationFailed);
  const std::vector<render_graph::ResourceBinding> uploadBindings{
      {.resource = uploadCompileResult.value().resourceAt(0),
       .sampledTexture = fixture.sampledTextureUnorm.get(),
       .finalState = rhi::ResourceState::ShaderRead},
      {.resource = uploadCompileResult.value().resourceAt(1),
       .sampledTexture = fixture.sampledTextureSrgb.get(),
       .finalState = rhi::ResourceState::ShaderRead},
  };
  render_graph::execute(uploadCompileResult.value(), uploadBindings, *commandList);

  // Step 3: Renderer::drawFrame()'s own draw graph -- two DrawItems, one
  // per quad/Material/SampledTexture, both sampling their own now-
  // ShaderRead texture via bindTexture() (Spec 0016/D3, inside
  // Renderer's own pass callback). Recorded into the SAME commandList,
  // immediately after the upload above.
  DrawItem leftItem;
  leftItem.mesh = &*fixture.meshLeft;
  leftItem.material = &*fixture.materialUnorm;
  leftItem.objectToWorld = identity;
  DrawItem rightItem;
  rightItem.mesh = &*fixture.meshRight;
  rightItem.material = &*fixture.materialSrgb;
  rightItem.objectToWorld = identity;
  const std::array<DrawItem, 2> drawItems{leftItem, rightItem};

  Renderer renderer;
  renderer.drawFrame(*commandList, *target, *fixture.depthTexture, *fixture.cameraBuffer, drawItems,
                      rhi::ResourceState::TransferSource, *fixture.hdrColorTarget,
                      *fixture.fullscreenTriangleVertexBuffer, *fixture.fullscreenTriangleIndexBuffer,
                      *fixture.outputTransformPipeline, *fixture.outputTransformSampler);

  // Step 4: readback graph, recorded into the SAME commandList,
  // immediately after the draw above -- matches minimal_cube_fixture.cpp's
  // own identical copyBuilder pattern exactly.
  render_graph::RenderGraphBuilder copyBuilder;
  const auto copyResource = copyBuilder.declareResource("color-copy");
  const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
  copyBuilder.writes(copyPass, copyResource, rhi::ResourceState::TransferSource);
  copyBuilder.setExecute(copyPass, [&target, &fixture](rhi::CommandList& cmd) {
    cmd.copyRenderTargetToBuffer(*target, *fixture.readbackBuffer);
  });
  auto copyCompileResult = copyBuilder.compile();
  if (copyCompileResult.isErr()) return ResultT::Err(TexturedQuadRenderError::CommandListCreationFailed);
  const std::vector<render_graph::ResourceBinding> copyBindings{{.resource = copyCompileResult.value().resourceAt(0),
                                                                   .target = target.get(),
                                                                   .incomingState = rhi::ResourceState::TransferSource}};
  render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  // Step 5: exactly one submit(), covering steps 1-4 together.
  auto submitResult = fixture.device->submit(std::move(commandList), *target);
  if (submitResult.isErr()) {
    // Spec 0016/D5a case 2: also safe to destroy immediately,
    // unconditionally -- a failed vkQueueSubmit means the GPU driver
    // never accepted this CommandList's own recorded work at all.
    return ResultT::Err(TexturedQuadRenderError::SubmitFailed);
  }

  // Step 6: exactly one waitIdle().
  auto waitResult = fixture.device->waitIdle();
  if (waitResult.isErr()) {
    // Spec 0016/D5a case 3: treated as fatal, not a recoverable state
    // this function attempts to gracefully unwind -- matching every
    // existing headless GPU test's own REQUIRE(waitIdle().isOk())
    // fail-fast pattern. Ordinary RAII still runs regardless on this
    // function's own return (stagingBufferUnorm/stagingBufferSrgb/
    // readbackBuffer's destructors), exactly the same behavior every
    // other VulkanBuffer already has under DeviceLost, with no special-
    // casing introduced here.
    return ResultT::Err(TexturedQuadRenderError::WaitIdleFailed);
  }

  // Step 7: CPU reads the readback Buffer -- only now, after waitIdle()
  // returned Ok, are the staging Buffers (via this function's own return
  // below) and the readback Buffer (via the fixture's own next call, or
  // fixture teardown) destroyed.
  PixelBuffer result;
  result.width = kTexturedQuadExtentPixels;
  result.height = kTexturedQuadExtentPixels;
  const std::size_t byteCount = static_cast<std::size_t>(kTexturedQuadExtentPixels) * kTexturedQuadExtentPixels * 4;
  const auto* readbackData = static_cast<const std::uint8_t*>(fixture.readbackBuffer->mappedData());
  result.rgba8.assign(readbackData, readbackData + byteCount);

  target.reset();  // Ends this cycle's borrow (RAII, ADR-0038) -- no release()/consume() call.

  return ResultT::Ok(std::move(result));
}

Result<PixelBuffer, TexturedQuadRenderError> renderTexturedQuadBaselineFrame(TexturedQuadFixture& fixture) {
  namespace rhi = atlantis::rhi;
  namespace render_graph = atlantis::render_graph;
  using ResultT = Result<PixelBuffer, TexturedQuadRenderError>;

  auto acquireResult = fixture.offscreenTarget->acquireTarget();
  if (acquireResult.isErr()) return ResultT::Err(TexturedQuadRenderError::AcquireFailed);
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

  const Mat4 identity = identityMatrix();
  auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = identity[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = identity[i];

  auto commandListResult = fixture.device->createCommandList();
  if (commandListResult.isErr()) return ResultT::Err(TexturedQuadRenderError::CommandListCreationFailed);
  std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

  // Spec 0016/D11's own "Proof the RenderTarget is genuinely used" test
  // requirement: steps 4-6 only -- an empty DrawItem span still clears
  // the color target via Renderer::drawFrame()'s own existing, fixed
  // background clear color (no new API), producing a freshly-cleared,
  // undrawn target's own known baseline.
  const std::array<DrawItem, 0> noDrawItems{};
  Renderer renderer;
  renderer.drawFrame(*commandList, *target, *fixture.depthTexture, *fixture.cameraBuffer, noDrawItems,
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
  if (copyCompileResult.isErr()) return ResultT::Err(TexturedQuadRenderError::CommandListCreationFailed);
  const std::vector<render_graph::ResourceBinding> copyBindings{{.resource = copyCompileResult.value().resourceAt(0),
                                                                   .target = target.get(),
                                                                   .incomingState = rhi::ResourceState::TransferSource}};
  render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  auto submitResult = fixture.device->submit(std::move(commandList), *target);
  if (submitResult.isErr()) return ResultT::Err(TexturedQuadRenderError::SubmitFailed);

  auto waitResult = fixture.device->waitIdle();
  if (waitResult.isErr()) return ResultT::Err(TexturedQuadRenderError::WaitIdleFailed);

  PixelBuffer result;
  result.width = kTexturedQuadExtentPixels;
  result.height = kTexturedQuadExtentPixels;
  const std::size_t byteCount = static_cast<std::size_t>(kTexturedQuadExtentPixels) * kTexturedQuadExtentPixels * 4;
  const auto* readbackData = static_cast<const std::uint8_t*>(fixture.readbackBuffer->mappedData());
  result.rgba8.assign(readbackData, readbackData + byteCount);

  target.reset();

  return ResultT::Ok(std::move(result));
}

}  // namespace atlantis::image_regression
