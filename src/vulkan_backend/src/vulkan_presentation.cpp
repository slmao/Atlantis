#include "vulkan_presentation.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include <atlantis/assert.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include "vulkan_result.h"
#include "wsi/win32_surface.h"

namespace atlantis::vulkan_backend::detail {

namespace {

// Move-only RAII guards, matching vulkan_device.cpp's own established
// pattern: constructed while still owning a handle, get() offers
// non-owning read access, release() transfers ownership out only after
// whatever depends on the handle has been proven to succeed.

class SurfaceGuard {
 public:
  SurfaceGuard(VkInstance instance, VkSurfaceKHR surface) : instance_(instance), surface_(surface) {}
  ~SurfaceGuard() {
    if (surface_ != VK_NULL_HANDLE) {
      vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
  }
  SurfaceGuard(const SurfaceGuard&) = delete;
  SurfaceGuard& operator=(const SurfaceGuard&) = delete;

  [[nodiscard]] VkSurfaceKHR get() const noexcept { return surface_; }
  [[nodiscard]] VkSurfaceKHR release() noexcept {
    VkSurfaceKHR released = surface_;
    surface_ = VK_NULL_HANDLE;
    return released;
  }

 private:
  VkInstance instance_;
  VkSurfaceKHR surface_;
};

class SwapchainGuard {
 public:
  SwapchainGuard(VkDevice device, VkSwapchainKHR swapchain) : device_(device), swapchain_(swapchain) {}
  ~SwapchainGuard() {
    if (swapchain_ != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    }
  }
  SwapchainGuard(const SwapchainGuard&) = delete;
  SwapchainGuard& operator=(const SwapchainGuard&) = delete;

  [[nodiscard]] VkSwapchainKHR get() const noexcept { return swapchain_; }
  [[nodiscard]] VkSwapchainKHR release() noexcept {
    VkSwapchainKHR released = swapchain_;
    swapchain_ = VK_NULL_HANDLE;
    return released;
  }

 private:
  VkDevice device_;
  VkSwapchainKHR swapchain_;
};

using FormatsResultT = atlantis::Result<std::vector<VkSurfaceFormatKHR>, atlantis::rhi::PresentationError>;

// Two-call idiom for vkGetPhysicalDeviceSurfaceFormatsKHR. Same
// VK_INCOMPLETE/failure handling and final-count-resize rationale
// established in vulkan_instance.cpp/vulkan_device.cpp's own enumeration
// helpers: partial data is never treated as a complete result. Unlike
// those helpers, this one preserves the specific VkResult each call
// failed with (via toSwapchainCreationError()) rather than collapsing
// every failure into a single generic error -- callers can still
// distinguish SurfaceLost/DeviceLost/Unknown from a genuine "the surface
// has no compatible format" outcome.
[[nodiscard]] FormatsResultT querySurfaceFormats(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
  std::uint32_t count = 0;
  const VkResult countResult = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr);
  if (countResult != VK_SUCCESS) {
    return FormatsResultT::Err(toSwapchainCreationError(countResult));
  }
  if (count == 0) {
    return FormatsResultT::Ok(std::vector<VkSurfaceFormatKHR>{});
  }

  std::vector<VkSurfaceFormatKHR> formats(count);
  const VkResult fillResult = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data());
  if (fillResult != VK_SUCCESS) {
    return FormatsResultT::Err(toSwapchainCreationError(fillResult));
  }
  formats.resize(count);
  return FormatsResultT::Ok(std::move(formats));
}

// Fixed, deterministic backend-private preference order -- the Approved
// Plan does not fix a literal priority among the four RHI-approved
// formats, so this module picks one: atlantis/rhi/types.h's own
// declaration order (Bgra8Unorm, Bgra8Srgb, Rgba8Unorm, Rgba8Srgb), each
// paired with VK_COLOR_SPACE_SRGB_NONLINEAR_KHR -- the color space every
// consumer Windows driver reports for these formats, and the Plan does
// not fix a different one either. Color space is matched, not just
// format, per the swapchain creation requirement that both agree.
constexpr FormatSelection kApprovedFormatsInPreferenceOrder[] = {
    {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, atlantis::rhi::Format::Bgra8Unorm},
    {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, atlantis::rhi::Format::Bgra8Srgb},
    {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, atlantis::rhi::Format::Rgba8Unorm},
    {VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, atlantis::rhi::Format::Rgba8Srgb},
};

}  // namespace

