#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/texture.h>
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

// Plan 0026 Milestone 1 (ADR-0071 P4): a real, discriminating GPU test
// for PipelineCreateParams::depthWriteEnabled -- proves both halves of
// its contract (depth writes disabled, depth TEST still enabled), not
// merely that a Pipeline with the field set still compiles/draws.
// Reuses the existing, already-compiled minimal_mesh shader pair
// unchanged (no new shader) with an identity camera/objectToWorld, the
// same convention descriptor_pool_growth_gpu_tests.cpp's own
// kIdentityMatrix and minimal_renderer_gpu_tests.cpp's own "view =
// identity, projection = identity" comment already establish -- so
// output.position == float4(input.position, 1.0) directly: vertex z is
// the NDC depth with no other transform to account for.
//
// One CommandList, one submit(), mirroring
// headless_rendering_gpu_tests.cpp's own established headless
// draw-then-copy sequence exactly: a "draw" RenderGraphBuilder (three
// discriminating draws only, finalState = TransferSource) followed by a
// separate "copy" RenderGraphBuilder (copyRenderTargetToBuffer() only
// inside its own pass callback, incomingState = TransferSource), both
// executed against the same CommandList -- never a copy call inside the
// draw pass, never a copy recorded outside RenderGraph, never a second
// CommandList or submit().

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
using atlantis::rhi::VertexInputLayout;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;

// Duplicated, not shared -- matches descriptor_pool_growth_gpu_tests.cpp's/
// headless_rendering_gpu_tests.cpp's own established "each test file owns
// its small fixture" precedent exactly.
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
  float color[3];
};

[[nodiscard]] std::optional<VertexInputLayout> minimalMeshVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, color)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

constexpr std::array<float, 16> kIdentityMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// A quad's own 4 corners, sharing one index pattern -- two triangles,
// {0,1,2} (lower-left) and {2,1,3} (upper-right), cover the full
// [minX,maxX] x [minY,maxY] rectangle. Cull mode is VK_CULL_MODE_NONE
// (vulkan_device.cpp) so winding never matters here.
constexpr std::uint16_t kQuadIndices[6] = {0, 1, 2, 2, 1, 3};

[[nodiscard]] std::array<Vertex, 4> makeQuad(float minX, float maxX, float z, float r, float g, float b) {
  return {Vertex{{minX, -1.0f, z}, {r, g, b}}, Vertex{{maxX, -1.0f, z}, {r, g, b}}, Vertex{{minX, 1.0f, z}, {r, g, b}},
          Vertex{{maxX, 1.0f, z}, {r, g, b}}};
}

struct QuadDraw {
  std::unique_ptr<atlantis::rhi::Buffer> vertexBuffer;
  std::unique_ptr<atlantis::rhi::Buffer> indexBuffer;
};

[[nodiscard]] std::optional<QuadDraw> createQuadDraw(Device& device, const std::array<Vertex, 4>& vertices) {
  auto vertexBufferResult = device.createBuffer({.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(vertices)});
  if (vertexBufferResult.isErr()) return std::nullopt;
  QuadDraw draw;
  draw.vertexBuffer = std::move(vertexBufferResult.value());
  std::memcpy(draw.vertexBuffer->mappedData(), vertices.data(), sizeof(vertices));

  auto indexBufferResult =
      device.createBuffer({.purpose = BufferPurpose::Index, .sizeBytes = sizeof(kQuadIndices)});
  if (indexBufferResult.isErr()) return std::nullopt;
  draw.indexBuffer = std::move(indexBufferResult.value());
  std::memcpy(draw.indexBuffer->mappedData(), kQuadIndices, sizeof(kQuadIndices));
  return draw;
}

}  // namespace

