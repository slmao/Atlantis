#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <atlantis/platform/native_window_handle.h>

// Windows private WSI boundary (ADR-0005 amended, ADR-0011,
// docs/architecture/platform-vulkan-wsi-boundary.md). win32_surface.cpp is
// the only implementation file in this module permitted to include
// <windows.h> or <vulkan/vulkan_win32.h> -- this header itself stays
// limited to Vulkan core types (vulkan_core.h, not vulkan_win32.h) and
// Platform's already-opaque NativeWindowHandle, so nothing here forces an
// OS header on any consumer.
namespace atlantis::vulkan_backend::detail {

// Wraps vkGetPhysicalDeviceWin32PresentationSupportKHR -- confirms, for
// one queue family, that it *can generically* present to a Win32 window.
// Needs no VkSurfaceKHR, and cannot need one: called during physical-
// device/queue-family selection, before any Presentation or surface
// exists (Plan Section 7 item 2). This is a necessary but not sufficient
// check -- the concrete-surface presentation-support check
// (vkGetPhysicalDeviceSurfaceSupportKHR, against an actual VkSurfaceKHR)
// is a separate, later check a future createPresentation() (Step 9)
// performs, not this function.
[[nodiscard]] bool win32PresentationSupported(VkPhysicalDevice physicalDevice, std::uint32_t queueFamilyIndex);

struct Win32SurfaceCreateResult {
  VkResult result = VK_ERROR_INITIALIZATION_FAILED;
  VkSurfaceKHR surface = VK_NULL_HANDLE;  // valid, non-null only when result == VK_SUCCESS
};

// Creates a VkSurfaceKHR from a borrowed, non-owning NativeWindowHandle
// (ADR-0011). windowHandle.kind must be PlatformKind::Windows -- any
// other kind reaching this function is a programmer error
// (ATLANTIS_CHECK), since only Windows is implemented in Phase 1. Never
// destroys, resizes, or otherwise manages the underlying HWND (ADR-0013)
// -- reads it only long enough to build a VkWin32SurfaceCreateInfoKHR and
// call vkCreateWin32SurfaceKHR. The returned VkSurfaceKHR is owned by the
// caller from that point on (Vulkan Backend, not Platform, per ADR-0005
// amended). Not called anywhere in this round -- compiled and ready for
// Step 9's createPresentation(); this round performs no surface creation
// at runtime.
[[nodiscard]] Win32SurfaceCreateResult createWin32Surface(VkInstance instance,
                                                           atlantis::platform::NativeWindowHandle windowHandle);

}  // namespace atlantis::vulkan_backend::detail
