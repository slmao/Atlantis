#include <atlantis/log.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/renderer/renderer.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/hdr_color_target.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

// Spec 0010's non-shipping headless verification composition (Plan 0010
// Section 7.1). Not the Atlantis Runtime module and not a preview of it.
// No atlantis::platform::initialize() call anywhere in this file, no
// atlantis::platform::* symbol referenced, no Presentation/
// VkSwapchainKHR/VkSurfaceKHR anywhere -- the exact same
// Renderer -> RenderGraph -> RHI -> Vulkan Backend stack
// examples/minimal_renderer_demo uses draws and reads back a frame with
// no window involved at all. Runs the acquire -> write-camera ->
// createCommandList() -> Renderer::drawFrame(finalColorState =
// TransferSource) -> caller-built copy-pass graph -> submit() ->
// waitIdle() -> read -> content-check -> drop borrow -> loop cycle from
// Spec 0010's own flow diagram, three times against the same, still-live
// OffscreenTarget (Spec 0010 Requirements: may be acquired-and-borrowed
// more than once, each cycle independent).

namespace {

using atlantis::renderer::createMaterial;
using atlantis::renderer::createMesh;
using atlantis::renderer::DrawItem;
using atlantis::renderer::Material;
using atlantis::renderer::Mesh;
using atlantis::renderer::Renderer;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Extent2D;
using atlantis::rhi::Format;
using atlantis::rhi::OffscreenTargetCreateParams;
using atlantis::rhi::VertexInputLayout;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;

// Fixed, candidate resolution/format for this demo's own OffscreenTarget
// -- confirmable at Plan Review, not a general RHI/Renderer limitation.
constexpr std::uint32_t kExtentPixels = 512;
constexpr int kCycleCount = 3;

[[nodiscard]] const char* deviceCreateErrorToString(atlantis::vulkan_backend::DeviceCreateError error) {
  using atlantis::vulkan_backend::DeviceCreateError;
  switch (error) {
    case DeviceCreateError::InstanceCreationFailed:
      return "InstanceCreationFailed";
    case DeviceCreateError::ValidationLayerUnavailable:
      return "ValidationLayerUnavailable";
    case DeviceCreateError::NoSuitablePhysicalDevice:
      return "NoSuitablePhysicalDevice";
    case DeviceCreateError::DeviceCreationFailed:
      return "DeviceCreationFailed";
    case DeviceCreateError::DynamicRenderingUnavailable:
      return "DynamicRenderingUnavailable";
  }
  return "(unrecognized DeviceCreateError)";
}

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

// Identical fixed cube to examples/minimal_renderer_demo's own fixture --
// duplicated, not shared (Plan 0010 Section 7.1's own default, matching
// every prior example's self-contained style).
constexpr Vertex kCubeVertices[8] = {
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 0.0f}}, {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}},   {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},  {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 1.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}},    {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 1.0f}},
};

constexpr std::uint16_t kCubeIndices[36] = {
    0, 1, 2, 2, 3, 0,  // back  (z = -0.5)
    5, 4, 7, 7, 6, 5,  // front (z = +0.5)
    4, 0, 3, 3, 7, 4,  // left  (x = -0.5)
    1, 5, 6, 6, 2, 1,  // right (x = +0.5)
    4, 5, 1, 1, 0, 4,  // bottom (y = -0.5)
    3, 2, 6, 6, 7, 3,  // top    (y = +0.5)
};

using Mat4 = std::array<float, 16>;

[[nodiscard]] Mat4 identityMatrix() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// Fixed camera -- the same eye/look-at examples/minimal_renderer_demo's
// own orbit camera uses at angle 0 (eyeX=0, eyeY=1.5, eyeZ=2.5, looking
// at the origin), known to frame the cube centered in view.
[[nodiscard]] Mat4 lookAt(float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ) {
  float fx = centerX - eyeX, fy = centerY - eyeY, fz = centerZ - eyeZ;
  const float fLen = std::sqrt(fx * fx + fy * fy + fz * fz);
  fx /= fLen;
  fy /= fLen;
  fz /= fLen;

  const float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
  float sx = fy * upZ - fz * upY;
  float sy = fz * upX - fx * upZ;
  float sz = fx * upY - fy * upX;
  const float sLen = std::sqrt(sx * sx + sy * sy + sz * sz);
  sx /= sLen;
  sy /= sLen;
  sz /= sLen;

  const float ux = sy * fz - sz * fy;
  const float uy = sz * fx - sx * fz;
  const float uz = sx * fy - sy * fx;

  Mat4 result = identityMatrix();
  result[0] = sx;
  result[4] = sy;
  result[8] = sz;
  result[1] = ux;
  result[5] = uy;
  result[9] = uz;
  result[2] = -fx;
  result[6] = -fy;
  result[10] = -fz;
  result[12] = -(sx * eyeX + sy * eyeY + sz * eyeZ);
  result[13] = -(ux * eyeX + uy * eyeY + uz * eyeZ);
  result[14] = fx * eyeX + fy * eyeY + fz * eyeZ;
  return result;
}

