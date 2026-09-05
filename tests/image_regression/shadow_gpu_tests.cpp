// Plan 0027 Milestone 10 (ADR-0072): real-GPU discriminating verification
// for the directional shadow feature -- occlusion, movement (occluder
// translation and light-direction change), out-of-bounds (via the real
// shadow-NDC transform, never a world-space magnitude comparison), and
// first-use-vs-reused ShadowMap byte-identity (Group A, no environment),
// plus IBL/ambient isolation (Group B, reuses IblMaterialDemoFixture).
// No-directional-light byte compatibility is NOT retested here -- it is
// already a hard, golden-backed requirement on ibl_material_demo. Mirrors
// sky_background_gpu_tests.cpp's own placement/style: every GPU command
// is recorded by Renderer::drawFrame()'s own internal RenderGraph and one
// further RenderGraphBuilder/execute() copy pass, one CommandList, one
// submit().

#include "fixture/ibl_material_demo_fixture.h"

#include <atlantis/asset_system/mesh_artifact.h>
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
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/shadow_map.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using atlantis::image_regression::IblMaterialDemoFixture;
using atlantis::image_regression::kIblMaterialDemoExtentPixels;
using atlantis::image_regression::PixelBuffer;
using atlantis::renderer::createMaterial;
using atlantis::renderer::createMesh;
using atlantis::renderer::DrawItem;
using atlantis::renderer::Material;
using atlantis::renderer::MaterialEnvironmentBinding;
using atlantis::renderer::MaterialPushConstantLayout;
using atlantis::renderer::Mesh;
using atlantis::renderer::Renderer;
using atlantis::rhi::AddressMode;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::CommandList;
using atlantis::rhi::DepthFormat;
using atlantis::rhi::Device;
using atlantis::rhi::Extent2D;
using atlantis::rhi::Filter;
using atlantis::rhi::Format;
using atlantis::rhi::SampledTextureCreateParams;
using atlantis::rhi::SampledTextureFormat;
using atlantis::rhi::SamplerCreateParams;
using atlantis::rhi::VertexInputLayout;
using atlantis::runtime::CameraMatrices;
using atlantis::runtime::CameraWorldPositionData;
using atlantis::runtime::computeShadowLightSpaceMatrices;
using atlantis::runtime::FrameLightingData;
using atlantis::runtime::Mat4;
using atlantis::runtime::Vec3;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;
using atlantis::vulkan_backend::createDevice;
using atlantis::vulkan_backend::DeviceCreateParams;

constexpr Extent2D kExtent{512, 512};
constexpr Format kColorFormat = Format::Rgba8Unorm;
constexpr float kFovYRadians = 1.0471975512f;  // 60 degrees -- sky_background_gpu_tests.cpp's own constant
constexpr float kNearZ = 0.1f;
constexpr float kFarZ = 100.0f;

// The real, 44-byte position/color/uv/normal mesh-artifact layout, every
// other composition root in this codebase already shares.
struct Vertex {
  float position[3];
  float color[3];
  float uv[2];
  float normal[3];
};
static_assert(std::is_standard_layout_v<Vertex>);
static_assert(offsetof(Vertex, position) == atlantis::asset_system::kMeshArtifactPositionOffsetBytes);
static_assert(offsetof(Vertex, color) == atlantis::asset_system::kMeshArtifactColorOffsetBytes);
static_assert(offsetof(Vertex, uv) == atlantis::asset_system::kMeshArtifactUv0OffsetBytes);
static_assert(offsetof(Vertex, normal) == atlantis::asset_system::kMeshArtifactNormalOffsetBytes);
static_assert(sizeof(Vertex) == atlantis::asset_system::kMeshArtifactVertexStrideBytes);

[[nodiscard]] std::optional<std::vector<std::uint32_t>> loadSpirvFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return std::nullopt;
  const std::streamsize sizeBytes = file.tellg();
  if (sizeBytes <= 0 || sizeBytes % 4 != 0) return std::nullopt;
  file.seekg(0);
  std::vector<std::uint32_t> words(static_cast<std::size_t>(sizeBytes) / 4);
  if (!file.read(reinterpret_cast<char*>(words.data()), sizeBytes)) return std::nullopt;
  return words;
}

