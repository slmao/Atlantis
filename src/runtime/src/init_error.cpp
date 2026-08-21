#include <atlantis/runtime/init_error.h>

#include <atlantis/assert.h>

namespace atlantis::runtime {

const char* toString(RuntimeInitError error) noexcept {
  switch (error) {  // no default -- see Plan 0013 Section D3 (2026-08-21 amendment)
    case RuntimeInitError::PlatformInitFailed:
      return "PlatformInitFailed";
    case RuntimeInitError::ShaderLoadFailed:
      return "ShaderLoadFailed";
    case RuntimeInitError::DeviceCreateFailed:
      return "DeviceCreateFailed";
    case RuntimeInitError::AssetLoadFailed:
      return "AssetLoadFailed";
    case RuntimeInitError::MeshCreateFailed:
      return "MeshCreateFailed";
    case RuntimeInitError::CameraBufferCreateFailed:
      return "CameraBufferCreateFailed";
  }
  // Reached only if a future RuntimeInitError value is added without a
  // corresponding case above -- see exit_reason.cpp's identical comment
  // for why this fallback is required and why it must never become a
  // `default:` case.
  ATLANTIS_CHECK_MSG(false, "toString(RuntimeInitError): unhandled enumerator");
  return "(unrecognized RuntimeInitError)";
}

}  // namespace atlantis::runtime