[[nodiscard]] Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
  const float f = 1.0f / std::tan(fovYRadians * 0.5f);
  Mat4 result{};
  result[0] = f / aspect;
  result[5] = -f;
  result[10] = farZ / (nearZ - farZ);
  result[11] = -1.0f;
  result[14] = (nearZ * farZ) / (nearZ - farZ);
  return result;
}

struct Rgba8 {
  std::uint8_t r, g, b, a;
};

[[nodiscard]] Rgba8 pixelAt(const std::uint8_t* data, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
  const std::uint8_t* p = data + (static_cast<std::size_t>(y) * width + x) * 4;
  return Rgba8{p[0], p[1], p[2], p[3]};
}

// kBackgroundClearColor (renderer.cpp, private to Renderer): {0.05, 0.05,
// 0.08, 1.0} -- reproduced here only as the small, fixed set of known
// 8-bit-UNORM-quantized sample values this demo's own content check
// compares against (Spec 0010 Requirements: "in the direction the fixed
// mesh/camera/material fixture predicts"), not as a shared constant.
// Plan 0024 Milestone 6/7 (ADR-0068 D-5/D-6): this fixed linear color is
// now cleared into the HDR intermediate, then carried through the
// output-transform pass's own floor/exposure/Reinhard tone-mapping and
// explicit sRGB OETF before landing in this demo's own Rgba8Unorm
// OffscreenTarget -- no longer a raw, unencoded linear-to-byte
// quantization. Recomputed: Reinhard(0.05) = 0.05/1.05 = 0.047619,
// sRGB-encoded = 0.241707 -> round(*255) = 62; Reinhard(0.08) =
// 0.08/1.08 = 0.074074, sRGB-encoded = 0.301681 -> round(*255) = 77.
[[nodiscard]] bool closeToBackground(const Rgba8& pixel) {
  constexpr int kTolerance = 12;
  constexpr int kBackgroundR = 62;
  constexpr int kBackgroundG = 62;
  constexpr int kBackgroundB = 77;
  return std::abs(static_cast<int>(pixel.r) - kBackgroundR) <= kTolerance &&
         std::abs(static_cast<int>(pixel.g) - kBackgroundG) <= kTolerance &&
         std::abs(static_cast<int>(pixel.b) - kBackgroundB) <= kTolerance;
}

