#pragma once

#include <memory>

#include <vulkan/vulkan_core.h>

#include <atlantis/result.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/types.h>

// Concrete Vulkan implementation of atlantis::rhi::OffscreenTarget
// (Spec 0010/ADR-0038). See vulkan_device.cpp for
// VulkanDevice::createOffscreenTarget()'s full allocation sequence.
namespace atlantis::vulkan_backend::detail {

class VulkanOffscreenRenderTarget;

// Owning: exclusively owns its color VkImage, its own individual
// VkDeviceMemory allocation, and its VkImageView, for its entire
// lifetime -- the exact same "one owning Vulkan Backend type per public
// RHI resource type" shape VulkanTexture already establishes
// (ADR-0023). VulkanOffscreenTarget is to VulkanOffscreenRenderTarget
// exactly what VulkanPresentation is to VulkanRenderTarget (ADR-0038).
// Constructed only via VulkanDevice::createOffscreenTarget().
// Non-copyable/non-movable, matching every other concrete Vulkan Backend
// resource type. Not internally thread-safe; caller-thread-only
// (ADR-0004).
//
// Lifetime contract (ADR-0038's Lifetime contract section is the full,
// authoritative statement): the destructor below does NOT call
// vkDeviceWaitIdle() or wait on any fence -- deliberately the same
// "destructor does not itself wait" tier as
// VulkanPresentation::~VulkanPresentation(). The caller must call
// Device::waitIdle() after the last Device::submit() call that
// referenced this object's resources before destroying it -- an
// undetectable lifetime precondition, not something this destructor
// re-verifies. Only the double-acquire and destroy-while-borrow-
// outstanding misuse cases below are guaranteed-detectable, via
// outstandingBorrow_.
class VulkanOffscreenTarget final : public atlantis::rhi::OffscreenTarget {
 public:
  VulkanOffscreenTarget(VkDevice device, VkImage image, VkDeviceMemory memory, VkImageView imageView,
                        atlantis::rhi::Extent2D extent, atlantis::rhi::Format format);
  ~VulkanOffscreenTarget() override;

  VulkanOffscreenTarget(const VulkanOffscreenTarget&) = delete;
  VulkanOffscreenTarget& operator=(const VulkanOffscreenTarget&) = delete;
  VulkanOffscreenTarget(VulkanOffscreenTarget&&) = delete;
  VulkanOffscreenTarget& operator=(VulkanOffscreenTarget&&) = delete;

  [[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::RenderTarget>, atlantis::rhi::OffscreenAcquireError>
  acquireTarget() override;

  // Vulkan-Backend-internal-only accessors, exist solely for
  // VulkanOffscreenRenderTarget to delegate to -- never reached from
  // RHI's public surface, RenderGraph, or Renderer.
  [[nodiscard]] VkImage image() const noexcept { return image_; }
  [[nodiscard]] VkImageView imageView() const noexcept { return imageView_; }
  [[nodiscard]] atlantis::rhi::Extent2D extent() const noexcept { return extent_; }
  [[nodiscard]] atlantis::rhi::Format format() const noexcept { return format_; }

 private:
  friend class VulkanOffscreenRenderTarget;
  // Called only by VulkanOffscreenRenderTarget's destructor, when a
  // vended borrow ends -- clears the outstanding-borrow tracking that
  // backs the guaranteed-detectable double-acquire/destroy-while-
  // borrowed checks (ADR-0038).
  void clearOutstandingBorrow() noexcept { outstandingBorrow_ = false; }

  VkDevice device_;  // non-owning; must outlive this object (caller-enforced)
  VkImage image_;
  VkDeviceMemory memory_;
  VkImageView imageView_;
  atlantis::rhi::Extent2D extent_;
  atlantis::rhi::Format format_;
  bool outstandingBorrow_ = false;
};

}  // namespace atlantis::vulkan_backend::detail
