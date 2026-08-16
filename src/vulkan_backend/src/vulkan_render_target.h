#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/render_target.h>

#include "vulkan_render_target_access.h"

// Concrete Vulkan implementation of atlantis::rhi::RenderTarget
// (ADR-0019, ADR-0014's mechanism). See vulkan_presentation.cpp for where
// this is constructed (VulkanPresentation::acquireNextTarget()).
namespace atlantis::vulkan_backend::detail {

// Non-owning: the swapchain image and the acquire-complete semaphore both
// belong to the VulkanPresentation that vended this object -- see that
// class's own header comment. Frame-scoped, per rhi::RenderTarget's own
// contract; write-only this round (no method here or on VulkanCommandList
// ever reads its prior contents). Not copyable, not movable -- held
// exclusively behind std::unique_ptr<atlantis::rhi::RenderTarget>. Not
// internally thread-safe; caller-thread-only (ADR-0004).
//
// Also implements VulkanRenderTargetAccess (Spec 0010/ADR-0038) --
// inheritance-list-only addition; every accessor below already has the
// exact shape that interface's pure virtuals require, so none of their
// bodies change.
class VulkanRenderTarget final : public atlantis::rhi::RenderTarget, public VulkanRenderTargetAccess {
 public:
  VulkanRenderTarget(VkImage image, VkImageView imageView, std::uint32_t imageIndex, atlantis::rhi::Extent2D extent,
                      atlantis::rhi::Format format, VkSemaphore acquireCompleteSemaphore,
                      VkSemaphore renderFinishedSemaphore);
  ~VulkanRenderTarget() override = default;

  VulkanRenderTarget(const VulkanRenderTarget&) = delete;
  VulkanRenderTarget& operator=(const VulkanRenderTarget&) = delete;
  VulkanRenderTarget(VulkanRenderTarget&&) = delete;
  VulkanRenderTarget& operator=(VulkanRenderTarget&&) = delete;

  [[nodiscard]] atlantis::rhi::Extent2D extent() const override { return extent_; }
  [[nodiscard]] atlantis::rhi::Format format() const override { return format_; }

  // Accessors below exist solely for VulkanCommandList (barrier/clear
  // recording) and VulkanDevice::submit()/VulkanPresentation::present()
  // (reading which image/semaphore(s) this target refers to) -- never
  // reached from RHI's public surface, RenderGraph, or Renderer. Also
  // satisfy VulkanRenderTargetAccess (Spec 0010/ADR-0038).
  [[nodiscard]] VkImage image() const noexcept override { return image_; }
  // Spec 0007: the color VkImageView VulkanCommandList::beginRendering()
  // needs for VkRenderingAttachmentInfo -- non-owning, owned by the
  // VulkanPresentation that vended this object, same lifetime tier as
  // image() above.
  [[nodiscard]] VkImageView imageView() const noexcept override { return imageView_; }
  [[nodiscard]] std::uint32_t imageIndex() const noexcept { return imageIndex_; }
  [[nodiscard]] VkSemaphore acquireCompleteSemaphore() const noexcept override { return acquireCompleteSemaphore_; }
  // The one render-finished semaphore dedicated to this target's own
  // image index (VulkanPresentation's per-image pool, not a single
  // shared semaphore -- see that class's own header comment for why).
  // VulkanDevice::submit() signals this; VulkanSubmissionSignal wraps it
  // for Presentation::present() to wait on.
  [[nodiscard]] VkSemaphore renderFinishedSemaphore() const noexcept override { return renderFinishedSemaphore_; }

 private:
  VkImage image_;
  VkImageView imageView_;  // non-owning; VulkanPresentation owns it
  std::uint32_t imageIndex_;
  atlantis::rhi::Extent2D extent_;
  atlantis::rhi::Format format_;
  VkSemaphore acquireCompleteSemaphore_;   // non-owning; VulkanPresentation owns it
  VkSemaphore renderFinishedSemaphore_;    // non-owning; VulkanPresentation owns it
};

}  // namespace atlantis::vulkan_backend::detail
