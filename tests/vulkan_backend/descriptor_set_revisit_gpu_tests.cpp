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

// Plan 0035 Milestone 5 (2026-09-14 fix): a real, discriminating GPU
// regression test for the exact defect found while implementing this
// Plan's own ~28-sphere showcase scene -- a VUID-vkCmdBindDescriptorSets-
// commandBuffer-recording Validation Layers FATAL, root-caused to
// VulkanCommandList::bindUniformBuffer()/bindTexture()'s own redundant-
// write memo (vulkan_command_list.h/.cpp) tracking only the single
// most-recently-touched VkDescriptorSet, not "has this exact set already
// been written+bound anywhere earlier in this recording." Any Pipeline
// A, drawn, then a DIFFERENT Pipeline B, drawn, then Pipeline A
// REVISITED (the exact same Pipeline instance, same VkDescriptorSet) in
// the SAME CommandList recording reproduced it before the fix --
// pipeline_depth_write_gpu_tests.cpp's own pre-2026-09-14 comment already
// disclosed this as a known, reproduced, worked-around (not fixed)
// pre-existing bug (a second, distinct Pipeline instance sidestepped it
// there, rather than genuinely revisiting the same one). This test does
// the real thing: the same Pipeline object, bound a second time, after a
// different Pipeline's own descriptor set was bound in between --
// exactly the "shared pedestal Material, sphere Material interposed"
// pattern the showcase scene's own draw order produces.
//
// Real GPU rendering twice (not a single-shot smoke test): confirms both
// that the revisit itself does not crash (Validation Layers enabled,
// any VUID hit aborts the whole process, ctest would report the
// executable itself failed -- unlike a Catch2 REQUIRE failure) and that
// the resulting frame is byte-identical across two independent
// acquire/record/submit/waitIdle cycles against the same Device/
// Pipelines/Buffers (determinism -- no residual per-recording memo state
// leaking across cycles, matching every other established multi-cycle
// GPU test's own convention, e.g. descriptor_pool_growth_gpu_tests.cpp).
//
// Reuses the existing, already-compiled minimal_mesh shader pair
// unchanged (no new shader), mirroring pipeline_depth_write_gpu_tests.cpp's
// own identity-camera/three-non-overlapping-quad-columns convention so a
// wrong or missing draw is immediately visible as a black (clear-color)
// region rather than requiring a pixel-perfect golden.

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

constexpr std::uint16_t kQuadIndices[6] = {0, 1, 2, 2, 1, 3};

[[nodiscard]] std::array<Vertex, 4> makeQuad(float minX, float maxX, float r, float g, float b) {
  return {Vertex{{minX, -1.0f, 0.0f}, {r, g, b}}, Vertex{{maxX, -1.0f, 0.0f}, {r, g, b}},
          Vertex{{minX, 1.0f, 0.0f}, {r, g, b}}, Vertex{{maxX, 1.0f, 0.0f}, {r, g, b}}};
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

  auto indexBufferResult = device.createBuffer({.purpose = BufferPurpose::Index, .sizeBytes = sizeof(kQuadIndices)});
  if (indexBufferResult.isErr()) return std::nullopt;
  draw.indexBuffer = std::move(indexBufferResult.value());
  std::memcpy(draw.indexBuffer->mappedData(), kQuadIndices, sizeof(kQuadIndices));
  return draw;
}

}  // namespace

