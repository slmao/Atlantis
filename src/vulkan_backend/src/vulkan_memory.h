#pragma once

#include <cstdint>
#include <optional>

#include <vulkan/vulkan_core.h>

// Pure, GPU-independent memory-type selection (Spec 0007 / ADR-0023),
// shared by VulkanBuffer/VulkanTexture's construction. Does not touch a
// live VkDevice or VkDeviceMemory -- safe to unit-test with a synthetic
// VkPhysicalDeviceMemoryProperties and no real device.
namespace atlantis::vulkan_backend::detail {

// Standard Vulkan idiom: scans memoryProperties.memoryTypes for the first
// entry whose bit is set in typeFilterBits and whose propertyFlags fully
// contain requiredProperties. Returns an empty optional if none matches
// (the real caller maps that to BufferCreateError::AllocationFailed /
// TextureCreateError::AllocationFailed).
[[nodiscard]] std::optional<std::uint32_t> selectMemoryTypeIndex(
    const VkPhysicalDeviceMemoryProperties& memoryProperties, std::uint32_t typeFilterBits,
    VkMemoryPropertyFlags requiredProperties);

}  // namespace atlantis::vulkan_backend::detail
