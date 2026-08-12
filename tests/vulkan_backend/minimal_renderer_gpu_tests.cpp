#include <atlantis/platform/platform.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/presentation.h>
#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <catch2/catch_test_macros.hpp>

// GPU-required, windowed integration coverage for Plan 0007's own
// Buffer/Texture/Pipeline/Renderer::drawFrame() draw path (Section 15).
// Windows-only, real Vulkan device, real window -- mirrors
// frame_execution_gpu_tests.cpp's own structure. Every case here carries
// the "gpu" CTest label (Section 17's catch_discover_tests()).
//
// What this file can and cannot confirm about depth occlusion: it
// confirms every API call succeeds and Validation Layers report zero
// warnings/errors -- it does not read back or inspect rendered pixels,
// so it cannot by itself confirm the depth test actually *behaved*
// correctly. That confirmation is the manual demo's job (Plan 0007
// Section 15), not this automated test's.
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

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
using atlantis::rhi::Presentation;
using atlantis::rhi::RenderTarget;
using atlantis::rhi::VertexAttribute;
using atlantis::rhi::VertexAttributeFormat;
using atlantis::rhi::VertexInputLayout;
using atlantis::vulkan_backend::createDevice;
using atlantis::vulkan_backend::createPresentation;
using atlantis::vulkan_backend::DeviceCreateParams;

class PlatformLifecycleGuard {
 public:
  PlatformLifecycleGuard() = default;
  ~PlatformLifecycleGuard() {
    if (!shutDown_) {
      atlantis::platform::shutdown();
    }
  }

  PlatformLifecycleGuard(const PlatformLifecycleGuard&) = delete;
  PlatformLifecycleGuard& operator=(const PlatformLifecycleGuard&) = delete;

  void markShutDown() { shutDown_ = true; }

 private:
  bool shutDown_ = false;
};

// Plan 0007 Section 12: plain std::ifstream over a relative path, no
// Core/Platform path-resolution API. Returns an empty optional on any
// failure to open or read the file.
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

[[nodiscard]] VertexInputLayout minimalMeshVertexLayout() {
  VertexInputLayout layout;
  layout.strideBytes = sizeof(Vertex);
  layout.attributes = {
      VertexAttribute{.location = 0, .offsetBytes = offsetof(Vertex, position), .format = VertexAttributeFormat::Float3},
      VertexAttribute{.location = 1, .offsetBytes = offsetof(Vertex, color), .format = VertexAttributeFormat::Float3},
  };
  return layout;
}

// A single flat triangle -- this round's minimal mesh, sufficient to
// exercise the full draw path (Spec 0007 Non-Goals: no scene graph/asset
// system, one fixed mesh shape).
constexpr Vertex kTriangleVertices[3] = {
    {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
};
constexpr std::uint16_t kTriangleIndices[3] = {0, 1, 2};

// Column-major 4x4 identity -- sufficient for this test's own purpose
// (exercising the push-constant/uniform-binding path, not real camera
// math).
constexpr std::array<float, 16> kIdentityMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

}  // namespace

