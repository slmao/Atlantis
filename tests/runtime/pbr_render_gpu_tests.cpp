// Plan 0023 Milestone 7: real-GPU rendering coverage for
// MaterialKind::PbrDirectLit -- parameter transmission, a mixed
// UnlitTextured+LitTextured+PbrDirectLit scene in one frame, and the
// Spec 0022 regression (a runtime Light/Transform change reflected on
// the next frame). GPU-required (real Device, real Pipeline/Sampler/
// SampledTexture creation, real draw+readback), zero window
// (OffscreenTarget), matching headless_rendering_gpu_tests.cpp's own
// established pattern.

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
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/shader_system/reflection_loader.h>
#include <atlantis/shader_system/rhi_integration/vertex_input_mapping.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include "../image_regression/support/tone_mapping_reference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using atlantis::renderer::createMaterial;
using atlantis::renderer::createMesh;
using atlantis::renderer::DrawItem;
using atlantis::renderer::Material;
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
using atlantis::rhi::HdrColorTarget;
using atlantis::rhi::HdrFormat;
using atlantis::rhi::OffscreenTarget;
using atlantis::rhi::Pipeline;
using atlantis::rhi::SampledTextureCreateParams;
using atlantis::rhi::SampledTextureFormat;
using atlantis::rhi::SamplerCreateParams;
using atlantis::rhi::VertexInputLayout;
using atlantis::runtime::CameraWorldPositionData;
using atlantis::runtime::computePbrDirectLighting;
using atlantis::runtime::FrameLightingData;
using atlantis::runtime::Vec3;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;
using atlantis::vulkan_backend::createDevice;
using atlantis::vulkan_backend::DeviceCreateParams;

constexpr Extent2D kExtent{64, 64};
constexpr Format kColorFormat = Format::Rgba8Unorm;

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

// position/uv/normal only -- no color, unlike the mesh-artifact's own
// real 44-byte stride (Plan 0020/ADR-0063). This file constructs raw
// vertex buffers directly (never round-tripping through the Asset
// System's own cooked mesh format, matching headless_rendering_gpu_tests.cpp's
// own established precedent), so it is free to choose its own minimal,
// sufficient layout -- position@0/uv@1 (UnlitTextured) and
// position@0/uv@1/normal@2 (LitTextured/PbrDirectLit) are both valid
// sub-schemas of this SAME interleaved byte layout, applied via
// independent VertexInputLayout values against the one shared Mesh.
struct Vertex {
  float position[3];
  float uv[2];
  float normal[3];
};

constexpr Vertex kTriangleVertices[3] = {
    {{0.0f, -0.5f, 0.0f}, {0.5f, 1.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
};
constexpr std::uint16_t kTriangleIndices[3] = {0, 1, 2};
constexpr std::array<float, 16> kIdentityMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

[[nodiscard]] std::optional<VertexInputLayout> unlitTexturedLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

[[nodiscard]] std::optional<VertexInputLayout> litOrPbrLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
      MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)},
      MeshVertexAttributeSchema{.location = 2, .offsetBytes = offsetof(Vertex, normal)},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex));
  if (result.isErr()) return std::nullopt;
  return result.value();
}

// Plan 0027 Milestone 9 (ADR-0072 D-3): shadow_cast.slang's own real,
// position-only VertexInput -- mirrors runtime_application.cpp's own
// identical shadowCastVertexLayout(), against this file's own Vertex
// struct above (position/uv/normal), not the mesh-artifact's 44-byte one.
[[nodiscard]] std::optional<VertexInputLayout> shadowCastVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},
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

// The Camera/Lighting/CameraWorldPosition/light-space region (Plan 0023
// Milestone 2, widened by Plan 0027 Milestone 9), built directly --
// mirrors runtime_application.cpp's own real per-frame write sites
// exactly (view/projection at floats 0-31, FrameLightingData at byte
// offset 128, CameraWorldPositionData at byte offset 304), never a
// second, independent layout. CameraMatrices/FrameLightingData/
// CameraWorldPositionData themselves stay byte-for-byte unmodified;
// bytes 320-464 (the _shadowPad tail) are left untouched, matching
// Runtime's own runFrame() which never writes them either.
void writeCameraBuffer(atlantis::rhi::Buffer& buffer, const std::array<float, 16>& view,
                        const std::array<float, 16>& projection, const FrameLightingData& lighting,
                        const CameraWorldPositionData& cameraWorldPosition) {
  auto* data = static_cast<std::byte*>(buffer.mappedData());
  std::memcpy(data, view.data(), sizeof(float) * 16);
  std::memcpy(data + sizeof(float) * 16, projection.data(), sizeof(float) * 16);
  std::memcpy(data + sizeof(float) * 32, &lighting, sizeof(FrameLightingData));
  std::memcpy(data + sizeof(float) * 32 + sizeof(FrameLightingData), &cameraWorldPosition,
              sizeof(CameraWorldPositionData));
  // Plan 0027 Milestone 9 (ADR-0072 D-1/P5): the no-directional-light
  // sentinel (identity view+projection at the fixed 464-byte tail
  // offset) is written unconditionally here too, exactly like
  // Runtime's own runFrame() -- none of this file's own TEST_CASEs
  // configure a real shadow-casting occluder, so the shadow map is
  // always cleared to its own maximum depth (1.0) and computeShadowFactor()
  // must therefore always evaluate to "fully lit," matching every one
  // of this file's existing, unmodified pixel expectations.
  std::memcpy(data + 464, kIdentityMatrix.data(), sizeof(float) * 16);
  std::memcpy(data + 464 + sizeof(float) * 16, kIdentityMatrix.data(), sizeof(float) * 16);
}

