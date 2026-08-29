#include "vulkan_descriptor_pool_growth.h"

#include <atlantis/assert.h>

namespace atlantis::vulkan_backend::detail {

std::uint32_t descriptorPoolMaxSetsForGeneration(std::size_t generationIndex) {
  ATLANTIS_CHECK(generationIndex < kMaxDescriptorPoolCount);
  return kDescriptorPoolMaxSetsByGeneration[generationIndex];
}

}  // namespace atlantis::vulkan_backend::detail
