#include "vulkan_hdr_color_target.h"

namespace atlantis::vulkan_backend::detail {

VulkanHdrColorTarget::VulkanHdrColorTarget(VkDevice device, VkImage image, VkDeviceMemory memory,
                                            VkImageView imageView, atlantis::rhi::Extent2D extent,
                                            atlantis::rhi::HdrFormat format)
    : device_(device), image_(image), memory_(memory), imageView_(imageView), extent_(extent), format_(format) {}

VulkanHdrColorTarget::~VulkanHdrColorTarget() {
  vkDestroyImageView(device_, imageView_, nullptr);
  vkFreeMemory(device_, memory_, nullptr);
  vkDestroyImage(device_, image_, nullptr);
}

}  // namespace atlantis::vulkan_backend::detail
