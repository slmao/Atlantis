#include "textured_quad_fixture.h"

#include <atlantis/asset_system/load_texture.h>
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

struct Vertex {
  float position[3];
  float uv[2];
};

[[nodiscard]] std::optional<VertexInputLayout> texturedQuadVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// Spec 0016/D11: two 1x2-triangle quads in clip space directly (the
// fixture's own camera/projection/objectToWorld matrices are all
// identity, below -- position.xyz passes through to NDC unchanged). No
// per-vertex color -- the quads' own visible color comes entirely from
// the sampled texture. v=0 at the top row, v=1 at the bottom row,
// matching the texture artifact's own "row 0 = first-decoded row (top),
// no vertical flip" contract (texture_artifact.h) so the checkerboard
// appears right-side-up.
constexpr Vertex kLeftQuadVertices[4] = {
    {{-0.9f, -0.5f, 0.0f}, {0.0f, 1.0f}},
    {{-0.1f, -0.5f, 0.0f}, {1.0f, 1.0f}},
    {{-0.1f, 0.5f, 0.0f}, {1.0f, 0.0f}},
    {{-0.9f, 0.5f, 0.0f}, {0.0f, 0.0f}},
};
constexpr Vertex kRightQuadVertices[4] = {
    {{0.1f, -0.5f, 0.0f}, {0.0f, 1.0f}},
    {{0.9f, -0.5f, 0.0f}, {1.0f, 1.0f}},
    {{0.9f, 0.5f, 0.0f}, {1.0f, 0.0f}},
    {{0.1f, 0.5f, 0.0f}, {0.0f, 0.0f}},
};
constexpr std::uint16_t kQuadIndices[6] = {0, 1, 2, 2, 3, 0};

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
                                                                              const char* srgbMetadataPath) {
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
  const auto vertexInputLayout = texturedQuadVertexLayout(vertexReflectionResult.value());
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
       .colorFormat = kTexturedQuadColorFormat,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .hasSampledTextureBinding = true},
      fixture.sampledTextureUnorm.get(), fixture.sampler.get());
  if (materialUnormResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.materialUnorm = std::move(materialUnormResult.value());

  auto materialSrgbResult = createMaterial(
      *fixture.device,
      {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
       .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
       .vertexInputLayout = *vertexInputLayout,
       .colorFormat = kTexturedQuadColorFormat,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .hasSampledTextureBinding = true},
      fixture.sampledTextureSrgb.get(), fixture.sampler.get());
  if (materialSrgbResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.materialSrgb = std::move(materialSrgbResult.value());

  auto meshLeftResult = createMesh(*fixture.device, *vertexInputLayout, kLeftQuadVertices, sizeof(kLeftQuadVertices),
                                    kQuadIndices, static_cast<std::uint32_t>(std::size(kQuadIndices)));
  if (meshLeftResult.isErr()) return ResultT::Err(TexturedQuadSetupError::ResourceCreationFailed);
  fixture.meshLeft = std::move(meshLeftResult.value());

  auto meshRightResult =
      createMesh(*fixture.device, *vertexInputLayout, kRightQuadVertices, sizeof(kRightQuadVertices), kQuadIndices,
                 static_cast<std::uint32_t>(std::size(kQuadIndices)));
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
                      rhi::ResourceState::TransferSource);

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
                      rhi::ResourceState::TransferSource);

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
