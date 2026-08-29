#include "vulkan_result.h"

#include <catch2/catch_test_macros.hpp>

using atlantis::rhi::CommandListCreateError;
using atlantis::rhi::PresentationError;
using atlantis::rhi::SubmitError;
using atlantis::vulkan_backend::DeviceCreateError;
using atlantis::vulkan_backend::PresentationCreateError;
using atlantis::vulkan_backend::detail::classifyFailure;
using atlantis::vulkan_backend::detail::isDescriptorPoolGrowthEligible;
using atlantis::vulkan_backend::detail::toAcquireFailureError;
using atlantis::vulkan_backend::detail::toCommandListCreateError;
using atlantis::vulkan_backend::detail::toDeviceCreationError;
using atlantis::vulkan_backend::detail::toInstanceCreationError;
using atlantis::vulkan_backend::detail::toPresentFailureError;
using atlantis::vulkan_backend::detail::toSubmitError;
using atlantis::vulkan_backend::detail::toSurfaceCreationError;
using atlantis::vulkan_backend::detail::toSwapchainCreationError;
using atlantis::vulkan_backend::detail::VulkanFailureCategory;

TEST_CASE("classifyFailure recognizes surface-lost and device-lost distinctly", "[vulkan_backend][vulkan_result]") {
  REQUIRE(classifyFailure(VK_ERROR_SURFACE_LOST_KHR) == VulkanFailureCategory::SurfaceLost);
  REQUIRE(classifyFailure(VK_ERROR_DEVICE_LOST) == VulkanFailureCategory::DeviceLost);
}

TEST_CASE("classifyFailure recognizes out-of-memory results", "[vulkan_backend][vulkan_result]") {
  REQUIRE(classifyFailure(VK_ERROR_OUT_OF_HOST_MEMORY) == VulkanFailureCategory::OutOfMemory);
  REQUIRE(classifyFailure(VK_ERROR_OUT_OF_DEVICE_MEMORY) == VulkanFailureCategory::OutOfMemory);
}

TEST_CASE("classifyFailure falls back to Other for unrecognized failures", "[vulkan_backend][vulkan_result]") {
  REQUIRE(classifyFailure(VK_ERROR_INITIALIZATION_FAILED) == VulkanFailureCategory::Other);
  REQUIRE(classifyFailure(VK_ERROR_TOO_MANY_OBJECTS) == VulkanFailureCategory::Other);
}

TEST_CASE("toInstanceCreationError maps any failure to InstanceCreationFailed", "[vulkan_backend][vulkan_result]") {
  REQUIRE(toInstanceCreationError(VK_ERROR_OUT_OF_HOST_MEMORY) == DeviceCreateError::InstanceCreationFailed);
  REQUIRE(toInstanceCreationError(VK_ERROR_INITIALIZATION_FAILED) == DeviceCreateError::InstanceCreationFailed);
}

TEST_CASE("toDeviceCreationError maps any failure to DeviceCreationFailed", "[vulkan_backend][vulkan_result]") {
  REQUIRE(toDeviceCreationError(VK_ERROR_OUT_OF_HOST_MEMORY) == DeviceCreateError::DeviceCreationFailed);
  REQUIRE(toDeviceCreationError(VK_ERROR_DEVICE_LOST) == DeviceCreateError::DeviceCreationFailed);
}

TEST_CASE("toSurfaceCreationError maps any failure to SurfaceCreationFailed", "[vulkan_backend][vulkan_result]") {
  REQUIRE(toSurfaceCreationError(VK_ERROR_OUT_OF_HOST_MEMORY) == PresentationCreateError::SurfaceCreationFailed);
  REQUIRE(toSurfaceCreationError(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR) ==
          PresentationCreateError::SurfaceCreationFailed);
}

TEST_CASE("toSwapchainCreationError maps surface-lost and device-lost distinctly",
          "[vulkan_backend][vulkan_result]") {
  REQUIRE(toSwapchainCreationError(VK_ERROR_SURFACE_LOST_KHR) == PresentationError::SurfaceLost);
  REQUIRE(toSwapchainCreationError(VK_ERROR_DEVICE_LOST) == PresentationError::DeviceLost);
}

TEST_CASE("toSwapchainCreationError maps out-of-memory to SwapchainCreationFailed",
          "[vulkan_backend][vulkan_result]") {
  REQUIRE(toSwapchainCreationError(VK_ERROR_OUT_OF_HOST_MEMORY) == PresentationError::SwapchainCreationFailed);
  REQUIRE(toSwapchainCreationError(VK_ERROR_OUT_OF_DEVICE_MEMORY) == PresentationError::SwapchainCreationFailed);
}

