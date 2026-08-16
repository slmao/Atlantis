#include <atlantis/assert.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/renderer/renderer.h>
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

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// GPU-required, headless integration coverage for Spec 0010/Plan 0010's
// own OffscreenTarget/readback path (Section 7.2). Real Vulkan device,
// zero windows, zero Presentation, zero VkSwapchainKHR/VkSurfaceKHR
// anywhere in this file -- drives Atlantis's own public API only, no
// Vk* type referenced directly. Every case here carries the "gpu" CTest
// label (catch_discover_tests(), tests/vulkan_backend/CMakeLists.txt).

namespace {

using atlantis::renderer::createMaterial;
using atlantis::renderer::createMesh;
using atlantis::renderer::DrawItem;
using atlantis::renderer::Material;
using atlantis::renderer::Mesh;
using atlantis::renderer::Renderer;
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
using atlantis::vulkan_backend::createDevice;
using atlantis::vulkan_backend::DeviceCreateParams;

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// Same RAII pattern as tests/render_graph/execution_tests.cpp's own
// ScopedFailureHandler -- installs a recording, non-terminating
// replacement failure handler for the lifetime of one test/SECTION.
class ScopedFailureHandler {
 public:
  explicit ScopedFailureHandler(std::vector<RecordedFailure>& recorded)
      : previous_(atlantis::assertions::setFailureHandler([&recorded](const atlantis::AssertFailureInfo& info) {
          recorded.push_back({std::string(info.expression), std::string(info.message)});
        })) {}

  ~ScopedFailureHandler() { atlantis::assertions::setFailureHandler(std::move(previous_)); }

  ScopedFailureHandler(const ScopedFailureHandler&) = delete;
  ScopedFailureHandler& operator=(const ScopedFailureHandler&) = delete;

