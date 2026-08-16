#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/render_target.h>

#include "vulkan_offscreen_target.h"
#include "vulkan_render_target_access.h"

// Concrete Vulkan implementation of atlantis::rhi::RenderTarget vended by
// VulkanOffscreenTarget::acquireTarget() (Spec 0010/ADR-0038) -- a second,
// distinct concrete RenderTarget implementation alongside the existing
// VulkanRenderTarget; both implement the same unchanged public interface
// (extent()/format() only), so RenderGraph/Renderer cannot and do not
// need to distinguish them (ADR-0002).
namespace atlantis::vulkan_backend::detail {

// Non-owning: borrows the image/view from the owning VulkanOffscreenTarget
// exactly as the existing VulkanRenderTarget already borrows from
// VulkanPresentation -- holds no VkImage/VkImageView/Extent2D/Format
// member of its own, delegating every accessor to owner_ instead, to
// avoid duplicated/copied handle state. Destroying this object has no
// Vulkan side effect (non-owning) beyond notifying owner_ that the borrow
// has ended -- the entire RAII-return contract from ADR-0038; no public
// release()/consume() method exists. Not copyable, not movable -- held
// exclusively behind std::unique_ptr<atlantis::rhi::RenderTarget>. Not
// internally thread-safe; caller-thread-only (ADR-0004).
class VulkanOffscreenRenderTarget final : public atlantis::rhi::RenderTarget, public VulkanRenderTargetAccess {
 public:
  explicit VulkanOffscreenRenderTarget(VulkanOffscreenTarget* owner) : owner_(owner) {}
  ~VulkanOffscreenRenderTarget() override { owner_->clearOutstandingBorrow(); }

  VulkanOffscreenRenderTarget(const VulkanOffscreenRenderTarget&) = delete;
  VulkanOffscreenRenderTarget& operator=(const VulkanOffscreenRenderTarget&) = delete;
  VulkanOffscreenRenderTarget(VulkanOffscreenRenderTarget&&) = delete;
  VulkanOffscreenRenderTarget& operator=(VulkanOffscreenRenderTarget&&) = delete;

  [[nodiscard]] atlantis::rhi::Extent2D extent() const override { return owner_->extent(); }
  [[nodiscard]] atlantis::rhi::Format format() const override { return owner_->format(); }
  [[nodiscard]] VkImage image() const noexcept override { return owner_->image(); }
  [[nodiscard]] VkImageView imageView() const noexcept override { return owner_->imageView(); }

  // Explicit, legal "nothing to wait on / nothing meaningful to signal"
  // value -- an offscreen submission has no swapchain acquire semaphore
  // to wait on and nothing that ever waits on a headless render-finished
  // signal, since present() is never called for a headless target
  // (ADR-0038). Never a fabricated valid handle.
  [[nodiscard]] VkSemaphore acquireCompleteSemaphore() const noexcept override { return VK_NULL_HANDLE; }
  [[nodiscard]] VkSemaphore renderFinishedSemaphore() const noexcept override { return VK_NULL_HANDLE; }

 private:
  VulkanOffscreenTarget* owner_;  // non-owning; must outlive this borrow --
                                   // enforced by the outstanding-borrow
                                   // contract (ADR-0038), not the type system
};

}  // namespace atlantis::vulkan_backend::detail
