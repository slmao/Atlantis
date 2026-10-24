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

// Plan 0066 M3a (ADR-0133 D2, D3): an attachment loaded by a later
// execute() keeps what an earlier one stored -- the colour target's pixels
// and the depth Texture's values -- through the Vulkan Backend's
// LOAD_OP_LOAD and its same-state barriers, with fatal validation. The
// later graph's incoming states are the end states the earlier execute()
// reported (ADR-0133 D5), never literal ones. Fixture as
// pipeline_depth_write_gpu_tests.cpp's: the minimal_mesh shader pair with
// identity camera and objectToWorld, so a vertex's z is its NDC depth. One
// CommandList, one submit(): draw graphs, then a copy graph.

namespace {

using atlantis::rhi::AttachmentLoad;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::CommandList;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Device;
using atlantis::rhi::Extent2D;
using atlantis::rhi::Format;
using atlantis::rhi::OffscreenTarget;
using atlantis::rhi::OffscreenTargetCreateParams;
using atlantis::rhi::RenderTarget;
using atlantis::rhi::ResourceState;
using atlantis::rhi::VertexInputLayout;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;
using Pixel = std::array<std::uint8_t, 4>;

constexpr Extent2D kExtent{64, 64};
constexpr Format kColorFormat = Format::Rgba8Unorm;
constexpr std::array<float, 16> kIdentityMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr std::uint16_t kQuadIndices[6] = {0, 1, 2, 2, 1, 3};

// Each test file owns its small fixture (pipeline_depth_write_gpu_tests.cpp's precedent).
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

// A full-height quad over NDC x in [minX, maxX] at depth z.
[[nodiscard]] std::array<Vertex, 4> makeQuad(float minX, float maxX, float z, float r, float g, float b) {
  return {Vertex{{minX, -1.0f, z}, {r, g, b}}, Vertex{{maxX, -1.0f, z}, {r, g, b}}, Vertex{{minX, 1.0f, z}, {r, g, b}},
          Vertex{{maxX, 1.0f, z}, {r, g, b}}};
}

struct Quad {
  std::unique_ptr<atlantis::rhi::Buffer> vertexBuffer;
  std::unique_ptr<atlantis::rhi::Buffer> indexBuffer;
};

[[nodiscard]] std::optional<Quad> createQuad(Device& device, const std::array<Vertex, 4>& vertices) {
  auto vertexBuffer = device.createBuffer({.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(vertices)});
  auto indexBuffer = device.createBuffer({.purpose = BufferPurpose::Index, .sizeBytes = sizeof(kQuadIndices)});
  if (vertexBuffer.isErr() || indexBuffer.isErr()) return std::nullopt;
  Quad quad{std::move(vertexBuffer.value()), std::move(indexBuffer.value())};
  std::memcpy(quad.vertexBuffer->mappedData(), vertices.data(), sizeof(vertices));
  std::memcpy(quad.indexBuffer->mappedData(), kQuadIndices, sizeof(kQuadIndices));
  return quad;
}

// The device, the pipelines and the targets of one case.
struct Fixture {
  std::unique_ptr<Device> device;
  std::unique_ptr<atlantis::rhi::Pipeline> writingDepth;
  std::unique_ptr<atlantis::rhi::Pipeline> testingDepth;  // depthWriteEnabled = false, as the overlay's tested pass
  std::unique_ptr<atlantis::rhi::Pipeline> noDepth;       // no depth attachment, as the overlay's on-top pass
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture;
  std::unique_ptr<atlantis::rhi::Texture> freshDepthTexture;  // never written: the "not loaded" depth
  std::unique_ptr<OffscreenTarget> offscreenTarget;
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer;

