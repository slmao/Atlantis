#pragma once

#include <optional>
#include <variant>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <atlantis/result.h>
#include <atlantis/rhi/presentation.h>
#include <atlantis/rhi/types.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include "vulkan_device.h"

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

struct FormatSelection {
  VkFormat vkFormat;
  VkColorSpaceKHR colorSpace;
  atlantis::rhi::Format approvedFormat;
};

// Selects one of Atlantis's four RHI-approved (format, color space) pairs
// from the surface's own reported list -- see vulkan_presentation.cpp for
// the fixed backend-private preference order and the VK_FORMAT_UNDEFINED
// special case, which examines the surface's reported color space rather
// than assuming VK_COLOR_SPACE_SRGB_NONLINEAR_KHR unconditionally. GPU-
// independent and pure: takes only already-obtained data, calls no
// Vulkan function. Declared here (rather than kept file-local) so
// GPU-independent unit tests can exercise it directly.
[[nodiscard]] std::optional<FormatSelection> selectSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);

// True when capabilities reports support for the one swapchain image
// usage this module requires (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) --
// Spec 0003's non-frame lifecycle needs nothing else (no transfer,
// storage, or sampled usage). GPU-independent and pure. Declared here for
// the same testability reason as selectSurfaceFormat() above.
[[nodiscard]] bool supportsRequiredSwapchainUsage(const VkSurfaceCapabilitiesKHR& capabilities);

// Concrete Vulkan implementation of atlantis::rhi::Presentation
// (ADR-0014), scoped to its non-frame lifecycle only (ADR-0016). See
// vulkan_presentation.cpp for the full recreateIfNeeded() implementation.
//
// Exclusively owns its VkSurfaceKHR and (once one exists) its
// VkSwapchainKHR. Holds a borrowed, non-owning reference to the
// VulkanDevice it was constructed from -- that device must outlive this
// object (caller-enforced; ADR-0003's explicit-ownership model, matching
// rhi::Presentation's own documented contract). Not copyable, not
// movable -- held exclusively behind
// std::unique_ptr<atlantis::rhi::Presentation>. Not internally
// thread-safe; every method here is caller-thread-only, the single
// Phase 1 logical frame thread (ADR-0004). No global mutable state.
// Stores nothing beyond this non-frame lifecycle: no swapchain image
// handle, no per-image view, no RenderTarget, no synchronization
// primitive, no command pool/buffer -- see ADR-0016.
class VulkanPresentation final : public atlantis::rhi::Presentation {
 public:
  // Takes ownership of an already-created surface; constructs in a
  // "recreation needed" state with no swapchain (Section 5's construction
  // sequence). Makes no Vulkan call itself -- surface must already be a
  // valid VkSurfaceKHR the caller created and is transferring ownership
  // of.
  VulkanPresentation(VulkanDevice& device, VkSurfaceKHR surface);
  ~VulkanPresentation() override;

  VulkanPresentation(const VulkanPresentation&) = delete;
  VulkanPresentation& operator=(const VulkanPresentation&) = delete;
  VulkanPresentation(VulkanPresentation&&) = delete;
  VulkanPresentation& operator=(VulkanPresentation&&) = delete;

  void notifyResized(atlantis::rhi::Extent2D extent) override;
  [[nodiscard]] atlantis::Result<std::monostate, atlantis::rhi::PresentationError> recreateIfNeeded() override;
  [[nodiscard]] atlantis::rhi::SwapchainMetadata metadata() const override;

 private:
  VulkanDevice& device_;
  VkSurfaceKHR surface_;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  atlantis::rhi::Extent2D trackedExtent_;
  bool recreationNeeded_ = true;
  atlantis::rhi::SwapchainMetadata metadata_;
};

}  // namespace atlantis::vulkan_backend::detail
