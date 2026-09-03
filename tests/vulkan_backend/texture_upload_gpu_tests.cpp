#include <atlantis/assert.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// Spec 0016/Plan 0016 Milestone 3 (D4/D5): the isolated upload-primitive
// GPU test deferred from Milestone 2 -- confirms the barrier/copy
// mechanics of a real SampledTexture upload alone, on real Vulkan
// hardware, before Milestone 9's own combined-submission fixture
// exercises them together with a draw (V17). Real Vulkan device, zero
// windows, zero Presentation -- same headless shape as
// headless_rendering_gpu_tests.cpp. Every case here carries the "gpu"
// CTest label (catch_discover_tests(), tests/vulkan_backend/CMakeLists.txt).

namespace {

using atlantis::rhi::Buffer;
using atlantis::rhi::BufferPurpose;
using atlantis::rhi::CommandList;
using atlantis::rhi::Device;
using atlantis::rhi::Extent2D;
using atlantis::rhi::Format;
using atlantis::rhi::OffscreenTarget;
using atlantis::rhi::OffscreenTargetCreateParams;
using atlantis::rhi::RenderTarget;
using atlantis::rhi::SampledTexture;
using atlantis::rhi::SampledTextureCreateParams;
using atlantis::rhi::SampledTextureFormat;
using atlantis::rhi::SampledTextureUploadRegion;
using atlantis::vulkan_backend::createDevice;
using atlantis::vulkan_backend::DeviceCreateParams;

constexpr Extent2D kExtent{4, 4};

// Spec 0016 D4: a named pass-builder, not an anonymous lambda hiding its
// own inputs -- stagingBuffer/destination are explicit, required
// parameters (V39's own requirement, exercised here in its natural
// GPU-required home rather than duplicated as a separate compile-time-only
// check). Declares SampledTexture as the one tracked resource, with only
// a single TransferDestination usage on this pass -- execute() records
// every one of a pass's declared-usage transitions before invoking that
// pass's own executeFn, so a second, ShaderRead-tagged usage declared
// directly on this same pass would land its barrier *before* the copy
// below, not after it (confirmed the hard way: real Vulkan Validation
// Layers rejected exactly that ordering during this test's own
// development). The trailing TransferDestination -> ShaderRead
// transition is instead reached via the caller's own
// ResourceBinding::finalState, applied once after every pass in the
// graph has executed -- see execution.cpp's own trailing-transition loop.
// stagingBuffer is never itself RenderGraph-tracked (ADR-0056 Decision 4).
void buildTextureUploadPass(atlantis::render_graph::RenderGraphBuilder& builder, Buffer& stagingBuffer,
                             SampledTexture& destination) {
  const auto resource = builder.declareResource("texture-upload");
  const auto pass = builder.declarePass("TextureUpload");
  builder.writes(pass, resource, atlantis::rhi::ResourceState::TransferDestination);
  builder.setExecute(pass, [&stagingBuffer, &destination](CommandList& cmd) {
    cmd.copyBufferToTexture(stagingBuffer, destination);
  });
}

TEST_CASE("Real cube/mip and RG16F LUT uploads cover every declared subresource and sampler LOD cleanly",
          "[vulkan_backend][gpu][headless]") {
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis IBL Sampled Image GPU Tests", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  auto offscreenResult =
      device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kExtent, .format = Format::Rgba8Unorm});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());
  auto acquireResult = offscreenTarget->acquireTarget();
  REQUIRE(acquireResult.isOk());
  std::unique_ptr<RenderTarget> target = std::move(acquireResult.value());

  constexpr std::uint32_t kMipCount = 3;
  constexpr std::size_t kCubeBytes = 6U * (4U * 4U + 2U * 2U + 1U) * 8U;
  auto cubeStagingResult =
      device->createBuffer({.purpose = BufferPurpose::Staging, .sizeBytes = kCubeBytes});
  REQUIRE(cubeStagingResult.isOk());
  std::unique_ptr<Buffer> cubeStaging = std::move(cubeStagingResult.value());
  std::memset(cubeStaging->mappedData(), 0x3C, kCubeBytes);

  std::vector<SampledTextureUploadRegion> cubeRegions;
  std::size_t cubeOffset = 0;
  for (std::uint32_t mip = 0; mip < kMipCount; ++mip) {
    const std::uint32_t side = kExtent.width >> mip;
    const std::size_t faceBytes = static_cast<std::size_t>(side) * side * 8U;
    for (std::uint32_t face = 0; face < 6; ++face) {
      cubeRegions.push_back({.bufferOffsetBytes = cubeOffset, .mipLevel = mip, .arrayLayer = face, .extent = {side, side}});
      cubeOffset += faceBytes;
    }
  }
  REQUIRE(cubeOffset == kCubeBytes);

  auto cubeResult = device->createSampledTexture({.extent = kExtent,
                                                   .format = SampledTextureFormat::Rgba16Float,
                                                   .dimension = atlantis::rhi::SampledTextureDimension::TextureCube,
                                                   .mipLevelCount = kMipCount});
  REQUIRE(cubeResult.isOk());
  std::unique_ptr<SampledTexture> cube = std::move(cubeResult.value());
  CHECK(cube->dimension() == atlantis::rhi::SampledTextureDimension::TextureCube);
  CHECK(cube->mipLevelCount() == kMipCount);

  constexpr std::size_t kLutBytes = static_cast<std::size_t>(kExtent.width) * kExtent.height * 4U;
  auto lutStagingResult = device->createBuffer({.purpose = BufferPurpose::Staging, .sizeBytes = kLutBytes});
  REQUIRE(lutStagingResult.isOk());
  std::unique_ptr<Buffer> lutStaging = std::move(lutStagingResult.value());
  std::memset(lutStaging->mappedData(), 0x5A, kLutBytes);
  auto lutResult = device->createSampledTexture(
      {.extent = kExtent, .format = SampledTextureFormat::Rg16Float});
  REQUIRE(lutResult.isOk());
  std::unique_ptr<SampledTexture> lut = std::move(lutResult.value());

  auto samplerResult = device->createSampler({.filter = atlantis::rhi::Filter::Linear,
                                               .addressMode = atlantis::rhi::AddressMode::ClampToEdge,
                                               .mipFilter = atlantis::rhi::MipFilter::Linear,
                                               .minLod = 0.0F,
                                               .maxLod = 2.0F});
  REQUIRE(samplerResult.isOk());
  std::unique_ptr<atlantis::rhi::Sampler> sampler = std::move(samplerResult.value());
  CHECK(sampler->mipFilter() == atlantis::rhi::MipFilter::Linear);
  CHECK(sampler->maxLod() == 2.0F);

  auto commandListResult = device->createCommandList();
  REQUIRE(commandListResult.isOk());
  std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

  atlantis::render_graph::RenderGraphBuilder builder;
  const auto cubeResource = builder.declareResource("ibl-cube-upload");
  const auto cubePass = builder.declarePass("IblCubeUpload");
  builder.writes(cubePass, cubeResource, atlantis::rhi::ResourceState::TransferDestination);
  builder.setExecute(cubePass, [&cubeStaging, &cube, &cubeRegions](CommandList& cmd) {
    cmd.copyBufferToTexture(*cubeStaging, *cube, cubeRegions);
  });
  const auto lutResource = builder.declareResource("ibl-dfg-upload");
  const auto lutPass = builder.declarePass("IblDfgUpload");
  builder.writes(lutPass, lutResource, atlantis::rhi::ResourceState::TransferDestination);
  builder.setExecute(lutPass, [&lutStaging, &lut](CommandList& cmd) {
    cmd.copyBufferToTexture(*lutStaging, *lut);
  });
  auto compileResult = builder.compile();
  REQUIRE(compileResult.isOk());
  const std::vector<atlantis::render_graph::ResourceBinding> bindings{
      {.resource = compileResult.value().resourceAt(0),
       .sampledTexture = cube.get(),
       .finalState = atlantis::rhi::ResourceState::ShaderRead},
      {.resource = compileResult.value().resourceAt(1),
       .sampledTexture = lut.get(),
       .finalState = atlantis::rhi::ResourceState::ShaderRead},
  };
  atlantis::render_graph::execute(compileResult.value(), bindings, *commandList);

  auto submitResult = device->submit(std::move(commandList), *target);
  REQUIRE(submitResult.isOk());
  REQUIRE(device->waitIdle().isOk());
}

}  // namespace