// Selects the first entry of kApprovedFormatsInPreferenceOrder (in this
// module's own preference order, not the surface's returned order) whose
// (format, colorSpace) pair the surface actually reports. Handles
// Vulkan's documented special case: exactly one entry with
// VK_FORMAT_UNDEFINED means the surface has no preferred format and the
// application may choose freely -- but only the *format* is
// unconstrained in that case, not the color space: formats[0].colorSpace
// is still the surface's own reported value and must be checked, not
// assumed. Only when it equals VK_COLOR_SPACE_SRGB_NONLINEAR_KHR (the one
// color space this module supports) does this function pick its top
// preference; any other reported color space means none of Atlantis's
// four RHI-approved (format, color space) pairs is actually usable here,
// so no selection is made -- never a color space the surface did not
// report support for.
[[nodiscard]] std::optional<FormatSelection> selectSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
  if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
    if (formats[0].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return kApprovedFormatsInPreferenceOrder[0];
    }
    return std::nullopt;
  }
  for (const FormatSelection& candidate : kApprovedFormatsInPreferenceOrder) {
    for (const VkSurfaceFormatKHR& available : formats) {
      if (available.format == candidate.vkFormat && available.colorSpace == candidate.colorSpace) {
        return candidate;
      }
    }
  }
  return std::nullopt;
}

// True when capabilities.supportedUsageFlags reports
// VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT -- the only swapchain image usage
// Spec 0003's non-frame lifecycle needs (no transfer, storage, sampled,
// or any usage a future Renderer might want).
[[nodiscard]] bool supportsRequiredSwapchainUsage(const VkSurfaceCapabilitiesKHR& capabilities) {
  return (capabilities.supportedUsageFlags & static_cast<VkFlags>(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) != 0;
}

namespace {

// capabilities.currentExtent.width != UINT32_MAX means the surface
// dictates its own extent (typical on Windows) -- use it as-is. The
// UINT32_MAX sentinel means the surface lets the application choose
// within [minImageExtent, maxImageExtent]; the tracked extent
// (framebuffer extent from the most recent notifyResized(), never a
// logical window extent) is clamped into that range. A {0, 0} tracked
// extent never reaches this function -- decideRecreateAction() already
// returned Skip before any surface/swapchain query in that case.
[[nodiscard]] VkExtent2D selectSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                                                atlantis::rhi::Extent2D trackedExtent) {
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }
  VkExtent2D extent{};
  extent.width =
      std::clamp(trackedExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
  extent.height =
      std::clamp(trackedExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
  return extent;
}

// Minimal fixed policy: one more than the surface's minimum, capped at
// its maximum (0 means "no maximum"). No caller-visible configuration.
[[nodiscard]] std::uint32_t selectImageCount(const VkSurfaceCapabilitiesKHR& capabilities) {
  std::uint32_t requested =
      (capabilities.minImageCount < UINT32_MAX) ? capabilities.minImageCount + 1 : capabilities.minImageCount;
  if (capabilities.maxImageCount != 0 && requested > capabilities.maxImageCount) {
    requested = capabilities.maxImageCount;
  }
  return requested;
}

// Prefers VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; otherwise the first of
// Vulkan's other three legal composite-alpha bits, in their own
// declared-in-the-Vulkan-header order, that this surface's capabilities
// actually support. Never passes a bit capabilities.supportedCompositeAlpha
// does not report. Stays private -- no public API added.
[[nodiscard]] std::optional<VkCompositeAlphaFlagBitsKHR> selectCompositeAlpha(
    const VkSurfaceCapabilitiesKHR& capabilities) {
  constexpr VkCompositeAlphaFlagBitsKHR kCandidatesInOrder[] = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
  };
  for (VkCompositeAlphaFlagBitsKHR candidate : kCandidatesInOrder) {
    if ((capabilities.supportedCompositeAlpha & static_cast<VkFlags>(candidate)) != 0) {
      return candidate;
    }
  }
  return std::nullopt;
}

}  // namespace

VulkanPresentation::VulkanPresentation(VulkanDevice& device, VkSurfaceKHR surface)
    : device_(device), surface_(surface) {}

VulkanPresentation::~VulkanPresentation() {
  // Destruction order: swapchain (if any), then surface -- never the
  // Device, PhysicalDevice, Instance, Queue, or native window (those are
  // the caller's responsibility; ADR-0013). No acquired image can ever be
  // outstanding under this spec's contract (ADR-0016), so there is no
  // synchronization precondition (no device-idle wait) to satisfy first.
  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_.device(), swapchain_, nullptr);
  }
  vkDestroySurfaceKHR(device_.instance(), surface_, nullptr);
}

void VulkanPresentation::notifyResized(atlantis::rhi::Extent2D extent) {
  trackedExtent_ = extent;
  recreationNeeded_ = true;
}

atlantis::rhi::SwapchainMetadata VulkanPresentation::metadata() const {
  return metadata_;
}

