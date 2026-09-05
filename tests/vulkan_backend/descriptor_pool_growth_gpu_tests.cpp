#include "vulkan_descriptor_pool_growth.h"

#include <atlantis/platform/platform.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/renderer/renderer.h>
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
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <numeric>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// Spec 0021/ADR-0064, Plan 0021 Milestone 3: dedicated, Vulkan-Backend-
// level pool-mechanics GPU tests, distinct from
// tests/runtime/material_realization_gpu_tests.cpp's own Material/
// Runtime-level scenarios (V16/V17, that file's own new N=6 TEST_CASE).
// Everything here drives Device::createPipeline() directly, headless
// (OffscreenTarget, no window), reusing the checked-in minimal_mesh
// SPIR-V pair -- no new shader dependency (see this target's own
// CMakeLists.txt).
//
// V11 (mid-creation-failure-after-growth leaves no leak) is verified by
// static code review, not a GPU test here: reliably forcing a genuine
// vkCreatePipelineLayout/vkCreateGraphicsPipelines failure from valid,
// portable PipelineCreateParams is not achievable deterministically
// across drivers. The property itself is confirmed by direct reading of
// these exact branches, not a general "RAII looks correct" claim:
//   - vulkan_device.cpp:930-931 (VulkanDevice::allocateDescriptorSet(),
//     Step 3's own publish): the array-slot write and the count
//     increment are the literal next two statements after
//     createDescriptorPoolOfSize() succeeds, with zero intervening
//     fallible operation -- so a newly-grown pool is always published
//     before Step 4's own retry is ever attempted.
//   - vulkan_device.cpp:1055 (createPipeline()'s own
//     PipelineLayoutCreationFailed branch) and vulkan_device.cpp:1174
//     (its own final vkCreateGraphicsPipelines-failure branch): both
//     call vkFreeDescriptorSets(device_, originPool, ...) -- the real
//     origin pool this specific allocation came from, never the array's
//     current/last entry -- before returning, so the already-allocated
//     set is always freed back to a pool that stays in descriptorPools_
//     (never rolled back), leaving it immediately available for the
//     next createPipeline() call's own scan to reuse.
// This matches this codebase's own established precedent of relying on
// static review for GPU failure paths no test can reliably, portably
// trigger.

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
using atlantis::rhi::PipelineCreateError;
using atlantis::rhi::RenderTarget;
using atlantis::rhi::VertexInputLayout;
using atlantis::shader_system::loadReflectionMetadata;
using atlantis::shader_system::ReflectionMetadata;
using atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema;
using atlantis::shader_system::rhi_integration::toVertexInputLayout;
using atlantis::vulkan_backend::createDevice;
using atlantis::vulkan_backend::DeviceCreateParams;
using atlantis::vulkan_backend::detail::kDescriptorPoolMaxSetsByGeneration;
using atlantis::vulkan_backend::detail::kMaxDescriptorPoolCount;

// Mirrors minimal_renderer_gpu_tests.cpp's/headless_rendering_gpu_tests.cpp's
// own established fixture shape exactly -- duplicated, not shared,
// matching this codebase's own "each test file owns its small fixture"
// precedent.
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

// Plan 0027 Milestone 9 (ADR-0072 D-3): shadow_cast.slang's own real,
// position-only VertexInput -- against this file's own shared Vertex
// struct above, mirrors runtime_application.cpp's own identical
// shadowCastVertexLayout().
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

