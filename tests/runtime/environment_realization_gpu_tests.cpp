#include <atlantis/asset_system/load_environment.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/environment_realization.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <cstddef>
#include <memory>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Runtime realizes and uploads one complete environment candidate before publication",
          "[runtime][gpu][environment_realization]") {
  auto dataResult = atlantis::asset_system::loadEnvironmentAsset(ATLANTIS_RUNTIME_ENVIRONMENT_ARTIFACT_PATH,
                                                                  ATLANTIS_RUNTIME_ENVIRONMENT_METADATA_PATH);
  REQUIRE(dataResult.isOk());
  const auto& data = dataResult.value();

  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Environment Realization GPU Tests", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<atlantis::rhi::Device> device = std::move(deviceResult.value());

  auto candidateResult = atlantis::runtime::realizeEnvironmentCandidate(*device, data);
  REQUIRE(candidateResult.isOk());
  atlantis::runtime::EnvironmentLightingCandidate candidate = std::move(candidateResult.value());
  CHECK(candidate.resources.prefilteredEnvironment->dimension() ==
        atlantis::rhi::SampledTextureDimension::TextureCube);
  CHECK(candidate.resources.prefilteredEnvironment->format() ==
        atlantis::rhi::SampledTextureFormat::Rgba16Float);
  CHECK(candidate.resources.prefilteredEnvironment->mipLevelCount() == data.mipCount);
  CHECK(candidate.resources.dfgLut->dimension() == atlantis::rhi::SampledTextureDimension::Texture2D);
  CHECK(candidate.resources.dfgLut->format() == atlantis::rhi::SampledTextureFormat::Rg16Float);
  REQUIRE(candidate.specularUploadRegions.size() == static_cast<std::size_t>(data.mipCount) * 6);
  CHECK(candidate.specularUploadRegions.front().bufferOffsetBytes == 0);
  CHECK(candidate.specularUploadRegions.front().mipLevel == 0);
  CHECK(candidate.specularUploadRegions.front().arrayLayer == 0);
  CHECK(candidate.specularUploadRegions.back().mipLevel == data.mipCount - 1);
  CHECK(candidate.specularUploadRegions.back().arrayLayer == 5);
  CHECK(candidate.resources.irradianceSh == data.irradianceSh);

  const auto view = candidate.resources.borrowedView();
  CHECK(&view.prefilteredEnvironment == candidate.resources.prefilteredEnvironment.get());
  CHECK(&view.dfgLut == candidate.resources.dfgLut.get());

  auto targetResult = device->createOffscreenTarget({.extent = {1, 1}, .format = atlantis::rhi::Format::Rgba8Unorm});
  REQUIRE(targetResult.isOk());
  auto target = targetResult.value()->acquireTarget();
  REQUIRE(target.isOk());
  auto commandListResult = device->createCommandList();
  REQUIRE(commandListResult.isOk());
  atlantis::runtime::recordEnvironmentUploads(*commandListResult.value(), candidate);
  auto submitResult = device->submit(std::move(commandListResult.value()), *target.value());
  REQUIRE(submitResult.isOk());
  REQUIRE(device->waitIdle().isOk());

  // This move models RuntimeApplication's post-wait publish. Candidate staging
  // buffers remain local and may now be destroyed without affecting resources.
  atlantis::runtime::EnvironmentLightingResources published = std::move(candidate.resources);
  CHECK(published.prefilteredEnvironment != nullptr);
  CHECK(published.dfgLut != nullptr);
  REQUIRE(device->waitIdle().isOk());
}