TEST_CASE("Buffer/Texture/Pipeline creation and destruction", "[vulkan_backend][renderer][gpu][integration]") {
  auto deviceResult =
      createDevice(DeviceCreateParams{.applicationName = "Atlantis Minimal Renderer GPU Tests (resources)",
                                       .enableValidationLayers = true});
  if (deviceResult.isErr()) {
    FAIL("createDevice() failed");
  }
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  SECTION("A Buffer of each purpose can be created and destroyed") {
    for (const BufferPurpose purpose : {BufferPurpose::Vertex, BufferPurpose::Index, BufferPurpose::Uniform}) {
      auto bufferResult = device->createBuffer({.purpose = purpose, .sizeBytes = 256});
      REQUIRE(bufferResult.isOk());
      REQUIRE(bufferResult.value()->purpose() == purpose);
      REQUIRE(bufferResult.value()->sizeBytes() == 256);
      REQUIRE(bufferResult.value()->mappedData() != nullptr);
      // bufferResult.value() destroyed here, at end of loop iteration.
    }
  }

  SECTION("A depth Texture can be created and destroyed, including at a resized extent") {
    auto textureResult = device->createTexture({.extent = Extent2D{256, 256}, .format = DepthFormat::D32Sfloat});
    REQUIRE(textureResult.isOk());
    REQUIRE(textureResult.value()->extent().width == 256);
    REQUIRE(textureResult.value()->extent().height == 256);
    textureResult.value().reset();

    auto resizedResult = device->createTexture({.extent = Extent2D{512, 384}, .format = DepthFormat::D32Sfloat});
    REQUIRE(resizedResult.isOk());
    REQUIRE(resizedResult.value()->extent().width == 512);
    REQUIRE(resizedResult.value()->extent().height == 384);
  }

  SECTION("A Pipeline can be created from the checked-in SPIR-V pair, and destroyed without error") {
    const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
    const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
    REQUIRE(vertexSpirv.has_value());
    REQUIRE(fragmentSpirv.has_value());

    auto pipelineResult = device->createPipeline(
        {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
         .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
         .vertexInputLayout = minimalMeshVertexLayout(),
         .colorFormat = atlantis::rhi::Format::Bgra8Unorm,
         .depthFormat = DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = sizeof(float) * 16});
    REQUIRE(pipelineResult.isOk());
    // pipelineResult.value() destroyed at end of SECTION -- its one
    // VkDescriptorSet is freed from VulkanDevice's pool without error
    // (validation would flag a leak/misuse otherwise).
  }

  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("Renderer::drawFrame() draws a real, multi-item frame through a real acquired RenderTarget",
          "[vulkan_backend][renderer][gpu][integration]") {
  const auto initResult = atlantis::platform::initialize();
  if (initResult.isErr()) {
    FAIL("platform::initialize() failed");
  }
  PlatformLifecycleGuard platformGuard;

  const auto initialBatch = atlantis::platform::processEvents();
  REQUIRE(initialBatch.size() >= 1);
  REQUIRE(std::holds_alternative<atlantis::platform::SurfaceCreated>(initialBatch[0]));
  const atlantis::platform::NativeWindowHandle windowHandle =
      std::get<atlantis::platform::SurfaceCreated>(initialBatch[0]).handle;
  const HWND hwnd = reinterpret_cast<HWND>(windowHandle.value0);
  REQUIRE(hwnd != nullptr);

  auto deviceResult = createDevice(DeviceCreateParams{
      .applicationName = "Atlantis Minimal Renderer GPU Tests (draw)", .enableValidationLayers = true});
  if (deviceResult.isErr()) {
    FAIL("createDevice() failed");
  }
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  auto presentationResult = createPresentation(*device, windowHandle);
  if (presentationResult.isErr()) {
    FAIL("createPresentation() failed");
  }
  std::unique_ptr<Presentation> presentation = std::move(presentationResult.value());

  SetWindowPos(hwnd, nullptr, 0, 0, 400, 300, SWP_NOMOVE | SWP_NOZORDER);
  Extent2D framebuffer{400, 300};
  for (int i = 0; i < 8; ++i) {
    const auto events = atlantis::platform::processEvents();
    for (const auto& event : events) {
      if (const auto* resize = std::get_if<atlantis::platform::WindowResize>(&event)) {
        if (!resize->framebuffer.isZero()) {
          framebuffer = Extent2D{resize->framebuffer.width, resize->framebuffer.height};
        }
      }
    }
  }
  presentation->notifyResized(framebuffer);

  // Lifetime precondition (Plan 0007 Section 14): every Buffer/Texture/
  // Pipeline a Device backs must be destroyed before the Device itself.
  // mesh/material/depthTexture/cameraBuffer/target/commandList are all
  // scoped to this nested block, declared after device/presentation
  // above -- so they are all destroyed, in reverse declaration order, at
  // this block's closing brace below, strictly before
  // presentation.reset()/device.reset() run afterward.
  {
    const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
    const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
    REQUIRE(vertexSpirv.has_value());
    REQUIRE(fragmentSpirv.has_value());

    auto meshResult = createMesh(*device, minimalMeshVertexLayout(), kTriangleVertices, sizeof(kTriangleVertices),
                                  kTriangleIndices, 3);
    REQUIRE(meshResult.isOk());
    Mesh mesh = std::move(meshResult.value());

  // Presentation::metadata() is only meaningful once the swapchain has
  // actually been (re)created -- folded into acquireNextTarget()'s own
  // internal recreateIfNeeded() call (ADR-0019) -- so the real,
  // just-acquired RenderTarget's own format() (Section 13's exact
  // pattern) is used here, not metadata() read before any acquire.
  auto acquireResult = presentation->acquireNextTarget();
  REQUIRE(acquireResult.isOk());
  REQUIRE(acquireResult.value() != nullptr);
  std::unique_ptr<RenderTarget> target = std::move(acquireResult.value());

  auto materialResult = createMaterial(
      *device, {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
                .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
                .vertexInputLayout = minimalMeshVertexLayout(),
                .colorFormat = target->format(),
                .depthFormat = DepthFormat::D32Sfloat,
                .pushConstantSizeBytes = sizeof(float) * 16});
  REQUIRE(materialResult.isOk());
  Material material = std::move(materialResult.value());

  auto depthTextureResult = device->createTexture({.extent = target->extent(), .format = DepthFormat::D32Sfloat});
  REQUIRE(depthTextureResult.isOk());
  std::unique_ptr<atlantis::rhi::Texture> depthTexture = std::move(depthTextureResult.value());

  auto cameraBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  REQUIRE(cameraBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer = std::move(cameraBufferResult.value());
  {
    // view = identity, projection = identity -- sufficient to exercise the
    // binding path; correctness of the actual projected image is the
    // manual demo's job (Section 15).
    auto* cameraData = static_cast<float*>(cameraBuffer->mappedData());
    for (int i = 0; i < 16; ++i) cameraData[i] = kIdentityMatrix[static_cast<std::size_t>(i)];
    for (int i = 0; i < 16; ++i) cameraData[16 + i] = kIdentityMatrix[static_cast<std::size_t>(i)];
  }

  DrawItem itemA;
  itemA.mesh = &mesh;
  itemA.material = &material;
  itemA.objectToWorld = kIdentityMatrix;

  DrawItem itemB = itemA;
  itemB.objectToWorld[12] = 0.25f;  // distinct translation -- second, independent DrawItem

  const std::vector<DrawItem> drawItems{itemA, itemB};
  Renderer renderer;

  auto commandListResult = device->createCommandList();
  REQUIRE(commandListResult.isOk());
  std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

  renderer.drawFrame(*commandList, *target, *depthTexture, *cameraBuffer, drawItems);

  auto submitResult = device->submit(std::move(commandList), *target);
  REQUIRE(submitResult.isOk());

  auto presentResult = presentation->present(std::move(target), std::move(submitResult.value()));
  REQUIRE(presentResult.isOk());

  REQUIRE(device->waitIdle().isOk());
  }  // end of the nested block opened above -- mesh/material/depthTexture/
     // cameraBuffer/target/commandList are all destroyed here, strictly
     // before presentation.reset()/device.reset() below (Plan 0007
     // Section 14's Device-outlives-everything-it-backed precondition).

  presentation.reset();
  device.reset();
  atlantis::platform::shutdown();
  platformGuard.markShutDown();
  static_cast<void>(atlantis::platform::processEvents());
}

#endif  // defined(_WIN32)