constexpr Vertex kTriangleVertices[3] = {
    {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
};
constexpr std::uint16_t kTriangleIndices[3] = {0, 1, 2};
constexpr std::array<float, 16> kIdentityMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// Loads the real, current fallback (minimal_mesh) vertex layout once --
// every TEST_CASE below reuses this same shader pair, varying only
// sampledTextureBindingCount, per this file's own header comment.
struct MinimalMeshFixture {
  std::vector<std::uint32_t> vertexSpirv;
  std::vector<std::uint32_t> fragmentSpirv;
  VertexInputLayout vertexInputLayout;
};

[[nodiscard]] std::optional<MinimalMeshFixture> loadMinimalMeshFixture() {
  auto vertexSpirv = loadSpirvFile("shaders/minimal_mesh.vert.spv");
  auto fragmentSpirv = loadSpirvFile("shaders/minimal_mesh.frag.spv");
  if (!vertexSpirv.has_value() || !fragmentSpirv.has_value()) return std::nullopt;
  const auto vertexReflection = loadReflectionMetadata("shaders/minimal_mesh.vert.refl.json");
  if (vertexReflection.isErr()) return std::nullopt;
  const auto vertexInputLayout = minimalMeshVertexLayout(vertexReflection.value());
  if (!vertexInputLayout.has_value()) return std::nullopt;
  return MinimalMeshFixture{std::move(*vertexSpirv), std::move(*fragmentSpirv), *vertexInputLayout};
}

}  // namespace

