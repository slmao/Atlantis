# ADR 0080: Android Asset Delivery and Composition-Root Boundary

- **Status:** Accepted
- **Date:** 2026-09-12
- **Deciders:** slmao
- **Acceptance:** Human approval confirmed 2026-09-12 (chat confirmation);
  no reviewing PR yet — see [Spec 0034](../specs/0034-android-platform-vulkan-presentation.md)'s
  own Approval note.
- **Related Spec:** [specs/0034-android-platform-vulkan-presentation.md](../specs/0034-android-platform-vulkan-presentation.md)

## Context

Two related gaps remain once [ADR-0077](0077-android-native-entry-point-and-process-model.md)–[ADR-0079](0079-android-native-window-reference-management.md)
settle Platform/WSI mechanics: how a running Android process actually reads
its cooked assets, and what owns driving `atlantis_runtime_host`'s frame loop
on Android. Direct inspection of this repository's current source settles the
facts each decision must be made against:

- `src/asset_system/src/load.cpp`'s `loadStaticMeshAsset()` (and its sibling
  loaders for texture/material/scene/environment assets) take plain
  `std::string` filesystem paths and read them via `std::ifstream` — ordinary
  POSIX/Win32 file I/O. An APK's packaged `assets/` entries are **not**
  addressable this way on Android; they live inside the APK's own ZIP
  container and require `AAssetManager` (obtained from the Activity/
  `android_app` context) to read, an entirely different I/O API with no
  `std::ifstream`-compatible path.
- `src/runtime/main.cpp` (Spec 0013) is a plain `int main(int argc, char**
  argv)` containing no Win32-specific code; it constructs a `BootstrapConfig`
  of asset/shader path strings, calls `createRuntimeApplication(config)`, and
  drives `RuntimeApplication::shouldContinue()`/`runFrame()`/`shutdown()` in a
  simple loop. This loop shape is already platform-neutral; nothing in it
  assumes a Win32 message pump. [ADR-0047](0047-runtime-host-executable-library-structure-and-test-boundary.md)
  already separates this reusable logic (`atlantis_runtime_host`, a static
  library) from its thin, platform-specific executable entry point
  (`atlantis_runtime`).

[Spec 0034](../specs/0034-android-platform-vulkan-presentation.md) identified
both gaps as needing resolution without weakening
[ADR-0043](0043-asset-system-module-boundary.md)'s "Asset System depends on
Core only" boundary, and without abandoning `atlantis_runtime_host`'s
existing, already-verified frame-loop logic.

## Decision

### Asset delivery: extract-to-private-storage, not `AAssetManager` reads

- Cooked assets and compiled shaders are packaged into the APK's `assets/`
  directory at Gradle build time (per
  [ADR-0078](0078-android-ndk-build-and-packaging-integration.md)), exactly
  the same artifact files the Windows build already produces — no change to
  the asset cooker or shader compiler pipeline.
- At process startup, **before** `createRuntimeApplication()` is called, a
  new, small Android-Platform-owned step (not inside `Atlantis Asset
  System`) copies every needed packaged asset out of `AAssetManager` (via
  `AAssetManager_open`/`AAsset_read`) into ordinary files under the
  process's app-private internal storage directory
  (`android_app->activity->internalDataPath`, a real, `std::ifstream`-
  addressable filesystem path Android guarantees is writable and private to
  the app). This copy is skipped on a subsequent run if the destination file
  already exists with the expected size (a cheap, sufficient staleness check
  for this Spec's scope — not a general cache-invalidation system).
- `BootstrapConfig`'s existing path fields (`assetArtifactPath`,
  `sceneArtifactPath`, shader paths, etc. — see `src/runtime/main.cpp`) are
  populated with these extracted, app-private filesystem paths on Android,
  exactly as they are populated with build-tree-relative CMake-injected paths
  on Windows today. **`Atlantis Asset System`'s public API — `loadStaticMeshAsset()`
  and every sibling loader — is not modified at all.** It keeps taking plain
  filesystem paths; Android Platform/the new Android composition root is
  solely responsible for making sure a real, readable path exists before
  calling in.
- This resolves the trade-off [Spec 0034](../specs/0034-android-platform-vulkan-presentation.md)'s
  Draft flagged as open: extraction requires **zero** `Atlantis Asset
  System` changes (confirmed by the `load.cpp` inspection above — its file
  I/O is already a thin, separable step ahead of in-memory decode logic that
  never itself touches a path), fully preserving
  [ADR-0043](0043-asset-system-module-boundary.md)'s Core-only dependency
  boundary, at the cost of a one-time on-device copy and doubled on-device
  storage (APK-packaged copy plus extracted copy) — judged acceptable for a
  foundation milestone with no production storage budget requirement (see
  [Spec 0034](../specs/0034-android-platform-vulkan-presentation.md)
  Non-Goals).

### Composition root: `atlantis_runtime_android`, a peer of `atlantis_runtime`

