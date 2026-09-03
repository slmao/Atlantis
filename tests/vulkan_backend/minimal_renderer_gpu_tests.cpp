#include <atlantis/platform/platform.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/hdr_color_target.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/presentation.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
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
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
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
using atlantis::rhi::VertexInputLayout;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;
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

// Plan 0008 Section 8: replaces the hand-written minimalMeshVertexLayout()
// literal -- `vertexMetadata` is loaded once per TEST_CASE from the
// build-tree reflection JSON the Shader System pipeline produced.
[[nodiscard]] std::optional<VertexInputLayout> minimalMeshVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, color)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// Plan 0024 Milestone 6/7: the output-transform pass's own fixed vertex
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

// Plan 0024 Milestone 6/7 (ADR-0068 D-6): mirrors
// material_realization.cpp's own isSrgbFormat() exactly -- duplicated,
// not shared (this file links neither Atlantis::RuntimeHost nor
// material_realization.cpp's own translation unit).
[[nodiscard]] bool isSrgbFormat(atlantis::rhi::Format format) {
  switch (format) {
    case atlantis::rhi::Format::Unknown:
    case atlantis::rhi::Format::Bgra8Unorm:
    case atlantis::rhi::Format::Rgba8Unorm:
      return false;
    case atlantis::rhi::Format::Bgra8Srgb:
    case atlantis::rhi::Format::Rgba8Srgb:
      return true;
  }
  return false;
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

  SECTION("An HDR color target can be created and replaced at a resized extent") {
    auto targetResult = device->createHdrColorTarget({.extent = Extent2D{256, 256}});
    REQUIRE(targetResult.isOk());
    REQUIRE(targetResult.value()->extent().width == 256);
    REQUIRE(targetResult.value()->extent().height == 256);
    REQUIRE(targetResult.value()->format() == atlantis::rhi::HdrFormat::Rgba16Float);
    targetResult.value().reset();

    auto resizedResult = device->createHdrColorTarget({.extent = Extent2D{512, 384}});
    REQUIRE(resizedResult.isOk());
    REQUIRE(resizedResult.value()->extent().width == 512);
    REQUIRE(resizedResult.value()->extent().height == 384);
    REQUIRE(resizedResult.value()->format() == atlantis::rhi::HdrFormat::Rgba16Float);
  }

  SECTION("A Pipeline can be created from the Shader-System-produced SPIR-V pair, and destroyed without error") {
    const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
    const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
    REQUIRE(vertexSpirv.has_value());
    REQUIRE(fragmentSpirv.has_value());

    const auto vertexReflection = loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
    REQUIRE(vertexReflection.isOk());
    const auto vertexInputLayout = minimalMeshVertexLayout(vertexReflection.value());
    REQUIRE(vertexInputLayout.has_value());

    auto pipelineResult = device->createPipeline(
        {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
         .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
         .vertexInputLayout = *vertexInputLayout,
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

    const auto vertexReflection = loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
    REQUIRE(vertexReflection.isOk());
    const auto vertexInputLayout = minimalMeshVertexLayout(vertexReflection.value());
    REQUIRE(vertexInputLayout.has_value());

    auto meshResult = createMesh(*device, *vertexInputLayout, kTriangleVertices, sizeof(kTriangleVertices),
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
                .vertexInputLayout = *vertexInputLayout,
                // Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3): every
                // geometry Pipeline now renders into the fixed HDR
                // intermediate, never target->format() directly.
                .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
                .depthFormat = DepthFormat::D32Sfloat,
                .pushConstantSizeBytes = sizeof(float) * 16});
  REQUIRE(materialResult.isOk());
  Material material = std::move(materialResult.value());

  // Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3/D-6): this test's own HDR
  // intermediate, fullscreen-triangle geometry/sampler, and the ONE
  // output-transform Pipeline variant this real swapchain format
  // actually needs (isSrgbFormat(target->format()) selects it, mirroring
  // runtime_application.cpp's own identical selection -- no format-
  // change path exists in this single-draw test, so only one variant is
  // ever built).
  auto hdrColorTargetResult = device->createHdrColorTarget({.extent = target->extent()});
  REQUIRE(hdrColorTargetResult.isOk());
  std::unique_ptr<atlantis::rhi::HdrColorTarget> hdrColorTarget = std::move(hdrColorTargetResult.value());

  const float fullscreenTriangleVertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  auto fullscreenTriangleVertexBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  REQUIRE(fullscreenTriangleVertexBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleVertexBuffer =
      std::move(fullscreenTriangleVertexBufferResult.value());
  std::memcpy(fullscreenTriangleVertexBuffer->mappedData(), fullscreenTriangleVertices,
              sizeof(fullscreenTriangleVertices));

  const std::uint16_t fullscreenTriangleIndices[3] = {0, 1, 2};
  auto fullscreenTriangleIndexBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Index, .sizeBytes = sizeof(fullscreenTriangleIndices)});
  REQUIRE(fullscreenTriangleIndexBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleIndexBuffer =
      std::move(fullscreenTriangleIndexBufferResult.value());
  std::memcpy(fullscreenTriangleIndexBuffer->mappedData(), fullscreenTriangleIndices,
              sizeof(fullscreenTriangleIndices));

  auto outputTransformSamplerResult = device->createSampler(
      {.filter = atlantis::rhi::Filter::Linear, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  REQUIRE(outputTransformSamplerResult.isOk());
  std::unique_ptr<atlantis::rhi::Sampler> outputTransformSampler = std::move(outputTransformSamplerResult.value());

  const bool useSrgbVariant = isSrgbFormat(target->format());
  const std::string outputTransformShaderName = useSrgbVariant ? "output_transform_srgb" : "output_transform_unorm";
  const auto outputTransformVertexSpirv = loadSpirvFile(("shaders/" + outputTransformShaderName + ".vert.spv").c_str());
  const auto outputTransformFragmentSpirv =
      loadSpirvFile(("shaders/" + outputTransformShaderName + ".frag.spv").c_str());
  REQUIRE(outputTransformVertexSpirv.has_value());
  REQUIRE(outputTransformFragmentSpirv.has_value());
  const auto outputTransformVertexReflection =
      loadReflectionMetadata(("shaders/" + outputTransformShaderName + ".vert.refl.json").c_str());
  REQUIRE(outputTransformVertexReflection.isOk());
  const auto outputTransformVertexInputLayout = outputTransformVertexLayout(outputTransformVertexReflection.value());
  REQUIRE(outputTransformVertexInputLayout.has_value());

  auto outputTransformPipelineResult = device->createPipeline(
      {.vertexShader = {.spirvWords = outputTransformVertexSpirv->data(),
                         .wordCount = outputTransformVertexSpirv->size()},
       .fragmentShader = {.spirvWords = outputTransformFragmentSpirv->data(),
                           .wordCount = outputTransformFragmentSpirv->size()},
       .vertexInputLayout = *outputTransformVertexInputLayout,
       .colorFormat = target->format(),
       .sampledTextureBindingCount = 1,
       .hasCameraUniformBinding = false,
       .hasDepthAttachment = false});
  REQUIRE(outputTransformPipelineResult.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> outputTransformPipeline = std::move(outputTransformPipelineResult.value());

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

  renderer.drawFrame(*commandList, *target, *depthTexture, *cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::PresentSource, *hdrColorTarget, *fullscreenTriangleVertexBuffer,
                      *fullscreenTriangleIndexBuffer, *outputTransformPipeline, *outputTransformSampler);

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