[[nodiscard]] FrameLightingData oneDirectionalLight(float intensity) {
  FrameLightingData lighting;
  lighting.directionalLightCount = 1;
  lighting.directionalLights[0].direction[2] = -1.0f;  // L = -direction = (0,0,1), matching the triangle's normal
  lighting.directionalLights[0].color[0] = 1.0f;
  lighting.directionalLights[0].color[1] = 1.0f;
  lighting.directionalLights[0].color[2] = 1.0f;
  lighting.directionalLights[0].intensity = intensity;
  return lighting;
}

struct PbrTestRig {
  std::unique_ptr<Device> device;
  Mesh mesh;
  std::unique_ptr<atlantis::rhi::SampledTexture> texture;
  std::unique_ptr<atlantis::rhi::Sampler> sampler;
  VertexInputLayout pbrLayout;
  std::vector<std::uint32_t> pbrVertexSpirv;
  std::vector<std::uint32_t> pbrFragmentSpirv;
};

// Shared setup for every TEST_CASE below: a real Device, one shared
// triangle Mesh, one shared solid-Srgb base-color texture+sampler
// (dedicated to this file, never the Asset System's own cooked
// textured_quad_srgb -- this file constructs everything directly,
// matching headless_rendering_gpu_tests.cpp's own established
// precedent), and the real, compiled pbr_direct_lit shader pair.
[[nodiscard]] std::optional<PbrTestRig> setUpPbrTestRig(const std::string& applicationName) {
  auto deviceResult = createDevice(DeviceCreateParams{.applicationName = applicationName, .enableValidationLayers = true});
  if (deviceResult.isErr()) return std::nullopt;
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  auto meshResult = createMesh(*device, VertexInputLayout{}, kTriangleVertices, sizeof(kTriangleVertices),
                                kTriangleIndices, 3);
  if (meshResult.isErr()) return std::nullopt;

  constexpr std::uint32_t kTexExtent = 2;
  const std::vector<std::uint8_t> pixelBytes(static_cast<std::size_t>(kTexExtent) * kTexExtent * 4, 0x80);
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

  // Upload the texture via a real one-pass RenderGraph, matching every
  // other composition root's own identical upload pattern.
  auto commandListResult = device->createCommandList();
  if (commandListResult.isErr()) return std::nullopt;
  {
    atlantis::render_graph::RenderGraphBuilder builder;
    const auto resource = builder.declareResource("pbr-test-texture-upload");
    const auto pass = builder.declarePass("PbrTestTextureUpload");
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
    atlantis::render_graph::execute(compileResult.value(), bindings, *commandListResult.value());
  }
  auto offscreenForUpload = device->createOffscreenTarget({.extent = kExtent, .format = kColorFormat});
  if (offscreenForUpload.isErr()) return std::nullopt;
  auto uploadTarget = offscreenForUpload.value()->acquireTarget();
  if (uploadTarget.isErr()) return std::nullopt;
  auto submitResult = device->submit(std::move(commandListResult.value()), *uploadTarget.value());
  if (submitResult.isErr()) return std::nullopt;
  if (device->waitIdle().isErr()) return std::nullopt;

  auto pbrVertexSpirv = loadSpirvFile(std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv");
  auto pbrFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv");
  if (!pbrVertexSpirv.has_value() || !pbrFragmentSpirv.has_value()) return std::nullopt;
  auto pbrVertexReflectionResult = loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) +
                                                           "/pbr_direct_lit.vert.refl.json");
  if (pbrVertexReflectionResult.isErr()) return std::nullopt;
  const auto pbrLayout = litOrPbrLayout(pbrVertexReflectionResult.value());
  if (!pbrLayout.has_value()) return std::nullopt;

  return PbrTestRig{std::move(device),
                     std::move(meshResult.value()),
                     std::move(textureResult.value()),
                     std::move(samplerResult.value()),
                     *pbrLayout,
                     std::move(*pbrVertexSpirv),
                     std::move(*pbrFragmentSpirv)};
}

[[nodiscard]] std::array<std::uint8_t, 4> readCenterPixel(const std::vector<std::uint8_t>& rgba8, std::uint32_t width,
                                                           std::uint32_t height) {
  const std::size_t offset = (static_cast<std::size_t>(height / 2) * width + width / 2) * 4;
  return {rgba8[offset], rgba8[offset + 1], rgba8[offset + 2], rgba8[offset + 3]};
}

