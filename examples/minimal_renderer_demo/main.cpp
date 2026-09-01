#include <atlantis/log.h>
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

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

// Spec 0007's non-shipping verification composition (Plan 0007 Section 15
// "Manual verification") -- mirrors examples/frame_execution_demo's own
// structure and disclaimer. This is NOT the Atlantis Runtime module and
// does not preview its future architecture. Every frame acquires a
// RenderTarget, implements Plan 0007 Section 13's resize/format-change
// contract, writes this frame's camera matrices, and calls
// Renderer::drawFrame() -- which itself builds/compiles/executes a
// one-pass RenderGraph. All GPU work goes through RenderGraph; no
// direct-submission bypass exists anywhere in this file.

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
using atlantis::rhi::VertexInputLayout;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;

[[nodiscard]] const char* platformErrorCodeToString(atlantis::platform::PlatformErrorCode code) {
  using atlantis::platform::PlatformErrorCode;
  switch (code) {
    case PlatformErrorCode::WindowClassRegistrationFailed:
      return "WindowClassRegistrationFailed";
    case PlatformErrorCode::WindowCreationFailed:
      return "WindowCreationFailed";
  }
  return "(unrecognized PlatformErrorCode)";
}

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

[[nodiscard]] const char* presentationCreateErrorToString(atlantis::vulkan_backend::PresentationCreateError error) {
  using atlantis::vulkan_backend::PresentationCreateError;
  switch (error) {
    case PresentationCreateError::SurfaceCreationFailed:
      return "SurfaceCreationFailed";
    case PresentationCreateError::UnsupportedDevice:
      return "UnsupportedDevice";
  }
  return "(unrecognized PresentationCreateError)";
}

[[nodiscard]] const char* presentationErrorToString(atlantis::rhi::PresentationError error) {
  using atlantis::rhi::PresentationError;
  switch (error) {
    case PresentationError::SurfaceLost:
      return "SurfaceLost";
    case PresentationError::SwapchainCreationFailed:
      return "SwapchainCreationFailed";
    case PresentationError::DeviceLost:
      return "DeviceLost";
    case PresentationError::Unknown:
      return "Unknown";
  }
  return "(unrecognized PresentationError)";
}

// Plan 0007 Section 12: plain std::ifstream over a relative path, no
// Core/Platform path-resolution API. Empty optional on any failure to
// open or read the file -- a recoverable, host-side I/O failure, never a
// crash, and never routed through PipelineCreateError (a distinct
// concern -- see Section 12).
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
// literal. `vertexMetadata` is loaded once, at startup, from the
// build-tree reflection JSON the Shader System pipeline produced --
// never per-frame (Section 6's own "program-startup-time, not per-frame"
// contract). A MappingError here can only mean this schema table itself
// is wrong (e.g. a location typo); the shader's own contract was already
// validated at build time.
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
// not shared (this demo links neither Atlantis::RuntimeHost, which
// "no other top-level module may depend on", runtime_application.h's
// own comment, nor material_realization.cpp's own translation unit).
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

// A single fixed cube -- this round's minimal mesh (Spec 0007 Non-Goals:
// no scene graph/asset system, one fixed mesh shape). Per-vertex color is
// the vertex's own position remapped to [0, 1], giving a visually
// distinct, recognizable RGB-gradient cube -- sufficient to confirm
// per-vertex shading and, together with the depth test, correct
// front/back occlusion as the camera orbits it.
constexpr Vertex kCubeVertices[8] = {
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 0.0f}}, {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}},   {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},  {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 1.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}},    {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 1.0f}},
};

// 12 triangles, 2 per face. Winding is not load-bearing: VulkanPipeline
// fixes VK_CULL_MODE_NONE (Section 10), so depth testing alone -- not
// triangle winding/culling -- is what makes occlusion correct here.
constexpr std::uint16_t kCubeIndices[36] = {
    0, 1, 2, 2, 3, 0,  // back  (z = -0.5)
    5, 4, 7, 7, 6, 5,  // front (z = +0.5)
    4, 0, 3, 3, 7, 4,  // left  (x = -0.5)
    1, 5, 6, 6, 2, 1,  // right (x = +0.5)
    4, 5, 1, 1, 0, 4,  // bottom (y = -0.5)
    3, 2, 6, 6, 7, 3,  // top    (y = +0.5)
};