- A new, thin target, `atlantis_runtime_android` (a shared library, `.so`,
  per Android's `NativeActivity` loading requirement — `android.app.NativeActivity`
  loads a named native library by `android:value` in `AndroidManifest.xml`,
  which must be a `.so`, unlike `atlantis_runtime`'s Windows `.exe`), living
  at `src/runtime/android/` (a new subdirectory of the existing `src/runtime/`
  module, sibling to `main.cpp`, not a new top-level module).
- `atlantis_runtime_android` links `Atlantis::RuntimeHost` (`atlantis_runtime_host`,
  [ADR-0047](0047-runtime-host-executable-library-structure-and-test-boundary.md)'s
  existing static library) exactly as `atlantis_runtime` already does — no
  change to `atlantis_runtime_host`'s own public interface or internals is
  authorized by this ADR. Its `android_main(struct android_app* app)` entry
  point (per [ADR-0077](0077-android-native-entry-point-and-process-model.md))
  performs the asset-extraction step above, builds the same shape of
  `BootstrapConfig` `main.cpp` already builds (Android-specific path values
  only), calls Android Platform's private `setAndroidApp(app)` injection
  function (per [ADR-0077](0077-android-native-entry-point-and-process-model.md)'s
  amendment — the only point in this sequence with direct access to the
  `android_app*` `android_main` itself received), calls
  `createRuntimeApplication(config)`, and drives
  `shouldContinue()`/`runFrame()`/`shutdown()` inside the loop
  `ALooper_pollAll` and Android Platform's `processEvents()` require — the
  same three-call frame-loop shape `main.cpp` already uses, adapted to
  Android's non-blocking, event-driven pacing rather than a bare `while`
  loop with no yield point.
- `atlantis_runtime_host`'s existing GPU-independent lifecycle/error-
  classification tests ([ADR-0047](0047-runtime-host-executable-library-structure-and-test-boundary.md))
  remain Windows-host-run, unmodified, and continue to be the sole
  regression coverage for that shared logic — this ADR adds no new
  Android-specific unit-test obligation for `atlantis_runtime_host` itself,
  since none of its own code changes.
- If a Plan-stage implementation attempt discovers `atlantis_runtime_host`
  actually does contain a latent Windows-specific assumption this ADR did
  not find by inspection (e.g. in error formatting, timing, or a
  Windows-path-shaped default), that is a **stop-and-escalate finding**
  requiring its own follow-up decision — this ADR authorizes reuse on the
  evidence gathered, not a guarantee no such assumption exists.

## Consequences

### Positive

- `Atlantis Asset System`'s module boundary
  ([ADR-0043](0043-asset-system-module-boundary.md)) is completely
  unaffected — no Android awareness, no `AAssetManager` dependency, no new
  loader overload anywhere in `src/asset_system/`.
- `atlantis_runtime_host`'s existing, already-verified frame-loop and
  error-classification logic is reused verbatim, matching
  [ADR-0047](0047-runtime-host-executable-library-structure-and-test-boundary.md)'s
  stated purpose for splitting it out from `atlantis_runtime` in the first
  place — this is precisely the reuse case that split was designed to
  enable, now exercised for real by a second platform.
- The extraction step is simple, inspectable, ordinary file I/O — no new
  Android-specific serialization or streaming protocol to design or test.

### Negative / Trade-offs

- A one-time (or check-and-skip) on-device copy at startup adds latency and
  doubles on-device storage for packaged assets — acceptable for this
  Spec's scope (see Context/Decision) but a real cost a future,
  storage-conscious spec might need to revisit (e.g. by moving to
  `AAssetManager`-backed streaming reads, which would then require the
  in-memory-buffer `Atlantis Asset System` API extension this ADR
  deliberately avoids for now).
- `atlantis_runtime_android` duplicates `main.cpp`'s `BootstrapConfig`-
  population logic in an Android-specific form (different path values, same
  shape) rather than sharing a single cross-platform config-building
  function — a real, small duplication this ADR accepts rather than forcing
  a premature shared abstraction between two call sites whose only
  difference is path strings; a future spec may consolidate this if a third
  platform (iOS) makes the duplication's cost clearer.
- Introduces a new `.so`-shaped build target and its own `CMakeLists.txt`
  wiring under `src/runtime/`, a small increase in that module's build
  surface area, gated entirely behind `if(ANDROID)` and inert on Windows
  builds.

## Alternatives Considered

- **Read packaged assets directly via `AAssetManager`, adding an
  in-memory-buffer-accepting overload to `Atlantis Asset System`'s public
  loader API** (e.g. `loadStaticMeshAsset(std::span<const std::byte>
  artifactBytes, std::string_view metadataText)`, with the existing
  path-based overload becoming a thin wrapper calling it after doing its own
  `std::ifstream` read). Rejected for this Spec: while more "native" to
  Android and avoiding the extraction-storage cost above, it requires an
  actual public API change to `Atlantis Asset System` — however narrow and
  additive — for a foundation milestone that has no stated production
  storage-budget requirement to justify taking on that API-surface risk now.
  Explicitly left open as a candidate follow-up if a future spec's storage or
  streaming requirements make the extraction approach's cost untenable.
- **Fold `atlantis_runtime_android`'s logic directly into
  `atlantis_runtime_host`** rather than keeping it a separate, thin peer
  target. Rejected: would require `atlantis_runtime_host` itself to gain
  conditional Android-specific code (`android_app*` handling, asset
  extraction), diluting [ADR-0047](0047-runtime-host-executable-library-structure-and-test-boundary.md)'s
  stated purpose — `atlantis_runtime_host` exists to hold *shared,
  platform-neutral* composition logic; platform-specific entry-point glue
  belongs in the thin executable/library layer alongside `atlantis_runtime`,
  not inside the module it is a peer of.
- **A generic cross-platform virtual filesystem abstraction** inside
  `Atlantis Asset System` or a new module, unifying "read this logical path"
  across a real filesystem and an APK's packaged assets. Rejected as
  speculative for this Spec's scope — [AGENTS.md](../../AGENTS.md)'s "no
  speculative abstraction" principle applies directly: exactly one
  additional path-shape (Android's app-private storage, which is already a
  real filesystem path) is needed to satisfy this Spec, and a full VFS
  abstraction is unjustified complexity with a single consumer.
