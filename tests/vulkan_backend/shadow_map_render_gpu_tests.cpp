#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/shadow_map.h>
#include <atlantis/rhi/types.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// Plan 0027 Milestones 1-2 (ADR-0072 D-1/D-2/D-4): real-GPU coverage for
// the new ShadowMap resource -- creation at the Plan's own fixed
// resolution, and a full depth-only write / downstream color-sampled
// read round-trip, driven entirely through RenderGraph (one CommandList,
// one submit(), no direct vkCmd*-adjacent call outside a compiled pass
// callback), fixed per this Plan's own "no new ShadowMap readback API"
// requirement: the read side goes through the EXISTING
// copyRenderTargetToBuffer() path, never a ShadowMap-specific one.

namespace {

using atlantis::rhi::BufferPurpose;
using atlantis::rhi::CommandList;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Device;
using atlantis::rhi::Extent2D;
using atlantis::rhi::Format;
using atlantis::rhi::OffscreenTarget;
using atlantis::rhi::OffscreenTargetCreateParams;
using atlantis::rhi::RenderTarget;
using atlantis::rhi::ShadowMap;
using atlantis::rhi::ShadowMapCreateParams;
using atlantis::rhi::VertexInputLayout;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;

// Duplicated, not shared -- matches pipeline_depth_write_gpu_tests.cpp's/
// descriptor_pool_growth_gpu_tests.cpp's own established "each test file
// owns its small fixture" precedent exactly.
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

// Position-only -- matches shadow_cast.slang's own real VertexInput
// (ADR-0072 D-3). A minimal, test-local 12-byte stride, not the
// production 44-byte asset-mesh layout Runtime's own shadowCastVertexLayout()
// uses (Milestone 9) -- this test only proves the RHI write/read
// mechanics, independent of asset-mesh semantics.
struct ShadowVertex {
  float position[3];
};

[[nodiscard]] std::optional<VertexInputLayout> shadowCastTestVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(ShadowVertex, position)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(ShadowVertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// Position+uv -- matches textured_quad.slang's own real VertexInput.
struct TexturedVertex {
  float position[3];
  float uv[2];
};

[[nodiscard]] std::optional<VertexInputLayout> texturedQuadTestVertexLayout(
    const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(TexturedVertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(TexturedVertex, uv)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(TexturedVertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

constexpr std::array<float, 16> kIdentityMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr std::uint16_t kQuadIndices[6] = {0, 1, 2, 2, 1, 3};

}  // namespace

TEST_CASE("Device::createShadowMap() succeeds at the fixed 1024x1024 resolution (Plan 0027 P4)",
          "[vulkan_backend][gpu][shadow_map]") {
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Shadow Map GPU Test", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  constexpr Extent2D kShadowMapExtent{1024, 1024};
  auto shadowMapResult = device->createShadowMap({.extent = kShadowMapExtent, .format = DepthFormat::D32Sfloat});
  REQUIRE(shadowMapResult.isOk());
  std::unique_ptr<ShadowMap> shadowMap = std::move(shadowMapResult.value());

  CHECK(shadowMap->extent().width == kShadowMapExtent.width);
  CHECK(shadowMap->extent().height == kShadowMapExtent.height);
  CHECK(shadowMap->format() == DepthFormat::D32Sfloat);

  REQUIRE(device->waitIdle().isOk());
}

// Plan 0027 Milestone 2 (ADR-0072 D-2/D-4): a real depth-only Pipeline
// (shadow_cast.slang, hasColorAttachment = false) writes a known NDC
// depth (0.5) into a ShadowMap via the new beginRendering(ShadowMap&,
// float) overload, inside a RenderGraph "shadow" pass. A second,
// ordinary color Pipeline (textured_quad.slang, ordinary Rgba8Unorm
// OffscreenTarget) reads it back via the new bindTexture(uint32_t, const
// ShadowMap&, const Sampler&) overload, inside a "draw" pass that reads
// ShaderRead -- the identical write-then-read single-producer pattern
// already proven for HdrColorTarget. The final readback goes through the
// EXISTING copyRenderTargetToBuffer() path, fixed per this Plan's own
// "no new ShadowMap readback API" requirement -- textured_quad.slang's
// own fragmentMain() returns texturedSampler.Sample(uv) directly, so the
// sampled depth value lands in the final RGBA8 image's own red channel
// unmodified. One CommandList, one submit().
TEST_CASE("ShadowMap depth written by a depth-only Pipeline is read back correctly via bindTexture() and the "
          "existing color-readback path (Plan 0027 Milestone 2, ADR-0072 D-2/D-4)",
          "[vulkan_backend][gpu][shadow_map]") {
  constexpr Extent2D kShadowMapExtent{64, 64};
  constexpr Extent2D kColorExtent{64, 64};
  constexpr Format kColorFormat = Format::Rgba8Unorm;
  constexpr float kWrittenDepth = 0.5f;

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Shadow Map Round-Trip GPU Test", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  // --- shadow_cast Pipeline (depth-only write half) ---
  const auto shadowVertexSpirv = loadSpirvFile("shaders/shadow_cast.vert.spv");
  const auto shadowFragmentSpirv = loadSpirvFile("shaders/shadow_cast.frag.spv");
  REQUIRE(shadowVertexSpirv.has_value());
  REQUIRE(shadowFragmentSpirv.has_value());
  const auto shadowVertexReflection = loadReflectionMetadata("shaders/shadow_cast.vert.refl.json");
  REQUIRE(shadowVertexReflection.isOk());
  const auto shadowVertexLayout = shadowCastTestVertexLayout(shadowVertexReflection.value());
  REQUIRE(shadowVertexLayout.has_value());

  auto shadowPipelineResult = device->createPipeline(
      {.vertexShader = {.spirvWords = shadowVertexSpirv->data(), .wordCount = shadowVertexSpirv->size()},
       .fragmentShader = {.spirvWords = shadowFragmentSpirv->data(), .wordCount = shadowFragmentSpirv->size()},
       .vertexInputLayout = *shadowVertexLayout,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .sampledTextureBindingCount = 0,
       .hasDepthAttachment = true,
       .depthWriteEnabled = true,
       .hasColorAttachment = false});
  REQUIRE(shadowPipelineResult.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> shadowPipeline = std::move(shadowPipelineResult.value());

  auto lightSpaceBufferResult = device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  REQUIRE(lightSpaceBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> lightSpaceBuffer = std::move(lightSpaceBufferResult.value());
  {
    auto* data = static_cast<float*>(lightSpaceBuffer->mappedData());
    for (int i = 0; i < 16; ++i) data[i] = kIdentityMatrix[static_cast<std::size_t>(i)];
    for (int i = 0; i < 16; ++i) data[16 + i] = kIdentityMatrix[static_cast<std::size_t>(i)];
  }

  // A full-NDC quad at z = kWrittenDepth: identity view/projection/objectToWorld
  // means output.position == float4(position, 1.0) directly (matching
  // pipeline_depth_write_gpu_tests.cpp's own established convention), so
  // NDC depth == kWrittenDepth exactly, everywhere on the ShadowMap.
  constexpr std::array<ShadowVertex, 4> kShadowQuad = {
      ShadowVertex{{-1.0f, -1.0f, kWrittenDepth}}, ShadowVertex{{1.0f, -1.0f, kWrittenDepth}},
      ShadowVertex{{-1.0f, 1.0f, kWrittenDepth}}, ShadowVertex{{1.0f, 1.0f, kWrittenDepth}}};
  auto shadowVertexBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(kShadowQuad)});
  REQUIRE(shadowVertexBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> shadowVertexBuffer = std::move(shadowVertexBufferResult.value());
  std::memcpy(shadowVertexBuffer->mappedData(), kShadowQuad.data(), sizeof(kShadowQuad));

  auto shadowIndexBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Index, .sizeBytes = sizeof(kQuadIndices)});
  REQUIRE(shadowIndexBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> shadowIndexBuffer = std::move(shadowIndexBufferResult.value());
  std::memcpy(shadowIndexBuffer->mappedData(), kQuadIndices, sizeof(kQuadIndices));

  auto shadowMapResult =
      device->createShadowMap({.extent = kShadowMapExtent, .format = DepthFormat::D32Sfloat});
  REQUIRE(shadowMapResult.isOk());
  std::unique_ptr<ShadowMap> shadowMap = std::move(shadowMapResult.value());

  auto shadowSamplerResult = device->createSampler(
      {.filter = atlantis::rhi::Filter::Nearest, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  REQUIRE(shadowSamplerResult.isOk());
  std::unique_ptr<atlantis::rhi::Sampler> shadowSampler = std::move(shadowSamplerResult.value());

  // --- textured_quad Pipeline (color read half) ---
  const auto colorVertexSpirv = loadSpirvFile("shaders/textured_quad.vert.spv");
  const auto colorFragmentSpirv = loadSpirvFile("shaders/textured_quad.frag.spv");
  REQUIRE(colorVertexSpirv.has_value());
  REQUIRE(colorFragmentSpirv.has_value());
  const auto colorVertexReflection = loadReflectionMetadata("shaders/textured_quad.vert.refl.json");
  REQUIRE(colorVertexReflection.isOk());
  const auto colorVertexLayout = texturedQuadTestVertexLayout(colorVertexReflection.value());
  REQUIRE(colorVertexLayout.has_value());

  auto colorPipelineResult = device->createPipeline(
      {.vertexShader = {.spirvWords = colorVertexSpirv->data(), .wordCount = colorVertexSpirv->size()},
       .fragmentShader = {.spirvWords = colorFragmentSpirv->data(), .wordCount = colorFragmentSpirv->size()},
       .vertexInputLayout = *colorVertexLayout,
       .colorFormat = kColorFormat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .sampledTextureBindingCount = 1,
       .hasDepthAttachment = false});
  REQUIRE(colorPipelineResult.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> colorPipeline = std::move(colorPipelineResult.value());

  auto cameraBufferResult = device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  REQUIRE(cameraBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer = std::move(cameraBufferResult.value());
  {
    auto* data = static_cast<float*>(cameraBuffer->mappedData());
    for (int i = 0; i < 16; ++i) data[i] = kIdentityMatrix[static_cast<std::size_t>(i)];
    for (int i = 0; i < 16; ++i) data[16 + i] = kIdentityMatrix[static_cast<std::size_t>(i)];
  }

  // A full-NDC fullscreen quad, UV (0,0)-(1,1) -- covers every pixel of
  // the color target, sampling the (uniformly kWrittenDepth) ShadowMap
  // at every texel regardless of exact UV mapping.
  constexpr std::array<TexturedVertex, 4> kColorQuad = {
      TexturedVertex{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}}, TexturedVertex{{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
      TexturedVertex{{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}}, TexturedVertex{{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}}};
  auto colorVertexBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(kColorQuad)});
  REQUIRE(colorVertexBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> colorVertexBuffer = std::move(colorVertexBufferResult.value());
  std::memcpy(colorVertexBuffer->mappedData(), kColorQuad.data(), sizeof(kColorQuad));

  auto colorIndexBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Index, .sizeBytes = sizeof(kQuadIndices)});
  REQUIRE(colorIndexBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> colorIndexBuffer = std::move(colorIndexBufferResult.value());
  std::memcpy(colorIndexBuffer->mappedData(), kQuadIndices, sizeof(kQuadIndices));

  auto offscreenResult =
      device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kColorExtent, .format = kColorFormat});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());

  const std::size_t readbackSizeBytes = static_cast<std::size_t>(kColorExtent.width) * kColorExtent.height * 4;
  auto readbackBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = readbackSizeBytes});
  REQUIRE(readbackBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer = std::move(readbackBufferResult.value());

  auto acquireResult = offscreenTarget->acquireTarget();
  REQUIRE(acquireResult.isOk());
  std::unique_ptr<RenderTarget> target = std::move(acquireResult.value());

  auto commandListResult = device->createCommandList();
  REQUIRE(commandListResult.isOk());
  std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

  // "shadow" (writes shadowMap, depth-only) -> "draw" (reads shadowMap
  // ShaderRead, writes the color target) -- the real, two-pass shape
  // ADR-0072 D-4 establishes, in ONE RenderGraphBuilder so the
  // write-then-read dependency between them is derived automatically,
  // never asserted by this test.
  atlantis::render_graph::RenderGraphBuilder drawBuilder;
  const auto shadowResource = drawBuilder.declareResource("shadow-map");
  const auto colorResource = drawBuilder.declareResource("color");
  const auto shadowPass = drawBuilder.declarePass("shadow");
  drawBuilder.writes(shadowPass, shadowResource, atlantis::rhi::ResourceState::DepthAttachmentReadWrite);
  drawBuilder.setExecute(shadowPass, [&](CommandList& cmd) {
    cmd.bindPipeline(*shadowPipeline);
    cmd.bindVertexBuffer(*shadowVertexBuffer);
    cmd.bindIndexBuffer(*shadowIndexBuffer);
    cmd.bindUniformBuffer(*lightSpaceBuffer);
    cmd.pushConstant(kIdentityMatrix.data(), kIdentityMatrix.size() * sizeof(float));
    cmd.drawIndexed(6);
  });
  const auto drawPass = drawBuilder.declarePass("draw");
  drawBuilder.reads(drawPass, shadowResource, atlantis::rhi::ResourceState::ShaderRead);
  drawBuilder.writes(drawPass, colorResource, atlantis::rhi::ResourceState::ColorAttachmentOutput);
  drawBuilder.setExecute(drawPass, [&](CommandList& cmd) {
    cmd.bindPipeline(*colorPipeline);
    cmd.bindVertexBuffer(*colorVertexBuffer);
    cmd.bindIndexBuffer(*colorIndexBuffer);
    cmd.bindUniformBuffer(*cameraBuffer);
    cmd.pushConstant(kIdentityMatrix.data(), kIdentityMatrix.size() * sizeof(float));
    cmd.bindTexture(1, *shadowMap, *shadowSampler);
    cmd.drawIndexed(6);
  });
  auto drawCompileResult = drawBuilder.compile();
  REQUIRE(drawCompileResult.isOk());
  const std::vector<atlantis::render_graph::ResourceBinding> drawBindings{
      {.resource = drawCompileResult.value().resourceAt(0), .depthClear = 1.0f, .shadowMap = shadowMap.get()},
      {.resource = drawCompileResult.value().resourceAt(1),
       .target = target.get(),
       .colorClear = atlantis::rhi::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f},
       .finalState = atlantis::rhi::ResourceState::TransferSource},
  };
  atlantis::render_graph::execute(drawCompileResult.value(), drawBindings, *commandList);

  // Copy graph: a second, independent RenderGraphBuilder -- mirrors
  // pipeline_depth_write_gpu_tests.cpp's/headless_rendering_gpu_tests.cpp's
  // own copyBuilder exactly. This IS this Plan's own "existing color-
  // readback path" -- no ShadowMap-specific readback API is introduced
  // anywhere in this RHI.
  atlantis::render_graph::RenderGraphBuilder copyBuilder;
  const auto copyResource = copyBuilder.declareResource("color-copy");
  const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
  copyBuilder.writes(copyPass, copyResource, atlantis::rhi::ResourceState::TransferSource);
  copyBuilder.setExecute(copyPass, [&](CommandList& cmd) { cmd.copyRenderTargetToBuffer(*target, *readbackBuffer); });
  auto copyCompileResult = copyBuilder.compile();
  REQUIRE(copyCompileResult.isOk());
  const std::vector<atlantis::render_graph::ResourceBinding> copyBindings{
      {.resource = copyCompileResult.value().resourceAt(0),
       .target = target.get(),
       .incomingState = atlantis::rhi::ResourceState::TransferSource}};
  atlantis::render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  // One submit(), one waitIdle() -- never a second CommandList.
  auto submitResult = device->submit(std::move(commandList), *target);
  REQUIRE(submitResult.isOk());
  REQUIRE(device->waitIdle().isOk());

  const auto* readbackData = static_cast<const std::uint8_t*>(readbackBuffer->mappedData());
  REQUIRE(readbackData != nullptr);
  const std::size_t centerOffset =
      (static_cast<std::size_t>(kColorExtent.height / 2) * kColorExtent.width + kColorExtent.width / 2) * 4;
  const std::uint8_t redChannel = readbackData[centerOffset];

  // kWrittenDepth (0.5) Unorm-encoded is ~127-128; a generous +-15
  // tolerance absorbs sampler/quantization rounding without weakening
  // the actual round-trip claim (a wrong depth -- e.g. the ShadowMap
  // never actually written, or read back as 0.0/1.0 -- would miss by
  // roughly 127 levels, far outside this band).
  INFO("readback red channel = " << static_cast<int>(redChannel));
  CHECK(redChannel >= 112);
  CHECK(redChannel <= 143);

  target.reset();
  offscreenTarget.reset();
  REQUIRE(device->waitIdle().isOk());
}
