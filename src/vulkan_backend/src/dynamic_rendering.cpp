#include "dynamic_rendering.h"

namespace atlantis::vulkan_backend::detail {

DynamicRenderingPath decideDynamicRenderingPath(bool physicalDeviceProperties2InstanceExtensionAvailable,
                                                  bool apiVersionAtLeast1_3, bool coreFeatureSupported,
                                                  bool extensionAdvertised, bool extensionFeatureSupported) {
  if (!physicalDeviceProperties2InstanceExtensionAvailable) {
    return DynamicRenderingPath::Unavailable;
  }
  if (apiVersionAtLeast1_3 && coreFeatureSupported) {
    return DynamicRenderingPath::Core;
  }
  if (extensionAdvertised && extensionFeatureSupported) {
    return DynamicRenderingPath::Extension;
  }
  return DynamicRenderingPath::Unavailable;
}

}  // namespace atlantis::vulkan_backend::detail