atlantis::Result<std::monostate, atlantis::rhi::PresentationError> VulkanPresentation::recreateIfNeeded() {
  using ResultT = atlantis::Result<std::monostate, atlantis::rhi::PresentationError>;

  const RecreateAction action = decideRecreateAction(trackedExtent_, recreationNeeded_);
  if (action == RecreateAction::Skip) {
    // {0, 0} tracked extent: return immediately. No Vulkan swapchain
    // call is issued anywhere on this path -- the Vulkan-calling code
    // below is physically unreachable from this branch, not merely
    // undialed by convention. recreationNeeded_ is left exactly as it
    // was, so a later non-zero notifyResized() can still trigger
    // Recreate. Any existing swapchain (from a prior non-zero extent) is
    // left untouched (ADR-0016's explicit "not eagerly released" choice).
    return ResultT::Ok(std::monostate{});
  }
  if (action == RecreateAction::NoOp) {
    // Non-zero extent, no recreation flagged: the swapchain already
    // matches the tracked extent. No Vulkan call, metadata unchanged.
    return ResultT::Ok(std::monostate{});
  }

  // RecreateAction::Recreate: destroy the previous swapchain, if any,
  // before creating a new one (ADR-0016's approved ordering) -- no
  // alternate oldSwapchain-handoff lifetime is used.
  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_.device(), swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
  // From here until a new swapchain is fully constructed and its image
  // count queried, this object correctly has no valid swapchain -- any
  // failure below must leave exactly this state (swapchain_ ==
  // VK_NULL_HANDLE, metadata_ at its approved no-swapchain default,
  // recreationNeeded_ left set for retry), which resetting metadata_
  // here, before any of the fallible queries, already guarantees
  // structurally rather than needing a reset on every failure branch.
  metadata_ = atlantis::rhi::SwapchainMetadata{};

  VkSurfaceCapabilitiesKHR capabilities{};
  const VkResult capabilitiesResult =
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_.physicalDevice(), surface_, &capabilities);
  if (capabilitiesResult != VK_SUCCESS) {
    return ResultT::Err(toSwapchainCreationError(capabilitiesResult));
  }

  const FormatsResultT formatsResult = querySurfaceFormats(device_.physicalDevice(), surface_);
  if (formatsResult.isErr()) {
    // Propagates the specific error querySurfaceFormats() mapped the
    // failing VkResult to (SurfaceLost/DeviceLost/Unknown/...) --
    // never collapsed into a single generic failure.
    return ResultT::Err(formatsResult.error());
  }
  const std::optional<FormatSelection> formatSelection = selectSurfaceFormat(formatsResult.value());
  if (!formatSelection.has_value()) {
    // None of Atlantis's four RHI-approved (format, color space) pairs is
    // available on this surface -- a genuine, explicit failure, never
    // silently mapped to a format or color space outside the approved set.
    return ResultT::Err(atlantis::rhi::PresentationError::SwapchainCreationFailed);
  }

  const std::optional<VkCompositeAlphaFlagBitsKHR> compositeAlpha = selectCompositeAlpha(capabilities);
  if (!compositeAlpha.has_value()) {
    return ResultT::Err(atlantis::rhi::PresentationError::SwapchainCreationFailed);
  }

  if (!supportsRequiredSwapchainUsage(capabilities)) {
    return ResultT::Err(atlantis::rhi::PresentationError::SwapchainCreationFailed);
  }

  const VkExtent2D swapchainExtent = selectSwapchainExtent(capabilities, trackedExtent_);
  if (swapchainExtent.width == 0 || swapchainExtent.height == 0) {
    // The tracked extent itself was non-zero (the structural Skip branch
    // above already handles {0, 0}), but the surface's own capabilities --
    // queried after that check, so genuinely racy against a live window --
    // report an actual zero extent right now (e.g. the window was
    // minimized between the notifyResized() that set trackedExtent_ and
    // this query). ADR-0016's zero-extent rule ("no code path from a zero
    // extent to a Vulkan call, at any point") is honored for the extent
    // Vulkan would actually use, not only the caller-tracked one: this is
    // treated identically to the structural Skip branch -- deferred, not
    // failed. recreationNeeded_ stays set so the next recreateIfNeeded()
    // call retries; metadata_ is already at its reset default; swapchain_
    // is already VK_NULL_HANDLE (reset above). No vkCreateSwapchainKHR
    // call is made, and no 1x1 or other substitute swapchain is created.
    return ResultT::Ok(std::monostate{});
  }
  const std::uint32_t imageCount = selectImageCount(capabilities);

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = surface_;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = formatSelection->vkFormat;
  createInfo.imageColorSpace = formatSelection->colorSpace;
  createInfo.imageExtent = swapchainExtent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  // Single combined graphics/present queue family (Step 7/8) -- exclusive
  // sharing needs no queue family index list.
  createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  createInfo.queueFamilyIndexCount = 0;
  createInfo.pQueueFamilyIndices = nullptr;
  createInfo.preTransform = capabilities.currentTransform;
  createInfo.compositeAlpha = *compositeAlpha;
  // VK_PRESENT_MODE_FIFO_KHR is required to be supported by every
  // conformant Vulkan implementation for every surface, so no
  // vkGetPhysicalDeviceSurfacePresentModesKHR query is needed to confirm
  // it -- fixed, not configurable (per this round's explicit scope).
  createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  createInfo.clipped = VK_TRUE;
  // The previous swapchain was already destroyed above (ADR-0016's
  // approved ordering), so there is no live handle to hand off here.
  createInfo.oldSwapchain = VK_NULL_HANDLE;

  VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
  const VkResult createResult = vkCreateSwapchainKHR(device_.device(), &createInfo, nullptr, &newSwapchain);
  if (createResult != VK_SUCCESS) {
    return ResultT::Err(toSwapchainCreationError(createResult));
  }
  SwapchainGuard swapchainGuard(device_.device(), newSwapchain);

  // Queries the image count only -- never a second call for the image
  // handles themselves, never allocated into a std::vector<VkImage>,
  // never stored. This is not an acquire and creates no per-image
  // ownership; metadata() only ever exposes the count, format, and
  // extent below.
  std::uint32_t actualImageCount = 0;
  const VkResult imageCountResult =
      vkGetSwapchainImagesKHR(device_.device(), swapchainGuard.get(), &actualImageCount, nullptr);
  if (imageCountResult != VK_SUCCESS) {
    // swapchainGuard's destructor destroys the just-created swapchain on
    // this return; swapchain_ was never updated, so it is still
    // VK_NULL_HANDLE, and metadata_ is still its already-reset default.
    return ResultT::Err(toSwapchainCreationError(imageCountResult));
  }

  swapchain_ = swapchainGuard.release();
  metadata_.imageCount = actualImageCount;
  metadata_.format = formatSelection->approvedFormat;
  metadata_.extent = atlantis::rhi::Extent2D{swapchainExtent.width, swapchainExtent.height};
  recreationNeeded_ = false;

  return ResultT::Ok(std::monostate{});
}

}  // namespace atlantis::vulkan_backend::detail