TEST_CASE(
    "The hard four-pool ceiling is enforced exactly at the real, production capacity total, and freed capacity "
    "is reused rather than triggering further growth (Spec 0021 D6/D3/D7, V10)",
    "[vulkan_backend][gpu][descriptor_pool_growth]") {
  // The real, current capacity total -- computed from the same
  // constants VulkanDevice's own production code reads, never a
  // hand-copied literal (Human Review's own explicit instruction; see
  // this target's own new CMakeLists.txt include path).
  const auto kTotalCapacity =
      std::accumulate(kDescriptorPoolMaxSetsByGeneration.begin(), kDescriptorPoolMaxSetsByGeneration.end(), 0u);
  REQUIRE(kTotalCapacity == 60);

  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Descriptor Pool Growth GPU Tests (ceiling+reuse)",
                          .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  const auto fixture = loadMinimalMeshFixture();
  REQUIRE(fixture.has_value());

  // This TEST_CASE's own dedicated Device -- no other Pipeline is ever
  // created against it outside this loop, so the capacity-total
  // derivation below is never confounded by an untracked fixture
  // allocation.
  std::vector<std::unique_ptr<atlantis::rhi::Pipeline>> pipelines;
  pipelines.reserve(kTotalCapacity);

  // Phase 1: drive the pool set all the way to its own real, approved
  // hard ceiling with the maximum closed sampled binding count. If a
  // pool still allocated only maxSets sampler descriptors, it would
  // exhaust after roughly one third as many Pipelines.
  for (unsigned i = 0; i < kTotalCapacity; ++i) {
    auto result = device->createPipeline(
        {.vertexShader = {.spirvWords = fixture->vertexSpirv.data(), .wordCount = fixture->vertexSpirv.size()},
         .fragmentShader = {.spirvWords = fixture->fragmentSpirv.data(),
                             .wordCount = fixture->fragmentSpirv.size()},
         .vertexInputLayout = fixture->vertexInputLayout,
         .colorFormat = Format::Rgba8Unorm,
         .depthFormat = DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = sizeof(float) * 16,
         .sampledTextureBindingCount = 3});
    REQUIRE(result.isOk());
    pipelines.push_back(std::move(result.value()));
  }
  REQUIRE(pipelines.size() == kTotalCapacity);

  // The (kTotalCapacity + 1)th allocation must fail -- the hard ceiling
  // (4 pools) is reached; no 5th pool is ever created.
  {
    auto overflowResult = device->createPipeline(
        {.vertexShader = {.spirvWords = fixture->vertexSpirv.data(), .wordCount = fixture->vertexSpirv.size()},
         .fragmentShader = {.spirvWords = fixture->fragmentSpirv.data(),
                             .wordCount = fixture->fragmentSpirv.size()},
         .vertexInputLayout = fixture->vertexInputLayout,
         .colorFormat = Format::Rgba8Unorm,
         .depthFormat = DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = sizeof(float) * 16,
         .sampledTextureBindingCount = 3});
    REQUIRE(overflowResult.isErr());
    CHECK(overflowResult.error() == PipelineCreateError::DescriptorSetAllocationFailed);
  }

  // Phase 2: destroy the LAST 10 of the 60 created (pop_back(), reverse
  // of creation order). Because Phase 1 filled all 60 slots exactly (no
  // pool had any spare capacity at any point), and the creation-order
  // scan always fills pool0 (generation 0, 4 slots), then pool1 (8),
  // then pool2 (16), before ever touching pool3 (generation 3, 32
  // slots, holding creation indices 28-59), the last 10 created are
  // provably all in pool3 -- pool0/pool1/pool2 stay 100% full and
  // untouched by this destruction, never gaining any spare capacity.
  for (int i = 0; i < 10; ++i) {
    pipelines.pop_back();
  }
  REQUIRE(pipelines.size() == kTotalCapacity - 10);

  // Create 10 new Pipelines. Since the pool set is ALREADY at its own
  // hard ceiling (4 pools) -- unchanged by the destructions above, which
  // only free capacity WITHIN pool3, never remove a pool -- a 5th pool
  // cannot legally be created, and pool0/pool1/pool2 remain full with
  // nothing to offer. Success here is only possible if the freed pool3
  // capacity was found and reused by the creation-order scan:
  // definitive, indirect proof of reuse-before-growth (Spec 0021
  // D3/D7), with no new production introspection API.
  for (int i = 0; i < 10; ++i) {
    auto result = device->createPipeline(
        {.vertexShader = {.spirvWords = fixture->vertexSpirv.data(), .wordCount = fixture->vertexSpirv.size()},
         .fragmentShader = {.spirvWords = fixture->fragmentSpirv.data(),
                             .wordCount = fixture->fragmentSpirv.size()},
         .vertexInputLayout = fixture->vertexInputLayout,
         .colorFormat = Format::Rgba8Unorm,
         .depthFormat = DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = sizeof(float) * 16,
         .sampledTextureBindingCount = 3});
    REQUIRE(result.isOk());
    pipelines.push_back(std::move(result.value()));
  }
  REQUIRE(pipelines.size() == kTotalCapacity);

  pipelines.clear();
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("Mixed uniform-only and textured allocation crosses a growth boundary without premature, "
          "type-specific exhaustion (Spec 0021 D4/P8, V12)",
          "[vulkan_backend][gpu][descriptor_pool_growth]") {
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Descriptor Pool Growth GPU Tests (mixed allocation)",
                          .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  const auto fixture = loadMinimalMeshFixture();
  REQUIRE(fixture.has_value());

  // Alternate sampledTextureBindingCount = 0/1 -- reusing the
  // existing minimal_mesh shader pair for both (its own fragment shader
  // never references binding 1, so declaring it unused in the layout is
  // legal Vulkan usage; Validation-Layers-clean below confirms this
  // directly, not merely assumed). Enough allocations (12) to exhaust
  // generation 0 (4) and cross into generation 1 (8).
  std::vector<std::unique_ptr<atlantis::rhi::Pipeline>> pipelines;
  for (int i = 0; i < 12; ++i) {
    const bool textured = (i % 2) == 1;
    auto result = device->createPipeline(
        {.vertexShader = {.spirvWords = fixture->vertexSpirv.data(), .wordCount = fixture->vertexSpirv.size()},
         .fragmentShader = {.spirvWords = fixture->fragmentSpirv.data(),
                             .wordCount = fixture->fragmentSpirv.size()},
         .vertexInputLayout = fixture->vertexInputLayout,
         .colorFormat = Format::Rgba8Unorm,
         .depthFormat = DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = sizeof(float) * 16,
         .sampledTextureBindingCount = textured ? 1U : 0U});
    REQUIRE(result.isOk());
    pipelines.push_back(std::move(result.value()));
  }

  pipelines.clear();
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("Origin-pool correctness across generations, destroyed out of creation order, proven doubly: "
          "Validation Layers clean AND a real reallocation success (V13)",
          "[vulkan_backend][gpu][descriptor_pool_growth]") {
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Descriptor Pool Growth GPU Tests (origin-pool correctness)",
                          .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  const auto fixture = loadMinimalMeshFixture();
  REQUIRE(fixture.has_value());

  // 12 Pipelines span at least 2 pool generations (4 + 8).
  std::vector<std::unique_ptr<atlantis::rhi::Pipeline>> pipelines;
  for (int i = 0; i < 12; ++i) {
    auto result = device->createPipeline(
        {.vertexShader = {.spirvWords = fixture->vertexSpirv.data(), .wordCount = fixture->vertexSpirv.size()},
         .fragmentShader = {.spirvWords = fixture->fragmentSpirv.data(),
                             .wordCount = fixture->fragmentSpirv.size()},
         .vertexInputLayout = fixture->vertexInputLayout,
         .colorFormat = Format::Rgba8Unorm,
         .depthFormat = DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = sizeof(float) * 16});
    REQUIRE(result.isOk());
    pipelines.push_back(std::move(result.value()));
  }

  // Proof 1: destroy in a different-from-creation order (reverse-ish,
  // skipping every other one) -- every VulkanPipeline destructor's own
  // vkFreeDescriptorSets call targets its real origin pool. A
  // wrong-pool free would either abort the process (the existing
  // ATLANTIS_CHECK(freeResult == VK_SUCCESS) in vulkan_pipeline.cpp) or
  // produce a Validation Layers hit -- neither happens.
  for (std::size_t i = 0; i < pipelines.size(); i += 2) {
    pipelines[i].reset();
  }
  REQUIRE(device->waitIdle().isOk());

  // Proof 2: a real reallocation succeeds using the now-freed capacity
  // -- a wrong-pool free would leave the pool that was ACTUALLY used
  // still reporting exhaustion even though 6 objects were destroyed,
  // which this second, independent proof catches even if Validation
  // Layers somehow did not.
  for (int i = 0; i < 6; ++i) {
    auto result = device->createPipeline(
        {.vertexShader = {.spirvWords = fixture->vertexSpirv.data(), .wordCount = fixture->vertexSpirv.size()},
         .fragmentShader = {.spirvWords = fixture->fragmentSpirv.data(),
                             .wordCount = fixture->fragmentSpirv.size()},
         .vertexInputLayout = fixture->vertexInputLayout,
         .colorFormat = Format::Rgba8Unorm,
         .depthFormat = DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = sizeof(float) * 16});
    REQUIRE(result.isOk());
    pipelines.push_back(std::move(result.value()));
  }

  pipelines.clear();
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("A Pipeline created before a growth event remains valid and genuinely drawable after it (V14)",
          "[vulkan_backend][gpu][descriptor_pool_growth]") {
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Descriptor Pool Growth GPU Tests (pre-growth Pipeline "
                                             "remains drawable)",
                          .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  const auto fixture = loadMinimalMeshFixture();
  REQUIRE(fixture.has_value());

  constexpr Format kColorFormat = Format::Rgba8Unorm;
  constexpr Extent2D kExtent{4, 4};

  // The real Material (and its own real Pipeline, generation 0) this
  // test proves stays valid and drawable -- created FIRST, before any
  // growth.
  auto materialResult = createMaterial(
      *device, {.vertexShader = {.spirvWords = fixture->vertexSpirv.data(), .wordCount = fixture->vertexSpirv.size()},
                .fragmentShader = {.spirvWords = fixture->fragmentSpirv.data(),
                                    .wordCount = fixture->fragmentSpirv.size()},
                .vertexInputLayout = fixture->vertexInputLayout,
                // Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3): every
                // geometry Pipeline now renders into the fixed HDR
                // intermediate, never kColorFormat directly.
                .colorFormat = atlantis::rhi::HdrFormat::Rgba16Float,
                .depthFormat = DepthFormat::D32Sfloat,
                .pushConstantSizeBytes = sizeof(float) * 16});
  REQUIRE(materialResult.isOk());
  Material material = std::move(materialResult.value());

  // Force a real growth event: 4 more Pipelines exhaust generation 0
  // (the Material above already consumed its own first slot), 4 more
  // land in the newly-grown generation 1.
  std::vector<std::unique_ptr<atlantis::rhi::Pipeline>> otherPipelines;
  for (int i = 0; i < 8; ++i) {
    auto result = device->createPipeline(
        {.vertexShader = {.spirvWords = fixture->vertexSpirv.data(), .wordCount = fixture->vertexSpirv.size()},
         .fragmentShader = {.spirvWords = fixture->fragmentSpirv.data(),
                             .wordCount = fixture->fragmentSpirv.size()},
         .vertexInputLayout = fixture->vertexInputLayout,
         .colorFormat = kColorFormat,
         .depthFormat = DepthFormat::D32Sfloat,
         .pushConstantSizeBytes = sizeof(float) * 16});
    REQUIRE(result.isOk());
    otherPipelines.push_back(std::move(result.value()));
  }

  // Now prove the ORIGINAL Material's own Pipeline (generation 0,
  // created before any growth) is still valid -- a real draw and
  // submit, not merely a non-null handle check.
  auto meshResult =
      createMesh(*device, fixture->vertexInputLayout, kTriangleVertices, sizeof(kTriangleVertices), kTriangleIndices,
                 3);
  REQUIRE(meshResult.isOk());
  Mesh mesh = std::move(meshResult.value());

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

  auto offscreenResult = device->createOffscreenTarget({.extent = kExtent, .format = kColorFormat});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());

  auto acquireResult = offscreenTarget->acquireTarget();
  REQUIRE(acquireResult.isOk());
  std::unique_ptr<RenderTarget> target = std::move(acquireResult.value());

  // Plan 0024 Milestone 6/7 (ADR-0068 D-1/D-3/D-6): this test's own HDR
  // intermediate, fullscreen-triangle geometry/sampler, and output-
  // transform Pipeline -- kColorFormat above is fixed Rgba8Unorm, so
  // only this one variant is ever built.
  auto hdrColorTargetResult = device->createHdrColorTarget({.extent = kExtent});
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

  const auto outputTransformVertexSpirv = loadSpirvFile("shaders/output_transform_unorm.vert.spv");
  const auto outputTransformFragmentSpirv = loadSpirvFile("shaders/output_transform_unorm.frag.spv");
  REQUIRE(outputTransformVertexSpirv.has_value());
  REQUIRE(outputTransformFragmentSpirv.has_value());
  const auto outputTransformVertexReflection = loadReflectionMetadata("shaders/output_transform_unorm.vert.refl.json");
  REQUIRE(outputTransformVertexReflection.isOk());
  const auto outputTransformVertexInputLayout = outputTransformVertexLayout(outputTransformVertexReflection.value());
  REQUIRE(outputTransformVertexInputLayout.has_value());

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
  REQUIRE(outputTransformPipelineResult.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> outputTransformPipeline = std::move(outputTransformPipelineResult.value());

  // Plan 0027 Milestone 9 (ADR-0072 D-1/P9e): a minimal, always-possible
  // real ShadowMap/Sampler/Pipeline/Buffer -- shadowCasterDrawItems
  // stays empty; this test never configures a real occluder.
  auto shadowMapResult = device->createShadowMap({.extent = {1024, 1024}});
  REQUIRE(shadowMapResult.isOk());
  std::unique_ptr<atlantis::rhi::ShadowMap> shadowMap = std::move(shadowMapResult.value());

  auto shadowMapSamplerResult =
      device->createSampler({.filter = atlantis::rhi::Filter::Nearest, .addressMode = atlantis::rhi::AddressMode::ClampToEdge});
  REQUIRE(shadowMapSamplerResult.isOk());
  std::unique_ptr<atlantis::rhi::Sampler> shadowMapSampler = std::move(shadowMapSamplerResult.value());

  const auto shadowCastVertexSpirv = loadSpirvFile("shaders/shadow_cast.vert.spv");
  const auto shadowCastFragmentSpirv = loadSpirvFile("shaders/shadow_cast.frag.spv");
  REQUIRE(shadowCastVertexSpirv.has_value());
  REQUIRE(shadowCastFragmentSpirv.has_value());
  const auto shadowCastVertexReflection = loadReflectionMetadata("shaders/shadow_cast.vert.refl.json");
  REQUIRE(shadowCastVertexReflection.isOk());
  const auto shadowCastVertexInputLayout = shadowCastVertexLayout(shadowCastVertexReflection.value());
  REQUIRE(shadowCastVertexInputLayout.has_value());

  auto shadowCastPipelineResult = device->createPipeline(
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
  REQUIRE(shadowCastPipelineResult.isOk());
  std::unique_ptr<atlantis::rhi::Pipeline> shadowCastPipeline = std::move(shadowCastPipelineResult.value());

  auto shadowLightSpaceBufferResult = device->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes = 128});
  REQUIRE(shadowLightSpaceBufferResult.isOk());
  std::unique_ptr<atlantis::rhi::Buffer> shadowLightSpaceBuffer = std::move(shadowLightSpaceBufferResult.value());

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
                      atlantis::rhi::ResourceState::TransferSource, *hdrColorTarget, *fullscreenTriangleVertexBuffer,
                      *fullscreenTriangleIndexBuffer, *outputTransformPipeline, *outputTransformSampler, nullptr,
                      nullptr, *shadowMap, *shadowMapSampler, *shadowCastPipeline, *shadowLightSpaceBuffer, {});

  auto submitResult = device->submit(std::move(commandList), *target);
  REQUIRE(submitResult.isOk());
  REQUIRE(device->waitIdle().isOk());

  otherPipelines.clear();
  REQUIRE(device->waitIdle().isOk());
}

TEST_CASE("VulkanDevice's own destruction destroys every pool across at least 2 generations, "
          "Validation-Layers-clean (V15)",
          "[vulkan_backend][gpu][descriptor_pool_growth]") {
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Descriptor Pool Growth GPU Tests (multi-generation "
                                             "destruction)",
                          .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  const auto fixture = loadMinimalMeshFixture();
  REQUIRE(fixture.has_value());

  {
    std::vector<std::unique_ptr<atlantis::rhi::Pipeline>> pipelines;
    // 12 Pipelines force generation 0 (4) and generation 1 (8) both
    // into existence.
    for (int i = 0; i < 12; ++i) {
      auto result = device->createPipeline(
          {.vertexShader = {.spirvWords = fixture->vertexSpirv.data(), .wordCount = fixture->vertexSpirv.size()},
           .fragmentShader = {.spirvWords = fixture->fragmentSpirv.data(),
                               .wordCount = fixture->fragmentSpirv.size()},
           .vertexInputLayout = fixture->vertexInputLayout,
           .colorFormat = Format::Rgba8Unorm,
           .depthFormat = DepthFormat::D32Sfloat,
           .pushConstantSizeBytes = sizeof(float) * 16});
      REQUIRE(result.isOk());
      pipelines.push_back(std::move(result.value()));
    }
    REQUIRE(device->waitIdle().isOk());
    // Every Pipeline destroyed here, before device -- matching this
    // codebase's own existing caller-discipline invariant
    // ("Pre-draft verification", Plan 0021).
  }

  // device destroyed here (end of TEST_CASE scope) -- ~VulkanDevice()
  // must destroy both live pools (generations 0 and 1), never just the
  // first. Zero Validation Layers hits and zero ATLANTIS_LOG_ERROR
  // output from its own drain sequence is the only way this TEST_CASE
  // can pass at all (a leaked/double-destroyed pool would be caught by
  // Validation Layers; a hung drain would time out the whole suite).
}
