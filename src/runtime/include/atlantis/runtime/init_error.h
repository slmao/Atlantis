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
  // Plan 0018 Section P9: widened in kind, not in enumerator count, to
  // cover material/texture dependency resolution/load too -- the real
  // call sites (SceneDependencyResolver::find() returning nullptr; a
  // load call failing) are already identical regardless of which asset
  // kind's AssetId triggered them, confirmed directly against
  // scene_manifest.h's own kind-agnostic resolver. A dedicated
  // Material-named pair would be a new enumerator for a failure mode
  // that already has one, which Spec 0018 D6's own discipline directs
  // against.
  SceneDependencyUnresolved,  // a referenced AssetId (mesh, material, or texture) has no resolver entry
  SceneDependencyLoadFailed,  // a resolved AssetId's own mesh, material, or texture load failed
  // Plan 0023 Milestone 5 (ADR-0066 item 6): PbrDirectLit-only --
  // cookMaterial() can never run this check (it never resolves its own
  // texture reference, ADR-0059 Decision 7); this is Runtime's own
  // existing Phase 1 scene-dependency-resolution point (ADR-0060
  // Decision 6), the first point in the pipeline with both a material's
  // own kind and its resolved texture's own real colorSpace. Never an
  // Asset System type -- Milestone 1's own scope stays untouched.
  PbrBaseColorTextureNotSrgb,
};

// For logging only -- not part of any Result/error contract.
[[nodiscard]] const char* toString(RuntimeInitError error) noexcept;

}  // namespace atlantis::runtime
