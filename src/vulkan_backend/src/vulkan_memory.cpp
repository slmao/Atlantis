#include "vulkan_memory.h"

namespace atlantis::vulkan_backend::detail {

std::optional<std::uint32_t> selectMemoryTypeIndex(const VkPhysicalDeviceMemoryProperties& memoryProperties,
                                                     std::uint32_t typeFilterBits,
                                                     VkMemoryPropertyFlags requiredProperties) {
  for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
    const bool typeAllowed = (typeFilterBits & (1u << index)) != 0;
    const bool propertiesSatisfied =
        (memoryProperties.memoryTypes[index].propertyFlags & requiredProperties) == requiredProperties;
    if (typeAllowed && propertiesSatisfied) {
      return index;
    }
  }
  return std::nullopt;
}

}  // namespace atlantis::vulkan_backend::detail