TEST_CASE("A real SampledTexture upload, via buildTextureUploadPass() and a real one-pass RenderGraph execution, "
          "is Validation-Layers-clean",
          "[vulkan_backend][gpu][headless]") {
  auto deviceResult = createDevice(
      DeviceCreateParams{.applicationName = "Atlantis Texture Upload GPU Tests", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  // submit() requires a real RenderTarget& -- this test's own subject is
  // the upload path alone, so this OffscreenTarget exists only to satisfy
  // that signature, matching every other headless GPU test's own
  // established pattern.
  auto offscreenResult =
      device->createOffscreenTarget(OffscreenTargetCreateParams{.extent = kExtent, .format = Format::Rgba8Unorm});
  REQUIRE(offscreenResult.isOk());
  std::unique_ptr<OffscreenTarget> offscreenTarget = std::move(offscreenResult.value());

  auto acquireResult = offscreenTarget->acquireTarget();
  REQUIRE(acquireResult.isOk());
  std::unique_ptr<RenderTarget> target = std::move(acquireResult.value());

  const std::size_t sizeBytes = static_cast<std::size_t>(kExtent.width) * kExtent.height * 4;
  auto stagingBufferResult = device->createBuffer({.purpose = BufferPurpose::Staging, .sizeBytes = sizeBytes});
  REQUIRE(stagingBufferResult.isOk());
  std::unique_ptr<Buffer> stagingBuffer = std::move(stagingBufferResult.value());
  {
    auto* bytes = static_cast<std::uint8_t*>(stagingBuffer->mappedData());
    REQUIRE(bytes != nullptr);
    for (std::size_t i = 0; i < sizeBytes; i += 4) {
      bytes[i + 0] = 0x11;
      bytes[i + 1] = 0x22;
      bytes[i + 2] = 0x33;
      bytes[i + 3] = 0xFF;
    }
  }

  auto sampledTextureResult = device->createSampledTexture(
      SampledTextureCreateParams{.extent = kExtent, .format = SampledTextureFormat::Rgba8Unorm});
  REQUIRE(sampledTextureResult.isOk());
  std::unique_ptr<SampledTexture> sampledTexture = std::move(sampledTextureResult.value());

  auto commandListResult = device->createCommandList();
  REQUIRE(commandListResult.isOk());
  std::unique_ptr<CommandList> commandList = std::move(commandListResult.value());

  atlantis::render_graph::RenderGraphBuilder builder;
  buildTextureUploadPass(builder, *stagingBuffer, *sampledTexture);
  auto compileResult = builder.compile();
  REQUIRE(compileResult.isOk());

  const std::vector<atlantis::render_graph::ResourceBinding> bindings{
      {.resource = compileResult.value().resourceAt(0),
       .sampledTexture = sampledTexture.get(),
       .finalState = atlantis::rhi::ResourceState::ShaderRead}};
  atlantis::render_graph::execute(compileResult.value(), bindings, *commandList);

  auto submitResult = device->submit(std::move(commandList), *target);
  REQUIRE(submitResult.isOk());
  REQUIRE(device->waitIdle().isOk());

  sampledTexture.reset();
  target.reset();
  offscreenTarget.reset();
  REQUIRE(device->waitIdle().isOk());
}
