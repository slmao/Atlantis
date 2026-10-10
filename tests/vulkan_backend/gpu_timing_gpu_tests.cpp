#include <atlantis/profile.h>
#include <atlantis/profile_recorder.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/pipeline.h>
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
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// Spec 0059 R4/R6 / ADR-0115 D3, D7: GPU region timestamps arrive one frame late
// from the fence wait submit() already performs, and the backend's counters are
// exact. Meaningful only in a profiling build; under fatal validation layers.

#if defined(ATLANTIS_PROFILING)

namespace {

namespace rhi = atlantis::rhi;
namespace profile = atlantis::profile;
namespace rg = atlantis::render_graph;

struct Vertex {
  float position[3];
  float color[3];
};

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

struct RecordingGuard {
  RecordingGuard() { profile::startRecording(); }
  ~RecordingGuard() { profile::stopRecording(); }
};

// One frame's worth of work: the named passes, each a RenderGraph writing the one
// target, then submit().
bool submitFrame(rhi::Device& device, rhi::OffscreenTarget& offscreen, const std::vector<std::string>& passNames) {
  auto acquired = offscreen.acquireTarget();
  if (acquired.isErr()) return false;
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquired.value());
  auto commandListResult = device.createCommandList();
  if (commandListResult.isErr()) return false;
  std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

  // One single-pass graph per name, executed in turn on the one command list: each
  // pass is bracketed by its own region.
  for (std::size_t i = 0; i < passNames.size(); ++i) {
    rg::RenderGraphBuilder single;
    const auto resource = single.declareResource("color");
    const auto pass = single.declarePass(passNames[i]);
    single.writes(pass, resource, rhi::ResourceState::ColorAttachmentOutput);
    const auto singleCompiled = single.compile();
    if (!singleCompiled.isOk()) return false;
    rg::ResourceBinding binding{};
    binding.resource = singleCompiled.value().resourceAt(0);
    binding.target = target.get();
    binding.incomingState = i == 0 ? rhi::ResourceState::Undefined : rhi::ResourceState::ColorAttachmentOutput;
    binding.finalState = i + 1 == passNames.size() ? std::optional<rhi::ResourceState>(rhi::ResourceState::TransferSource)
                                                    : std::optional<rhi::ResourceState>(rhi::ResourceState::ColorAttachmentOutput);
    rg::execute(singleCompiled.value(), {binding}, *commandList);
  }
  auto submitted = device.submit(std::move(commandList), *target);
  return submitted.isOk();
}

}  // namespace

TEST_CASE("region timestamps are delivered one frame late, after the fence wait submit() already does",
          "[vulkan_backend][gpu][gpu_timing]") {
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis GPU Timing Test", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<rhi::Device> device = std::move(deviceResult.value());
  if (!profile::deviceInfo().timestampsSupported) SKIP("this device cannot write timestamps on its graphics queue");
  CHECK_FALSE(profile::deviceInfo().gpu.empty());
  CHECK(profile::deviceInfo().timestampPeriodNs > 0.0);

  auto offscreenResult = device->createOffscreenTarget({.extent = {16, 16}, .format = rhi::Format::Rgba8Unorm});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<rhi::OffscreenTarget> offscreen = std::move(offscreenResult.value());

  RecordingGuard recording;
  profile::beginFrame();
  const std::uint64_t first = profile::currentFrameId();
  REQUIRE(submitFrame(*device, *offscreen, {"gt_a", "gt_b"}));
  profile::FrameRecord record;
  REQUIRE(profile::readFrame(first, record));
  CHECK_FALSE(record.gpuDelivered);  // the work has only just been queued

  profile::beginFrame();
  REQUIRE(submitFrame(*device, *offscreen, {"gt_a", "gt_b"}));  // this submit() drains the first
  REQUIRE(profile::readFrame(first, record));
  REQUIRE(record.gpuDelivered);
  REQUIRE(record.regionCount == 2);
  CHECK(record.droppedRegions == 0);
  CHECK(profile::nameOf(record.regions[0].name) == "gt_a");
  CHECK(profile::nameOf(record.regions[1].name) == "gt_b");
  for (std::uint32_t i = 0; i < record.regionCount; ++i) {
    CHECK(record.regions[i].depth == 0);
    CHECK(record.regions[i].endNs >= record.regions[i].beginNs);
  }
  CHECK(record.regions[1].beginNs >= record.regions[0].beginNs);

  REQUIRE(device->waitIdle().isOk());  // drains the second
  profile::FrameRecord second;
  REQUIRE(profile::readFrame(profile::currentFrameId(), second));
  CHECK(second.gpuDelivered);
}