  Fixture() {
    auto deviceResult = atlantis::vulkan_backend::createDevice(
        {.applicationName = "Atlantis Attachment Load GPU Test", .enableValidationLayers = true});
    REQUIRE(deviceResult.isOk());
    device = std::move(deviceResult.value());

    const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
    const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
    REQUIRE(vertexSpirv.has_value());
    REQUIRE(fragmentSpirv.has_value());
    const auto vertexReflection = loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
    REQUIRE(vertexReflection.isOk());
    const auto vertexInputLayout = minimalMeshVertexLayout(vertexReflection.value());
    REQUIRE(vertexInputLayout.has_value());
    const auto pipeline = [&](bool hasDepth, bool depthWrite) {
      auto result = device->createPipeline(
          {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
           .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
           .vertexInputLayout = *vertexInputLayout,
           .colorFormat = kColorFormat,
           .depthFormat = DepthFormat::D32Sfloat,
           .pushConstantSizeBytes = sizeof(float) * 16,
           .hasDepthAttachment = hasDepth,
           .depthWriteEnabled = depthWrite});
      REQUIRE(result.isOk());
      return std::move(result.value());
    };
    writingDepth = pipeline(true, true);
    testingDepth = pipeline(true, false);
    noDepth = pipeline(false, false);

    auto camera = device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
    REQUIRE(camera.isOk());
    cameraBuffer = std::move(camera.value());
    auto* cameraData = static_cast<float*>(cameraBuffer->mappedData());
    for (std::size_t i = 0; i < 16; ++i) cameraData[i] = cameraData[16 + i] = kIdentityMatrix[i];

    auto depth = device->createTexture({.extent = kExtent, .format = DepthFormat::D32Sfloat});
    auto freshDepth = device->createTexture({.extent = kExtent, .format = DepthFormat::D32Sfloat});
    REQUIRE(depth.isOk());
    REQUIRE(freshDepth.isOk());
    depthTexture = std::move(depth.value());
    freshDepthTexture = std::move(freshDepth.value());
    auto offscreen = device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kExtent, .format = kColorFormat});
    REQUIRE(offscreen.isOk());
    offscreenTarget = std::move(offscreen.value());
    auto readback = device->createBuffer(
        {.purpose = BufferPurpose::Readback, .sizeBytes = static_cast<std::size_t>(kExtent.width) * kExtent.height * 4});
    REQUIRE(readback.isOk());
    readbackBuffer = std::move(readback.value());
  }

  [[nodiscard]] Pixel pixelAt(std::uint32_t x, std::uint32_t y) const {
    const auto* data = static_cast<const std::uint8_t*>(readbackBuffer->mappedData());
    const std::size_t offset = (static_cast<std::size_t>(y) * kExtent.width + x) * 4;
    return {data[offset], data[offset + 1], data[offset + 2], data[offset + 3]};
  }
};

void drawQuad(CommandList& cmd, atlantis::rhi::Pipeline& pipeline, const Quad& quad, atlantis::rhi::Buffer& camera) {
  cmd.bindPipeline(pipeline);
  cmd.bindVertexBuffer(*quad.vertexBuffer);
  cmd.bindIndexBuffer(*quad.indexBuffer);
  cmd.bindUniformBuffer(camera);
  cmd.pushConstant(kIdentityMatrix.data(), kIdentityMatrix.size() * sizeof(float));
  cmd.drawIndexed(6);
}

// What graph B binds for depth: none (the overlay's on-top pass), the depth
// graph A wrote, loaded from A's reported end state (the tested pass), or a
// never-written depth, cleared (the same quad with nothing loaded).
enum class SecondDepth { None, Loaded, Fresh };

// Graph A clears the target to red and draws `first` (writing depth); graph B
// loads the target from A's reported end state, clears to blue whatever it does
// not load, and draws `second` with depth writes off. Then the copy graph reads
// the target back. One CommandList, one submit().
void renderTwoGraphs(Fixture& f, const Quad* first, const Quad& second, SecondDepth secondDepth) {
  auto acquire = f.offscreenTarget->acquireTarget();
  REQUIRE(acquire.isOk());
  std::unique_ptr<RenderTarget> target = std::move(acquire.value());
  auto commandListResult = f.device->createCommandList();
  REQUIRE(commandListResult.isOk());
  std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

  atlantis::render_graph::RenderGraphBuilder a;
  const auto aColor = a.declareResource("color");
  const auto aDepth = a.declareResource("depth");
  const auto aPass = a.declarePass("scene");
  a.writes(aPass, aColor, ResourceState::ColorAttachmentOutput);
  a.writes(aPass, aDepth, ResourceState::DepthAttachmentReadWrite);
  a.setExecute(aPass, [&](CommandList& cmd) {
    if (first != nullptr) drawQuad(cmd, *f.writingDepth, *first, *f.cameraBuffer);
  });
  auto aCompiled = a.compile();
  REQUIRE(aCompiled.isOk());
  const auto aEnd = atlantis::render_graph::execute(
      aCompiled.value(),
      {{.resource = aCompiled.value().resourceAt(0),
        .target = target.get(),
        .colorClear = {1.0f, 0.0f, 0.0f, 1.0f},
        .finalState = ResourceState::ColorAttachmentOutput},
       {.resource = aCompiled.value().resourceAt(1), .depthTexture = f.depthTexture.get(), .depthClear = 1.0f}},
      *commandList);
  REQUIRE(aEnd == std::vector<ResourceState>{ResourceState::ColorAttachmentOutput, ResourceState::DepthAttachmentReadWrite});

  atlantis::render_graph::RenderGraphBuilder b;
  const auto bColor = b.declareResource("color");
  const auto bPass = b.declarePass("overlay");
  b.writes(bPass, bColor, ResourceState::ColorAttachmentOutput);
  std::vector<atlantis::render_graph::ResourceBinding> bBindings;
  if (secondDepth != SecondDepth::None) {
    const auto bDepth = b.declareResource("depth");
    b.writes(bPass, bDepth, ResourceState::DepthAttachmentReadWrite);
  }
  atlantis::rhi::Pipeline& secondPipeline = secondDepth == SecondDepth::None ? *f.noDepth : *f.testingDepth;
  b.setExecute(bPass, [&](CommandList& cmd) { drawQuad(cmd, secondPipeline, second, *f.cameraBuffer); });
  auto bCompiled = b.compile();
  REQUIRE(bCompiled.isOk());
  bBindings.push_back({.resource = bCompiled.value().resourceAt(0),
                       .target = target.get(),
                       .colorClear = {0.0f, 0.0f, 1.0f, 1.0f},
                       .incomingState = aEnd[0],
                       .finalState = ResourceState::TransferSource,
                       .load = AttachmentLoad::Load});
  if (secondDepth == SecondDepth::Loaded) {
    bBindings.push_back({.resource = bCompiled.value().resourceAt(1),
                         .depthTexture = f.depthTexture.get(),
                         .incomingState = aEnd[1],
                         .load = AttachmentLoad::Load});
  } else if (secondDepth == SecondDepth::Fresh) {
    bBindings.push_back(
        {.resource = bCompiled.value().resourceAt(1), .depthTexture = f.freshDepthTexture.get(), .depthClear = 1.0f});
  }
  static_cast<void>(atlantis::render_graph::execute(bCompiled.value(), bBindings, *commandList));

  atlantis::render_graph::RenderGraphBuilder copy;
  const auto copyResource = copy.declareResource("color-copy");
  const auto copyPass = copy.declarePass("copy-to-buffer");
  copy.writes(copyPass, copyResource, ResourceState::TransferSource);
  copy.setExecute(copyPass, [&](CommandList& cmd) { cmd.copyRenderTargetToBuffer(*target, *f.readbackBuffer); });
  auto copyCompiled = copy.compile();
  REQUIRE(copyCompiled.isOk());
  static_cast<void>(atlantis::render_graph::execute(
      copyCompiled.value(),
      {{.resource = copyCompiled.value().resourceAt(0),
        .target = target.get(),
        .incomingState = ResourceState::TransferSource}},
      *commandList));

  REQUIRE(f.device->submit(std::move(commandList), *target).isOk());
  REQUIRE(f.device->waitIdle().isOk());
  target.reset();
}

