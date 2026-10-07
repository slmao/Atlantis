#include "vulkan_offscreen_target.h"

#include <atlantis/assert.h>

#include "vulkan_offscreen_render_target.h"

namespace atlantis::vulkan_backend::detail {

VulkanOffscreenTarget::VulkanOffscreenTarget(VkDevice device, VkImage image, VkDeviceMemory memory,
                                              VkImageView imageView, atlantis::rhi::Extent2D extent,
                                              atlantis::rhi::Format format, bool sampled)
    : device_(device),
      image_(image),
      memory_(memory),
      imageView_(imageView),
      extent_(extent),
      format_(format),
      sampled_(sampled) {}

VulkanOffscreenTarget::~VulkanOffscreenTarget() {
  ATLANTIS_CHECK_MSG(!outstandingBorrow_,
                      "VulkanOffscreenTarget destroyed while a vended borrow is still outstanding");
  vkDestroyImageView(device_, imageView_, nullptr);
  vkFreeMemory(device_, memory_, nullptr);
  vkDestroyImage(device_, image_, nullptr);
}

atlantis::Result<std::unique_ptr<atlantis::rhi::RenderTarget>, atlantis::rhi::OffscreenAcquireError>
VulkanOffscreenTarget::acquireTarget() {
  using ResultT =
      atlantis::Result<std::unique_ptr<atlantis::rhi::RenderTarget>, atlantis::rhi::OffscreenAcquireError>;

  ATLANTIS_CHECK_MSG(!outstandingBorrow_,
                      "OffscreenTarget::acquireTarget() called while a previously-vended borrow is still outstanding");
  outstandingBorrow_ = true;
  return ResultT::Ok(std::make_unique<VulkanOffscreenRenderTarget>(this));
}

VkImageUsageFlags offscreenTargetImageUsage(bool sampled) noexcept {
  VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (sampled) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
  return usage;
}

}  // namespace atlantis::vulkan_backend::detail
