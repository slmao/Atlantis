#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

// Pure, GPU-independent VkResult -> RHI/Vulkan-Backend error mapping. None
// of these functions touch a VkInstance/VkDevice/VkSurfaceKHR; they only
// classify an already-obtained VkResult value, so they are safe to
// unit-test with literal VkResult enumerators and no real Vulkan call.
namespace atlantis::vulkan_backend::detail {

// Broad, VkResult-driven failure categories shared by the mapping
// functions below. Pure classification of the VkResult value alone -- it
// carries no notion of which Vulkan call produced it, so distinguishing
// "which call failed" (e.g. instance vs. device creation) is each mapping
// function's own job, not this classifier's.
enum class VulkanFailureCategory {
  SurfaceLost,
  DeviceLost,
  OutOfMemory,
  Other,
};

// result must not be VK_SUCCESS -- classifying a successful result is a
// programmer error (ATLANTIS_CHECK), not a case this function handles.
[[nodiscard]] VulkanFailureCategory classifyFailure(VkResult result);

// Each function below maps a non-success VkResult from one specific, named
// Vulkan call site to that call's own RHI/Vulkan-Backend error type.
// result must not be VK_SUCCESS in any of them.

// vkCreateInstance() (or an extension/layer-enumeration call made during
// instance creation).
[[nodiscard]] DeviceCreateError toInstanceCreationError(VkResult result);

// vkCreateDevice().
[[nodiscard]] DeviceCreateError toDeviceCreationError(VkResult result);

// The WSI surface-creation call (e.g. vkCreateWin32SurfaceKHR).
[[nodiscard]] PresentationCreateError toSurfaceCreationError(VkResult result);

// vkCreateSwapchainKHR and the calls immediately around it, inside
// Presentation::recreateIfNeeded().
[[nodiscard]] atlantis::rhi::PresentationError toSwapchainCreationError(VkResult result);

// vkAcquireNextImageKHR, for any result other than VK_SUCCESS,
// VK_ERROR_OUT_OF_DATE_KHR, and VK_SUBOPTIMAL_KHR -- those three are
// handled by acquireNextTarget()'s own branching (Plan 0006 Section 10)
// before this function is ever called.
[[nodiscard]] atlantis::rhi::PresentationError toAcquireFailureError(VkResult result);

// vkQueuePresentKHR, for any result other than VK_SUCCESS,
// VK_ERROR_OUT_OF_DATE_KHR, and VK_SUBOPTIMAL_KHR -- same split as
// toAcquireFailureError() above.
[[nodiscard]] atlantis::rhi::PresentationError toPresentFailureError(VkResult result);

// vkQueueSubmit, vkWaitForFences, vkResetFences, vkDeviceWaitIdle,
// vkEndCommandBuffer -- Device::submit()/waitIdle()'s own Vulkan calls.
[[nodiscard]] atlantis::rhi::SubmitError toSubmitError(VkResult result);

// vkAllocateCommandBuffers, vkBeginCommandBuffer --
// Device::createCommandList()'s own Vulkan calls.
[[nodiscard]] atlantis::rhi::CommandListCreateError toCommandListCreateError(VkResult result);

// vkCreateBuffer/vkAllocateMemory/vkBindBufferMemory --
// Device::createBuffer()'s own Vulkan calls (Spec 0007).
[[nodiscard]] atlantis::rhi::BufferCreateError toBufferCreateError(VkResult result);

// vkCreateImage/vkAllocateMemory/vkBindImageMemory/vkCreateImageView --
// Device::createTexture()'s own Vulkan calls (Spec 0007).
[[nodiscard]] atlantis::rhi::TextureCreateError toTextureCreateError(VkResult result);

// vkCreateShaderModule/vkCreateDescriptorSetLayout/vkAllocateDescriptorSets/
// vkCreatePipelineLayout/vkCreateGraphicsPipelines --
// Device::createPipeline()'s own Vulkan calls (Spec 0007).
[[nodiscard]] atlantis::rhi::PipelineCreateError toPipelineCreateError(VkResult result);

// vkCreateImage/vkAllocateMemory/vkBindImageMemory/vkCreateImageView --
// Device::createOffscreenTarget()'s own Vulkan calls (Spec 0010).
[[nodiscard]] atlantis::rhi::OffscreenTargetCreateError toOffscreenTargetCreateError(VkResult result);

}  // namespace atlantis::vulkan_backend::detail