// Column-major 4x4 float matrices, matching exactly what
// minimal_mesh.slang's `float4x4` uniforms/push constant expect
// (Plan 0008 Section 8) and what pushConstant()/the camera uniform Buffer copy
// verbatim (Section 5/9). Atlantis Core has no public math type yet
// (Section 11's own DrawItem comment) -- these are this demo's own,
// deliberately minimal, local helpers; not a general math library and
// not proposed as one.
using Mat4 = std::array<float, 16>;

[[nodiscard]] Mat4 identityMatrix() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

[[nodiscard]] Mat4 multiply(const Mat4& a, const Mat4& b) {
  Mat4 result{};
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) {
        sum += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(col * 4 + k)];
      }
      result[static_cast<std::size_t>(col * 4 + row)] = sum;
    }
  }
  return result;
}

// Right-handed look-at, matching the right-handed perspective projection
// below.
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

// Vulkan clip-space convention: right-handed, depth range [0, 1], Y
// flipped relative to the classic OpenGL convention (Vulkan's NDC has Y
// pointing down).
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

}  // namespace

int main() {
  namespace platform = atlantis::platform;
  namespace rhi = atlantis::rhi;
  namespace vulkan_backend = atlantis::vulkan_backend;
  namespace renderer_ns = atlantis::renderer;

  atlantis::log::setMinLevel(atlantis::LogLevel::Info);

  ATLANTIS_LOG_INFO("Atlantis Minimal Renderer demo starting");
  ATLANTIS_LOG_INFO(
      "This is Spec 0007's non-shipping verification composition, not the Atlantis Runtime module and not a "
      "preview of it. Every frame acquires a RenderTarget, applies Section 13's resize/format-change contract, "
      "writes this frame's camera matrices, and calls Renderer::drawFrame().");
  ATLANTIS_LOG_INFO(
      "Must be launched with its own build output directory as the current working directory (Section 12's plain "
      "relative shader path) -- use the 'run_minimal_renderer_demo' CMake target.");

  const auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
  const auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
  if (!vertexSpirv.has_value() || !fragmentSpirv.has_value()) {
    ATLANTIS_LOG_ERROR(
        "Failed to load shaders/minimal_mesh.{{vert,frag}}.spv -- run this demo via the "
        "run_minimal_renderer_demo CMake target, or from its own build output directory.");
    return EXIT_FAILURE;
  }

  // Plan 0008 Section 8: reflection metadata is loaded once, at
  // startup, from the same build-tree location the .spv files above
  // came from -- never per-frame (Section 6's own program-startup-time
  // contract).
  auto vertexReflectionResult = loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
  if (vertexReflectionResult.isErr()) {
    ATLANTIS_LOG_ERROR("Failed to load shaders/minimal_mesh.vert.refl.json");
    return EXIT_FAILURE;
  }
  const auto vertexInputLayout = minimalMeshVertexLayout(vertexReflectionResult.value());
  if (!vertexInputLayout.has_value()) {
    ATLANTIS_LOG_ERROR("minimalMeshVertexLayout(): reflected vertex-input attributes do not match this demo's own "
                        "Vertex schema");
    return EXIT_FAILURE;
  }

  // Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3/D-6): both output-transform
  // shader pairs -- unlike the OffscreenTarget-based fixtures/demos
  // (fixed colorFormat), this demo presents to a real swapchain whose
  // format can genuinely be either class, so both variants are loaded
  // up front, mirroring runtime_application.cpp's own identical need.
  const auto outputTransformUnormVertexSpirv = loadSpirvFile("shaders/output_transform_unorm.vert.spv");
  const auto outputTransformUnormFragmentSpirv = loadSpirvFile("shaders/output_transform_unorm.frag.spv");
  if (!outputTransformUnormVertexSpirv.has_value() || !outputTransformUnormFragmentSpirv.has_value()) {
    ATLANTIS_LOG_ERROR("Failed to load shaders/output_transform_unorm.{{vert,frag}}.spv");
    return EXIT_FAILURE;
  }
  auto outputTransformUnormVertexReflectionResult =
      loadReflectionMetadata("shaders/output_transform_unorm.vert.refl.json");
  if (outputTransformUnormVertexReflectionResult.isErr()) {
    ATLANTIS_LOG_ERROR("Failed to load shaders/output_transform_unorm.vert.refl.json");
    return EXIT_FAILURE;
  }
  const auto outputTransformUnormVertexInputLayout =
      outputTransformVertexLayout(outputTransformUnormVertexReflectionResult.value());
  if (!outputTransformUnormVertexInputLayout.has_value()) {
    ATLANTIS_LOG_ERROR("outputTransformVertexLayout(): reflected vertex-input attributes do not match the "
                        "fullscreen-triangle schema (unorm)");
    return EXIT_FAILURE;
  }

  const auto outputTransformSrgbVertexSpirv = loadSpirvFile("shaders/output_transform_srgb.vert.spv");
  const auto outputTransformSrgbFragmentSpirv = loadSpirvFile("shaders/output_transform_srgb.frag.spv");
  if (!outputTransformSrgbVertexSpirv.has_value() || !outputTransformSrgbFragmentSpirv.has_value()) {
    ATLANTIS_LOG_ERROR("Failed to load shaders/output_transform_srgb.{{vert,frag}}.spv");
    return EXIT_FAILURE;
  }
  auto outputTransformSrgbVertexReflectionResult =
      loadReflectionMetadata("shaders/output_transform_srgb.vert.refl.json");
  if (outputTransformSrgbVertexReflectionResult.isErr()) {
    ATLANTIS_LOG_ERROR("Failed to load shaders/output_transform_srgb.vert.refl.json");
    return EXIT_FAILURE;
  }
  const auto outputTransformSrgbVertexInputLayout =
      outputTransformVertexLayout(outputTransformSrgbVertexReflectionResult.value());
  if (!outputTransformSrgbVertexInputLayout.has_value()) {
    ATLANTIS_LOG_ERROR("outputTransformVertexLayout(): reflected vertex-input attributes do not match the "
                        "fullscreen-triangle schema (srgb)");
    return EXIT_FAILURE;
  }

  auto initResult = platform::initialize();
  if (initResult.isErr()) {
    const auto& error = initResult.error();
    ATLANTIS_LOG_ERROR("platform::initialize() failed: {} (nativeErrorCode={})",
                        platformErrorCodeToString(error.code), error.nativeErrorCode);
    return EXIT_FAILURE;
  }
  ATLANTIS_LOG_INFO("Platform initialized");

  auto deviceResult =
      vulkan_backend::createDevice({.applicationName = "Atlantis Minimal Renderer Demo", .enableValidationLayers = true});
  if (deviceResult.isErr()) {
    ATLANTIS_LOG_ERROR("createDevice() failed: {}", deviceCreateErrorToString(deviceResult.error()));
    platform::shutdown();
    static_cast<void>(platform::processEvents());
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Device> device = std::move(deviceResult.value());
  ATLANTIS_LOG_INFO("Vulkan Device created (Validation Layers requested)");

  auto meshResult = createMesh(*device, *vertexInputLayout, kCubeVertices, sizeof(kCubeVertices), kCubeIndices,
                                static_cast<std::uint32_t>(std::size(kCubeIndices)));
  if (meshResult.isErr()) {
    ATLANTIS_LOG_ERROR("createMesh() failed");
    device.reset();
    platform::shutdown();
    static_cast<void>(platform::processEvents());
    return EXIT_FAILURE;
  }
  std::optional<Mesh> mesh = std::move(meshResult.value());
  ATLANTIS_LOG_INFO("Mesh created (cube, 8 vertices, 36 indices)");

  auto cameraBufferResult = device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32});
  if (cameraBufferResult.isErr()) {
    ATLANTIS_LOG_ERROR("createBuffer() (camera uniform) failed");
    mesh.reset();  // Lifetime precondition (Section 14): destroy before device.
    device.reset();
    platform::shutdown();
    static_cast<void>(platform::processEvents());
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Buffer> cameraBuffer = std::move(cameraBufferResult.value());

  // Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3/D-6): the output-transform
  // pass's own fixed, never-scene-content fullscreen-triangle geometry
  // and Sampler -- format/extent-independent, created once here,
  // alongside cameraBuffer above, never resized or recreated.
  const float fullscreenTriangleVertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  auto fullscreenTriangleVertexBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  if (fullscreenTriangleVertexBufferResult.isErr()) {
    ATLANTIS_LOG_ERROR("createBuffer() (fullscreen-triangle vertex) failed");
    cameraBuffer.reset();
    mesh.reset();
    device.reset();
    platform::shutdown();
    static_cast<void>(platform::processEvents());
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
    cameraBuffer.reset();
    mesh.reset();
    device.reset();
    platform::shutdown();
    static_cast<void>(platform::processEvents());
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
    cameraBuffer.reset();
    mesh.reset();
    device.reset();
    platform::shutdown();
    static_cast<void>(platform::processEvents());
    return EXIT_FAILURE;
  }
  std::unique_ptr<rhi::Sampler> outputTransformSampler = std::move(outputTransformSamplerResult.value());

  std::unique_ptr<rhi::Presentation> presentation;
  std::optional<Material> material;
  std::unique_ptr<rhi::Texture> depthTexture;
  // Plan 0024 Milestone 6/7: hdrColorTarget shares depthTexture's own
  // resize-triggered lifecycle (see the extent-change branch below);
  // outputTransformUnormPipeline/...SrgbPipeline share material's own
  // format-change-triggered lifecycle (see the format-change branch
  // below) -- both pairs mirror runtime_application.cpp's own identical
  // member roles.
  std::unique_ptr<rhi::HdrColorTarget> hdrColorTarget;
  std::unique_ptr<rhi::Pipeline> outputTransformUnormPipeline;
  std::unique_ptr<rhi::Pipeline> outputTransformSrgbPipeline;
  std::optional<rhi::Format> lastSeenFormat;
  std::optional<Extent2D> lastSeenExtent;

  Renderer renderer;
  bool failed = false;
  std::uint64_t framesDrawn = 0;
  float orbitAngleRadians = 0.0f;

  while (!platform::shouldQuit()) {
    bool closeRequested = false;

    for (const auto& event : platform::processEvents()) {
      if (const auto* created = std::get_if<platform::SurfaceCreated>(&event)) {
        if (presentation) {
          ATLANTIS_LOG_ERROR("SurfaceCreated observed while a Presentation already exists");
          failed = true;
          continue;
        }
        auto presentationResult = vulkan_backend::createPresentation(*device, created->handle);
        if (presentationResult.isErr()) {
          ATLANTIS_LOG_ERROR("createPresentation() failed: {}",
                              presentationCreateErrorToString(presentationResult.error()));
          failed = true;
          continue;
        }
        presentation = std::move(presentationResult.value());
        ATLANTIS_LOG_INFO("Presentation created");
      } else if (const auto* resize = std::get_if<platform::WindowResize>(&event)) {
        ATLANTIS_LOG_DEBUG("WindowResize: logical={}x{} framebuffer={}x{}", resize->logical.width,
                            resize->logical.height, resize->framebuffer.width, resize->framebuffer.height);
        if (!presentation) {
          ATLANTIS_LOG_INFO("WindowResize observed before Presentation exists -- ignoring");
          continue;
        }
        const rhi::Extent2D framebufferExtent{resize->framebuffer.width, resize->framebuffer.height};
        presentation->notifyResized(framebufferExtent);
      } else if (std::holds_alternative<platform::WindowCloseRequested>(event)) {
        ATLANTIS_LOG_INFO("WindowCloseRequested");
        closeRequested = true;
      } else if (std::holds_alternative<platform::SurfaceDestroyed>(event)) {
        if (presentation) {
          ATLANTIS_LOG_ERROR("SurfaceDestroyed observed while a Presentation still exists");
          failed = true;
          continue;
        }
        ATLANTIS_LOG_INFO("SurfaceDestroyed");
      } else if (std::holds_alternative<platform::Quit>(event)) {
        ATLANTIS_LOG_INFO("Quit");
      } else if (std::holds_alternative<platform::FocusGained>(event)) {
        ATLANTIS_LOG_INFO("FocusGained");
      } else if (std::holds_alternative<platform::FocusLost>(event)) {
        ATLANTIS_LOG_INFO("FocusLost");
      } else if (std::holds_alternative<platform::ApplicationPause>(event)) {
        ATLANTIS_LOG_INFO("ApplicationPause");
      } else if (std::holds_alternative<platform::ApplicationResume>(event)) {
        ATLANTIS_LOG_INFO("ApplicationResume");
      }
    }

    if (presentation && !failed && !closeRequested) {
      auto acquireResult = presentation->acquireNextTarget();
      if (acquireResult.isErr()) {
        ATLANTIS_LOG_ERROR("acquireNextTarget() failed: {}", presentationErrorToString(acquireResult.error()));
        failed = true;
      } else if (acquireResult.value() == nullptr) {
        // Nothing to draw this frame (zero extent, or an out-of-date
        // swapchain deferred to the next call) -- not an error. No
        // Vulkan call is made while minimized (framebuffer extent
        // becomes zero), matching Plan 0007 Section 15's manual-
        // verification bullet.
        ATLANTIS_LOG_DEBUG("acquireNextTarget(): nothing to draw this frame (e.g. minimized)");
      } else {
        std::unique_ptr<rhi::RenderTarget> target = std::move(acquireResult.value());

        // Plan 0007 Section 13 step 1: format-change check, via
        // Presentation::metadata() -- valid here because
        // acquireNextTarget() has already internally recreated the
        // swapchain if needed (ADR-0019) before returning. Create-
        // before-destroy: the new Material is only ever move-assigned
        // over the old one after createMaterial() has already
        // succeeded.
        const rhi::Format currentFormat = presentation->metadata().format;
        if (!lastSeenFormat.has_value() || currentFormat != *lastSeenFormat) {
          const auto waitResult = device->waitIdle();
          if (waitResult.isErr()) {
            ATLANTIS_LOG_ERROR("waitIdle() before Material rebuild failed");
            failed = true;
          } else {
            auto newMaterialResult = createMaterial(
                *device, {.vertexShader = {.spirvWords = vertexSpirv->data(), .wordCount = vertexSpirv->size()},
                          .fragmentShader = {.spirvWords = fragmentSpirv->data(), .wordCount = fragmentSpirv->size()},
                          .vertexInputLayout = *vertexInputLayout,
                          // Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3):
                          // every geometry Pipeline now renders into the
                          // fixed HDR intermediate, never currentFormat
                          // directly.
                          .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
                          .depthFormat = DepthFormat::D32Sfloat,
                          .pushConstantSizeBytes = sizeof(float) * 16});
            // Plan 0024 Milestone 6/7 (ADR-0068 D-6): the one output-
            // transform Pipeline variant this format actually needs --
            // isSrgbFormat(currentFormat) selects which one; the other
            // is untouched, mirroring runtime_application.cpp's own
            // identical selection. Built alongside the geometry Material
            // above so both new-format resources become live together,
            // via the same immediate create-before-swap pattern this
            // demo's own Material rebuild already uses (safe for the
            // identical reason: acquireNextTarget()'s own internal drain
            // already confirms no prior-frame GPU work still references
            // the OLD resources by the time this branch runs).
            const bool useSrgbVariant = isSrgbFormat(currentFormat);
            const auto& outputTransformVertexSpirvForFormat =
                useSrgbVariant ? outputTransformSrgbVertexSpirv : outputTransformUnormVertexSpirv;
            const auto& outputTransformFragmentSpirvForFormat =
                useSrgbVariant ? outputTransformSrgbFragmentSpirv : outputTransformUnormFragmentSpirv;
            const auto& outputTransformVertexInputLayoutForFormat =
                useSrgbVariant ? outputTransformSrgbVertexInputLayout : outputTransformUnormVertexInputLayout;
            auto newOutputTransformPipelineResult = device->createPipeline(
                {.vertexShader = {.spirvWords = outputTransformVertexSpirvForFormat->data(),
                                   .wordCount = outputTransformVertexSpirvForFormat->size()},
                 .fragmentShader = {.spirvWords = outputTransformFragmentSpirvForFormat->data(),
                                     .wordCount = outputTransformFragmentSpirvForFormat->size()},
                 .vertexInputLayout = *outputTransformVertexInputLayoutForFormat,
                 .colorFormat = currentFormat,
                 .hasSampledTextureBinding = true,
                 .hasCameraUniformBinding = false,
                 .hasDepthAttachment = false});
            if (newMaterialResult.isErr() || newOutputTransformPipelineResult.isErr()) {
              ATLANTIS_LOG_ERROR(
                  "createMaterial()/createPipeline() (output-transform) failed during format-change rebuild -- "
                  "keeping the existing Material/outputTransform*Pipeline (still valid for lastSeenFormat) and "
                  "retrying next frame");
              // lastSeenFormat intentionally NOT updated -- retry next frame;
              // whichever of the two succeeded is discarded via ordinary
              // RAII, never partially adopted.
            } else {
              const bool wasFirstMaterial = !material.has_value();
              material = std::move(newMaterialResult.value());  // old Pipeline (if any) destroyed HERE, only
                                                                  // after the new one already succeeded.
              if (useSrgbVariant) {
                outputTransformSrgbPipeline = std::move(newOutputTransformPipelineResult.value());
              } else {
                outputTransformUnormPipeline = std::move(newOutputTransformPipelineResult.value());
              }
              lastSeenFormat = currentFormat;
              ATLANTIS_LOG_INFO("{} (format={})", wasFirstMaterial ? "Material created" : "Material REBUILT for format change",
                                 static_cast<int>(currentFormat));
            }
          }
        }

        // Plan 0007 Section 13 step 2: extent-change check -- the depth
        // Texture alone is recreated; Pipeline is never touched here
        // (dynamic viewport/scissor, Section 3/10 -- confirmed by the
        // log line below never coinciding with a "Material REBUILT"
        // line unless a format change also happened this same frame).
        const rhi::Extent2D currentExtent = target->extent();
        if (!lastSeenExtent.has_value() || !(currentExtent == *lastSeenExtent)) {
          auto newTextureResult = device->createTexture({.extent = currentExtent, .format = DepthFormat::D32Sfloat});
          // Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3): hdrColorTarget
          // shares the SAME trigger as depthTexture -- lastSeenExtent is
          // only updated if BOTH succeed, so a partial success never
          // desyncs the two resources' extents, mirroring
          // runtime_application.cpp's own identical joint gating.
          auto newHdrColorTargetResult = device->createHdrColorTarget({.extent = currentExtent});
          if (newTextureResult.isErr() || newHdrColorTargetResult.isErr()) {
            ATLANTIS_LOG_ERROR(
                "createTexture() (depth) / createHdrColorTarget() failed during resize -- keeping the existing "
                "depth Texture/HdrColorTarget and retrying next frame");
            // lastSeenExtent intentionally NOT updated -- retry both next frame.
          } else {
            depthTexture = std::move(newTextureResult.value());  // create-before-destroy, same as Material above.
            hdrColorTarget = std::move(newHdrColorTargetResult.value());
            lastSeenExtent = currentExtent;
            ATLANTIS_LOG_INFO(
                "Depth Texture/HdrColorTarget recreated for extent {}x{} -- Pipeline NOT recreated (dynamic "
                "viewport/scissor)",
                currentExtent.width, currentExtent.height);
          }
        }

        // Plan 0024 Milestone 6/7: mirrors runtime_application.cpp's own
        // identical effectiveOutputTransformPipeline selection --
        // whichever of outputTransformUnormPipeline/...SrgbPipeline
        // matches lastSeenFormat's own real classification.
        rhi::Pipeline* effectiveOutputTransformPipeline =
            lastSeenFormat.has_value()
                ? (isSrgbFormat(*lastSeenFormat) ? outputTransformSrgbPipeline.get() : outputTransformUnormPipeline.get())
                : nullptr;

        if (!material.has_value() || !depthTexture || !hdrColorTarget || !effectiveOutputTransformPipeline) {
          ATLANTIS_LOG_ERROR("No valid Material/depth Texture/HdrColorTarget/output-transform Pipeline yet -- "
                              "skipping this frame's draw");
        } else {
          // Camera write-timing (Section 9/13 step 3): safe here because
          // acquireNextTarget()'s own internal drain already guarantees
          // no prior-frame GPU work is still reading this Buffer.
          orbitAngleRadians += 0.01f;
          const float eyeX = std::sin(orbitAngleRadians) * 2.5f;
          const float eyeZ = std::cos(orbitAngleRadians) * 2.5f;
          const Mat4 view = lookAt(eyeX, 1.5f, eyeZ, 0.0f, 0.0f, 0.0f);
          const float aspect =
              currentExtent.height != 0 ? static_cast<float>(currentExtent.width) / static_cast<float>(currentExtent.height)
                                         : 1.0f;
          const Mat4 projection = perspective(60.0f * 3.14159265f / 180.0f, aspect, 0.1f, 100.0f);
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
            ATLANTIS_LOG_ERROR("createCommandList() failed");
            failed = true;
          } else {
            std::unique_ptr<rhi::CommandList> commandList = std::move(commandListResult.value());

            renderer.drawFrame(*commandList, *target, *depthTexture, *cameraBuffer, drawItems,
                                atlantis::rhi::ResourceState::PresentSource, *hdrColorTarget,
                                *fullscreenTriangleVertexBuffer, *fullscreenTriangleIndexBuffer,
                                *effectiveOutputTransformPipeline, *outputTransformSampler);

            auto submitResult = device->submit(std::move(commandList), *target);
            if (submitResult.isErr()) {
              ATLANTIS_LOG_ERROR("submit() failed");
              failed = true;
            } else {
              auto presentResult = presentation->present(std::move(target), std::move(submitResult.value()));
              if (presentResult.isErr()) {
                ATLANTIS_LOG_ERROR("present() failed: {}", presentationErrorToString(presentResult.error()));
                failed = true;
              } else {
                ++framesDrawn;
                if (framesDrawn % 120 == 0) {
                  ATLANTIS_LOG_DEBUG("Drawn {} frames so far", framesDrawn);
                }
              }
            }
          }
        }
      }
    }

    if (failed || closeRequested) {
      // Required before destroying Presentation/Device whenever a
      // RenderTarget may have been acquired or a submission made
      // (ADR-0019) -- drains any outstanding GPU work, including on a
      // mid-frame or submit-then-exit path (a deliberate manual-
      // verification case, Section 15 -- e.g. closing the window right
      // after an acquire but before submit/present).
      if (device) {
        const auto idleResult = device->waitIdle();
        if (idleResult.isErr()) {
          ATLANTIS_LOG_ERROR("waitIdle() failed during shutdown");
        }
      }
      // Lifetime precondition (Section 14): every Buffer/Texture/
      // Pipeline this Device backed is destroyed before the Device
      // itself -- material/depthTexture/cameraBuffer/mesh first, then
      // presentation, then device. Plan 0024 Milestone 6/7: the new HDR-
      // related resources reset alongside their own closest existing
      // sibling above (outputTransform*Pipeline/hdrColorTarget with
      // material/depthTexture; outputTransformSampler/fullscreenTriangle*
      // with cameraBuffer, since both groups share that sibling's own
      // lifecycle).
      outputTransformSrgbPipeline.reset();
      outputTransformUnormPipeline.reset();
      material.reset();
      hdrColorTarget.reset();
      depthTexture.reset();
      outputTransformSampler.reset();
      fullscreenTriangleIndexBuffer.reset();
      fullscreenTriangleVertexBuffer.reset();
      cameraBuffer.reset();
      mesh.reset();
      presentation.reset();
      device.reset();
      platform::shutdown();
    }
  }

  for (const auto& event : platform::processEvents()) {
    if (std::holds_alternative<platform::SurfaceDestroyed>(event)) {
      ATLANTIS_LOG_INFO("SurfaceDestroyed");
    } else if (std::holds_alternative<platform::Quit>(event)) {
      ATLANTIS_LOG_INFO("Quit");
    }
  }

  if (failed) {
    ATLANTIS_LOG_ERROR("Atlantis Minimal Renderer demo finished with failures");
    return EXIT_FAILURE;
  }

  ATLANTIS_LOG_INFO("Atlantis Minimal Renderer demo finished after drawing {} frames", framesDrawn);
  return EXIT_SUCCESS;
}
