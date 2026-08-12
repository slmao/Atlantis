#include "vulkan_buffer.h"

namespace atlantis::vulkan_backend::detail {

VulkanBuffer::VulkanBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, void* mappedData,
                            atlantis::rhi::BufferPurpose purpose, std::size_t sizeBytes)
    : device_(device),
      buffer_(buffer),
      memory_(memory),
      mappedData_(mappedData),
      purpose_(purpose),
      sizeBytes_(sizeBytes) {}

VulkanBuffer::~VulkanBuffer() {
  // vkMapMemory()'d Buffer memory needs no explicit unmap before free --
  // vkFreeMemory() implicitly unmaps (Plan 0007 Section 9).
  vkFreeMemory(device_, memory_, nullptr);
  vkDestroyBuffer(device_, buffer_, nullptr);
}

}  // namespace atlantis::vulkan_backend::detail
