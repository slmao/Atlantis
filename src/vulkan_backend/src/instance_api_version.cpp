#include "instance_api_version.h"

#include <vulkan/vulkan_core.h>

namespace atlantis::vulkan_backend::detail {

std::uint32_t decideRequestedInstanceApiVersion(bool loaderVersionQueryAvailable, std::uint32_t loaderVersion) {
  if (loaderVersionQueryAvailable && loaderVersion >= VK_API_VERSION_1_3) {
    return VK_API_VERSION_1_3;
  }
  return VK_API_VERSION_1_0;
}

}  // namespace atlantis::vulkan_backend::detail