// NDC x -> column at 64 pixels: ((x * 0.5 + 0.5) * 64).
constexpr std::uint32_t kLeftColumn = 4;     // x ~ -0.875
constexpr std::uint32_t kMiddleColumn = 32;  // x ~ 0.0
constexpr std::uint32_t kRightColumn = 56;   // x ~ 0.75
constexpr Pixel kRed{255, 0, 0, 255};
constexpr Pixel kGreen{0, 255, 0, 255};

}  // namespace

TEST_CASE("a loaded colour target keeps the earlier graph's pixels outside what the later graph draws",
          "[vulkan_backend][gpu][attachment_load]") {
  Fixture f;
  const auto green = createQuad(*f.device, makeQuad(-0.2f, 0.2f, 0.5f, 0.0f, 1.0f, 0.0f));
  REQUIRE(green.has_value());
  renderTwoGraphs(f, nullptr, *green, SecondDepth::None);
  CHECK(f.pixelAt(kLeftColumn, 32) == kRed);  // graph A's clear, kept: graph B's blue clear value is unused
  CHECK(f.pixelAt(kRightColumn, 32) == kRed);
  CHECK(f.pixelAt(kMiddleColumn, 32) == kGreen);
  f.offscreenTarget.reset();
  REQUIRE(f.device->waitIdle().isOk());
}

TEST_CASE("a loaded depth hides what lies behind the earlier graph's draw; a cleared one does not",
          "[vulkan_backend][gpu][attachment_load]") {
  // Graph A: a near quad over [-1, 0.2] writes depth 0.3 (its colour is red, as the clear).
  // Graph B: a farther quad over [-0.2, 1] at depth 0.6, depth-tested.
  for (const SecondDepth secondDepth : {SecondDepth::Loaded, SecondDepth::Fresh}) {
    Fixture f;
    const auto nearQuad = createQuad(*f.device, makeQuad(-1.0f, 0.2f, 0.3f, 1.0f, 0.0f, 0.0f));
    const auto farQuad = createQuad(*f.device, makeQuad(-0.2f, 1.0f, 0.6f, 0.0f, 1.0f, 0.0f));
    REQUIRE(nearQuad.has_value());
    REQUIRE(farQuad.has_value());
    renderTwoGraphs(f, &*nearQuad, *farQuad, secondDepth);
    if (secondDepth == SecondDepth::Loaded) {
      CHECK(f.pixelAt(kMiddleColumn, 32) == kRed);  // behind the loaded depth: hidden
    } else {
      CHECK(f.pixelAt(kMiddleColumn, 32) == kGreen);  // nothing loaded: nothing hides it
    }
    CHECK(f.pixelAt(kRightColumn, 32) == kGreen);
    CHECK(f.pixelAt(kLeftColumn, 32) == kRed);  // the colour target loaded either way
    f.offscreenTarget.reset();
    REQUIRE(f.device->waitIdle().isOk());
  }
}
