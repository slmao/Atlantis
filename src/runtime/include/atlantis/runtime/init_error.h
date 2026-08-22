#pragma once

namespace atlantis::runtime {

// See Plan 0013 Section D6. Covers exactly the six fixed initialization
// steps createRuntimeApplication()/RuntimeApplication::initializeSteps()
// run -- nothing else. Every value maps uniformly to
// RuntimeExitReason::InitializationFailed (Spec 0013's own Requirements
// fix exactly one initialization-failure exit-code category).
enum class RuntimeInitError {
  PlatformInitFailed,
  ShaderLoadFailed,
  DeviceCreateFailed,
  AssetLoadFailed,
  MeshCreateFailed,
  CameraBufferCreateFailed,
};

// For logging only -- not part of any Result/error contract.
[[nodiscard]] const char* toString(RuntimeInitError error) noexcept;

}  // namespace atlantis::runtime
