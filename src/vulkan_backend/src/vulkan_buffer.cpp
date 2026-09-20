#include "vulkan_buffer.h"

#include <atlantis/assert.h>

namespace atlantis::vulkan_backend::detail {

VulkanBuffer::VulkanBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, void* mappedData,
                            atlantis::rhi::BufferPurpose purpose, std::size_t sizeBytes,
                            atlantis::rhi::IndexType indexType)
    : device_(device),
      buffer_(buffer),
      memory_(memory),
      mappedData_(mappedData),
      purpose_(purpose),
      sizeBytes_(sizeBytes),
      indexType_(indexType) {}

atlantis::rhi::IndexType VulkanBuffer::indexType() const {
  // Spec 0039 ruling O2 / ADR-0086: a partial accessor by design. A
  // meaningless value returned for a Vertex/Uniform/Readback/Staging
  // Buffer would look meaningful at the one call site that reads it
  // (VulkanCommandList::bindIndexBuffer()).
  ATLANTIS_CHECK(purpose_ == atlantis::rhi::BufferPurpose::Index);
  return indexType_;
}

VulkanBuffer::~VulkanBuffer() {
  // vkMapMemory()'d Buffer memory needs no explicit unmap before free --
  // vkFreeMemory() implicitly unmaps (Plan 0007 Section 9).
  vkFreeMemory(device_, memory_, nullptr);
  vkDestroyBuffer(device_, buffer_, nullptr);
}

}  // namespace atlantis::vulkan_backend::detail