TEST_CASE("the backend counters are exact on a known draw sequence", "[vulkan_backend][gpu][gpu_timing]") {
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis GPU Counters Test", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<rhi::Device> device = std::move(deviceResult.value());

  const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
  const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
  REQUIRE(vertexSpirv.has_value());
  REQUIRE(fragmentSpirv.has_value());
  const auto reflection = atlantis::shader_system::loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
  REQUIRE(reflection.isOk());
  const std::vector<atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema> schema = {
      {.location = 0, .offsetBytes = offsetof(Vertex, position)}, {.location = 1, .offsetBytes = offsetof(Vertex, color)}};
  auto layout = atlantis::shader_system::rhi_integration::toVertexInputLayout(reflection.value(), schema, sizeof(Vertex));
  REQUIRE(layout.isOk());
  auto pipeline = device->createPipeline(
      {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
       .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
       .vertexInputLayout = layout.value(),
       .colorFormat = rhi::Format::Rgba8Unorm,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .hasDepthAttachment = false});
  REQUIRE(pipeline.isOk());

  const std::array<Vertex, 4> quad = {Vertex{{-1, -1, 0}, {1, 0, 0}}, Vertex{{1, -1, 0}, {1, 0, 0}},
                                      Vertex{{-1, 1, 0}, {1, 0, 0}}, Vertex{{1, 1, 0}, {1, 0, 0}}};
  const std::array<std::uint16_t, 6> indices = {0, 1, 2, 2, 1, 3};
  auto vertexBuffer = device->createBuffer({.purpose = rhi::BufferPurpose::Vertex, .sizeBytes = sizeof(quad)});
  auto indexBuffer = device->createBuffer({.purpose = rhi::BufferPurpose::Index, .sizeBytes = sizeof(indices)});
  auto cameraBuffer = device->createBuffer({.purpose = rhi::BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  REQUIRE(vertexBuffer.isOk());
  REQUIRE(indexBuffer.isOk());
  REQUIRE(cameraBuffer.isOk());
  std::memcpy(vertexBuffer.value()->mappedData(), quad.data(), sizeof(quad));
  std::memcpy(indexBuffer.value()->mappedData(), indices.data(), sizeof(indices));
  std::memset(cameraBuffer.value()->mappedData(), 0, sizeof(float) * 32);

  auto offscreenResult = device->createOffscreenTarget({.extent = {16, 16}, .format = rhi::Format::Rgba8Unorm});
  REQUIRE(offscreenResult.isOk());
  auto acquired = offscreenResult.value()->acquireTarget();
  REQUIRE(acquired.isOk());
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquired.value());
  auto commandListResult = device->createCommandList();
  REQUIRE(commandListResult.isOk());
  std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

  RecordingGuard recording;
  profile::beginFrame();
  const std::uint64_t frameId = profile::currentFrameId();

  rg::RenderGraphBuilder builder;
  const auto color = builder.declareResource("color");
  const auto pass = builder.declarePass("draws");
  builder.writes(pass, color, rhi::ResourceState::ColorAttachmentOutput);
  const std::array<float, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  builder.setExecute(pass, [&](rhi::CommandList& cmd) {
    cmd.bindPipeline(*pipeline.value());
    cmd.bindVertexBuffer(*vertexBuffer.value());
    cmd.bindIndexBuffer(*indexBuffer.value());
    for (int i = 0; i < 3; ++i) {
      cmd.bindUniformBuffer(*cameraBuffer.value());
      cmd.pushConstant(identity.data(), sizeof(identity));
      cmd.drawIndexed(6);
    }
  });
  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());
  rg::ResourceBinding binding{};
  binding.resource = compiled.value().resourceAt(0);
  binding.target = target.get();
  binding.finalState = rhi::ResourceState::TransferSource;
  rg::execute(compiled.value(), {binding}, *commandList);
  REQUIRE(device->submit(std::move(commandList), *target).isOk());
  REQUIRE(device->waitIdle().isOk());

  profile::FrameRecord record;
  REQUIRE(profile::readFrame(frameId, record));
  const auto counter = [&](const char* name) { return record.counter[profile::internName(name)]; };
  CHECK(counter("draw_calls") == 3.0);
  CHECK(counter("triangles") == 6.0);  // three draws of two triangles
  CHECK(counter("pipeline_binds") == 1.0);
  CHECK(counter("vertex_buffer_binds") == 1.0);
  CHECK(counter("index_buffer_binds") == 1.0);
  CHECK(counter("push_constants") == 3.0);
  CHECK(counter("descriptor_set_binds") == 3.0);  // one per bindUniformBuffer
  CHECK(counter("descriptor_writes") == 1.0);     // the memo skips the two repeats
}

#else

TEST_CASE("GPU timing and backend counters exist only in a profiling build", "[vulkan_backend][gpu][gpu_timing]") {
  SKIP("ATLANTIS_PROFILING is off in this build");
}

#endif