// Renders one frame of the shared triangle through `material` into a
// fresh OffscreenTarget, reads it back, and returns the full RGBA8
// buffer. `commandListReuse` is deliberately absent -- each call gets
// its own fresh CommandList/OffscreenTarget/readback Buffer, matching
// this file's own single-shot-per-call convention (no multi-cycle
// fixture needed for these three, independent TEST_CASEs).
[[nodiscard]] std::optional<std::vector<std::uint8_t>> renderOneFrame(Device& device, atlantis::rhi::Buffer& cameraBuffer,
                                                                        const std::vector<DrawItem>& drawItems,
                                                                        Format finalFormat = kColorFormat) {
  auto depthTextureResult = device.createTexture({.extent = kExtent, .format = DepthFormat::D32Sfloat});
  if (depthTextureResult.isErr()) return std::nullopt;

  auto offscreenResult = device.createOffscreenTarget({.extent = kExtent, .format = finalFormat});
  if (offscreenResult.isErr()) return std::nullopt;
  auto acquireResult = offscreenResult.value()->acquireTarget();
  if (acquireResult.isErr()) return std::nullopt;
  std::unique_ptr<atlantis::rhi::RenderTarget> target = std::move(acquireResult.value());

  auto readbackBufferResult = device.createBuffer(
      {.purpose = BufferPurpose::Readback, .sizeBytes = static_cast<std::size_t>(kExtent.width) * kExtent.height * 4});
  if (readbackBufferResult.isErr()) return std::nullopt;
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer = std::move(readbackBufferResult.value());

  // Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3/D-6): this call's own HDR
  // intermediate, fullscreen-triangle geometry/sampler, and output-
  // transform Pipeline -- everything fresh per call, matching this
  // function's own established "no multi-cycle fixture" convention
  // above.
  auto hdrColorTargetResult = device.createHdrColorTarget({.extent = kExtent});
  if (hdrColorTargetResult.isErr()) return std::nullopt;
  std::unique_ptr<HdrColorTarget> hdrColorTarget = std::move(hdrColorTargetResult.value());

  const float fullscreenTriangleVertices[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
  auto fullscreenTriangleVertexBufferResult =
      device.createBuffer({.purpose = BufferPurpose::Vertex, .sizeBytes = sizeof(fullscreenTriangleVertices)});
  if (fullscreenTriangleVertexBufferResult.isErr()) return std::nullopt;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleVertexBuffer =
      std::move(fullscreenTriangleVertexBufferResult.value());
  std::memcpy(fullscreenTriangleVertexBuffer->mappedData(), fullscreenTriangleVertices,
              sizeof(fullscreenTriangleVertices));

  const std::uint16_t fullscreenTriangleIndices[3] = {0, 1, 2};
  auto fullscreenTriangleIndexBufferResult =
      device.createBuffer({.purpose = BufferPurpose::Index, .sizeBytes = sizeof(fullscreenTriangleIndices)});
  if (fullscreenTriangleIndexBufferResult.isErr()) return std::nullopt;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleIndexBuffer =
      std::move(fullscreenTriangleIndexBufferResult.value());
  std::memcpy(fullscreenTriangleIndexBuffer->mappedData(), fullscreenTriangleIndices,
              sizeof(fullscreenTriangleIndices));

  auto outputTransformSamplerResult =
      device.createSampler({.filter = Filter::Linear, .addressMode = AddressMode::ClampToEdge});
  if (outputTransformSamplerResult.isErr()) return std::nullopt;
  std::unique_ptr<atlantis::rhi::Sampler> outputTransformSampler = std::move(outputTransformSamplerResult.value());

  const bool useSrgbVariant = finalFormat == Format::Rgba8Srgb || finalFormat == Format::Bgra8Srgb;
  const std::string outputTransformShaderDirectory =
      useSrgbVariant ? ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR
                     : ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR;
  const std::string outputTransformShaderName =
      useSrgbVariant ? "output_transform_srgb" : "output_transform_unorm";
  const auto outputTransformVertexSpirv =
      loadSpirvFile(outputTransformShaderDirectory + "/" + outputTransformShaderName + ".vert.spv");
  const auto outputTransformFragmentSpirv =
      loadSpirvFile(outputTransformShaderDirectory + "/" + outputTransformShaderName + ".frag.spv");
  if (!outputTransformVertexSpirv.has_value() || !outputTransformFragmentSpirv.has_value()) return std::nullopt;
  auto outputTransformVertexReflectionResult = loadReflectionMetadata(
      outputTransformShaderDirectory + "/" + outputTransformShaderName + ".vert.refl.json");
  if (outputTransformVertexReflectionResult.isErr()) return std::nullopt;
  const auto outputTransformVertexInputLayout =
      outputTransformVertexLayout(outputTransformVertexReflectionResult.value());
  if (!outputTransformVertexInputLayout.has_value()) return std::nullopt;

  auto outputTransformPipelineResult = device.createPipeline(
      {.vertexShader = {.spirvWords = outputTransformVertexSpirv->data(),
                         .wordCount = outputTransformVertexSpirv->size()},
       .fragmentShader = {.spirvWords = outputTransformFragmentSpirv->data(),
                           .wordCount = outputTransformFragmentSpirv->size()},
       .vertexInputLayout = *outputTransformVertexInputLayout,
       .colorFormat = finalFormat,
       .sampledTextureBindingCount = 1,
       .hasCameraUniformBinding = false,
       .hasDepthAttachment = false});
  if (outputTransformPipelineResult.isErr()) return std::nullopt;
  std::unique_ptr<Pipeline> outputTransformPipeline = std::move(outputTransformPipelineResult.value());

  // Plan 0027 Milestone 9 (ADR-0072 D-1/P9e): a minimal, always-possible
  // real ShadowMap/Sampler/Pipeline/Buffer -- shadowCasterDrawItems
  // stays empty (none of this file's TEST_CASEs configure a real
  // occluder), so the shadow map is always cleared to its own maximum
  // depth (1.0) and never darkens any of this file's existing pixel
  // expectations (see writeCameraBuffer()'s own light-space sentinel
  // write above).
  auto shadowMapResult = device.createShadowMap({.extent = {1024, 1024}});
  if (shadowMapResult.isErr()) return std::nullopt;
  std::unique_ptr<atlantis::rhi::ShadowMap> shadowMap = std::move(shadowMapResult.value());

  auto shadowMapSamplerResult =
      device.createSampler({.filter = Filter::Nearest, .addressMode = AddressMode::ClampToEdge});
  if (shadowMapSamplerResult.isErr()) return std::nullopt;
  std::unique_ptr<atlantis::rhi::Sampler> shadowMapSampler = std::move(shadowMapSamplerResult.value());

  const auto shadowCastVertexSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv");
  const auto shadowCastFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv");
  if (!shadowCastVertexSpirv.has_value() || !shadowCastFragmentSpirv.has_value()) return std::nullopt;
  auto shadowCastVertexReflectionResult =
      loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json");
  if (shadowCastVertexReflectionResult.isErr()) return std::nullopt;
  const auto shadowCastVertexInputLayout = shadowCastVertexLayout(shadowCastVertexReflectionResult.value());
  if (!shadowCastVertexInputLayout.has_value()) return std::nullopt;

  auto shadowCastPipelineResult = device.createPipeline(
      {.vertexShader = {.spirvWords = shadowCastVertexSpirv->data(), .wordCount = shadowCastVertexSpirv->size()},
       .fragmentShader = {.spirvWords = shadowCastFragmentSpirv->data(),
                           .wordCount = shadowCastFragmentSpirv->size()},
       .vertexInputLayout = *shadowCastVertexInputLayout,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .sampledTextureBindingCount = 0,
       .hasCameraUniformBinding = true,
       .hasDepthAttachment = true,
       .depthWriteEnabled = true,
       .hasColorAttachment = false});
  if (shadowCastPipelineResult.isErr()) return std::nullopt;
  std::unique_ptr<Pipeline> shadowCastPipeline = std::move(shadowCastPipelineResult.value());

  auto shadowLightSpaceBufferResult = device.createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = 128});
  if (shadowLightSpaceBufferResult.isErr()) return std::nullopt;
  std::unique_ptr<atlantis::rhi::Buffer> shadowLightSpaceBuffer = std::move(shadowLightSpaceBufferResult.value());

  auto commandListResult = device.createCommandList();
  if (commandListResult.isErr()) return std::nullopt;
  std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

  Renderer renderer;
  renderer.drawFrame(*commandList, *target, *depthTextureResult.value(), cameraBuffer, drawItems,
                      atlantis::rhi::ResourceState::TransferSource, *hdrColorTarget, *fullscreenTriangleVertexBuffer,
                      *fullscreenTriangleIndexBuffer, *outputTransformPipeline, *outputTransformSampler, nullptr,
                      nullptr, *shadowMap, *shadowMapSampler, *shadowCastPipeline, *shadowLightSpaceBuffer, {});

  atlantis::render_graph::RenderGraphBuilder copyBuilder;
  const auto copyResource = copyBuilder.declareResource("color-copy");
  const auto copyPass = copyBuilder.declarePass("copy-to-buffer");
  copyBuilder.writes(copyPass, copyResource, atlantis::rhi::ResourceState::TransferSource);
  copyBuilder.setExecute(copyPass, [&target, &readbackBuffer](CommandList& cmd) {
    cmd.copyRenderTargetToBuffer(*target, *readbackBuffer);
  });
  auto copyCompileResult = copyBuilder.compile();
  if (copyCompileResult.isErr()) return std::nullopt;
  const std::vector<atlantis::render_graph::ResourceBinding> copyBindings{
      {.resource = copyCompileResult.value().resourceAt(0),
       .target = target.get(),
       .incomingState = atlantis::rhi::ResourceState::TransferSource}};
  atlantis::render_graph::execute(copyCompileResult.value(), copyBindings, *commandList);

  auto submitResult = device.submit(std::move(commandList), *target);
  if (submitResult.isErr()) return std::nullopt;
  if (device.waitIdle().isErr()) return std::nullopt;

  const auto* readbackData = static_cast<const std::uint8_t*>(readbackBuffer->mappedData());
  if (readbackData == nullptr) return std::nullopt;
  return std::vector<std::uint8_t>(readbackData, readbackData + static_cast<std::size_t>(kExtent.width) * kExtent.height * 4);
}

}  // namespace