 private:
  atlantis::AssertFailureHandler previous_;
};

constexpr Extent2D kExtent{256, 256};

// Plan 0007 Section 12 precedent: plain std::ifstream over a relative
// path, no Core/Platform path-resolution API.
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

constexpr Vertex kTriangleVertices[3] = {
    {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
};
constexpr std::uint16_t kTriangleIndices[3] = {0, 1, 2};
constexpr std::array<float, 16> kIdentityMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

}  // namespace

TEST_CASE("OffscreenTarget can be created and destroyed", "[vulkan_backend][gpu][headless]") {
  // Item 1: creation/destruction alone, Validation-Layers-clean.
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Headless GPU Tests (create/destroy)", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  auto offscreenResult =
      device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kExtent, .format = Format::Rgba8Unorm});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());

  offscreenTarget.reset();
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("Acquiring, trivially submitting, and returning a borrow succeeds repeatedly against the same instance",
          "[vulkan_backend][gpu][headless]") {
  // Item 2: a second acquireTarget() succeeds only after the first borrow
  // is destroyed/reset -- exercised here across three consecutive cycles.
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Headless GPU Tests (repeat acquire)", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  auto offscreenResult =
      device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kExtent, .format = Format::Rgba8Unorm});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());

  for (int cycle = 0; cycle < 3; ++cycle) {
    auto acquireResult = offscreenTarget->acquireTarget();
    REQUIRE(acquireResult.isOk());
    std::unique_ptr<RenderTarget> target = std::move(acquireResult.value());

    auto commandListResult = device->createCommandList();
    REQUIRE(commandListResult.isOk());
    std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

    // Trivial submit with no draw -- exercises the acquire/submit/return
    // cycle mechanics alone, not draw correctness (Plan 0010 Section 7.2
    // item 2's own framing).
    auto submitResult = device->submit(std::move(commandList), *target);
    REQUIRE(submitResult.isOk());
    REQUIRE(device->waitIdle().isOk());

    target.reset();
  }

  offscreenTarget.reset();
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("A second acquireTarget() before returning the first fires the guaranteed-detectable assertion",
          "[vulkan_backend][gpu][headless]") {
  // Item 3.
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Headless GPU Tests (double acquire)", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  auto offscreenResult =
      device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kExtent, .format = Format::Rgba8Unorm});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());

  auto firstAcquireResult = offscreenTarget->acquireTarget();
  REQUIRE(firstAcquireResult.isOk());
  std::unique_ptr<RenderTarget> firstBorrow = std::move(firstAcquireResult.value());

  std::vector<RecordedFailure> failures;
  {
    ScopedFailureHandler handler(failures);
    static_cast<void>(offscreenTarget->acquireTarget());  // second, while firstBorrow is still outstanding
  }
  REQUIRE_FALSE(failures.empty());

  firstBorrow.reset();
  offscreenTarget.reset();
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("Destroying OffscreenTarget while a borrow is still outstanding fires the guaranteed-detectable assertion",
          "[vulkan_backend][gpu][headless]") {
  // Item 4.
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Headless GPU Tests (destroy while borrowed)",
                          .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  auto offscreenResult =
      device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kExtent, .format = Format::Rgba8Unorm});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());

  auto acquireResult = offscreenTarget->acquireTarget();
  REQUIRE(acquireResult.isOk());
  std::unique_ptr<RenderTarget> borrow = std::move(acquireResult.value());

  std::vector<RecordedFailure> failures;
  {
    ScopedFailureHandler handler(failures);
    offscreenTarget.reset();  // destroyed while borrow is still outstanding
  }
  REQUIRE_FALSE(failures.empty());

  // borrow now dangles (its owner was destroyed under the non-terminating
  // handler) -- released without dereferencing it again; offscreenTarget
  // itself was already destroyed above (its Vulkan objects released) even
  // though the precondition was violated, matching every other
  // guaranteed-detectable-but-still-destructive assertion in this
  // codebase (non-terminating handler is a test-only override).
  borrow.release();
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("Dropping a borrow immediately after submit(), before waitIdle(), then a correct subsequent cycle",
          "[vulkan_backend][gpu][headless]") {
  // Item 5: the minimum-borrow-lifetime contract in practice (ADR-0038) --
  // the borrow needs to remain alive only through submit()'s return, not
  // through waitIdle().
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Headless GPU Tests (early drop)", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  auto offscreenResult =
      device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kExtent, .format = Format::Rgba8Unorm});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());

  for (int cycle = 0; cycle < 2; ++cycle) {
    auto acquireResult = offscreenTarget->acquireTarget();
    REQUIRE(acquireResult.isOk());
    std::unique_ptr<RenderTarget> target = std::move(acquireResult.value());

    auto commandListResult = device->createCommandList();
    REQUIRE(commandListResult.isOk());
    std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

    auto submitResult = device->submit(std::move(commandList), *target);
    REQUIRE(submitResult.isOk());

    // Borrow dropped here -- immediately after submit() returns, strictly
    // before waitIdle() below. Vulkan bakes handles at record time, so
    // dropping this C++ wrapper now is legal (ADR-0038's own minimum-
    // lifetime claim).
    target.reset();

    REQUIRE(device->waitIdle().isOk());
  }

  offscreenTarget.reset();
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("A readback Buffer can be created and destroyed", "[vulkan_backend][gpu][headless]") {
  // Item 6.
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Headless GPU Tests (readback buffer)", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  auto bufferResult = device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = static_cast<std::size_t>(kExtent.width) * kExtent.height * 4});
  REQUIRE(bufferResult.isOk());
  REQUIRE(bufferResult.value()->purpose() == BufferPurpose::Readback);
  REQUIRE(bufferResult.value()->mappedData() != nullptr);
  bufferResult.value().reset();

  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("One full render-and-readback cycle exercises all five corrected call sites end to end",
          "[vulkan_backend][gpu][headless]") {
  // Item 7: Renderer::drawFrame() (beginRendering()/transitionResource()
  // via its own internal graph), the caller-built copy pass
  // (copyRenderTargetToBuffer()), and submit() -- the concrete,
  // real-hardware closure of Plan 0010 Section 5/6's Must Fix.
  //
  // No ResourceState::PresentSource is ever supplied anywhere in this
  // test's own code path (finalColorState = TransferSource throughout),
  // and resource_state_mapping_tests.cpp's own GPU-independent coverage
  // already confirms planTransition()'s closed table has no entry that
  // could produce a PresentSource/VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  // transition from any state this cycle passes through -- so by
  // construction, combined with Validation Layers reporting clean below
  // (which would flag a PRESENT_SRC_KHR layout used on a non-swapchain
  // image), no such transition is ever recorded here.
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Headless GPU Tests (full cycle)", .enableValidationLayers = true});
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

  auto meshResult =
      createMesh(*device, *vertexInputLayout, kTriangleVertices, sizeof(kTriangleVertices), kTriangleIndices, 3);
  REQUIRE(meshResult.isOk());
  Mesh mesh = std::move(meshResult.value());

  constexpr Format kColorFormat = Format::Rgba8Unorm;

  auto materialResult = createMaterial(
      *device, {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
                .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
                .vertexInputLayout = *vertexInputLayout,
                .colorFormat = kColorFormat,
                .depthFormat = DepthFormat::D32Sfloat,
                .pushConstantSizeBytes = sizeof(float) * 16});
  REQUIRE(materialResult.isOk());
  Material material = std::move(materialResult.value());

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

  auto readbackBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = static_cast<std::size_t>(kExtent.width) * kExtent.height * 4});
  REQUIRE(readbackBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer = std::move(readbackBufferResult.value());

  auto acquireResult = offscreenTarget->acquireTarget();
  REQUIRE(acquireResult.isOk());
  std::unique_ptr<RenderTarget> target = std::move(acquireResult.value());

  DrawItem item;
  item.mesh = &mesh;
  item.material = &material;
  item.objectToWorld = kIdentityMatrix;
  const std::vector<DrawItem> drawItems{item};

  auto commandListResult = device->createCommandList();
  REQUIRE(commandListResult.isOk());
  std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

  Renderer renderer;
  renderer.drawFrame(*commandList, *target, *depthTexture, *cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::TransferSource);

  atlantis::render_graph::RenderGraphBuilder copyBuilder;
  const auto copyResource = copyBuilder.declareResource("color-copy");
  const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
  copyBuilder.writes(copyPass, copyResource, atlantis::rhi::ResourceState::TransferSource);
  copyBuilder.setExecute(copyPass, [&target, &readbackBuffer](CommandList& cmd) {
    cmd.copyRenderTargetToBuffer(*target, *readbackBuffer);
  });
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
  // Content correctness (exact pixel values) is examples/headless_rendering_demo's
  // own job (Plan 0010 Section 7.1 item 7) -- this test's job is
  // confirming the full API cycle succeeds end to end, Validation-Layers-
  // clean, with a non-null mapped readback pointer.

  target.reset();
  offscreenTarget.reset();
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("A cycle that calls waitIdle() immediately after submit(), before dropping the borrow or destroying "
          "OffscreenTarget, is Validation-Layers-clean",
          "[vulkan_backend][gpu][headless]") {
  // Item 8: the documented, correct-order flow (ADR-0038's own
  // "Recommended flow") -- confirms it works with no reliance on
  // Device's own destructor-time drain.
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Headless GPU Tests (documented order)",
                          .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  auto offscreenResult =
      device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kExtent, .format = Format::Rgba8Unorm});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());

  auto acquireResult = offscreenTarget->acquireTarget();
  REQUIRE(acquireResult.isOk());
  std::unique_ptr<RenderTarget> target = std::move(acquireResult.value());

  auto commandListResult = device->createCommandList();
  REQUIRE(commandListResult.isOk());
  std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

  auto submitResult = device->submit(std::move(commandList), *target);
  REQUIRE(submitResult.isOk());

  // waitIdle() first, establishing GPU completion...
  REQUIRE(device->waitIdle().isOk());
  // ...then drop the borrow...
  target.reset();
  // ...then destroy OffscreenTarget -- both now safe (ADR-0038's own
  // "Recommended flow" ordering, verbatim).
  offscreenTarget.reset();

  REQUIRE(device->waitIdle().isOk());
}
