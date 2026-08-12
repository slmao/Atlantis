#include "device_extension_list.h"

#include <atlantis/assert.h>

namespace atlantis::vulkan_backend::detail {

std::vector<std::string> buildDeviceExtensionList(DynamicRenderingPath path,
                                                    const std::vector<std::string>& requiredExtensions) {
  ATLANTIS_CHECK(path != DynamicRenderingPath::Unavailable);

  std::vector<std::string> extensions = requiredExtensions;
  if (path == DynamicRenderingPath::Extension) {
    // Dependency order, per the Vulkan specification's own
    // extension-dependency table for VK_KHR_dynamic_rendering: the
    // extension itself requires VK_KHR_depth_stencil_resolve, which
    // requires VK_KHR_create_renderpass2, which requires
    // VK_KHR_multiview and VK_KHR_maintenance2 -- all listed here
    // explicitly because this repository's instance never requests above
    // VK_API_VERSION_1_0 on a loader that does not itself support 1.3
    // (see instance_api_version.h), so none of these promoted-to-1.1/1.2
    // extensions is implicitly available on such an instance.
    extensions.push_back("VK_KHR_multiview");
    extensions.push_back("VK_KHR_maintenance2");
    extensions.push_back("VK_KHR_create_renderpass2");
    extensions.push_back("VK_KHR_depth_stencil_resolve");
    extensions.push_back("VK_KHR_dynamic_rendering");
  }
  return extensions;
}

}  // namespace atlantis::vulkan_backend::detail