// A "reproducible basic content check" (Spec 0010 Requirements): not
// uniformly one color/all-zero -- the center pixel (expect mesh-colored,
// non-background) and all four corner pixels (expect background-clear-
// colored) must differ in the direction the fixed mesh/camera/material
// fixture predicts.
[[nodiscard]] bool basicContentCheckPasses(const std::uint8_t* data, std::uint32_t width, std::uint32_t height) {
  const Rgba8 center = pixelAt(data, width, width / 2, height / 2);
  if (closeToBackground(center)) {
    ATLANTIS_LOG_ERROR("Content check failed: center pixel ({},{},{},{}) is background-colored, expected the cube",
                        center.r, center.g, center.b, center.a);
    return false;
  }

  const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> corners{{
      {0, 0},
      {width - 1, 0},
      {0, height - 1},
      {width - 1, height - 1},
  }};
  for (const auto& [x, y] : corners) {
    const Rgba8 corner = pixelAt(data, width, x, y);
    if (!closeToBackground(corner)) {
      ATLANTIS_LOG_ERROR("Content check failed: corner ({},{}) pixel ({},{},{},{}) is not background-colored", x, y,
                          corner.r, corner.g, corner.b, corner.a);
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  namespace rhi = atlantis::rhi;
  namespace vulkan_backend = atlantis::vulkan_backend;

  atlantis::log::setMinLevel(atlantis::LogLevel::Info);

  ATLANTIS_LOG_INFO("Atlantis Headless Rendering demo starting");
  ATLANTIS_LOG_INFO(
      "Spec 0010's non-shipping headless verification composition -- no window, no Presentation, no "
      "VkSwapchainKHR anywhere. Runs {} independent acquire/draw/copy/readback cycles against one OffscreenTarget.",
      kCycleCount);
  ATLANTIS_LOG_INFO(
      "Must be launched with its own build output directory as the current working directory (plain relative "
      "shader path) -- use the 'run_headless_rendering_demo' CMake target.");

  const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
  const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
  if (!vertexSpirv.has_value() || !fragmentSpirv.has_value()) {
    ATLANTIS_LOG_ERROR(
        "Failed to load shaders/minimal_mesh.{{vert,frag}}.spv -- run this demo via the "
        "run_headless_rendering_demo CMake target, or from its own build output directory.");
    return EXIT_FAILURE;
  }

  auto vertexReflectionResult = loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
  if (vertexReflectionResult.isErr()) {
    ATLANTIS_LOG_ERROR("Failed to load shaders/minimal_mesh.vert.refl.json");
    return EXIT_FAILURE;
  }
  const auto vertexInputLayout = minimalMeshVertexLayout(vertexReflectionResult.value());
  if (!vertexInputLayout.has_value()) {
    ATLANTIS_LOG_ERROR(
        "minimalMeshVertexLayout(): reflected vertex-input attributes do not match this demo's own Vertex schema");
    return EXIT_FAILURE;
  }

  // Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3/D-6): the output-transform-
  // unorm shader pair -- this demo's own colorFormat is fixed
  // Rgba8Unorm, so only this one variant is ever loaded.
  const auto outputTransformVertexSpirv = loadSpirvFile("shaders/output_transform_unorm.vert.spv");
  const auto outputTransformFragmentSpirv = loadSpirvFile("shaders/output_transform_unorm.frag.spv");
  if (!outputTransformVertexSpirv.has_value() || !outputTransformFragmentSpirv.has_value()) {
    ATLANTIS_LOG_ERROR("Failed to load shaders/output_transform_unorm.{{vert,frag}}.spv");
    return EXIT_FAILURE;
  }
  auto outputTransformVertexReflectionResult = loadReflectionMetadata("shaders/output_transform_unorm.vert.refl.json");
  if (outputTransformVertexReflectionResult.isErr()) {
    ATLANTIS_LOG_ERROR("Failed to load shaders/output_transform_unorm.vert.refl.json");
    return EXIT_FAILURE;
  }
  const auto outputTransformVertexInputLayout =
      outputTransformVertexLayout(outputTransformVertexReflectionResult.value());
  if (!outputTransformVertexInputLayout.has_value()) {
    ATLANTIS_LOG_ERROR(
        "outputTransformVertexLayout(): reflected vertex-input attributes do not match the fullscreen-triangle "
        "schema");
    return EXIT_FAILURE;
  }

  // No atlantis::platform::initialize() call -- this composition never
  // needs a window, a message pump, or any Platform symbol.
  auto deviceResult = vulkan_backend::createDevice(
      {.applicationName = "Atlantis Headless Rendering Demo", .enableValidationLayers = true});
  if (deviceResult.isErr()) {
    ATLANTIS_LOG_ERROR("createDevice() failed: {}", deviceCreateErrorToString(deviceResult.error()));
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Device> device = std::move(deviceResult.value());
  ATLANTIS_LOG_INFO("Vulkan Device created (Validation Layers requested)");

  auto meshResult = createMesh(*device, *vertexInputLayout, kCubeVertices, sizeof(kCubeVertices), kCubeIndices,
                                static_cast<std::uint32_t>(std::size(kCubeIndices)));
  if (meshResult.isErr()) {
    ATLANTIS_LOG_ERROR("createMesh() failed");
    device.reset();
    return EXIT_FAILURE;
  }
  std::optional<Mesh> mesh = std::move(meshResult.value());
  ATLANTIS_LOG_INFO("Mesh created (cube, 8 vertices, 36 indices)");

  auto cameraBufferResult = device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  if (cameraBufferResult.isErr()) {
    ATLANTIS_LOG_ERROR("createBuffer() (camera uniform) failed");
    mesh.reset();
    device.reset();
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Buffer> cameraBuffer = std::move(cameraBufferResult.value());

  constexpr Format kColorFormat = Format::Rgba8Unorm;
  constexpr Extent2D kExtent{kExtentPixels, kExtentPixels};
  constexpr std::size_t kReadbackSizeBytes = static_cast<std::size_t>(kExtentPixels) * kExtentPixels * 4;

  auto materialResult = createMaterial(
      *device, {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
                .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
                .vertexInputLayout = *vertexInputLayout,
                // Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3): every
                // geometry Pipeline now renders into the fixed HDR
                // intermediate, never this demo's own real kColorFormat
                // directly.
                .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
                .depthFormat = DepthFormat::D32Sfloat,
                .pushConstantSizeBytes = sizeof(float) * 16});
  if (materialResult.isErr()) {
    ATLANTIS_LOG_ERROR("createMaterial() failed");
    cameraBuffer.reset();
    mesh.reset();
    device.reset();
    return EXIT_FAILURE;
  }
  std::optional<Material> material = std::move(materialResult.value());
  ATLANTIS_LOG_INFO("Material created (format={})", static_cast<int>(kColorFormat));

  auto depthTextureResult = device->createTexture({.extent = kExtent, .format = DepthFormat::D32Sfloat});
  if (depthTextureResult.isErr()) {
    ATLANTIS_LOG_ERROR("createTexture() (depth) failed");
    material.reset();
    cameraBuffer.reset();
    mesh.reset();
    device.reset();
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Texture> depthTexture = std::move(depthTextureResult.value());

  auto offscreenTargetResult =
      device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kExtent, .format = kColorFormat});
  if (offscreenTargetResult.isErr()) {
    ATLANTIS_LOG_ERROR("createOffscreenTarget() failed");
    depthTexture.reset();
    material.reset();
    cameraBuffer.reset();
    mesh.reset();
    device.reset();
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::OffscreenTarget> offscreenTarget = std::move(offscreenTargetResult.value());
  ATLANTIS_LOG_INFO("OffscreenTarget created ({}x{}, format={})", kExtent.width, kExtent.height,
                     static_cast<int>(kColorFormat));

  auto readbackBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = kReadbackSizeBytes});
  if (readbackBufferResult.isErr()) {
    ATLANTIS_LOG_ERROR("createBuffer() (readback) failed");
    offscreenTarget.reset();
    depthTexture.reset();
    material.reset();
    cameraBuffer.reset();
    mesh.reset();
    device.reset();
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Buffer> readbackBuffer = std::move(readbackBufferResult.value());

  // Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3/D-6): this demo's own HDR
  // intermediate, fullscreen-triangle geometry/sampler, and output-
  // transform Pipeline -- created once, alongside every other resource
  // above.
  auto hdrColorTargetResult = device->createHdrColorTarget({.extent = kExtent});
  if (hdrColorTargetResult.isErr()) {
    ATLANTIS_LOG_ERROR("createHdrColorTarget() failed");
    readbackBuffer.reset();
    offscreenTarget.reset();
    depthTexture.reset();
    material.reset();
    cameraBuffer.reset();
    mesh.reset();
    device.reset();
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::HdrColorTarget> hdrColorTarget = std::move(hdrColorTargetResult.value());

  const float fullscreenTriangleVertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  auto fullscreenTriangleVertexBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  if (fullscreenTriangleVertexBufferResult.isErr()) {
    ATLANTIS_LOG_ERROR("createBuffer() (fullscreen-triangle vertex) failed");
    hdrColorTarget.reset();
    readbackBuffer.reset();
    offscreenTarget.reset();
    depthTexture.reset();
    material.reset();
    cameraBuffer.reset();
    mesh.reset();
    device.reset();
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Buffer> fullscreenTriangleVertexBuffer = std::move(fullscreenTriangleVertexBufferResult.value());
  std::memcpy(fullscreenTriangleVertexBuffer->mappedData(), fullscreenTriangleVertices,
              sizeof(fullscreenTriangleVertices));

  const std::uint16_t fullscreenTriangleIndices[3] = {0, 1, 2};
  auto fullscreenTriangleIndexBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Index, .sizeBytes = sizeof(fullscreenTriangleIndices)});
  if (fullscreenTriangleIndexBufferResult.isErr()) {
    ATLANTIS_LOG_ERROR("createBuffer() (fullscreen-triangle index) failed");
    fullscreenTriangleVertexBuffer.reset();
    hdrColorTarget.reset();
    readbackBuffer.reset();
    offscreenTarget.reset();
    depthTexture.reset();
    material.reset();
    cameraBuffer.reset();
    mesh.reset();
    device.reset();
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Buffer> fullscreenTriangleIndexBuffer = std::move(fullscreenTriangleIndexBufferResult.value());
  std::memcpy(fullscreenTriangleIndexBuffer->mappedData(), fullscreenTriangleIndices,
              sizeof(fullscreenTriangleIndices));

  auto outputTransformSamplerResult = device->createSampler(
      {.filter = atlantis::rhi::Filter::Linear, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  if (outputTransformSamplerResult.isErr()) {
    ATLANTIS_LOG_ERROR("createSampler() (output-transform) failed");
    fullscreenTriangleIndexBuffer.reset();
    fullscreenTriangleVertexBuffer.reset();
    hdrColorTarget.reset();
    readbackBuffer.reset();
    offscreenTarget.reset();
    depthTexture.reset();
    material.reset();
    cameraBuffer.reset();
    mesh.reset();
    device.reset();
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Sampler> outputTransformSampler = std::move(outputTransformSamplerResult.value());

  auto outputTransformPipelineResult = device->createPipeline(
      {.vertexShader = {.spirvWords = outputTransformVertexSpirv->data(),
                         .wordCount = outputTransformVertexSpirv->size()},
       .fragmentShader = {.spirvWords = outputTransformFragmentSpirv->data(),
                           .wordCount = outputTransformFragmentSpirv->size()},
       .vertexInputLayout = *outputTransformVertexInputLayout,
       .colorFormat = kColorFormat,
       .sampledTextureBindingCount = 1,
       .hasCameraUniformBinding = false,
       .hasDepthAttachment = false});
  if (outputTransformPipelineResult.isErr()) {
    ATLANTIS_LOG_ERROR("createPipeline() (output-transform) failed");
    outputTransformSampler.reset();
    fullscreenTriangleIndexBuffer.reset();
    fullscreenTriangleVertexBuffer.reset();
    hdrColorTarget.reset();
    readbackBuffer.reset();
    offscreenTarget.reset();
    depthTexture.reset();
    material.reset();
    cameraBuffer.reset();
    mesh.reset();
    device.reset();
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Pipeline> outputTransformPipeline = std::move(outputTransformPipelineResult.value());

  Renderer renderer;
  bool failed = false;
  int cyclesCompleted = 0;

  const Mat4 view = lookAt(0.0f, 1.5f, 2.5f, 0.0f, 0.0f, 0.0f);
  const Mat4 projection = perspective(60.0f * 3.14159265f / 180.0f, 1.0f, 0.1f, 100.0f);

  for (int cycle = 0; cycle < kCycleCount && !failed; ++cycle) {
    auto acquireResult = offscreenTarget->acquireTarget();
    if (acquireResult.isErr()) {
      ATLANTIS_LOG_ERROR("acquireTarget() failed on cycle {}", cycle);
      failed = true;
      break;
    }
    std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

    auto* cameraData = static_cast<float*>(cameraBuffer->mappedData());
    for (std::size_t i = 0; i < 16; ++i) cameraData[i] = view[i];
    for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = projection[i];

    DrawItem item;
    item.mesh = &*mesh;
    item.material = &*material;
    item.objectToWorld = identityMatrix();
    const std::array<DrawItem, 1> drawItems{item};

    auto commandListResult = device->createCommandList();
    if (commandListResult.isErr()) {
      ATLANTIS_LOG_ERROR("createCommandList() failed on cycle {}", cycle);
      failed = true;
      break;
    }
    std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

    // Renderer::drawFrame() builds/compiles/executes its own internal
    // one-pass RenderGraph, leaving colorTarget in TransferSource
    // (Spec 0010/ADR-0022 Accepted Amendment) -- no intermediate,
    // meaningless presentation-shaped layout is ever recorded.
    renderer.drawFrame(*commandList, *target, *depthTexture, *cameraBuffer, drawItems,
                        atlantis::rhi::ResourceState::TransferSource, *hdrColorTarget,
                        *fullscreenTriangleVertexBuffer, *fullscreenTriangleIndexBuffer, *outputTransformPipeline,
                        *outputTransformSampler);

    // Caller-built copy-pass graph (Spec 0010's own flow) -- separate
    // from, and executed after, Renderer::drawFrame()'s own internal
    // graph, into the same CommandList. incomingState = TransferSource
    // matches exactly what drawFrame() already left the target in, so
    // execute() inserts no redundant transition (headless_binding_tests.cpp
    // items 2/5's own regression coverage for this exact case).
    atlantis::render_graph::RenderGraphBuilder copyBuilder;
    const auto copyResource = copyBuilder.declareResource("color-copy");
    const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
    copyBuilder.writes(copyPass, copyResource, atlantis::rhi::ResourceState::TransferSource);
    copyBuilder.setExecute(copyPass, [&target, &readbackBuffer](atlantis::rhi::CommandList& cmd) {
      cmd.copyRenderTargetToBuffer(*target, *readbackBuffer);
    });
    auto copyCompileResult = copyBuilder.compile();
    if (copyCompileResult.isErr()) {
      ATLANTIS_LOG_ERROR("copy-pass RenderGraphBuilder::compile() failed on cycle {}", cycle);
      failed = true;
      break;
    }
    const std::vector<atlantis::render_graph::ResourceBinding> copyBindings{
        {.resource = copyCompileResult.value().resourceAt(0),
         .target = target.get(),
         .incomingState = atlantis::rhi::ResourceState::TransferSource}};
    atlantis::render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

    auto submitResult = device->submit(std::move(commandList), *target);
    if (submitResult.isErr()) {
      ATLANTIS_LOG_ERROR("submit() failed on cycle {}", cycle);
      failed = true;
      break;
    }

    // Recommended flow (ADR-0038): waitIdle() before reading the readback
    // Buffer's contents, before dropping the borrow -- satisfies both the
    // borrow-wrapper minimum lifetime (already satisfied at submit()'s
    // return) and OffscreenTarget's own GPU-completion destruction
    // precondition in the simplest order to reason about.
    auto waitResult = device->waitIdle();
    if (waitResult.isErr()) {
      ATLANTIS_LOG_ERROR("waitIdle() failed on cycle {}", cycle);
      failed = true;
      break;
    }

    const auto* readbackData = static_cast<const std::uint8_t*>(readbackBuffer->mappedData());
    const bool contentOk = basicContentCheckPasses(readbackData, kExtent.width, kExtent.height);
    ATLANTIS_LOG_INFO("Cycle {}: readback content check {}", cycle, contentOk ? "PASSED" : "FAILED");
    if (!contentOk) {
      failed = true;
      break;
    }

    target.reset();  // Ends this cycle's borrow (RAII, ADR-0038) -- no release()/consume() call.
    ++cyclesCompleted;
  }

  // Every exit path (success or failure): waitIdle() before destroying
  // OffscreenTarget -- matches ADR-0038's documented destruction
  // precondition and correct-order requirement exactly, including a
  // failure path where the loop broke before its own per-cycle waitIdle()
  // ran.
  const auto finalWaitResult = device->waitIdle();
  if (finalWaitResult.isErr()) {
    ATLANTIS_LOG_ERROR("Final waitIdle() before teardown failed");
    failed = true;
  }

  // Lifetime precondition (ADR-0003/0038): every Buffer/Texture/
  // OffscreenTarget/Pipeline this Device backed is destroyed before the
  // Device itself.
  outputTransformPipeline.reset();
  outputTransformSampler.reset();
  fullscreenTriangleIndexBuffer.reset();
  fullscreenTriangleVertexBuffer.reset();
  hdrColorTarget.reset();
  readbackBuffer.reset();
  offscreenTarget.reset();
  depthTexture.reset();
  material.reset();
  cameraBuffer.reset();
  mesh.reset();
  device.reset();

  if (failed || cyclesCompleted != kCycleCount) {
    ATLANTIS_LOG_ERROR("Atlantis Headless Rendering demo finished with failures ({} of {} cycles completed)",
                        cyclesCompleted, kCycleCount);
    return EXIT_FAILURE;
  }

  ATLANTIS_LOG_INFO("Atlantis Headless Rendering demo finished after {} successful cycles", cyclesCompleted);
  return EXIT_SUCCESS;
}