TEST_CASE("PbrDirectLit parameter transmission: two draws differing only in metallicFactor produce different "
          "captured pixels",
          "[runtime][gpu][pbr][render]") {
  auto rigOpt = setUpPbrTestRig("Atlantis PBR Render GPU Tests (parameter transmission)");
  REQUIRE(rigOpt.has_value());
  PbrTestRig& rig = *rigOpt;

  auto cameraBufferResult =
      // Plan 0027 Milestone 9 (ADR-0072 D-9/P9d): widened from 320 to the new 592-byte PBR camera-buffer
      // tail (light-space view+projection, P5) -- CameraMatrices/FrameLightingData/CameraWorldPositionData
      // themselves stay byte-for-byte unmodified (writeCameraBuffer()'s own offsets below are untouched).
      rig.device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = 592});
  REQUIRE(cameraBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer = std::move(cameraBufferResult.value());
  const FrameLightingData lighting = oneDirectionalLight(2.0f);
  writeCameraBuffer(*cameraBuffer, kIdentityMatrix, kIdentityMatrix, lighting, CameraWorldPositionData{0, 0, 5, 0});

  auto makeMaterial = [&](float metallicFactor, float roughnessFactor) -> std::optional<Material> {
    auto materialResult = createMaterial(
        *rig.device,
        {.vertexShader = {.spirvWords = rig.pbrVertexSpirv.data(), .wordCount = rig.pbrVertexSpirv.size()},
         .fragmentShader = {.spirvWords = rig.pbrFragmentSpirv.data(), .wordCount = rig.pbrFragmentSpirv.size()},
         .vertexInputLayout = rig.pbrLayout,
         .colorFormat = HdrFormat::Rgba16Float,  // Plan 0024 Milestone 6/7: geometry Pipeline, not the final target.
         .depthFormat = DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = 96,
         .sampledTextureBindingCount = 2},
        rig.texture.get(), rig.sampler.get(), MaterialPushConstantLayout::PbrDirectLit,
        std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}, metallicFactor, roughnessFactor);
    if (materialResult.isErr()) return std::nullopt;
    return std::move(materialResult.value());
  };

  auto dielectricMaterial = makeMaterial(0.0f, 0.5f);
  auto metallicMaterial = makeMaterial(1.0f, 0.5f);
  REQUIRE(dielectricMaterial.has_value());
  REQUIRE(metallicMaterial.has_value());

  DrawItem item;
  item.mesh = &rig.mesh;
  item.objectToWorld = kIdentityMatrix;

  item.material = &*dielectricMaterial;
  auto dielectricPixels = renderOneFrame(*rig.device, *cameraBuffer, {item});
  REQUIRE(dielectricPixels.has_value());

  item.material = &*metallicMaterial;
  auto metallicPixels = renderOneFrame(*rig.device, *cameraBuffer, {item});
  REQUIRE(metallicPixels.has_value());

  const auto dielectricCenter = readCenterPixel(*dielectricPixels, kExtent.width, kExtent.height);
  const auto metallicCenter = readCenterPixel(*metallicPixels, kExtent.width, kExtent.height);
  CHECK(dielectricCenter != metallicCenter);

  // The same real cross-check, varying ONLY roughnessFactor this time
  // (metallic held fixed) -- confirms roughnessFactor's own real
  // transmission through the push-constant payload independently of
  // metallicFactor's own.
  auto roughMaterial = makeMaterial(0.0f, 0.9f);
  auto smoothMaterial = makeMaterial(0.0f, 0.1f);
  REQUIRE(roughMaterial.has_value());
  REQUIRE(smoothMaterial.has_value());

  item.material = &*roughMaterial;
  auto roughPixels = renderOneFrame(*rig.device, *cameraBuffer, {item});
  REQUIRE(roughPixels.has_value());

  item.material = &*smoothMaterial;
  auto smoothPixels = renderOneFrame(*rig.device, *cameraBuffer, {item});
  REQUIRE(smoothPixels.has_value());

  const auto roughCenter = readCenterPixel(*roughPixels, kExtent.width, kExtent.height);
  const auto smoothCenter = readCenterPixel(*smoothPixels, kExtent.width, kExtent.height);
  CHECK(roughCenter != smoothCenter);
}

