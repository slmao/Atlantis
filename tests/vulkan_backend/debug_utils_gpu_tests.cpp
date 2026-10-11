#include "vulkan_device.h"

#include <atlantis/profile.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/types.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// Spec 0059 R5 / ADR-0115 D2, D5, D6: object names and pass labels reach the
// Vulkan layer. Vulkan has no call to read a name back, so the backend's own
// DebugUtils observer records what it was asked to set; validation layers (on,
// fatal) check each call against the real driver.

namespace {

namespace rhi = atlantis::rhi;
using atlantis::vulkan_backend::detail::DebugUtilsObserver;
using atlantis::vulkan_backend::detail::VulkanDevice;

struct Recorded {
  std::vector<std::pair<VkObjectType, std::string>> names;
  std::vector<std::string> events;  // "begin:<label>" / "end"
  [[nodiscard]] bool hasName(VkObjectType type, const std::string& name) const {
    for (const auto& [t, n] : names) {
      if (t == type && n == name) return true;
    }
    return false;
  }
};

[[nodiscard]] DebugUtilsObserver observerFor(Recorded& recorded) {
  DebugUtilsObserver observer;
  observer.onName = [&recorded](VkObjectType type, std::string_view name) {
    recorded.names.emplace_back(type, std::string(name));
  };
  observer.onBeginLabel = [&recorded](std::string_view label) { recorded.events.push_back("begin:" + std::string(label)); };
  observer.onEndLabel = [&recorded]() { recorded.events.push_back("end"); };
  return observer;
}

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

}  // namespace

