#pragma once

namespace atlantis::runtime {

// See Plan 0013 Section D6, extended by Plan 0014 Section D8 with two
// additional steps (the AssetId metadata re-read, and the fixed
// validation scene's own construction). Covers exactly the fixed
// initialization steps createRuntimeApplication()/
// RuntimeApplication::initializeSteps() run -- nothing else. Every value
// maps uniformly to RuntimeExitReason::InitializationFailed (Spec 0013's
// own Requirements fix exactly one initialization-failure exit-code
// category).
enum class RuntimeInitError {
  PlatformInitFailed,
  ShaderLoadFailed,
  DeviceCreateFailed,
  AssetLoadFailed,
  MeshCreateFailed,
  CameraBufferCreateFailed,
  AssetMetadataParseFailed,
  SceneConstructionFailed,
  // Plan 0015 Section D2/D10: each wraps the underlying
  // SceneManifestError/SceneArtifactDecodeError/etc. only for a logged
  // diagnostic string -- the enumerator itself is Runtime's own
  // classification, per ADR-0054's own explicit "never
  // WorldError/SceneCookError/SceneArtifactDecodeError/AssetLoadError
  // directly" requirement.
  SceneManifestLoadFailed,    // manifest missing, malformed, or fails its own validation (D8)
  SceneArtifactLoadFailed,    // decodeScene() returned Err
  SceneDependencyUnresolved,  // a referenced AssetId has no resolver entry
  SceneDependencyLoadFailed,  // a resolved AssetId's own mesh load failed
};

// For logging only -- not part of any Result/error contract.
[[nodiscard]] const char* toString(RuntimeInitError error) noexcept;

}  // namespace atlantis::runtime
