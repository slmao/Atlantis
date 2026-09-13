#include "android_surface.h"

#include <atlantis/assert.h>
#include <atlantis/platform/platform_kind.h>

// This translation unit -- and no other file in this module -- is
// permitted to include <android/native_window.h> and
// <vulkan/vulkan_android.h>. Mirrors win32_surface.cpp's own pattern
// exactly (Plan 0034 Milestone 3).
#include <android/native_window.h>

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan_android.h>

namespace atlantis::vulkan_backend::detail {

AndroidSurfaceCreateResult createAndroidSurface(VkInstance instance,
                                                 atlantis::platform::NativeWindowHandle windowHandle) {
  ATLANTIS_CHECK_MSG(windowHandle.kind == atlantis::platform::PlatformKind::Android,
                      "createAndroidSurface() received a NativeWindowHandle produced by a non-Android platform");

  // ADR-0011: Android payload is value0 = ANativeWindow*, value1 unused.
  VkAndroidSurfaceCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  createInfo.window = reinterpret_cast<ANativeWindow*>(windowHandle.value0);

  AndroidSurfaceCreateResult result;
  result.result = vkCreateAndroidSurfaceKHR(instance, &createInfo, nullptr, &result.surface);
  return result;
}

}  // namespace atlantis::vulkan_backend::detail