TEST_CASE("PipelineCreateParams::depthWriteEnabled disables depth writes while keeping the depth test enabled "
          "(Plan 0026 Milestone 1, ADR-0071 P4)",
          "[vulkan_backend][gpu][pipeline_depth_write]") {
  constexpr Extent2D kExtent{64, 64};
  constexpr Format kColorFormat = Format::Rgba8Unorm;

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Pipeline Depth Write GPU Test", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
  const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
  REQUIRE(vertexSpirv.has_value());
  REQUIRE(fragmentSpirv.has_value());
  const auto vertexReflection = loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
  REQUIRE(vertexReflection.isOk());
  const auto vertexInputLayout = minimalMeshVertexLayout(vertexReflection.value());
  REQUIRE(vertexInputLayout.has_value());

  auto pipelineAResult = device->createPipeline(
      {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
       .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
       .vertexInputLayout = *vertexInputLayout,
       .colorFormat = kColorFormat,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .hasDepthAttachment = true,
       .depthWriteEnabled = false});
  REQUIRE(pipelineAResult.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> pipelineA = std::move(pipelineAResult.value());

  auto pipelineBResult = device->createPipeline(
      {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
       .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
       .vertexInputLayout = *vertexInputLayout,
       .colorFormat = kColorFormat,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .hasDepthAttachment = true});  // depthWriteEnabled left at its own default (true)
  REQUIRE(pipelineBResult.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> pipelineB = std::move(pipelineBResult.value());

  // A second write-off Pipeline instance, identical PipelineCreateParams
  // to pipelineA, for the blue draw. This exists ONLY to sidestep a real,
  // pre-existing, independent constraint of
  // VulkanCommandList::bindUniformBuffer() -- it is a TEST-side
  // workaround, not a fix, and this Plan 0026 PR does not fix the
  // underlying issue (out of scope; disclosed here so it is not mistaken
  // for "resolved").
  //
  // The underlying issue: bindUniformBuffer()'s own redundant-write skip
  // (vulkan_command_list.cpp:328, `if (boundDescriptorSet_ !=
  // lastUpdatedDescriptorSet_ || vkBuffer != lastUpdatedUniformBuffer_)`)
  // only covers immediately-consecutive rebinds of the SAME descriptor
  // set. Revisiting pipelineA's own set for a third draw, after
  // pipelineB's own distinct set was bound in between, would call
  // vkUpdateDescriptorSets() on a set already bound via
  // vkCmdBindDescriptorSets() earlier in this same recording -- a real
  // Validation Layers error (no UPDATE_AFTER_BIND pool), reproduced and
  // confirmed during this Plan's own implementation before this
  // workaround was added. This is a general constraint of
  // bindUniformBuffer() itself (any A-B-A Pipeline sequence sharing one
  // CommandList would hit it, not something specific to depthWriteEnabled
  // or this test), pre-existing and independent of Plan 0026/ADR-0071.
  //
  // A distinct Pipeline/descriptor-set per draw sidesteps it without
  // changing what this TEST_CASE exercises, since pipelineA2 shares
  // pipelineA's own exact depthWriteEnabled = false configuration.
  auto pipelineA2Result = device->createPipeline(
      {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
       .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
       .vertexInputLayout = *vertexInputLayout,
       .colorFormat = kColorFormat,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .hasDepthAttachment = true,
       .depthWriteEnabled = false});
  REQUIRE(pipelineA2Result.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> pipelineA2 = std::move(pipelineA2Result.value());

  auto depthTextureResult = device->createTexture({.extent = kExtent, .format = DepthFormat::D32Sfloat});
  REQUIRE(depthTextureResult.isOk());
  std::unique_ptr<atlantis::rhi::Texture> depthTexture = std::move(depthTextureResult.value());

  auto cameraBufferResult = device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  REQUIRE(cameraBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer = std::move(cameraBufferResult.value());
  {
    auto* cameraData = static_cast<float*>(cameraBuffer->mappedData());
    for (int i = 0; i < 16; ++i) cameraData[i] = kIdentityMatrix[static_cast<std::size_t>(i)];
    for (int i = 0; i < 16; ++i) cameraData[16 + i] = kIdentityMatrix[static_cast<std::size_t>(i)];
  }

  auto offscreenResult =
      device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kExtent, .format = kColorFormat});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());

  const std::size_t readbackSizeBytes = static_cast<std::size_t>(kExtent.width) * kExtent.height * 4;
  auto readbackBufferResult = device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = readbackSizeBytes});
  REQUIRE(readbackBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer = std::move(readbackBufferResult.value());

  // Red-only region: x in [-1.0, -0.2). Overlap region: x in [-0.2, 0.2].
  // Red covers [-1.0, 0.2]; green/blue both cover [-0.2, 1.0].
  auto redQuad = createQuadDraw(*device, makeQuad(-1.0f, 0.2f, 0.3f, 1.0f, 0.0f, 0.0f));
  auto greenQuad = createQuadDraw(*device, makeQuad(-0.2f, 1.0f, 0.6f, 0.0f, 1.0f, 0.0f));
  auto blueQuad = createQuadDraw(*device, makeQuad(-0.2f, 1.0f, 0.9f, 0.0f, 0.0f, 1.0f));
  REQUIRE(redQuad.has_value());
  REQUIRE(greenQuad.has_value());
  REQUIRE(blueQuad.has_value());

  auto acquireResult = offscreenTarget->acquireTarget();
  REQUIRE(acquireResult.isOk());
  std::unique_ptr<RenderTarget> target = std::move(acquireResult.value());

  auto commandListResult = device->createCommandList();
  REQUIRE(commandListResult.isOk());
  std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

  // Draw graph: three discriminating draws only -- no copy call anywhere
  // in this pass's own execute callback.
  atlantis::render_graph::RenderGraphBuilder drawBuilder;
  const auto colorResource = drawBuilder.declareResource("color");
  const auto depthResource = drawBuilder.declareResource("depth");
  const auto drawPass = drawBuilder.declarePass("draw");
  drawBuilder.writes(drawPass, colorResource, atlantis::rhi::ResourceState::ColorAttachmentOutput);
  drawBuilder.writes(drawPass, depthResource, atlantis::rhi::ResourceState::DepthAttachmentReadWrite);
  drawBuilder.setExecute(drawPass, [&](CommandList& cmd) {
    cmd.bindPipeline(*pipelineA);
    cmd.bindVertexBuffer(*redQuad->vertexBuffer);
    cmd.bindIndexBuffer(*redQuad->indexBuffer);
    cmd.bindUniformBuffer(*cameraBuffer);
    cmd.pushConstant(kIdentityMatrix.data(), kIdentityMatrix.size() * sizeof(float));
    cmd.drawIndexed(6);

    cmd.bindPipeline(*pipelineB);
    cmd.bindVertexBuffer(*greenQuad->vertexBuffer);
    cmd.bindIndexBuffer(*greenQuad->indexBuffer);
    cmd.bindUniformBuffer(*cameraBuffer);
    cmd.pushConstant(kIdentityMatrix.data(), kIdentityMatrix.size() * sizeof(float));
    cmd.drawIndexed(6);

    cmd.bindPipeline(*pipelineA2);
    cmd.bindVertexBuffer(*blueQuad->vertexBuffer);
    cmd.bindIndexBuffer(*blueQuad->indexBuffer);
    cmd.bindUniformBuffer(*cameraBuffer);
    cmd.pushConstant(kIdentityMatrix.data(), kIdentityMatrix.size() * sizeof(float));
    cmd.drawIndexed(6);
  });
  auto drawCompileResult = drawBuilder.compile();
  REQUIRE(drawCompileResult.isOk());
  const std::vector<atlantis::render_graph::ResourceBinding> drawBindings{
      {.resource = drawCompileResult.value().resourceAt(0),
       .target = target.get(),
       .colorClear = atlantis::rhi::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f},
       .finalState = atlantis::rhi::ResourceState::TransferSource},
      {.resource = drawCompileResult.value().resourceAt(1), .depthTexture = depthTexture.get(), .depthClear = 1.0f},
  };
  atlantis::render_graph::execute(drawCompileResult.value(), drawBindings, *commandList);

  // Copy graph: a second, independent RenderGraphBuilder -- mirrors
  // headless_rendering_gpu_tests.cpp's own copyBuilder exactly.
  // copyRenderTargetToBuffer() is called only from inside this pass's
  // own callback, never from the draw pass above.
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

  const auto pixelAt = [&](std::uint32_t x, std::uint32_t y) {
    const std::size_t offset = (static_cast<std::size_t>(y) * kExtent.width + x) * 4;
    return std::array<std::uint8_t, 4>{readbackData[offset], readbackData[offset + 1], readbackData[offset + 2],
                                        readbackData[offset + 3]};
  };

  // Red-only region: NDC x ~= -0.6 -> column (( -0.6 * 0.5 + 0.5) * 64) = 12.8 -> 12.
  const auto redOnlyPixel = pixelAt(12, 32);
  CHECK(redOnlyPixel == std::array<std::uint8_t, 4>{255, 0, 0, 255});

  // Overlap region: NDC x ~= 0.0 -> column ((0.0 * 0.5 + 0.5) * 64) = 32.
  const auto overlapPixel = pixelAt(32, 32);
  CHECK(overlapPixel == std::array<std::uint8_t, 4>{0, 255, 0, 255});

  target.reset();
  offscreenTarget.reset();
  REQUIRE(device->waitIdle().isOk());
}
