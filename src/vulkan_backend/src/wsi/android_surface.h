#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <atlantis/platform/native_window_handle.h>

// Android private WSI boundary (ADR-0005 amended, ADR-0011, ADR-0079,
// docs/architecture/platform-vulkan-wsi-boundary.md). android_surface.cpp
// is the only implementation file in this module permitted to include
// <android/native_window.h> or <vulkan/vulkan_android.h> -- this header
// itself stays limited to Vulkan core types (vulkan_core.h, not
// vulkan_android.h) and Platform's already-opaque NativeWindowHandle,
// so nothing here forces an OS header on any consumer. Structurally
// mirrors win32_surface.h exactly (Plan 0034 Milestone 3).
namespace atlantis::vulkan_backend::detail {

struct AndroidSurfaceCreateResult {
  VkResult result = VK_ERROR_INITIALIZATION_FAILED;
  VkSurfaceKHR surface = VK_NULL_HANDLE;  // valid, non-null only when result == VK_SUCCESS
};

// Creates a VkSurfaceKHR from a borrowed, non-owning NativeWindowHandle
// (ADR-0011). windowHandle.kind must be PlatformKind::Android -- any
// other kind reaching this function is a programmer error
// (ATLANTIS_CHECK). Never acquires, releases, destroys, resizes, or
// otherwise manages the underlying ANativeWindow (ADR-0013, ADR-0079) --
// vkCreateAndroidSurfaceKHR itself internally acquires and later
// releases its own reference (per the Vulkan specification's
// VK_KHR_android_surface extension), so this function takes no
// additional reference of its own. The returned VkSurfaceKHR is owned by
// the caller from that point on (Vulkan Backend, not Platform, per
// ADR-0005 amended).
[[nodiscard]] AndroidSurfaceCreateResult createAndroidSurface(VkInstance instance,
                                                                atlantis::platform::NativeWindowHandle windowHandle);

}  // namespace atlantis::vulkan_backend::detail