namespace atlantis::vulkan_backend {

atlantis::Result<std::unique_ptr<atlantis::rhi::Presentation>, PresentationCreateError> createPresentation(
    atlantis::rhi::Device& device, atlantis::platform::NativeWindowHandle windowHandle) {
  using ResultT = atlantis::Result<std::unique_ptr<atlantis::rhi::Presentation>, PresentationCreateError>;

  // Per ADR-0014: a Device not produced by this module's own
  // createDevice() is a programmer error, not a supported input --
  // impossible in Phase 1 since no second backend exists.
  auto* vulkanDevice = dynamic_cast<detail::VulkanDevice*>(&device);
  ATLANTIS_CHECK_MSG(vulkanDevice != nullptr,
                      "createPresentation() received a Device not produced by this module's own createDevice()");

  const detail::Win32SurfaceCreateResult surfaceResult =
      detail::createWin32Surface(vulkanDevice->instance(), windowHandle);
  if (surfaceResult.result != VK_SUCCESS) {
    return ResultT::Err(PresentationCreateError::SurfaceCreationFailed);
  }
  // Phase 1 of a two-phase ownership transfer, matching vulkan_device.cpp's
  // own guard pattern: the guard owns the surface until VulkanPresentation
  // has been successfully constructed below.
  detail::SurfaceGuard surfaceGuard(vulkanDevice->instance(), surfaceResult.surface);

  // The concrete-surface presentation-support check (Section 5): the
  // Win32-generic check at Device construction only confirmed the queue
  // family *can generically* present to a Win32 window; this confirms
  // the *specific* surface just created is supported by that exact queue
  // family. This call can itself fail, independent of the boolean it
  // writes.
  VkBool32 supported = VK_FALSE;
  const VkResult supportResult = vkGetPhysicalDeviceSurfaceSupportKHR(
      vulkanDevice->physicalDevice(), vulkanDevice->queueFamilyIndex(), surfaceGuard.get(), &supported);
  if (supportResult != VK_SUCCESS) {
    return ResultT::Err(detail::toSurfaceCreationError(supportResult));
  }

  const std::optional<PresentationCreateError> supportError = detail::checkSurfaceSupported(supported == VK_TRUE);
  if (supportError.has_value()) {
    return ResultT::Err(*supportError);
  }

  // Phase 2: VulkanPresentation is constructed from the guard's
  // non-owning get() value, and only after std::make_unique returns
  // successfully does the guard release ownership -- never inside the
  // constructor's own argument list.
  std::unique_ptr<atlantis::rhi::Presentation> presentation =
      std::make_unique<detail::VulkanPresentation>(*vulkanDevice, surfaceGuard.get());

  (void)surfaceGuard.release();

  return ResultT::Ok(std::move(presentation));
}

}  // namespace atlantis::vulkan_backend
