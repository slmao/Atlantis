#include "vulkan_texture.h"

namespace atlantis::vulkan_backend::detail {

VulkanTexture::VulkanTexture(VkDevice device, VkImage image, VkDeviceMemory memory, VkImageView imageView,
                              atlantis::rhi::Extent2D extent, atlantis::rhi::DepthFormat format)
    : device_(device), image_(image), memory_(memory), imageView_(imageView), extent_(extent), format_(format) {}

VulkanTexture::~VulkanTexture() {
  vkDestroyImageView(device_, imageView_, nullptr);
  vkFreeMemory(device_, memory_, nullptr);
  vkDestroyImage(device_, image_, nullptr);
}

}  // namespace atlantis::vulkan_backend::detail
