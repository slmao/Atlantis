#include "vulkan_sampled_texture.h"

namespace atlantis::vulkan_backend::detail {

VulkanSampledTexture::VulkanSampledTexture(VkDevice device, VkImage image, VkDeviceMemory memory,
                                            VkImageView imageView, atlantis::rhi::Extent2D extent,
                                            atlantis::rhi::SampledTextureFormat format)
    : device_(device), image_(image), memory_(memory), imageView_(imageView), extent_(extent), format_(format) {}

VulkanSampledTexture::~VulkanSampledTexture() {
  vkDestroyImageView(device_, imageView_, nullptr);
  vkFreeMemory(device_, memory_, nullptr);
  vkDestroyImage(device_, image_, nullptr);
}

}  // namespace atlantis::vulkan_backend::detail