[[nodiscard]] Mat4 identityMatrix() {
  return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

[[nodiscard]] Mat4 translationMatrix(float x, float y, float z) {
  Mat4 result = identityMatrix();
  result[12] = x;
  result[13] = y;
  result[14] = z;
  return result;
}

// Duplicated -- not shared -- from sky_background_gpu_tests.cpp's own
// identical functions (this codebase's established "duplicated, not
// shared" precedent for this exact class of small camera-math helper).
[[nodiscard]] Mat4 lookAtMatrixFromForward(float fx, float fy, float fz, float eyeX, float eyeY, float eyeZ) {
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

[[nodiscard]] Mat4 perspectiveMatrixDirect(float fovYRadians, float aspect, float nearZ, float farZ) {
  const float f = 1.0f / std::tan(fovYRadians * 0.5f);
  Mat4 result{};
  result[0] = f / aspect;
  result[5] = -f;
  result[10] = farZ / (nearZ - farZ);
  result[11] = -1.0f;
  result[14] = (nearZ * farZ) / (nearZ - farZ);
  return result;
}

[[nodiscard]] std::optional<VertexInputLayout> pbrVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
      MeshVertexAttributeSchema{.location = 2, .offsetBytes = offsetof(Vertex, normal)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

[[nodiscard]] std::optional<VertexInputLayout> shadowCastVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

[[nodiscard]] std::optional<VertexInputLayout> outputTransformVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = 0},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(float) * 2);
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// A flat, axis-aligned quad in the XZ plane at y=0, normal +Y, centered
// at (centerX, 0, centerZ), spanning +/-halfExtent in both X and Z.
[[nodiscard]] Vertex quadVertex(float x, float z, float u, float v) {
  Vertex vertex{};
  vertex.position[0] = x;
  vertex.position[1] = 0.0f;
  vertex.position[2] = z;
  vertex.color[0] = vertex.color[1] = vertex.color[2] = 1.0f;
  vertex.uv[0] = u;
  vertex.uv[1] = v;
  vertex.normal[0] = 0.0f;
  vertex.normal[1] = 1.0f;
  vertex.normal[2] = 0.0f;
  return vertex;
}

[[nodiscard]] atlantis::Result<Mesh, atlantis::renderer::CreateMeshError> makeQuadMesh(Device& device, float centerX,
                                                                                        float centerZ,
                                                                                        float halfExtent) {
  const std::array<Vertex, 4> vertices{
      quadVertex(centerX - halfExtent, centerZ - halfExtent, 0.0f, 0.0f),
      quadVertex(centerX + halfExtent, centerZ - halfExtent, 1.0f, 0.0f),
      quadVertex(centerX + halfExtent, centerZ + halfExtent, 1.0f, 1.0f),
      quadVertex(centerX - halfExtent, centerZ + halfExtent, 0.0f, 1.0f),
  };
  const std::uint16_t indices[6] = {0, 1, 2, 2, 3, 0};
  return createMesh(device, VertexInputLayout{}, vertices.data(), sizeof(vertices), indices, 6);
}

// A unit cube, [-0.5,0.5]^3, position-only correctness matters (this
// mesh is only ever bound via shadowCastPipeline's own position-only
// VertexInputLayout, in shadowCasterDrawItems -- never drawn into the
// main color pass in this file, so color/uv/normal are never read).
constexpr Vertex kCubeVertices[8] = {
    {{-0.5f, -0.5f, -0.5f}, {1, 1, 1}, {0, 0}, {0, 1, 0}}, {{0.5f, -0.5f, -0.5f}, {1, 1, 1}, {0, 0}, {0, 1, 0}},
    {{0.5f, 0.5f, -0.5f}, {1, 1, 1}, {0, 0}, {0, 1, 0}},   {{-0.5f, 0.5f, -0.5f}, {1, 1, 1}, {0, 0}, {0, 1, 0}},
    {{-0.5f, -0.5f, 0.5f}, {1, 1, 1}, {0, 0}, {0, 1, 0}},  {{0.5f, -0.5f, 0.5f}, {1, 1, 1}, {0, 0}, {0, 1, 0}},
    {{0.5f, 0.5f, 0.5f}, {1, 1, 1}, {0, 0}, {0, 1, 0}},    {{-0.5f, 0.5f, 0.5f}, {1, 1, 1}, {0, 0}, {0, 1, 0}},
};
constexpr std::uint16_t kCubeIndices[36] = {
    0, 1, 2, 2, 3, 0,  // back
    5, 4, 7, 7, 6, 5,  // front
    4, 0, 3, 3, 7, 4,  // left
    1, 5, 6, 6, 2, 1,  // right
    4, 5, 1, 1, 0, 4,  // bottom
    3, 2, 6, 6, 7, 3,  // top
};

struct ShadowTestRig {
  std::unique_ptr<Device> device;
  Mesh groundMesh;
  Mesh probeMesh;
  Mesh occluderMesh;
  std::unique_ptr<atlantis::rhi::SampledTexture> texture;
  std::unique_ptr<atlantis::rhi::Sampler> sampler;
  Material material;  // PbrDirectLit, environmentBinding=None -- shared by ground/probe DrawItems
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture;
  std::unique_ptr<atlantis::rhi::OffscreenTarget> offscreenTarget;
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer;
  std::unique_ptr<atlantis::rhi::HdrColorTarget> hdrColorTarget;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleVertexBuffer;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleIndexBuffer;
  std::unique_ptr<atlantis::rhi::Sampler> outputTransformSampler;
  std::unique_ptr<atlantis::rhi::Pipeline> outputTransformPipeline;
  std::unique_ptr<atlantis::rhi::ShadowMap> shadowMap;
  std::unique_ptr<atlantis::rhi::Sampler> shadowMapSampler;
  std::unique_ptr<atlantis::rhi::Pipeline> shadowCastPipeline;
  std::unique_ptr<atlantis::rhi::Buffer> shadowLightSpaceBuffer;
};

// Builds every long-lived resource once: a real Device, the fixed P10
// Group A scene's ground/probe/occluder meshes, one shared PbrDirectLit
// Material (baseColorFactor=(0.8,0.8,0.8,1), metallic=0, roughness=0.8 --
// a solid-white texture supplies the neutral 1.0 factor the effective
// 0.8 albedo needs, mirroring pbr_render_gpu_tests.cpp's own
// rig.texture/rig.sampler precedent), and the shadow-casting Pipeline.
[[nodiscard]] std::optional<ShadowTestRig> setUpShadowTestRig() {
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Shadow GPU Tests", .enableValidationLayers = true});
  if (deviceResult.isErr()) return std::nullopt;
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  auto groundMeshResult = makeQuadMesh(*device, 0.0f, 0.0f, 6.0f);
  if (groundMeshResult.isErr()) return std::nullopt;
  auto probeMeshResult = makeQuadMesh(*device, 7.765f, -11.647f, 1.0f);
  if (probeMeshResult.isErr()) return std::nullopt;
  auto occluderMeshResult =
      createMesh(*device, VertexInputLayout{}, kCubeVertices, sizeof(kCubeVertices), kCubeIndices, 36);
  if (occluderMeshResult.isErr()) return std::nullopt;

  constexpr std::uint32_t kTexExtent = 2;
  const std::vector<std::uint8_t> pixelBytes(static_cast<std::size_t>(kTexExtent) * kTexExtent * 4, 0xFF);
  auto textureResult = device->createSampledTexture(
      SampledTextureCreateParams{.extent = {kTexExtent, kTexExtent}, .format = SampledTextureFormat::Rgba8Srgb});
  if (textureResult.isErr()) return std::nullopt;
  auto stagingResult = device->createBuffer(
      {.purpose = BufferPurpose::Staging, .sizeBytes = static_cast<std::size_t>(kTexExtent) * kTexExtent * 4});
  if (stagingResult.isErr()) return std::nullopt;
  std::memcpy(stagingResult.value()->mappedData(), pixelBytes.data(), pixelBytes.size());
  auto samplerResult =
      device->createSampler(SamplerCreateParams{.filter = Filter::Linear, .addressMode = AddressMode::Repeat});
  if (samplerResult.isErr()) return std::nullopt;

  auto uploadCommandListResult = device->createCommandList();
  if (uploadCommandListResult.isErr()) return std::nullopt;
  {
    atlantis::render_graph::RenderGraphBuilder builder;
    const auto resource = builder.declareResource("shadow-test-texture-upload");
    const auto pass = builder.declarePass("ShadowTestTextureUpload");
    builder.writes(pass, resource, atlantis::rhi::ResourceState::TransferDestination);
    builder.setExecute(pass, [&stagingResult, &textureResult](CommandList& cmd) {
      cmd.copyBufferToTexture(*stagingResult.value(), *textureResult.value());
    });
    auto compileResult = builder.compile();
    if (compileResult.isErr()) return std::nullopt;
    const std::vector<atlantis::render_graph::ResourceBinding> bindings{
        {.resource = compileResult.value().resourceAt(0),
         .sampledTexture = textureResult.value().get(),
         .finalState = atlantis::rhi::ResourceState::ShaderRead}};
    atlantis::render_graph::execute(compileResult.value(), bindings, *uploadCommandListResult.value());
  }
  auto uploadOffscreen = device->createOffscreenTarget({.extent = kExtent, .format = kColorFormat});
  if (uploadOffscreen.isErr()) return std::nullopt;
  auto uploadTarget = uploadOffscreen.value()->acquireTarget();
  if (uploadTarget.isErr()) return std::nullopt;
  auto uploadSubmitResult = device->submit(std::move(uploadCommandListResult.value()), *uploadTarget.value());
  if (uploadSubmitResult.isErr()) return std::nullopt;
  if (device->waitIdle().isErr()) return std::nullopt;

  const auto pbrVertexSpirv = loadSpirvFile(std::string(ATLANTIS_SHADOW_TEST_PBR_DIRECT_LIT_SHADER_DIR) +
                                             "/pbr_direct_lit.vert.spv");
  const auto pbrFragmentSpirv = loadSpirvFile(std::string(ATLANTIS_SHADOW_TEST_PBR_DIRECT_LIT_SHADER_DIR) +
                                               "/pbr_direct_lit.frag.spv");
  if (!pbrVertexSpirv.has_value() || !pbrFragmentSpirv.has_value()) return std::nullopt;
  auto pbrVertexReflection = loadReflectionMetadata(std::string(ATLANTIS_SHADOW_TEST_PBR_DIRECT_LIT_SHADER_DIR) +
                                                     "/pbr_direct_lit.vert.refl.json");
  if (pbrVertexReflection.isErr()) return std::nullopt;
  const auto pbrLayout = pbrVertexLayout(pbrVertexReflection.value());
  if (!pbrLayout.has_value()) return std::nullopt;

  auto materialResult = createMaterial(
      *device,
      {.vertexShader = {.spirvWords = pbrVertexSpirv->data(), .wordCount = pbrVertexSpirv->size()},
       .fragmentShader = {.spirvWords = pbrFragmentSpirv->data(), .wordCount = pbrFragmentSpirv->size()},
       .vertexInputLayout = *pbrLayout,
       .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = 96,
       .sampledTextureBindingCount = 2},
      textureResult.value().get(), samplerResult.value().get(), MaterialPushConstantLayout::PbrDirectLit,
      std::array<float, 4>{0.8f, 0.8f, 0.8f, 1.0f}, 0.0f, 0.8f, MaterialEnvironmentBinding::None);
  if (materialResult.isErr()) return std::nullopt;

  auto cameraBufferResult = device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = 592});
  if (cameraBufferResult.isErr()) return std::nullopt;

  auto depthTextureResult = device->createTexture({.extent = kExtent, .format = DepthFormat::D32Sfloat});
  if (depthTextureResult.isErr()) return std::nullopt;

  auto offscreenTargetResult = device->createOffscreenTarget({.extent = kExtent, .format = kColorFormat});
  if (offscreenTargetResult.isErr()) return std::nullopt;

  const std::size_t readbackSizeBytes = static_cast<std::size_t>(kExtent.width) * kExtent.height * 4;
  auto readbackBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Readback, .sizeBytes = readbackSizeBytes});
  if (readbackBufferResult.isErr()) return std::nullopt;

  auto hdrColorTargetResult = device->createHdrColorTarget({.extent = kExtent});
  if (hdrColorTargetResult.isErr()) return std::nullopt;

  const float fullscreenTriangleVertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  auto fullscreenTriangleVertexBufferResult = device->createBuffer(
      {.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  if (fullscreenTriangleVertexBufferResult.isErr()) return std::nullopt;
  std::memcpy(fullscreenTriangleVertexBufferResult.value()->mappedData(), fullscreenTriangleVertices,
              sizeof(fullscreenTriangleVertices));

  const std::uint16_t fullscreenTriangleIndices[3] = {0, 1, 2};
  auto fullscreenTriangleIndexBufferResult =
      device->createBuffer({.purpose = BufferPurpose::Index, .sizeBytes = sizeof(fullscreenTriangleIndices)});
  if (fullscreenTriangleIndexBufferResult.isErr()) return std::nullopt;
  std::memcpy(fullscreenTriangleIndexBufferResult.value()->mappedData(), fullscreenTriangleIndices,
              sizeof(fullscreenTriangleIndices));

  auto outputTransformSamplerResult =
      device->createSampler({.filter = Filter::Linear, .addressMode = AddressMode::ClampToEdge});
  if (outputTransformSamplerResult.isErr()) return std::nullopt;

  const auto outputTransformVertexSpirv =
      loadSpirvFile(std::string(ATLANTIS_SHADOW_TEST_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
                    "/output_transform_unorm.vert.spv");
  const auto outputTransformFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_SHADOW_TEST_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
                    "/output_transform_unorm.frag.spv");
  if (!outputTransformVertexSpirv.has_value() || !outputTransformFragmentSpirv.has_value()) return std::nullopt;
  auto outputTransformVertexReflection =
      loadReflectionMetadata(std::string(ATLANTIS_SHADOW_TEST_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
                              "/output_transform_unorm.vert.refl.json");
  if (outputTransformVertexReflection.isErr()) return std::nullopt;
  const auto outputTransformVertexInputLayout = outputTransformVertexLayout(outputTransformVertexReflection.value());
  if (!outputTransformVertexInputLayout.has_value()) return std::nullopt;

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
  if (outputTransformPipelineResult.isErr()) return std::nullopt;

  auto shadowMapResult = device->createShadowMap({.extent = {1024, 1024}});
  if (shadowMapResult.isErr()) return std::nullopt;

  auto shadowMapSamplerResult =
      device->createSampler({.filter = Filter::Nearest, .addressMode = AddressMode::ClampToEdge});
  if (shadowMapSamplerResult.isErr()) return std::nullopt;

  const auto shadowCastVertexSpirv =
      loadSpirvFile(std::string(ATLANTIS_SHADOW_TEST_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv");
  const auto shadowCastFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_SHADOW_TEST_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv");
  if (!shadowCastVertexSpirv.has_value() || !shadowCastFragmentSpirv.has_value()) return std::nullopt;
  auto shadowCastVertexReflection = loadReflectionMetadata(
      std::string(ATLANTIS_SHADOW_TEST_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json");
  if (shadowCastVertexReflection.isErr()) return std::nullopt;
  const auto shadowCastLayout = shadowCastVertexLayout(shadowCastVertexReflection.value());
  if (!shadowCastLayout.has_value()) return std::nullopt;

  auto shadowCastPipelineResult = device->createPipeline(
      {.vertexShader = {.spirvWords = shadowCastVertexSpirv->data(), .wordCount = shadowCastVertexSpirv->size()},
       .fragmentShader = {.spirvWords = shadowCastFragmentSpirv->data(),
                           .wordCount = shadowCastFragmentSpirv->size()},
       .vertexInputLayout = *shadowCastLayout,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .sampledTextureBindingCount = 0,
       .hasCameraUniformBinding = true,
       .hasDepthAttachment = true,
       .depthWriteEnabled = true,
       .hasColorAttachment = false});
  if (shadowCastPipelineResult.isErr()) return std::nullopt;

  auto shadowLightSpaceBufferResult = device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = 128});
  if (shadowLightSpaceBufferResult.isErr()) return std::nullopt;

  return ShadowTestRig{.device = std::move(device),
                        .groundMesh = std::move(groundMeshResult.value()),
                        .probeMesh = std::move(probeMeshResult.value()),
                        .occluderMesh = std::move(occluderMeshResult.value()),
                        .texture = std::move(textureResult.value()),
                        .sampler = std::move(samplerResult.value()),
                        .material = std::move(materialResult.value()),
                        .cameraBuffer = std::move(cameraBufferResult.value()),
                        .depthTexture = std::move(depthTextureResult.value()),
                        .offscreenTarget = std::move(offscreenTargetResult.value()),
                        .readbackBuffer = std::move(readbackBufferResult.value()),
                        .hdrColorTarget = std::move(hdrColorTargetResult.value()),
                        .fullscreenTriangleVertexBuffer = std::move(fullscreenTriangleVertexBufferResult.value()),
                        .fullscreenTriangleIndexBuffer = std::move(fullscreenTriangleIndexBufferResult.value()),
                        .outputTransformSampler = std::move(outputTransformSamplerResult.value()),
                        .outputTransformPipeline = std::move(outputTransformPipelineResult.value()),
                        .shadowMap = std::move(shadowMapResult.value()),
                        .shadowMapSampler = std::move(shadowMapSamplerResult.value()),
                        .shadowCastPipeline = std::move(shadowCastPipelineResult.value()),
                        .shadowLightSpaceBuffer = std::move(shadowLightSpaceBufferResult.value())};
}

// P10's own fixed camera: eye (0,6,10), forward = normalize(origin-eye),
// world-up (0,1,0). Written once per render (unchanged across every
// check in this file).
void writeCamera(ShadowTestRig& rig) {
  constexpr float eyeX = 0.0f, eyeY = 6.0f, eyeZ = 10.0f;
  float fx = -eyeX, fy = -eyeY, fz = -eyeZ;
  const float fLen = std::sqrt(fx * fx + fy * fy + fz * fz);
  fx /= fLen;
  fy /= fLen;
  fz /= fLen;
  const Mat4 view = lookAtMatrixFromForward(fx, fy, fz, eyeX, eyeY, eyeZ);
  const Mat4 projection = perspectiveMatrixDirect(kFovYRadians, 1.0f, kNearZ, kFarZ);
  auto* cameraData = static_cast<float*>(rig.cameraBuffer->mappedData());
  for (std::size_t i = 0; i < 16; ++i) cameraData[i] = view[i];
  for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = projection[i];
}

// Writes one Directional light (P10's own fixed color/intensity, caller-
// supplied direction) into the camera buffer's FrameLightingData region,
// plus the real light-space view/projection (P4/P11's own fixed
// derivation) into BOTH the camera buffer's own 464-byte tail and
// shadowLightSpaceBuffer -- identical values in both (P5), matching
// Runtime's own runFrame() exactly.
void writeDirectionalLight(ShadowTestRig& rig, const Vec3& direction) {
  auto* cameraData = static_cast<float*>(rig.cameraBuffer->mappedData());
  FrameLightingData lighting;
  lighting.directionalLightCount = 1;
  lighting.directionalLights[0].direction[0] = direction.x;
  lighting.directionalLights[0].direction[1] = direction.y;
  lighting.directionalLights[0].direction[2] = direction.z;
  lighting.directionalLights[0].color[0] = 1.0f;
  lighting.directionalLights[0].color[1] = 1.0f;
  lighting.directionalLights[0].color[2] = 1.0f;
  lighting.directionalLights[0].intensity = 3.0f;
  auto* lightingData = reinterpret_cast<FrameLightingData*>(cameraData + 32);
  *lightingData = lighting;

  const CameraMatrices lightSpaceMatrices = computeShadowLightSpaceMatrices(direction);
  const Mat4& lightSpaceView = lightSpaceMatrices.view;
  const Mat4& lightSpaceProjection = lightSpaceMatrices.projection;
  float* lightSpaceTail = cameraData + 116;  // byte offset 464 / sizeof(float)
  std::memcpy(lightSpaceTail, lightSpaceView.data(), sizeof(float) * 16);
  std::memcpy(lightSpaceTail + 16, lightSpaceProjection.data(), sizeof(float) * 16);

  auto* shadowLightSpaceData = static_cast<float*>(rig.shadowLightSpaceBuffer->mappedData());
  std::memcpy(shadowLightSpaceData, lightSpaceView.data(), sizeof(float) * 16);
  std::memcpy(shadowLightSpaceData + 16, lightSpaceProjection.data(), sizeof(float) * 16);
}

// One full Renderer::drawFrame() + copy-to-readback cycle, mirroring
// sky_background_gpu_tests.cpp's own renderOnce() exactly. occluderCenter
// == std::nullopt means shadowCasterDrawItems stays empty (no occluder in
// the shadow map this frame); otherwise the occluder cube is translated
// there and cast (never drawn into the main color pass -- this file only
// ever samples the ground/probe surfaces, never the occluder's own).
// includeProbe adds the out-of-bounds probe quad to the main draw pass.
PixelBuffer renderShadowScene(ShadowTestRig& rig, std::optional<Vec3> occluderCenter, bool includeProbe) {
  auto acquireResult = rig.offscreenTarget->acquireTarget();
  REQUIRE(acquireResult.isOk());
  std::unique_ptr<atlantis::rhi::RenderTarget> target = std::move(acquireResult.value());

  DrawItem groundItem;
  groundItem.mesh = &rig.groundMesh;
  groundItem.material = &rig.material;
  groundItem.objectToWorld = identityMatrix();
  std::vector<DrawItem> drawItems{groundItem};
  if (includeProbe) {
    DrawItem probeItem;
    probeItem.mesh = &rig.probeMesh;
    probeItem.material = &rig.material;
    probeItem.objectToWorld = identityMatrix();
    drawItems.push_back(probeItem);
  }

  std::vector<DrawItem> shadowCasterDrawItems;
  DrawItem occluderItem;
  if (occluderCenter.has_value()) {
    occluderItem.mesh = &rig.occluderMesh;
    occluderItem.material = &rig.material;
    occluderItem.objectToWorld = translationMatrix(occluderCenter->x, occluderCenter->y, occluderCenter->z);
    shadowCasterDrawItems.push_back(occluderItem);
  }

  auto commandListResult = rig.device->createCommandList();
  REQUIRE(commandListResult.isOk());
  std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

  Renderer renderer;
  renderer.drawFrame(*commandList, *target, *rig.depthTexture, *rig.cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::TransferSource, *rig.hdrColorTarget,
                      *rig.fullscreenTriangleVertexBuffer, *rig.fullscreenTriangleIndexBuffer,
                      *rig.outputTransformPipeline, *rig.outputTransformSampler, nullptr, nullptr, *rig.shadowMap,
                      *rig.shadowMapSampler, *rig.shadowCastPipeline, *rig.shadowLightSpaceBuffer,
                      shadowCasterDrawItems);

  atlantis::render_graph::RenderGraphBuilder copyBuilder;
  const auto copyResource = copyBuilder.declareResource("color-copy");
  const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
  copyBuilder.writes(copyPass, copyResource, atlantis::rhi::ResourceState::TransferSource);
  copyBuilder.setExecute(copyPass, [&target, &rig](CommandList& cmd) {
    cmd.copyRenderTargetToBuffer(*target, *rig.readbackBuffer);
  });
  auto copyCompileResult = copyBuilder.compile();
  REQUIRE(copyCompileResult.isOk());
  const std::vector<atlantis::render_graph::ResourceBinding> copyBindings{
      {.resource = copyCompileResult.value().resourceAt(0),
       .target = target.get(),
       .incomingState = atlantis::rhi::ResourceState::TransferSource}};
  atlantis::render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  auto submitResult = rig.device->submit(std::move(commandList), *target);
  REQUIRE(submitResult.isOk());
  REQUIRE(rig.device->waitIdle().isOk());

  PixelBuffer result;
  result.width = kExtent.width;
  result.height = kExtent.height;
  const std::size_t byteCount = static_cast<std::size_t>(kExtent.width) * kExtent.height * 4;
  const auto* readbackData = static_cast<const std::uint8_t*>(rig.readbackBuffer->mappedData());
  result.rgba8.assign(readbackData, readbackData + byteCount);
  target.reset();
  return result;
}

[[nodiscard]] std::array<std::uint8_t, 4> pixelAt(const PixelBuffer& frame, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * frame.width + x) * 4;
  return {frame.rgba8[offset], frame.rgba8[offset + 1], frame.rgba8[offset + 2], frame.rgba8[offset + 3]};
}

[[nodiscard]] int luminance(const std::array<std::uint8_t, 4>& pixel) {
  return static_cast<int>(pixel[0]) + static_cast<int>(pixel[1]) + static_cast<int>(pixel[2]);
}

// Mirrors sky_background_gpu_tests.cpp's own identical helper and its
// own comment: comparing a byte count, never the two ~1 MB PixelBuffer::rgba8
// vectors directly, avoids a real Catch2 console-reporter crash
// (catch_textflow.cpp's own "m_it != m_string->begin()" assertion) when
// stringifying a failed comparison's operands.
[[nodiscard]] std::size_t firstDifferingByte(const PixelBuffer& a, const PixelBuffer& b) {
  const std::size_t count = std::min(a.rgba8.size(), b.rgba8.size());
  for (std::size_t i = 0; i < count; ++i) {
    if (a.rgba8[i] != b.rgba8[i]) return i;
  }
  return a.rgba8.size() == b.rgba8.size() ? count : count + 1;
}

// P10's own fixed check-1/check-2 light direction: normalize(-0.3,-1.0,-0.2).
[[nodiscard]] Vec3 kLightDirectionA() {
  constexpr float x = -0.3f, y = -1.0f, z = -0.2f;
  const float len = std::sqrt(x * x + y * y + z * z);
  return {x / len, y / len, z / len};
}

// P10's own fixed check-3 light direction: normalize(0.3,-1.0,-0.2) --
// mirrored in x.
[[nodiscard]] Vec3 kLightDirectionB() {
  constexpr float x = 0.3f, y = -1.0f, z = -0.2f;
  const float len = std::sqrt(x * x + y * y + z * z);
  return {x / len, y / len, z / len};
}

}  // namespace

TEST_CASE("Directional shadow: an occluder casts a shadow onto the ground, and a reference point outside the "
          "footprint stays lit (P10 Group A check 1)",
          "[image_regression][gpu][shadow]") {
  auto rigOpt = setUpShadowTestRig();
  REQUIRE(rigOpt.has_value());
  ShadowTestRig& rig = *rigOpt;

  writeCamera(rig);
  writeDirectionalLight(rig, kLightDirectionA());
  const PixelBuffer frame = renderShadowScene(rig, Vec3{0.0f, 1.5f, 0.0f}, /*includeProbe=*/false);

  const int shadowedLuminance = luminance(pixelAt(frame, 239, 250));
  const int litLuminance = luminance(pixelAt(frame, 402, 331));
  INFO("shadowed footprint pixel (239,250) luminance = " << shadowedLuminance);
  INFO("reference pixel (402,331) luminance = " << litLuminance);
  CHECK(shadowedLuminance == 0);
  CHECK(litLuminance > 30);
}

TEST_CASE("Directional shadow: moving the occluder moves the footprint; the old footprint point returns to being "
          "lit (P10 Group A check 2)",
          "[image_regression][gpu][shadow]") {
  auto rigOpt = setUpShadowTestRig();
  REQUIRE(rigOpt.has_value());
  ShadowTestRig& rig = *rigOpt;

  writeCamera(rig);
  writeDirectionalLight(rig, kLightDirectionA());
  const PixelBuffer frame = renderShadowScene(rig, Vec3{1.0f, 1.5f, 0.0f}, /*includeProbe=*/false);

  const int newFootprintLuminance = luminance(pixelAt(frame, 276, 250));
  const int oldFootprintLuminance = luminance(pixelAt(frame, 239, 250));
  INFO("new footprint pixel (276,250) luminance = " << newFootprintLuminance);
  INFO("old footprint pixel (239,250) luminance = " << oldFootprintLuminance);
  CHECK(newFootprintLuminance == 0);
  CHECK(oldFootprintLuminance > 30);
}

TEST_CASE("Directional shadow: changing the light direction moves the footprint via the same formula (P10 Group A "
          "check 3)",
          "[image_regression][gpu][shadow]") {
  auto rigOpt = setUpShadowTestRig();
  REQUIRE(rigOpt.has_value());
  ShadowTestRig& rig = *rigOpt;

  writeCamera(rig);
  writeDirectionalLight(rig, kLightDirectionB());
  const PixelBuffer frame = renderShadowScene(rig, Vec3{0.0f, 1.5f, 0.0f}, /*includeProbe=*/false);

  const int mirroredFootprintLuminance = luminance(pixelAt(frame, 276, 250));
  INFO("mirrored-light footprint pixel (276,250) luminance = " << mirroredFootprintLuminance);
  CHECK(mirroredFootprintLuminance == 0);
}

TEST_CASE("Directional shadow: a point outside the shadow map's own light-space volume is always lit, regardless "
          "of any occluder (P10 Group A check 4)",
          "[image_regression][gpu][shadow]") {
  auto rigOpt = setUpShadowTestRig();
  REQUIRE(rigOpt.has_value());
  ShadowTestRig& rig = *rigOpt;

  writeCamera(rig);
  writeDirectionalLight(rig, kLightDirectionA());
  const PixelBuffer frame = renderShadowScene(rig, Vec3{0.0f, 1.5f, 0.0f}, /*includeProbe=*/true);

  const int outOfBoundsLuminance = luminance(pixelAt(frame, 415, 133));
  INFO("out-of-bounds probe pixel (415,133) luminance = " << outOfBoundsLuminance);
  CHECK(outOfBoundsLuminance > 30);
}

TEST_CASE("Directional shadow: a freshly-created ShadowMap and the same ShadowMap reused on a later frame produce "
          "byte-identical output (P10 Group A check 5)",
          "[image_regression][gpu][shadow]") {
  auto rigOpt = setUpShadowTestRig();
  REQUIRE(rigOpt.has_value());
  ShadowTestRig& rig = *rigOpt;

  writeCamera(rig);
  writeDirectionalLight(rig, kLightDirectionA());

  // (i) the very first frame against a freshly-created ShadowMap.
  const PixelBuffer firstFrame = renderShadowScene(rig, std::nullopt, /*includeProbe=*/false);
  // One intervening frame, reusing the same ShadowMap.
  static_cast<void>(renderShadowScene(rig, std::nullopt, /*includeProbe=*/false));
  // (ii) a later frame, reusing that same ShadowMap.
  const PixelBuffer laterFrame = renderShadowScene(rig, std::nullopt, /*includeProbe=*/false);

  const std::size_t firstDiff = firstDifferingByte(firstFrame, laterFrame);
  INFO("first differing byte index (== rgba8.size() means byte-identical) = " << firstDiff);
  CHECK(firstDiff == firstFrame.rgba8.size());
}

TEST_CASE("Directional shadow leaves the IBL/ambient term untouched: shadowed vs. unshadowed vs. no-light "
          "(P10 Group B checks 1-2)",
          "[image_regression][gpu][shadow][pbr_ibl]") {
  atlantis::runtime::BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_ibl_material_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_ibl_material_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_ibl_material_demo_scene_MANIFEST_PATH;
  const std::string unlit = ATLANTIS_IBL_DEMO_UNLIT_TEXTURED_SHADER_DIR;
  config.unlitTexturedVertexShaderSpirvPath = unlit + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath = unlit + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath = unlit + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath = unlit + "/textured_quad.frag.refl.json";
  const std::string lit = ATLANTIS_IBL_DEMO_LIT_TEXTURED_SHADER_DIR;
  config.litTexturedVertexShaderSpirvPath = lit + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath = lit + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath = lit + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath = lit + "/lit_textured.frag.refl.json";
  const std::string direct = ATLANTIS_IBL_DEMO_PBR_DIRECT_LIT_SHADER_DIR;
  config.pbrDirectLitVertexShaderSpirvPath = direct + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath = direct + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath = direct + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath = direct + "/pbr_direct_lit.frag.refl.json";
  const std::string ibl = ATLANTIS_IBL_DEMO_PBR_IBL_SHADER_DIR;
  config.pbrIblVertexShaderSpirvPath = ibl + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath = ibl + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath = ibl + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath = ibl + "/pbr_ibl.frag.refl.json";
  const std::string sky = ATLANTIS_IBL_DEMO_SKY_SHADER_DIR;
  config.skyVertexShaderSpirvPath = sky + "/sky.vert.spv";
  config.skyVertexShaderReflectionPath = sky + "/sky.vert.refl.json";
  config.skyFragmentShaderSpirvPath = sky + "/sky.frag.spv";
  config.skyFragmentShaderReflectionPath = sky + "/sky.frag.refl.json";
  const std::string shadowCast = ATLANTIS_IBL_DEMO_SHADOW_CAST_SHADER_DIR;
  config.shadowCastVertexShaderSpirvPath = shadowCast + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath = shadowCast + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath = shadowCast + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath = shadowCast + "/shadow_cast.frag.refl.json";
  const std::string output = ATLANTIS_IBL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR;
  config.outputTransformUnormVertexShaderSpirvPath = output + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath = output + "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath = output + "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath = output + "/output_transform_unorm.frag.refl.json";
  config.environmentArtifactPath = ATLANTIS_IBL_DEMO_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_IBL_DEMO_ENVIRONMENT_METADATA_PATH;

  auto fixtureResult = atlantis::image_regression::setUpIblMaterialDemoFixture(config);
  REQUIRE(fixtureResult.isOk());
  IblMaterialDemoFixture& fixture = fixtureResult.value();

  // Publish the environment through the fixture's own real path (one full
  // renderIblMaterialDemoFrame() call), mirroring sky_background_gpu_tests.cpp's
  // own established precedent -- its own scene content is otherwise
  // irrelevant here (this TEST_CASE drives its own ground/occluder
  // DrawItems against the fixture's already-realized environment).
  const auto publishResult = atlantis::image_regression::renderIblMaterialDemoFrame(fixture);
  REQUIRE(publishResult.isOk());
  REQUIRE(fixture.environmentLightingResources.has_value());

  auto groundMeshResult = makeQuadMesh(*fixture.device, 0.0f, 0.0f, 6.0f);
  REQUIRE(groundMeshResult.isOk());
  Mesh groundMesh = std::move(groundMeshResult.value());
  auto occluderMeshResult =
      createMesh(*fixture.device, VertexInputLayout{}, kCubeVertices, sizeof(kCubeVertices), kCubeIndices, 36);
  REQUIRE(occluderMeshResult.isOk());
  Mesh occluderMesh = std::move(occluderMeshResult.value());

  constexpr std::uint32_t kTexExtent = 2;
  const std::vector<std::uint8_t> pixelBytes(static_cast<std::size_t>(kTexExtent) * kTexExtent * 4, 0xFF);
  auto textureResult = fixture.device->createSampledTexture(
      SampledTextureCreateParams{.extent = {kTexExtent, kTexExtent}, .format = SampledTextureFormat::Rgba8Srgb});
  REQUIRE(textureResult.isOk());
  auto stagingResult = fixture.device->createBuffer(
      {.purpose = BufferPurpose::Staging, .sizeBytes = static_cast<std::size_t>(kTexExtent) * kTexExtent * 4});
  REQUIRE(stagingResult.isOk());
  std::memcpy(stagingResult.value()->mappedData(), pixelBytes.data(), pixelBytes.size());
  auto samplerResult =
      fixture.device->createSampler(SamplerCreateParams{.filter = Filter::Linear, .addressMode = AddressMode::Repeat});
  REQUIRE(samplerResult.isOk());
  {
    auto uploadCommandListResult = fixture.device->createCommandList();
    REQUIRE(uploadCommandListResult.isOk());
    atlantis::render_graph::RenderGraphBuilder builder;
    const auto resource = builder.declareResource("shadow-ibl-texture-upload");
    const auto pass = builder.declarePass("ShadowIblTextureUpload");
    builder.writes(pass, resource, atlantis::rhi::ResourceState::TransferDestination);
    builder.setExecute(pass, [&stagingResult, &textureResult](CommandList& cmd) {
      cmd.copyBufferToTexture(*stagingResult.value(), *textureResult.value());
    });
    auto compileResult = builder.compile();
    REQUIRE(compileResult.isOk());
    const std::vector<atlantis::render_graph::ResourceBinding> bindings{
        {.resource = compileResult.value().resourceAt(0),
         .sampledTexture = textureResult.value().get(),
         .finalState = atlantis::rhi::ResourceState::ShaderRead}};
    atlantis::render_graph::execute(compileResult.value(), bindings, *uploadCommandListResult.value());
    auto uploadTargetResult = fixture.offscreenTarget->acquireTarget();
    REQUIRE(uploadTargetResult.isOk());
    auto submitResult =
        fixture.device->submit(std::move(uploadCommandListResult.value()), *uploadTargetResult.value());
    REQUIRE(submitResult.isOk());
    REQUIRE(fixture.device->waitIdle().isOk());
  }

  // Ibl-bound: pbr_ibl.slang is the only shader with any ambient/IBL term
  // at all (pbr_direct_lit.slang's own fragmentMain() has none, by
  // design) -- Group B's own R3 (light-off) reference needs one to
  // isolate.
  auto materialResult = createMaterial(
      *fixture.device,
      {.vertexShader = {.spirvWords = fixture.pbrIblVertexSpirv.data(), .wordCount = fixture.pbrIblVertexSpirv.size()},
       .fragmentShader = {.spirvWords = fixture.pbrIblFragmentSpirv.data(),
                           .wordCount = fixture.pbrIblFragmentSpirv.size()},
       .vertexInputLayout = fixture.pbrIblVertexInputLayout,
       .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = 96,
       .sampledTextureBindingCount = 4},
      textureResult.value().get(), samplerResult.value().get(), MaterialPushConstantLayout::PbrDirectLit,
      std::array<float, 4>{0.8f, 0.8f, 0.8f, 1.0f}, 0.0f, 0.8f, MaterialEnvironmentBinding::Ibl);
  REQUIRE(materialResult.isOk());
  Material material = std::move(materialResult.value());

  const auto renderVariant = [&](bool castOccluder, bool hasLight) -> PixelBuffer {
    auto acquireResult = fixture.offscreenTarget->acquireTarget();
    REQUIRE(acquireResult.isOk());
    std::unique_ptr<atlantis::rhi::RenderTarget> target = std::move(acquireResult.value());

    // Camera (same as Group A) and light (check 1's own direction, or
    // none for R3) written directly, every render -- this TEST_CASE
    // never relies on the fixture's own scene-driven camera/lighting.
    {
      constexpr float eyeX = 0.0f, eyeY = 6.0f, eyeZ = 10.0f;
      float fx = -eyeX, fy = -eyeY, fz = -eyeZ;
      const float fLen = std::sqrt(fx * fx + fy * fy + fz * fz);
      fx /= fLen;
      fy /= fLen;
      fz /= fLen;
      const Mat4 view = lookAtMatrixFromForward(fx, fy, fz, eyeX, eyeY, eyeZ);
      const Mat4 projection = perspectiveMatrixDirect(kFovYRadians, 1.0f, kNearZ, kFarZ);
      auto* cameraData = static_cast<float*>(fixture.cameraBuffer->mappedData());
      for (std::size_t i = 0; i < 16; ++i) cameraData[i] = view[i];
      for (std::size_t i = 0; i < 16; ++i) cameraData[16 + i] = projection[i];

      FrameLightingData lighting;
      const Vec3 direction = kLightDirectionA();
      if (hasLight) {
        lighting.directionalLightCount = 1;
        lighting.directionalLights[0].direction[0] = direction.x;
        lighting.directionalLights[0].direction[1] = direction.y;
        lighting.directionalLights[0].direction[2] = direction.z;
        lighting.directionalLights[0].color[0] = 1.0f;
        lighting.directionalLights[0].color[1] = 1.0f;
        lighting.directionalLights[0].color[2] = 1.0f;
        lighting.directionalLights[0].intensity = 3.0f;
      }
      auto* lightingData = reinterpret_cast<FrameLightingData*>(cameraData + 32);
      *lightingData = lighting;

      const CameraMatrices lightSpaceMatrices = computeShadowLightSpaceMatrices(direction);
      const Mat4& lightSpaceView = lightSpaceMatrices.view;
      const Mat4& lightSpaceProjection = lightSpaceMatrices.projection;
      float* lightSpaceTail = cameraData + 116;
      std::memcpy(lightSpaceTail, lightSpaceView.data(), sizeof(float) * 16);
      std::memcpy(lightSpaceTail + 16, lightSpaceProjection.data(), sizeof(float) * 16);
      auto* shadowLightSpaceData = static_cast<float*>(fixture.shadowLightSpaceBuffer->mappedData());
      std::memcpy(shadowLightSpaceData, lightSpaceView.data(), sizeof(float) * 16);
      std::memcpy(shadowLightSpaceData + 16, lightSpaceProjection.data(), sizeof(float) * 16);

      // irradianceSh (byte offset 320, float index 80): the fixture's own
      // already-realized environment SH coefficients, unconditionally
      // (R3's own IBL/ambient reference needs this regardless of
      // directional-light state).
      std::memcpy(cameraData + 80, fixture.environmentLightingResources->irradianceSh.data(), sizeof(float) * 36);
    }

    DrawItem groundItem;
    groundItem.mesh = &groundMesh;
    groundItem.material = &material;
    groundItem.objectToWorld = identityMatrix();
    const std::vector<DrawItem> drawItems{groundItem};

    std::vector<DrawItem> shadowCasterDrawItems;
    DrawItem occluderItem;
    if (castOccluder) {
      occluderItem.mesh = &occluderMesh;
      occluderItem.material = &material;
      occluderItem.objectToWorld = translationMatrix(0.0f, 1.5f, 0.0f);
      shadowCasterDrawItems.push_back(occluderItem);
    }

    auto commandListResult = fixture.device->createCommandList();
    REQUIRE(commandListResult.isOk());
    std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

    Renderer renderer;
    const atlantis::renderer::EnvironmentLighting lightingView = fixture.environmentLightingResources->borrowedView();
    renderer.drawFrame(*commandList, *target, *fixture.depthTexture, *fixture.cameraBuffer, drawItems,
                        atlantis::rhi::ResourceState::TransferSource, *fixture.hdrColorTarget,
                        *fixture.fullscreenTriangleVertexBuffer, *fixture.fullscreenTriangleIndexBuffer,
                        *fixture.outputTransformPipeline, *fixture.outputTransformSampler, &lightingView, nullptr,
                        *fixture.shadowMap, *fixture.shadowMapSampler, *fixture.shadowCastPipeline,
                        *fixture.shadowLightSpaceBuffer, shadowCasterDrawItems);

    atlantis::render_graph::RenderGraphBuilder copyBuilder;
    const auto copyResource = copyBuilder.declareResource("color-copy");
    const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
    copyBuilder.writes(copyPass, copyResource, atlantis::rhi::ResourceState::TransferSource);
    copyBuilder.setExecute(copyPass, [&target, &fixture](CommandList& cmd) {
      cmd.copyRenderTargetToBuffer(*target, *fixture.readbackBuffer);
    });
    auto copyCompileResult = copyBuilder.compile();
    REQUIRE(copyCompileResult.isOk());
    const std::vector<atlantis::render_graph::ResourceBinding> copyBindings{
        {.resource = copyCompileResult.value().resourceAt(0),
         .target = target.get(),
         .incomingState = atlantis::rhi::ResourceState::TransferSource}};
    atlantis::render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

    auto submitResult = fixture.device->submit(std::move(commandList), *target);
    REQUIRE(submitResult.isOk());
    REQUIRE(fixture.device->waitIdle().isOk());

    PixelBuffer result;
    result.width = kIblMaterialDemoExtentPixels;
    result.height = kIblMaterialDemoExtentPixels;
    const std::size_t byteCount = static_cast<std::size_t>(kIblMaterialDemoExtentPixels) * kIblMaterialDemoExtentPixels * 4;
    const auto* readbackData = static_cast<const std::uint8_t*>(fixture.readbackBuffer->mappedData());
    result.rgba8.assign(readbackData, readbackData + byteCount);
    target.reset();
    return result;
  };

  const PixelBuffer r1Shadowed = renderVariant(/*castOccluder=*/true, /*hasLight=*/true);
  const PixelBuffer r2Unshadowed = renderVariant(/*castOccluder=*/false, /*hasLight=*/true);
  const PixelBuffer r3NoLight = renderVariant(/*castOccluder=*/false, /*hasLight=*/false);

  const int r1Luminance = luminance(pixelAt(r1Shadowed, 239, 250));
  const int r2Luminance = luminance(pixelAt(r2Unshadowed, 239, 250));
  const int r3Luminance = luminance(pixelAt(r3NoLight, 239, 250));
  INFO("R1 (shadowed) luminance = " << r1Luminance);
  INFO("R2 (unshadowed) luminance = " << r2Luminance);
  INFO("R3 (no light) luminance = " << r3Luminance);

  CHECK(r2Luminance - r1Luminance > 15);
  CHECK(std::abs(r1Luminance - r3Luminance) <= 5);
}