TEST_CASE("A mixed UnlitTextured+LitTextured+PbrDirectLit scene renders all three DrawItems in one frame without "
          "error under both final-target format classes, with display-equivalent output",
          "[runtime][gpu][pbr][render]") {
  auto rigOpt = setUpPbrTestRig("Atlantis PBR Render GPU Tests (mixed scene)");
  REQUIRE(rigOpt.has_value());
  PbrTestRig& rig = *rigOpt;

  auto unlitVertexSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv");
  auto unlitFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv");
  REQUIRE(unlitVertexSpirv.has_value());
  REQUIRE(unlitFragmentSpirv.has_value());
  auto unlitVertexReflectionResult = loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) +
                                                             "/textured_quad.vert.refl.json");
  REQUIRE(unlitVertexReflectionResult.isOk());
  const auto unlitLayout = unlitTexturedLayout(unlitVertexReflectionResult.value());
  REQUIRE(unlitLayout.has_value());

  auto litVertexSpirv = loadSpirvFile(std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv");
  auto litFragmentSpirv =
      loadSpirvFile(std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv");
  REQUIRE(litVertexSpirv.has_value());
  REQUIRE(litFragmentSpirv.has_value());
  auto litVertexReflectionResult = loadReflectionMetadata(std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) +
                                                           "/lit_textured.vert.refl.json");
  REQUIRE(litVertexReflectionResult.isOk());
  const auto litLayout = litOrPbrLayout(litVertexReflectionResult.value());
  REQUIRE(litLayout.has_value());

  auto unlitMaterialResult = createMaterial(
      *rig.device,
      {.vertexShader = {.spirvWords = unlitVertexSpirv->data(), .wordCount = unlitVertexSpirv->size()},
       .fragmentShader = {.spirvWords = unlitFragmentSpirv->data(), .wordCount = unlitFragmentSpirv->size()},
       .vertexInputLayout = *unlitLayout,
       .colorFormat = HdrFormat::Rgba16Float,  // Plan 0024 Milestone 6/7: geometry Pipeline, not the final target.
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .sampledTextureBindingCount = 1},
      rig.texture.get(), rig.sampler.get());
  REQUIRE(unlitMaterialResult.isOk());

  auto litMaterialResult = createMaterial(
      *rig.device,
      {.vertexShader = {.spirvWords = litVertexSpirv->data(), .wordCount = litVertexSpirv->size()},
       .fragmentShader = {.spirvWords = litFragmentSpirv->data(), .wordCount = litFragmentSpirv->size()},
       .vertexInputLayout = *litLayout,
       .colorFormat = HdrFormat::Rgba16Float,  // Plan 0024 Milestone 6/7: geometry Pipeline, not the final target.
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = sizeof(float) * 16,
       .sampledTextureBindingCount = 1},
      rig.texture.get(), rig.sampler.get());
  REQUIRE(litMaterialResult.isOk());

  auto pbrMaterialResult = createMaterial(
      *rig.device,
      {.vertexShader = {.spirvWords = rig.pbrVertexSpirv.data(), .wordCount = rig.pbrVertexSpirv.size()},
       .fragmentShader = {.spirvWords = rig.pbrFragmentSpirv.data(), .wordCount = rig.pbrFragmentSpirv.size()},
       .vertexInputLayout = rig.pbrLayout,
       .colorFormat = HdrFormat::Rgba16Float,  // Plan 0024 Milestone 6/7: geometry Pipeline, not the final target.
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = 96,
       .sampledTextureBindingCount = 2},
      rig.texture.get(), rig.sampler.get(), MaterialPushConstantLayout::PbrDirectLit,
      std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, 0.3f);
  REQUIRE(pbrMaterialResult.isOk());

  auto cameraBufferResult =
      // Plan 0027 Milestone 9 (ADR-0072 D-9/P9d): widened from 320 to the new 592-byte PBR camera-buffer
      // tail (light-space view+projection, P5) -- CameraMatrices/FrameLightingData/CameraWorldPositionData
      // themselves stay byte-for-byte unmodified (writeCameraBuffer()'s own offsets below are untouched).
      rig.device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = 592});
  REQUIRE(cameraBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer = std::move(cameraBufferResult.value());
  const FrameLightingData lighting = oneDirectionalLight(2.0f);
  writeCameraBuffer(*cameraBuffer, kIdentityMatrix, kIdentityMatrix, lighting, CameraWorldPositionData{0, 0, 5, 0});

  // Three DrawItems, translated apart along X so each lands in its own
  // distinct screen-space third -- objectToWorld's own translation
  // column (Plan 0014's own established shape), never a new field.
  auto translated = [](float dx) {
    std::array<float, 16> m = kIdentityMatrix;
    m[12] = dx;
    return m;
  };

  DrawItem unlitItem;
  unlitItem.mesh = &rig.mesh;
  unlitItem.material = &unlitMaterialResult.value();
  unlitItem.objectToWorld = translated(-0.6f);

  DrawItem litItem;
  litItem.mesh = &rig.mesh;
  litItem.material = &litMaterialResult.value();
  litItem.objectToWorld = translated(0.0f);

  DrawItem pbrItem;
  pbrItem.mesh = &rig.mesh;
  pbrItem.material = &pbrMaterialResult.value();
  pbrItem.objectToWorld = translated(0.6f);

  const std::vector<DrawItem> drawItems{unlitItem, litItem, pbrItem};
  auto unormPixels = renderOneFrame(*rig.device, *cameraBuffer, drawItems, Format::Rgba8Unorm);
  auto srgbPixels = renderOneFrame(*rig.device, *cameraBuffer, drawItems, Format::Rgba8Srgb);
  REQUIRE(unormPixels.has_value());
  REQUIRE(srgbPixels.has_value());
  REQUIRE(unormPixels->size() == srgbPixels->size());

  // ADR-0068 D-6: the UNORM shader's manual OETF and the sRGB target's
  // fixed-function encode are two implementations of the same display
  // transfer. Rounding may differ by one 8-bit code value; exact byte
  // equality is deliberately not required.
  constexpr int kDisplayEquivalenceChannelTolerance = 1;
  int maximumChannelDifference = 0;
  for (std::size_t i = 0; i < unormPixels->size(); ++i) {
    const int difference = std::abs(static_cast<int>((*unormPixels)[i]) - static_cast<int>((*srgbPixels)[i]));
    maximumChannelDifference = std::max(maximumChannelDifference, difference);
  }
  CHECK(maximumChannelDifference <= kDisplayEquivalenceChannelTolerance);

  // Sample one column inside each translated triangle's own expected
  // screen region (left third / center / right third) at the vertical
  // midline, where kTriangleVertices' own shape guarantees coverage.
  const auto sampleAt = [&](const std::vector<std::uint8_t>& pixels, std::uint32_t x, std::uint32_t y) {
    const std::size_t requestedOffset = (static_cast<std::size_t>(y) * kExtent.width + x) * 4;
    return std::array<std::uint8_t, 4>{pixels[requestedOffset], pixels[requestedOffset + 1],
                                       pixels[requestedOffset + 2], pixels[requestedOffset + 3]};
  };
  const auto hasThreeDrawnRegions = [&](const std::vector<std::uint8_t>& pixels) {
    const auto background = sampleAt(pixels, 0, 0);
    const auto leftPixel = sampleAt(pixels, kExtent.width / 6, kExtent.height / 2);
    const auto centerPixel = sampleAt(pixels, kExtent.width / 2, kExtent.height / 2);
    const auto rightPixel = sampleAt(pixels, kExtent.width - kExtent.width / 6, kExtent.height / 2);
    const auto differsFromBackground = [&](const std::array<std::uint8_t, 4>& pixel) {
      return std::abs(static_cast<int>(pixel[0]) - static_cast<int>(background[0])) > 5 ||
             std::abs(static_cast<int>(pixel[1]) - static_cast<int>(background[1])) > 5 ||
             std::abs(static_cast<int>(pixel[2]) - static_cast<int>(background[2])) > 5;
    };
    return differsFromBackground(leftPixel) && differsFromBackground(centerPixel) &&
           differsFromBackground(rightPixel);
  };
  CHECK(hasThreeDrawnRegions(*unormPixels));
  CHECK(hasThreeDrawnRegions(*srgbPixels));
}

