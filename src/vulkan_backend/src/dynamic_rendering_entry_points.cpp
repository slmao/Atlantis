#include "dynamic_rendering_entry_points.h"

namespace atlantis::vulkan_backend::detail {

std::optional<DynamicRenderingEntryPointNames> selectDynamicRenderingEntryPointNames(DynamicRenderingPath path) {
  switch (path) {
    case DynamicRenderingPath::Core:
      return DynamicRenderingEntryPointNames{"vkCmdBeginRendering", "vkCmdEndRendering"};
    case DynamicRenderingPath::Extension:
      return DynamicRenderingEntryPointNames{"vkCmdBeginRenderingKHR", "vkCmdEndRenderingKHR"};
    case DynamicRenderingPath::Unavailable:
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace atlantis::vulkan_backend::detail
