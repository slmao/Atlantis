#pragma once

#include <vulkan/vulkan_core.h>

// Spec 0010/ADR-0038's private polymorphic access boundary: a
// Vulkan-Backend-only, pure-abstract interface implemented by every
// concrete atlantis::rhi::RenderTarget implementation (VulkanRenderTarget,
// VulkanOffscreenRenderTarget), alongside (not instead of) each one's own
// public atlantis::rhi::RenderTarget inheritance. Adopted, via a checked
// pointer-form dynamic_cast + ATLANTIS_CHECK_MSG, at every call site that
// previously assumed its RenderTarget argument was always a
// VulkanRenderTarget: VulkanCommandList::transitionResource()/clearColor()/
// beginRendering()/copyRenderTargetToBuffer(), and VulkanDevice::submit().
// Presentation::present() is intentionally not among them --
// OffscreenTarget has no present() counterpart (ADR-0038), so
// Presentation::present() only ever receives a real VulkanRenderTarget and
// keeps its existing static_cast unchanged.
namespace atlantis::vulkan_backend::detail {

class VulkanRenderTargetAccess {
 public:
  virtual ~VulkanRenderTargetAccess() = default;

  [[nodiscard]] virtual VkImage image() const noexcept = 0;
  [[nodiscard]] virtual VkImageView imageView() const noexcept = 0;

  // VK_NULL_HANDLE is a legal, explicit value of this contract --
  // "nothing to wait on" / "nothing meaningful to signal" -- not an error
  // state and not something a caller may substitute a fabricated valid
  // handle for. VulkanRenderTarget never returns VK_NULL_HANDLE here (a
  // real swapchain image always has both semaphores);
  // VulkanOffscreenRenderTarget always does (ADR-0038: "a signal that is
  // always-already-satisfied").
  [[nodiscard]] virtual VkSemaphore acquireCompleteSemaphore() const noexcept = 0;
  [[nodiscard]] virtual VkSemaphore renderFinishedSemaphore() const noexcept = 0;
};

}  // namespace atlantis::vulkan_backend::detail