TEST_CASE("Above-1.0 PBR radiance survives the HDR intermediate and follows Reinhard roll-off instead of clipping",
          "[runtime][gpu][pbr][render][hdr][tone_mapping]") {
  auto rigOpt = setUpPbrTestRig("Atlantis PBR Render GPU Tests (HDR roll-off)");
  REQUIRE(rigOpt.has_value());
  PbrTestRig& rig = *rigOpt;

  auto materialResult = createMaterial(
      *rig.device,
      {.vertexShader = {.spirvWords = rig.pbrVertexSpirv.data(), .wordCount = rig.pbrVertexSpirv.size()},
       .fragmentShader = {.spirvWords = rig.pbrFragmentSpirv.data(), .wordCount = rig.pbrFragmentSpirv.size()},
       .vertexInputLayout = rig.pbrLayout,
       .colorFormat = HdrFormat::Rgba16Float,
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = 96,
       .sampledTextureBindingCount = 2},
      rig.texture.get(), rig.sampler.get(), MaterialPushConstantLayout::PbrDirectLit,
      std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 0.5f);
  REQUIRE(materialResult.isOk());

  auto cameraBufferResult =
      // Plan 0027 Milestone 9 (ADR-0072 D-9/P9d): widened from 320 to the new 592-byte PBR camera-buffer
      // tail (light-space view+projection, P5) -- CameraMatrices/FrameLightingData/CameraWorldPositionData
      // themselves stay byte-for-byte unmodified (writeCameraBuffer()'s own offsets below are untouched).
      rig.device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = 592});
  REQUIRE(cameraBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer = std::move(cameraBufferResult.value());

  DrawItem item;
  item.mesh = &rig.mesh;
  item.material = &materialResult.value();
  item.objectToWorld = kIdentityMatrix;

  constexpr float kLowerHdrIntensity = 32.0f;
  constexpr float kHigherHdrIntensity = 128.0f;
  const float encodedTextureValue = 128.0f / 255.0f;
  const float linearTextureValue =
      std::pow((encodedTextureValue + 0.055f) / 1.055f, 2.4f);  // exact sRGB EOTF branch for 0x80
  const Vec3 baseColor{linearTextureValue, linearTextureValue, linearTextureValue};
  const Vec3 lowerHdr = computePbrDirectLighting(Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 0, 5}, baseColor,
                                                  0.0f, 0.5f, oneDirectionalLight(kLowerHdrIntensity));
  const Vec3 higherHdr = computePbrDirectLighting(Vec3{0, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 0, 5}, baseColor,
                                                   0.0f, 0.5f, oneDirectionalLight(kHigherHdrIntensity));
  REQUIRE(lowerHdr.x > 1.0f);
  REQUIRE(higherHdr.x > lowerHdr.x);
  REQUIRE(atlantis::image_regression::tonemapAndEncodeUnorm(higherHdr.x) >
          atlantis::image_regression::tonemapAndEncodeUnorm(lowerHdr.x));
  REQUIRE(atlantis::image_regression::tonemapAndEncodeUnorm(higherHdr.x) < 1.0f);

  writeCameraBuffer(*cameraBuffer, kIdentityMatrix, kIdentityMatrix, oneDirectionalLight(kLowerHdrIntensity),
                     CameraWorldPositionData{0, 0, 5, 0});
  auto lowerPixels = renderOneFrame(*rig.device, *cameraBuffer, {item}, Format::Rgba8Unorm);
  REQUIRE(lowerPixels.has_value());

  writeCameraBuffer(*cameraBuffer, kIdentityMatrix, kIdentityMatrix, oneDirectionalLight(kHigherHdrIntensity),
                     CameraWorldPositionData{0, 0, 5, 0});
  auto higherPixels = renderOneFrame(*rig.device, *cameraBuffer, {item}, Format::Rgba8Unorm);
  REQUIRE(higherPixels.has_value());

  const auto lowerCenter = readCenterPixel(*lowerPixels, kExtent.width, kExtent.height);
  const auto higherCenter = readCenterPixel(*higherPixels, kExtent.width, kExtent.height);
  CHECK(higherCenter[0] > lowerCenter[0]);
  CHECK(higherCenter[1] > lowerCenter[1]);
  CHECK(higherCenter[2] > lowerCenter[2]);
  CHECK(higherCenter[0] < 255);
  CHECK(higherCenter[1] < 255);
  CHECK(higherCenter[2] < 255);
}