TEST_CASE("toSwapchainCreationError falls back to Unknown for unrecognized failures",
          "[vulkan_backend][vulkan_result]") {
  REQUIRE(toSwapchainCreationError(VK_ERROR_INITIALIZATION_FAILED) == PresentationError::Unknown);
}

TEST_CASE("toAcquireFailureError maps surface-lost and device-lost distinctly", "[vulkan_backend][vulkan_result]") {
  REQUIRE(toAcquireFailureError(VK_ERROR_SURFACE_LOST_KHR) == PresentationError::SurfaceLost);
  REQUIRE(toAcquireFailureError(VK_ERROR_DEVICE_LOST) == PresentationError::DeviceLost);
}

TEST_CASE("toAcquireFailureError falls back to Unknown for out-of-memory and unrecognized failures",
          "[vulkan_backend][vulkan_result]") {
  REQUIRE(toAcquireFailureError(VK_ERROR_OUT_OF_HOST_MEMORY) == PresentationError::Unknown);
  REQUIRE(toAcquireFailureError(VK_ERROR_INITIALIZATION_FAILED) == PresentationError::Unknown);
}

TEST_CASE("toPresentFailureError maps surface-lost and device-lost distinctly", "[vulkan_backend][vulkan_result]") {
  REQUIRE(toPresentFailureError(VK_ERROR_SURFACE_LOST_KHR) == PresentationError::SurfaceLost);
  REQUIRE(toPresentFailureError(VK_ERROR_DEVICE_LOST) == PresentationError::DeviceLost);
}

TEST_CASE("toSubmitError maps device-lost distinctly and falls back to QueueSubmitFailed",
          "[vulkan_backend][vulkan_result]") {
  REQUIRE(toSubmitError(VK_ERROR_DEVICE_LOST) == SubmitError::DeviceLost);
  REQUIRE(toSubmitError(VK_ERROR_OUT_OF_HOST_MEMORY) == SubmitError::QueueSubmitFailed);
  REQUIRE(toSubmitError(VK_ERROR_INITIALIZATION_FAILED) == SubmitError::QueueSubmitFailed);
}

TEST_CASE("toCommandListCreateError maps any failure to CommandBufferAllocationFailed",
          "[vulkan_backend][vulkan_result]") {
  REQUIRE(toCommandListCreateError(VK_ERROR_OUT_OF_HOST_MEMORY) == CommandListCreateError::CommandBufferAllocationFailed);
  REQUIRE(toCommandListCreateError(VK_ERROR_OUT_OF_DEVICE_MEMORY) == CommandListCreateError::CommandBufferAllocationFailed);
}

// Spec 0021 D3, ADR-0064, Plan 0021 V4-V6: the descriptor-pool
// growth-eligibility classifier -- pure, GPU-independent, tested with
// literal VkResult values, matching every classifier above in this same
// file. This is the only reliable way to prove the classification
// boundary (including its handling of VK_ERROR_DEVICE_LOST) precisely:
// no Fake/mock Vulkan Backend test double exists anywhere in this
// module, so a real VK_ERROR_DEVICE_LOST cannot be reliably injected
// from a real Device (see Plan 0021's own "Pre-draft verification").

TEST_CASE("isDescriptorPoolGrowthEligible is true for the two real pool-exhaustion results",
          "[vulkan_backend][vulkan_result][descriptor_pool_growth]") {
  // V4
  REQUIRE(isDescriptorPoolGrowthEligible(VK_ERROR_OUT_OF_POOL_MEMORY));
  // V5
  REQUIRE(isDescriptorPoolGrowthEligible(VK_ERROR_FRAGMENTED_POOL));
}

TEST_CASE("isDescriptorPoolGrowthEligible is false for DeviceLost, host/device OOM, and any other failure",
          "[vulkan_backend][vulkan_result][descriptor_pool_growth]") {
  // V6: both explicitly-named non-eligible VkResults, plus the general
  // "anything else" catch-all, matching this file's own existing
  // "falls back to Other/Unknown" precedent.
  REQUIRE_FALSE(isDescriptorPoolGrowthEligible(VK_ERROR_DEVICE_LOST));
  REQUIRE_FALSE(isDescriptorPoolGrowthEligible(VK_ERROR_OUT_OF_HOST_MEMORY));
  REQUIRE_FALSE(isDescriptorPoolGrowthEligible(VK_ERROR_OUT_OF_DEVICE_MEMORY));
  REQUIRE_FALSE(isDescriptorPoolGrowthEligible(VK_ERROR_INITIALIZATION_FAILED));
}
