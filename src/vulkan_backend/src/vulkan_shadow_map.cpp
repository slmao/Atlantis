#include "vulkan_shadow_map.h"

namespace atlantis::vulkan_backend::detail {

VulkanShadowMap::VulkanShadowMap(VkDevice device, VkImage image, VkDeviceMemory memory, VkImageView imageView,
                                  atlantis::rhi::Extent2D extent, atlantis::rhi::DepthFormat format)
    : device_(device), image_(image), memory_(memory), imageView_(imageView), extent_(extent), format_(format) {}

VulkanShadowMap::~VulkanShadowMap() {
  vkDestroyImageView(device_, imageView_, nullptr);
  vkFreeMemory(device_, memory_, nullptr);
  vkDestroyImage(device_, image_, nullptr);
}

}  // namespace atlantis::vulkan_backend::detail