TEST_CASE("PbrDirectLit reflects a runtime Light intensity change on the next frame, exactly like LitTextured "
          "(Spec 0022 regression)",
          "[runtime][gpu][pbr][render][light]") {
  auto rigOpt = setUpPbrTestRig("Atlantis PBR Render GPU Tests (Spec 0022 regression)");
  REQUIRE(rigOpt.has_value());
  PbrTestRig& rig = *rigOpt;

  auto materialResult = createMaterial(
      *rig.device,
      {.vertexShader = {.spirvWords = rig.pbrVertexSpirv.data(), .wordCount = rig.pbrVertexSpirv.size()},
       .fragmentShader = {.spirvWords = rig.pbrFragmentSpirv.data(), .wordCount = rig.pbrFragmentSpirv.size()},
       .vertexInputLayout = rig.pbrLayout,
       .colorFormat = HdrFormat::Rgba16Float,  // Plan 0024 Milestone 6/7: geometry Pipeline, not the final target.
       .depthFormat = DepthFormat::D32Sfloat,
       .pushConstantSizeBytes = 96,
       .sampledTextureBindingCount = 2},
      rig.texture.get(), rig.sampler.get(), MaterialPushConstantLayout::PbrDirectLit,
      std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, 0.5f);
  REQUIRE(materialResult.isOk());

  auto cameraBufferResult =
      // Plan 0027 Milestone 9 (ADR-0072 D-9/P9d): widened from 320 to the new 592-byte PBR camera-buffer
      // tail (light-space view+projection, P5) -- CameraMatrices/FrameLightingData/CameraWorldPositionData
      // themselves stay byte-for-byte unmodified (writeCameraBuffer()'s own offsets below are untouched).
      rig.device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = 592});
  REQUIRE(cameraBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer = std::move(cameraBufferResult.value());

  DrawItem item;
  item.mesh = &rig.mesh;
  item.material = &materialResult.value();
  item.objectToWorld = kIdentityMatrix;

  // "Frame" 1: dim light.
  writeCameraBuffer(*cameraBuffer, kIdentityMatrix, kIdentityMatrix, oneDirectionalLight(0.5f),
                     CameraWorldPositionData{0, 0, 5, 0});
  auto dimPixels = renderOneFrame(*rig.device, *cameraBuffer, {item});
  REQUIRE(dimPixels.has_value());
  const auto dimCenter = readCenterPixel(*dimPixels, kExtent.width, kExtent.height);

  // "Frame" 2: SAME Material/Pipeline, no re-realization -- only the
  // camera buffer's own mapped bytes change, exactly matching
  // runFrame()'s own real, unconditional per-frame FrameLightingData
  // republish (Plan 0022's own corrected, dynamic contract).
  writeCameraBuffer(*cameraBuffer, kIdentityMatrix, kIdentityMatrix, oneDirectionalLight(4.0f),
                     CameraWorldPositionData{0, 0, 5, 0});
  auto brightPixels = renderOneFrame(*rig.device, *cameraBuffer, {item});
  REQUIRE(brightPixels.has_value());
  const auto brightCenter = readCenterPixel(*brightPixels, kExtent.width, kExtent.height);

  // Strictly brighter -- matching lighting_demo_gpu_tests.cpp's own
  // identical "increasing intensity strictly brightens" precedent.
  CHECK(brightCenter[0] > dimCenter[0]);
  CHECK(brightCenter[1] > dimCenter[1]);
  CHECK(brightCenter[2] > dimCenter[2]);
}
