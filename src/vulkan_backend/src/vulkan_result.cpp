#include "vulkan_result.h"

#include <atlantis/assert.h>

namespace atlantis::vulkan_backend::detail {

VulkanFailureCategory classifyFailure(VkResult result) {
  ATLANTIS_CHECK(result != VK_SUCCESS);
  switch (result) {
    case VK_ERROR_SURFACE_LOST_KHR:
      return VulkanFailureCategory::SurfaceLost;
    case VK_ERROR_DEVICE_LOST:
      return VulkanFailureCategory::DeviceLost;
    case VK_ERROR_OUT_OF_HOST_MEMORY:
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      return VulkanFailureCategory::OutOfMemory;
    default:
      return VulkanFailureCategory::Other;
  }
}

DeviceCreateError toInstanceCreationError(VkResult result) {
  ATLANTIS_CHECK(result != VK_SUCCESS);
  return DeviceCreateError::InstanceCreationFailed;
}

DeviceCreateError toDeviceCreationError(VkResult result) {
  ATLANTIS_CHECK(result != VK_SUCCESS);
  return DeviceCreateError::DeviceCreationFailed;
}

PresentationCreateError toSurfaceCreationError(VkResult result) {
  ATLANTIS_CHECK(result != VK_SUCCESS);
  return PresentationCreateError::SurfaceCreationFailed;
}

atlantis::rhi::PresentationError toSwapchainCreationError(VkResult result) {
  switch (classifyFailure(result)) {
    case VulkanFailureCategory::SurfaceLost:
      return atlantis::rhi::PresentationError::SurfaceLost;
    case VulkanFailureCategory::DeviceLost:
      return atlantis::rhi::PresentationError::DeviceLost;
    case VulkanFailureCategory::OutOfMemory:
      return atlantis::rhi::PresentationError::SwapchainCreationFailed;
    case VulkanFailureCategory::Other:
      return atlantis::rhi::PresentationError::Unknown;
  }
  ATLANTIS_CHECK_MSG(false, "unreachable: all VulkanFailureCategory enumerators handled above");
  return atlantis::rhi::PresentationError::Unknown;
}

atlantis::rhi::PresentationError toAcquireFailureError(VkResult result) {
  switch (classifyFailure(result)) {
    case VulkanFailureCategory::SurfaceLost:
      return atlantis::rhi::PresentationError::SurfaceLost;
    case VulkanFailureCategory::DeviceLost:
      return atlantis::rhi::PresentationError::DeviceLost;
    case VulkanFailureCategory::OutOfMemory:
    case VulkanFailureCategory::Other:
      return atlantis::rhi::PresentationError::Unknown;
  }
  ATLANTIS_CHECK_MSG(false, "unreachable: all VulkanFailureCategory enumerators handled above");
  return atlantis::rhi::PresentationError::Unknown;
}

atlantis::rhi::PresentationError toPresentFailureError(VkResult result) {
  // Same classification as acquire failures -- both are frame-level
  // swapchain operations with an identical PresentationError vocabulary.
  return toAcquireFailureError(result);
}

atlantis::rhi::SubmitError toSubmitError(VkResult result) {
  switch (classifyFailure(result)) {
    case VulkanFailureCategory::DeviceLost:
      return atlantis::rhi::SubmitError::DeviceLost;
    case VulkanFailureCategory::SurfaceLost:
    case VulkanFailureCategory::OutOfMemory:
    case VulkanFailureCategory::Other:
      return atlantis::rhi::SubmitError::QueueSubmitFailed;
  }
  ATLANTIS_CHECK_MSG(false, "unreachable: all VulkanFailureCategory enumerators handled above");
  return atlantis::rhi::SubmitError::QueueSubmitFailed;
}

atlantis::rhi::CommandListCreateError toCommandListCreateError(VkResult result) {
  ATLANTIS_CHECK(result != VK_SUCCESS);
  return atlantis::rhi::CommandListCreateError::CommandBufferAllocationFailed;
}

}  // namespace atlantis::vulkan_backend::detail
