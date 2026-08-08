#include "win32_surface.h"

#include <atlantis/assert.h>
#include <atlantis/platform/platform_kind.h>

// This translation unit -- and no other file in this module -- is
// permitted to include <windows.h> and <vulkan/vulkan_win32.h>. Mirrors
// src/platform/src/windows/windows_platform.cpp's own WIN32_LEAN_AND_MEAN/
// UNICODE/_UNICODE + <windows.h> pattern, defined immediately before use
// so nothing above this point in the file depends on Windows macros.
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_win32.h>

namespace atlantis::vulkan_backend::detail {

bool win32PresentationSupported(VkPhysicalDevice physicalDevice, std::uint32_t queueFamilyIndex) {
  return vkGetPhysicalDeviceWin32PresentationSupportKHR(physicalDevice, queueFamilyIndex) == VK_TRUE;
}

Win32SurfaceCreateResult createWin32Surface(VkInstance instance,
                                             atlantis::platform::NativeWindowHandle windowHandle) {
  ATLANTIS_CHECK_MSG(windowHandle.kind == atlantis::platform::PlatformKind::Windows,
                      "createWin32Surface() received a NativeWindowHandle produced by a non-Windows platform");

  // ADR-0011: Windows payload is value0 = HWND, value1 = HINSTANCE.
  VkWin32SurfaceCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  createInfo.hwnd = reinterpret_cast<HWND>(windowHandle.value0);
  createInfo.hinstance = reinterpret_cast<HINSTANCE>(windowHandle.value1);

  Win32SurfaceCreateResult result;
  result.result = vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &result.surface);
  return result;
}

}  // namespace atlantis::vulkan_backend::detail
