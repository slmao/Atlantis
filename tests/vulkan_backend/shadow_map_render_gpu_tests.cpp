#include <atlantis/rhi/device.h>
#include <atlantis/rhi/shadow_map.h>
#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include <memory>

#include <catch2/catch_test_macros.hpp>

// Plan 0027 Milestone 1 (ADR-0072 D-1): a real-GPU test proving
// Device::createShadowMap() succeeds against the real target GPU at the
// Plan's own fixed 1024x1024 resolution, exercising the real
// vkGetPhysicalDeviceFormatProperties() capability check this design
// depends on (hasRequiredShadowMapFeatures()'s own GPU-independent
// classification is covered separately, shadow_map_capability_tests.cpp).

using atlantis::rhi::DepthFormat;
using atlantis::rhi::Device;
using atlantis::rhi::Extent2D;
using atlantis::rhi::ShadowMap;
using atlantis::rhi::ShadowMapCreateParams;

TEST_CASE("Device::createShadowMap() succeeds at the fixed 1024x1024 resolution (Plan 0027 P4)",
          "[vulkan_backend][gpu][shadow_map]") {
  auto deviceResult = atlantis::vulkan_backend::createDevice(
      {.applicationName = "Atlantis Shadow Map GPU Test", .enableValidationLayers = true});
  REQUIRE(deviceResult.isOk());
  std::unique_ptr<Device> device = std::move(deviceResult.value());

  constexpr Extent2D kShadowMapExtent{1024, 1024};
  auto shadowMapResult = device->createShadowMap({.extent = kShadowMapExtent, .format = DepthFormat::D32Sfloat});
  REQUIRE(shadowMapResult.isOk());
  std::unique_ptr<ShadowMap> shadowMap = std::move(shadowMapResult.value());

  CHECK(shadowMap->extent().width == kShadowMapExtent.width);
  CHECK(shadowMap->extent().height == kShadowMapExtent.height);
  CHECK(shadowMap->format() == DepthFormat::D32Sfloat);

  REQUIRE(device->waitIdle().isOk());
}
