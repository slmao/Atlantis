#pragma once

#include <cstdint>
#include <memory>

#include <vulkan/vulkan_core.h>

#include <atlantis/result.h>
#include <atlantis/rhi/device.h>

// Concrete Vulkan implementation of atlantis::rhi::Device (ADR-0014). See
// vulkan_device.cpp for createDevice()'s full orchestration (instance
// creation, validation installation, physical device/queue-family
// selection, logical device/queue creation).
namespace atlantis::vulkan_backend::detail {

// Exclusively owns its VkInstance, the VkPhysicalDevice it selected, its
// VkDevice, the single VkQueue created from the selected combined
// graphics/present queue family, that queue family's index, (when
// validation is enabled) the explicit VkDebugUtilsMessengerEXT installed
// immediately after instance creation succeeds (see
// plans/0003-rhi-vulkan-windowed-foundation.md Section 6), and -- new in
// Plan 0006 -- a persistent VkCommandPool, a persistent render-finished
// VkSemaphore, and a persistent submission VkFence used by
// createCommandList()/submit()/waitIdle() below. No separate validation
// *state* beyond the messenger handle.
//
// Not copyable, not movable -- held exclusively behind
// std::unique_ptr<atlantis::rhi::Device> (ADR-0014); nothing in this
// Plan needs to relocate a VulkanDevice, so this simplifies destruction-
// order reasoning at no real cost. Not internally thread-safe;
// construction, use, and destruction all happen on the single Phase 1
// logical frame thread (ADR-0004). No global mutable state.
//
// Single frame-in-flight submission ownership (ADR-0020, Plan 0006
// Section 9): submit() takes ownership of the CommandList passed to it
// and retains it (plus submissionFence_) until the *next* submit() call,
// waitIdle(), or this destructor releases it -- a caller never manages a
// fence or decides when a CommandList is safe to destroy. Precondition,
// not enforced by the type system, confirmed by Human Review as an
// accepted design constraint: a caller must call
// VulkanPresentation::present() for a successful submit() before calling
// submit() again -- submit() followed directly by application exit
// remains legal (waitIdle() drains it). See
// plans/0006-rhi-render-graph-frame-execution-foundation.md.
//
// The accessor methods below exist solely for VulkanPresentation (build a
// VkSurfaceKHR, run the concrete-surface presentation-support check,
// vkQueuePresentKHR) and VulkanCommandList/VulkanDevice's own
// createCommandList()/submit() bodies -- never reach RHI's or Vulkan
// Backend's public surface, and are not a general GPU-handle escape
// hatch for a future RenderGraph, Renderer, or second backend.
class VulkanDevice final : public atlantis::rhi::Device {
 public:
  // destroyMessengerFn must be non-null whenever explicitMessenger is not
  // VK_NULL_HANDLE (validation enabled), and is otherwise never
  // dereferenced. Stored rather than re-resolved via vkGetInstanceProcAddr
  // at destruction time -- necessary destruction bookkeeping, not
  // additional diagnostics state. commandPool/renderFinishedSemaphore/
  // submissionFence are all already-created, valid handles this
  // constructor takes ownership of (created by createDevice() below,
  // guard-protected until this constructor succeeds -- same two-phase
  // ownership-transfer pattern as instance/device/messenger).
  VulkanDevice(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue,
               std::uint32_t queueFamilyIndex, VkDebugUtilsMessengerEXT explicitMessenger,
               PFN_vkDestroyDebugUtilsMessengerEXT destroyMessengerFn, VkCommandPool commandPool,
               VkSemaphore renderFinishedSemaphore, VkFence submissionFence);
  ~VulkanDevice() override;

  VulkanDevice(const VulkanDevice&) = delete;
  VulkanDevice& operator=(const VulkanDevice&) = delete;
  VulkanDevice(VulkanDevice&&) = delete;
  VulkanDevice& operator=(VulkanDevice&&) = delete;

  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::CommandList>, atlantis::rhi::CommandListCreateError>
  createCommandList() override;
  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::SubmissionSignal>, atlantis::rhi::SubmitError> submit(
      std::unique_ptr<atlantis::rhi::CommandList> commandList, const atlantis::rhi::RenderTarget& target) override;
  [[nodiscard]] atlantis::Result<std::monostate, atlantis::rhi::SubmitError> waitIdle() override;

  [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
  [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
  [[nodiscard]] VkDevice device() const noexcept { return device_; }
  [[nodiscard]] VkQueue queue() const noexcept { return queue_; }
  [[nodiscard]] std::uint32_t queueFamilyIndex() const noexcept { return queueFamilyIndex_; }

 private:
  // Waits on submissionFence_ (if a submission is currently retained),
  // resets it, and releases retainedSubmission_ -- the shared first step
  // of submit(), waitIdle(), and the destructor. Returns Err only for a
  // genuine vkWaitForFences/vkResetFences failure; a no-op (Ok) if
  // nothing is currently retained.
  [[nodiscard]] atlantis::Result<std::monostate, atlantis::rhi::SubmitError> waitAndReleaseRetainedSubmission();

  VkInstance instance_;
  VkPhysicalDevice physicalDevice_;
  VkDevice device_;
  VkQueue queue_;
  std::uint32_t queueFamilyIndex_;
  VkDebugUtilsMessengerEXT explicitMessenger_;  // VK_NULL_HANDLE when validation is disabled
  PFN_vkDestroyDebugUtilsMessengerEXT destroyMessengerFn_;  // meaningful only when explicitMessenger_ is set

  VkCommandPool commandPool_;
  VkSemaphore renderFinishedSemaphore_;
  VkFence submissionFence_;
  std::unique_ptr<atlantis::rhi::CommandList> retainedSubmission_;  // null until the first submit()
  bool hasRetainedSubmission_ = false;
};

}  // namespace atlantis::vulkan_backend::detail
