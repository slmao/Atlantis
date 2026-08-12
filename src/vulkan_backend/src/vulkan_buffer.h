#pragma once

#include <cstddef>

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/types.h>

// Concrete Vulkan implementation of atlantis::rhi::Buffer (Spec 0007 /
// ADR-0023). See vulkan_device.cpp for VulkanDevice::createBuffer()'s
// full allocation sequence (vulkan_memory.h's selectMemoryTypeIndex()).
namespace atlantis::vulkan_backend::detail {

// Exclusively owns its VkBuffer and its own individual VkDeviceMemory
// allocation (never shared with any other resource, per ADR-0023) --
// mapped once, for its whole lifetime, at construction (host-visible/
// host-coherent memory, every purpose this round). Constructed only via
// VulkanDevice::createBuffer(). Move-only mechanism is Buffer's own
// interface contract (held behind std::unique_ptr<Buffer>) -- this
// concrete type itself is non-copyable/non-movable, matching every other
// concrete Vulkan-Backend resource type in this codebase (VulkanRenderTarget,
// VulkanCommandList). Not internally thread-safe; caller-thread-only
// (ADR-0004).
class VulkanBuffer final : public atlantis::rhi::Buffer {
 public:
  VulkanBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, void* mappedData,
               atlantis::rhi::BufferPurpose purpose, std::size_t sizeBytes);
  ~VulkanBuffer() override;

  VulkanBuffer(const VulkanBuffer&) = delete;
  VulkanBuffer& operator=(const VulkanBuffer&) = delete;
  VulkanBuffer(VulkanBuffer&&) = delete;
  VulkanBuffer& operator=(VulkanBuffer&&) = delete;

  [[nodiscard]] atlantis::rhi::BufferPurpose purpose() const override { return purpose_; }
  [[nodiscard]] std::size_t sizeBytes() const override { return sizeBytes_; }
  [[nodiscard]] void* mappedData() override { return mappedData_; }

  // Exists solely for VulkanCommandList's bind*Buffer() bodies -- never
  // reached from RHI's public surface.
  [[nodiscard]] VkBuffer vkBuffer() const noexcept { return buffer_; }

 private:
  VkDevice device_;  // non-owning; must outlive this object (caller-enforced)
  VkBuffer buffer_;
  VkDeviceMemory memory_;
  void* mappedData_;
  atlantis::rhi::BufferPurpose purpose_;
  std::size_t sizeBytes_;
};

}  // namespace atlantis::vulkan_backend::detail
