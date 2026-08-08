#pragma once

#include <memory>
#include <string>

#include <atlantis/platform/native_window_handle.h>
#include <atlantis/result.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/presentation.h>

namespace atlantis::vulkan_backend {

struct DeviceCreateParams {
  std::string applicationName = "Atlantis";

  // Requests validation layers be enabled even in a Release build. Cannot
  // be used to DISABLE validation layers in a Debug build -- Debug builds
  // always enable them regardless of this field's value, per AGENTS.md
  // ("Validation Layers are always enabled in debug builds"). The
  // effective, structurally-enforced value used internally is
  // detail::effectiveValidationLayersEnabled(detail::IsDebugBuild,
  // enableValidationLayers) -- never this field read directly.
  bool enableValidationLayers = false;
};

enum class DeviceCreateError {
  InstanceCreationFailed,
  ValidationLayerUnavailable,
  NoSuitablePhysicalDevice,
  DeviceCreationFailed,
};

[[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::Device>, DeviceCreateError> createDevice(
    const DeviceCreateParams& params);

enum class PresentationCreateError {
  SurfaceCreationFailed,
  UnsupportedDevice,
};

// The only function anywhere in RHI/Vulkan Backend's public surface that
// accepts a NativeWindowHandle, per ADR-0011/ADR-0014. device must outlive
// the returned Presentation; passing a Device not produced by this
// module's own createDevice() is a programmer error, not a supported
// input -- impossible in Phase 1 since no second backend exists.
[[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::Presentation>, PresentationCreateError>
createPresentation(atlantis::rhi::Device& device, atlantis::platform::NativeWindowHandle windowHandle);

}  // namespace atlantis::vulkan_backend
