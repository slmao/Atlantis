#pragma once

#include <optional>

#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

namespace atlantis::vulkan_backend::detail {

enum class RecreateAction { Skip, NoOp, Recreate };

// Structural dispatch for Presentation::recreateIfNeeded() (ADR-0016): a
// {0, 0} tracked extent always yields Skip, regardless of
// recreationNeeded -- the Vulkan-calling branch is unreachable from Skip
// by construction, not by caller discipline.
[[nodiscard]] inline RecreateAction decideRecreateAction(atlantis::rhi::Extent2D trackedExtent,
                                                          bool recreationNeeded) {
  if (trackedExtent.isZero()) return RecreateAction::Skip;
  if (!recreationNeeded) return RecreateAction::NoOp;
  return RecreateAction::Recreate;
}

// The concrete-surface presentation-support check: supported is
// vkGetPhysicalDeviceSurfaceSupportKHR's own output parameter, examined
// here only after that call's own VkResult has already been checked by
// the caller.
[[nodiscard]] inline std::optional<PresentationCreateError> checkSurfaceSupported(bool supported) {
  if (!supported) return PresentationCreateError::UnsupportedDevice;
  return std::nullopt;
}

}  // namespace atlantis::vulkan_backend::detail