TEST_CASE("Revisiting the same Pipeline's descriptor set after a different Pipeline's own set was bound in "
          "between, in one CommandList recording, renders correctly and hits zero Validation Layer errors "
          "(2026-09-14 fix, Plan 0035 Milestone 5)",
          "[vulkan_backend][gpu][descriptor_set_revisit]") {
  constexpr Extent2D kExtent{96, 32};
  constexpr Format kColorFormat = Format::Rgba8Unorm;

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Descriptor Set Revisit GPU Test", .enableValidationLayers = true});
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

  // Pipeline A ("shared") is deliberately reused for both the first and
  // third draw below -- the exact same VkDescriptorSet, revisited after
  // Pipeline B's own distinct set was bound in between. Pipeline B
  // ("other") is created only once, matching a real scene's own
  // "one Material interposed between two uses of another" shape.
  // hasDepthAttachment = false: these three non-overlapping quads need
  // no depth test/write, and this test declares no depth resource at
  // all in its own RenderGraph -- the default (true) would otherwise
  // bind a Pipeline expecting a real depth attachment during a render
  // pass instance with none, itself a real Vulkan validation violation
  // (see PipelineCreateParams::hasDepthAttachment's own doc comment).
  auto pipelineSharedResult = device->createPipeline(
      {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
       .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
       .vertexInputLayout = *vertexInputLayout,
       .colorFormat = kColorFormat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .hasDepthAttachment = false});
  REQUIRE(pipelineSharedResult.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> pipelineShared = std::move(pipelineSharedResult.value());

  auto pipelineOtherResult = device->createPipeline(
      {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
       .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
       .vertexInputLayout = *vertexInputLayout,
       .colorFormat = kColorFormat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .hasDepthAttachment = false});
  REQUIRE(pipelineOtherResult.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> pipelineOther = std::move(pipelineOtherResult.value());

  auto cameraBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  REQUIRE(cameraBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer = std::move(cameraBufferResult.value());
  {
    auto* cameraData = static_cast<float*>(cameraBuffer->mappedData());
    for (int i = 0; i < 16; ++i) cameraData[i] = kIdentityMatrix[static_cast<std::size_t>(i)];
    for (int i = 0; i < 16; ++i) cameraData[16 + i] = kIdentityMatrix[static_cast<std::size_t>(i)];
  }

  // Three non-overlapping columns: left = Pipeline A (1st touch, red),
  // middle = Pipeline B (green), right = Pipeline A again (2nd touch,
  // REVISITED after B -- blue, so a missing/corrupted third draw is
  // immediately visible as black, not merely a wrong-but-still-red
  // pixel).
  auto redQuad = createQuadDraw(*device, makeQuad(-1.0f, -0.34f, 1.0f, 0.0f, 0.0f));
  auto greenQuad = createQuadDraw(*device, makeQuad(-0.33f, 0.33f, 0.0f, 1.0f, 0.0f));
  auto blueQuad = createQuadDraw(*device, makeQuad(0.34f, 1.0f, 0.0f, 0.0f, 1.0f));
  REQUIRE(redQuad.has_value());
  REQUIRE(greenQuad.has_value());
  REQUIRE(blueQuad.has_value());

  const std::size_t readbackSizeBytes = static_cast<std::size_t>(kExtent.width) * kExtent.height * 4;

  const auto pixelAt = [&](const std::uint8_t* data, std::uint32_t x, std::uint32_t y) {
    const std::size_t offset = (static_cast<std::size_t>(y) * kExtent.width + x) * 4;
    return std::array<std::uint8_t, 4>{data[offset], data[offset + 1], data[offset + 2], data[offset + 3]};
  };

  // Two independent, full acquire -> record (with the real A-B-A
  // revisit) -> submit -> waitIdle -> readback cycles against the same
  // Device/Pipelines/Buffers -- proves determinism, not merely "ran
  // once without crashing."
  std::optional<std::array<std::uint8_t, 4>> firstCycleRed;
  std::optional<std::array<std::uint8_t, 4>> firstCycleGreen;
  std::optional<std::array<std::uint8_t, 4>> firstCycleBlue;

  for (int cycle = 0; cycle < 2; ++cycle) {
    auto offscreenResult =
        device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kExtent, .format = kColorFormat});
    REQUIRE(offscreenResult.isOk());
    std::unique_ptr<OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());

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

    atlantis::render_graph::RenderGraphBuilder drawBuilder;
    const auto colorResource = drawBuilder.declareResource("color");
    const auto drawPass = drawBuilder.declarePass("draw");
    drawBuilder.writes(drawPass, colorResource, atlantis::rhi::ResourceState::ColorAttachmentOutput);
    drawBuilder.setExecute(drawPass, [&](CommandList& cmd) {
      // 1st touch of pipelineShared's own descriptor set.
      cmd.bindPipeline(*pipelineShared);
      cmd.bindVertexBuffer(*redQuad->vertexBuffer);
      cmd.bindIndexBuffer(*redQuad->indexBuffer);
      cmd.bindUniformBuffer(*cameraBuffer);
      cmd.pushConstant(kIdentityMatrix.data(), kIdentityMatrix.size() * sizeof(float));
      cmd.drawIndexed(6);

      // A different Pipeline's own, distinct descriptor set, interposed.
      cmd.bindPipeline(*pipelineOther);
      cmd.bindVertexBuffer(*greenQuad->vertexBuffer);
      cmd.bindIndexBuffer(*greenQuad->indexBuffer);
      cmd.bindUniformBuffer(*cameraBuffer);
      cmd.pushConstant(kIdentityMatrix.data(), kIdentityMatrix.size() * sizeof(float));
      cmd.drawIndexed(6);

      // 2nd touch of pipelineShared's own SAME descriptor set -- the
      // real revisit this test exists to exercise. Pre-fix, this would
      // call vkUpdateDescriptorSets() on a set already bound via
      // vkCmdBindDescriptorSets() during the 1st touch above, in this
      // same not-yet-submitted recording: a Validation Layers FATAL
      // (VUID-vkCmdBindDescriptorSets-commandBuffer-recording).
      cmd.bindPipeline(*pipelineShared);
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
         .finalState = atlantis::rhi::ResourceState::TransferSource}};
    atlantis::render_graph::execute(drawCompileResult.value(), drawBindings, *commandList);

    atlantis::render_graph::RenderGraphBuilder copyBuilder;
    const auto copyResource = copyBuilder.declareResource("color-copy");
    const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
    copyBuilder.writes(copyPass, copyResource, atlantis::rhi::ResourceState::TransferSource);
    copyBuilder.setExecute(copyPass,
                            [&](CommandList& cmd) { cmd.copyRenderTargetToBuffer(*target, *readbackBuffer); });
    auto copyCompileResult = copyBuilder.compile();
    REQUIRE(copyCompileResult.isOk());
    const std::vector<atlantis::render_graph::ResourceBinding> copyBindings{
        {.resource = copyCompileResult.value().resourceAt(0),
         .target = target.get(),
         .incomingState = atlantis::rhi::ResourceState::TransferSource}};
    atlantis::render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

    auto submitResult = device->submit(std::move(commandList), *target);
    REQUIRE(submitResult.isOk());
    REQUIRE(device->waitIdle().isOk());

    const auto* readbackData = static_cast<const std::uint8_t*>(readbackBuffer->mappedData());
    REQUIRE(readbackData != nullptr);

    const auto redPixel = pixelAt(readbackData, 16, 16);
    const auto greenPixel = pixelAt(readbackData, 48, 16);
    const auto bluePixel = pixelAt(readbackData, 80, 16);
    CHECK(redPixel == std::array<std::uint8_t, 4>{255, 0, 0, 255});
    CHECK(greenPixel == std::array<std::uint8_t, 4>{0, 255, 0, 255});
    CHECK(bluePixel == std::array<std::uint8_t, 4>{0, 0, 255, 255});

    if (cycle == 0) {
      firstCycleRed = redPixel;
      firstCycleGreen = greenPixel;
      firstCycleBlue = bluePixel;
    } else {
      CHECK(redPixel == *firstCycleRed);
      CHECK(greenPixel == *firstCycleGreen);
      CHECK(bluePixel == *firstCycleBlue);
    }

    target.reset();
    offscreenTarget.reset();
    REQUIRE(device->waitIdle().isOk());
  }
}