TEST_CASE("created objects carry their debug names, and a RenderGraph pass is labelled",
          "[vulkan_backend][gpu][debug_utils]") {
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Debug Utils GPU Test", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<rhi::Device> device = std::move(deviceResult.value());

  auto* vulkanDevice = dynamic_cast<VulkanDevice*>(device.get());
  REQUIRE(vulkanDevice != nullptr);
#if defined(ATLANTIS_GPU_MARKERS)
  // Validation is on here, which enables VK_EXT_debug_utils; the Release-without-
  // validation request path is covered by the instance-extension unit tests.
  REQUIRE(vulkanDevice->debugUtils().enabled());
#endif
  Recorded recorded;
  const DebugUtilsObserver observer = observerFor(recorded);
  vulkanDevice->debugUtils().setObserver(&observer);

  // Buffers, images, samplers.
  auto buffer = device->createBuffer({.purpose = rhi::BufferPurpose::Uniform, .sizeBytes = 64, .debugName = "t_uniform"});
  REQUIRE(buffer.isOk());
  auto depth = device->createTexture({.extent = {8, 8}, .debugName = "t_depth"});
  REQUIRE(depth.isOk());
  auto sampled = device->createSampledTexture({.extent = {4, 4}, .debugName = "t_sampled"});
  REQUIRE(sampled.isOk());
  auto sampler = device->createSampler({.debugName = "t_sampler"});
  REQUIRE(sampler.isOk());
  auto hdr = device->createHdrColorTarget({.extent = {8, 8}, .debugName = "t_hdr"});
  REQUIRE(hdr.isOk());
  auto shadow = device->createShadowMap({.extent = {8, 8}, .debugName = "t_shadow"});
  REQUIRE(shadow.isOk());
  auto offscreen = device->createOffscreenTarget({.extent = {8, 8}, .format = rhi::Format::Rgba8Unorm, .debugName = "t_target"});
  REQUIRE(offscreen.isOk());
  auto unnamed = device->createBuffer({.purpose = rhi::BufferPurpose::Vertex, .sizeBytes = 16});
  REQUIRE(unnamed.isOk());

  CHECK(recorded.hasName(VK_OBJECT_TYPE_BUFFER, "t_uniform"));
  CHECK(recorded.hasName(VK_OBJECT_TYPE_DEVICE_MEMORY, "t_uniform/memory"));
  CHECK(recorded.hasName(VK_OBJECT_TYPE_IMAGE, "t_depth"));
  CHECK(recorded.hasName(VK_OBJECT_TYPE_IMAGE_VIEW, "t_depth/view"));
  CHECK(recorded.hasName(VK_OBJECT_TYPE_IMAGE, "t_sampled"));
  CHECK(recorded.hasName(VK_OBJECT_TYPE_SAMPLER, "t_sampler"));
  CHECK(recorded.hasName(VK_OBJECT_TYPE_IMAGE, "t_hdr"));
  CHECK(recorded.hasName(VK_OBJECT_TYPE_IMAGE, "t_shadow"));
  CHECK(recorded.hasName(VK_OBJECT_TYPE_IMAGE, "t_target"));
  const std::size_t namedBefore = recorded.names.size();
  CHECK(namedBefore == 2 + 3 + 3 + 1 + 3 + 3 + 3);  // an unnamed object sets no name at all

  // Pipeline: pipeline, layout, set layout, descriptor set.
  const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
  const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
  REQUIRE(vertexSpirv.has_value());
  REQUIRE(fragmentSpirv.has_value());
  const auto vertexReflection = atlantis::shader_system::loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
  REQUIRE(vertexReflection.isOk());
  const std::vector<atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema> schema = {
      {.location = 0, .offsetBytes = offsetof(Vertex, position)}, {.location = 1, .offsetBytes = offsetof(Vertex, color)}};
  auto layout = atlantis::shader_system::rhi_integration::toVertexInputLayout(vertexReflection.value(), schema, sizeof(Vertex));
  REQUIRE(layout.isOk());
  auto pipeline = device->createPipeline(
      {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
       .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
       .vertexInputLayout = layout.value(),
       .colorFormat = rhi::Format::Rgba8Unorm,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .debugName = "t_pipeline"});
  REQUIRE(pipeline.isOk());
  CHECK(recorded.hasName(VK_OBJECT_TYPE_PIPELINE, "t_pipeline"));
  CHECK(recorded.hasName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, "t_pipeline/layout"));
  CHECK(recorded.hasName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "t_pipeline/set_layout"));
  CHECK(recorded.hasName(VK_OBJECT_TYPE_DESCRIPTOR_SET, "t_pipeline/set"));

  // A command list is named, and every RenderGraph pass is wrapped in a label.
  auto acquired = offscreen.value()->acquireTarget();
  REQUIRE(acquired.isOk());
  std::unique_ptr<rhi::RenderTarget> target = std::move(acquired.value());
  auto commandListResult = device->createCommandList();
  REQUIRE(commandListResult.isOk());
  std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());
  commandList->setDebugName("t_frame");
  CHECK(recorded.hasName(VK_OBJECT_TYPE_COMMAND_BUFFER, "t_frame"));

  atlantis::render_graph::RenderGraphBuilder builder;
  const auto color = builder.declareResource("color");
  const auto pass = builder.declarePass("t_clear_pass");
  builder.writes(pass, color, rhi::ResourceState::ColorAttachmentOutput);
  builder.setExecute(pass, [&recorded](rhi::CommandList& cmd) {
    ATL_PROFILE_GPU_SCOPE(cmd, "t_inner");
    recorded.events.push_back("pass body");
  });
  const auto compiled = builder.compile();
  REQUIRE(compiled.isOk());
  atlantis::render_graph::ResourceBinding binding{};
  binding.resource = compiled.value().resourceAt(0);
  binding.target = target.get();
  binding.finalState = rhi::ResourceState::TransferSource;
  atlantis::render_graph::execute(compiled.value(), {binding}, *commandList);

  auto submitted = device->submit(std::move(commandList), *target);
  REQUIRE(submitted.isOk());
  REQUIRE(device->waitIdle().isOk());

#if defined(ATLANTIS_GPU_MARKERS)
  REQUIRE(recorded.events.size() == 5);
  CHECK(recorded.events[0] == "begin:t_clear_pass");
  CHECK(recorded.events[1] == "begin:t_inner");
  CHECK(recorded.events[2] == "pass body");
  CHECK(recorded.events[3] == "end");
  CHECK(recorded.events[4] == "end");
#endif
  vulkanDevice->debugUtils().setObserver(nullptr);
}

TEST_CASE("GPU markers request VK_EXT_debug_utils even with validation off (Release)",
          "[vulkan_backend][gpu][debug_utils]") {
  // In a Debug build validation is forced on and enables the extension anyway; in
  // a Release build this is the path that matters: the instance asks the loader
  // (or a capture layer) for the extension because markers are on.
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Debug Utils No-Validation GPU Test", .enableValidationLayers = false});
  REQUIRE(deviceResult.isOk());
  auto* vulkanDevice = dynamic_cast<VulkanDevice*>(deviceResult.value().get());
  REQUIRE(vulkanDevice != nullptr);
#if defined(ATLANTIS_GPU_MARKERS)
  CHECK(vulkanDevice->debugUtils().enabled());
#else
  CHECK_FALSE(vulkanDevice->debugUtils().enabled());
#endif
}
