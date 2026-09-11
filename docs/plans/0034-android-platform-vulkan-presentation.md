# Plan: Android Platform and Vulkan Presentation

- **Spec:** [specs/0034-android-platform-vulkan-presentation.md](../specs/0034-android-platform-vulkan-presentation.md)
  (`Approved`, 2026-09-12, chat confirmation — no reviewing PR yet)
- **Status:** Draft
- **Author:** Drafted by Claude Sonnet 5 at explicit human direction.
- **Joint Human Review:** Pending; once approved: reviewer, date, PR naming
  this Plan and Spec 0034 together and explicitly authorizing
  Implementation. **Implementation must not begin before this record
  exists**, per [AGENTS.md](../../AGENTS.md#the-workflow-stage-by-stage).
- **Related ADR(s):** [ADR-0077](../adr/0077-android-native-entry-point-and-process-model.md)–[ADR-0080](../adr/0080-android-asset-delivery-and-composition-root-boundary.md),
  all `Accepted` 2026-09-12 (same chat-confirmation basis as the Spec).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0034 in full: an `Atlantis Platform` Android backend, a
Vulkan Android WSI boundary, the NDK/Gradle build and minimal APK packaging,
and a new `atlantis_runtime_android` composition-root entry point — reusing
`atlantis_runtime_host`, `Atlantis Renderer`/`RenderGraph`/`RHI`/`Vulkan
Backend`/`Shader System`/`Asset System`/`World` unmodified — so that an
existing Spec 0032 sample scene renders visibly on a real Android device or
emulator, with Vulkan Validation Layers clean.

## Milestones / Task Breakdown

Ordered so the riskiest, least-verified assumption (the NDK/CMake/Gradle
toolchain interplay — flagged as an open risk in Spec 0034 and
[ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md)) is
proven first, before any real Android-specific engine code is written
against it.

1. **NDK/CMake build-infrastructure smoke test.** Confirm, in isolation,
   that this repository's existing top-level `CMakeLists.txt` configures and
   builds successfully under the NDK's `android.toolchain.cmake` with
   `ANDROID_ABI=arm64-v8a`/`ANDROID_PLATFORM=android-29`
   ([ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md)),
   including that `find_package(Vulkan REQUIRED)` resolves correctly against
   the NDK's bundled Vulkan headers/loader stub. No new source file is
   written in this step — it only proves or disproves the build-tooling
   assumption Spec 0034's Risks section flagged as unverified. If this step
   fails, **stop and report** rather than working around it with a
   Windows-desktop-Vulkan-SDK substitution or similar — a real failure here
   is a finding for [ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md)
   to revisit, not something to route around silently.
2. **Android Platform module.** Implement `src/platform/src/android/android_platform.cpp`
   (plus any Android-private headers under that same directory, including
   the private `setAndroidApp(android_app*)` injection declaration — see
   [ADR-0077](../adr/0077-android-native-entry-point-and-process-model.md)'s
   amendment) against the existing `atlantis::platform` interface
   (`src/platform/include/atlantis/platform/platform.h`). **`android_main`
   itself is not implemented here** — per
   [ADR-0077](../adr/0077-android-native-entry-point-and-process-model.md)
   and [ADR-0080](../adr/0080-android-asset-delivery-and-composition-root-boundary.md),
   it lives in `atlantis_runtime_android` (Milestone/Step 5 below), which
   calls `setAndroidApp()` once, before `createRuntimeApplication()`. This
   step's own `processEvents()` implementation consumes that injected
   `android_app*`: it calls `ALooper_pollAll(0, ...)` (non-blocking) to drain
   `android_native_app_glue`'s command queue and translates each pending
   `APP_CMD_*` per the mapping table
   [ADR-0077](../adr/0077-android-native-entry-point-and-process-model.md)
   specifies. `NativeWindowHandle::value0` carries `app->window` verbatim, no
   acquire/release call, per
   [ADR-0079](../adr/0079-android-native-window-reference-management.md).
   Calling `processEvents()` (or anything else needing the injected pointer)
   before Step 5's `android_main` calls `setAndroidApp()` is a programmer
   error (assertion failure), per
   [ADR-0077](../adr/0077-android-native-entry-point-and-process-model.md)'s
   amendment — implement and test that failure mode here, not deferred to
   Step 5. Update `src/platform/CMakeLists.txt`'s existing `elseif(ANDROID)`
   stub (currently empty, citing this Spec by number) to add the new source
   file(s) and link `android_native_app_glue` (built as a static library
   from the NDK-supplied source, per the NDK's own recommended CMake
   pattern) plus Android's `log`/`android` system libraries.
3. **Vulkan Android WSI.** Add `src/vulkan_backend/src/wsi/android_surface.h`
   and `.cpp`, structurally mirroring `win32_surface.{h,cpp}` exactly
   (`createAndroidSurface(VkInstance, NativeWindowHandle)` returning the same
   `{VkResult, VkSurfaceKHR}` shape `Win32SurfaceCreateResult` uses), calling
   `vkCreateAndroidSurfaceKHR` and taking no manual `ANativeWindow`
   reference, per [ADR-0079](../adr/0079-android-native-window-reference-management.md).
   Add `kAndroidSurfaceExtension = "VK_KHR_android_surface"` alongside the
   existing `kSurfaceExtension`/`kWin32SurfaceExtension` constants in
   `src/vulkan_backend/src/vulkan_instance.cpp`. Confirmed by direct reading
   at Plan-drafting time: `kWin32SurfaceExtension` is currently used
   **unconditionally** — no `#ifdef _WIN32` guard exists anywhere in this
   file (lines 124 and 147, the availability check and the enabled-extensions
   list) — because the file has only ever been compiled on Windows. This
   step must introduce the first real platform branch in this file:
   `#if defined(_WIN32)` / `#elif defined(__ANDROID__)` around both the
   availability check and the enabled-extensions list, selecting
   `kWin32SurfaceExtension` or `kAndroidSurfaceExtension` accordingly — not
   simply appending the Android constant to the existing unconditional list,
   which would make Windows builds depend on an extension that does not
   exist there. Update `src/vulkan_backend/CMakeLists.txt`'s currently flat,
   unconditional source list to branch `if(WIN32) ... win32_surface.cpp
   elseif(ANDROID) ... android_surface.cpp endif()` around the `wsi/` entry
   — this is a real restructuring of that file, not an addition, since the
   Win32 file is not currently platform-gated at all.
4. **Gradle project and minimal installable APK.** Add a new top-level
   `android/` directory (`build.gradle`, `settings.gradle`, an app module's
   `build.gradle` with `externalNativeBuild` pointed at the existing
   top-level `CMakeLists.txt` with the ABI/API arguments from Step 1,
   `AndroidManifest.xml` declaring a single `android.app.NativeActivity`
   loading the (not-yet-existing until Milestone 5) `atlantis_runtime_android`
   library, a minimal app icon/label resource stub) per
   [ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md).
   At the end of this milestone the APK installs and launches into a blank
   (or crashing, if Milestone 5 hasn't landed) native activity — this
   milestone's own bar is "installable and launchable," not "renders
   anything."
5. **`atlantis_runtime_android` composition root and asset extraction.**
   New `src/runtime/android/` subdirectory: `android_main.cpp` implementing
   the asset-extraction-then-`createRuntimeApplication()`-then-frame-loop
   sequence [ADR-0080](../adr/0080-android-asset-delivery-and-composition-root-boundary.md)
   specifies. Before calling `createRuntimeApplication()`, `android_main`
   calls Android Platform's private `setAndroidApp(app)` injection function
   (Step 2 above, per
   [ADR-0077](../adr/0077-android-native-entry-point-and-process-model.md)'s
   amendment) — this is the only point in the sequence with direct access to
   the `android_app*` the NDK entry point received. Built as a new
   `atlantis_runtime_android` shared-library CMake
   target (gated `if(ANDROID)` inside `src/runtime/CMakeLists.txt`, alongside
   the existing, unconditional `atlantis_runtime` executable target — the
   two do not conflict, each gated to its own platform) linking
   `Atlantis::RuntimeHost` exactly as `atlantis_runtime` already does. Wire
   the Gradle project's asset-packaging step (cooked artifacts + compiled
   shaders copied into `android/app/src/main/assets/`) per
   [ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md).
   Select `integrated_showcase_demo` (Spec 0032's default whitelist entry) as
   the one scene this milestone targets — no scene-selection UI or
   command-line equivalent on Android.
6. **Validation Layer bundling and on-device verification.** Place the
   NDK/Vulkan-SDK-supplied `libVkLayer_khronos_validation.so` for
   `arm64-v8a` under `android/app/src/main/jniLibs/arm64-v8a/`; confirm
   `RuntimeApplication`'s existing `enableValidationLayers` path picks it up
   unmodified. Run the full Verification Checklist below on a real device or
   configured emulator. If layer loading does not work cleanly on the actual
   verification hardware, stop and report per Spec 0034's own Risk item —
   do not silently weaken the validation bar.

## Files / Modules Touched (expected)

- **New:** `src/platform/src/android/android_platform.cpp` (+ any
  Android-private headers alongside it).
- **Modified:** `src/platform/CMakeLists.txt` (fills the existing, empty
  `elseif(ANDROID)` stub).
- **New:** `src/vulkan_backend/src/wsi/android_surface.h`,
  `src/vulkan_backend/src/wsi/android_surface.cpp`.
- **Modified:** `src/vulkan_backend/src/vulkan_instance.cpp` (new
  `kAndroidSurfaceExtension` constant; the currently-unconditional
  `kWin32SurfaceExtension` use at lines 124 and 147 becomes
  `#if defined(_WIN32)` / `#elif defined(__ANDROID__)`-gated — the first
  platform branch this file has ever needed).
- **Modified:** `src/vulkan_backend/CMakeLists.txt` (the `wsi/` source entry
  becomes platform-conditional instead of flat).
- **New:** `src/runtime/android/android_main.cpp` (+ any supporting files)
  and its `atlantis_runtime_android` CMake target.
- **Modified:** `src/runtime/CMakeLists.txt` (adds the new, `if(ANDROID)`-gated
  target; `atlantis_runtime`'s existing target is unmodified).
- **New:** top-level `android/` directory — `build.gradle`,
  `settings.gradle`, `app/build.gradle`, `app/src/main/AndroidManifest.xml`,
  a minimal resource stub, `app/src/main/jniLibs/arm64-v8a/` (Validation
  Layer binary).
- **Not touched:** `Atlantis Core`, `Atlantis RHI`'s public API, `Atlantis
  RenderGraph`, `Atlantis Renderer`, `Atlantis Shader System`, `Atlantis
  Asset System`'s public API, `Atlantis World`, `atlantis_runtime_host`'s
  own internals, and every existing Windows target — a change to any of
  these during implementation is a deviation to call out explicitly, not a
  silent scope expansion.

## Sequencing & Dependencies

- Step 1 gates every later step — nothing Android-specific is worth writing
  against an unverified toolchain.
- Step 2 (Platform) and Step 3 (Vulkan WSI) can proceed in parallel once
  Step 1 passes — neither depends on the other's code, only on the shared
  build infrastructure Step 1 verified.
- Step 4 (Gradle/APK shell) depends on Step 2 existing enough to link (even
  if Step 5's library doesn't exist yet, Step 4's own milestone bar only
  needs *a* native activity library target to reference — sequencing note:
  if this proves awkward in practice, Steps 4 and 5 may be merged into one
  PR; call that out as a deviation if it happens, not a silent merge).
- Step 5 depends on Steps 2, 3, and 4 all landing.
- Step 6 depends on Step 5 producing a running, on-screen frame loop to
  actually verify against.
- No step depends on any change to Core/RHI/RenderGraph/Renderer/Shader
  System/Asset System/World — if one is discovered mid-implementation to be
  necessary, that is a stop-and-escalate finding (Spec 0034's own Risks
  section already flags this possibility for `atlantis_runtime_host`
  specifically).

## Verification Checklist

Mapped to Spec 0034's own Testing & Verification Plan:

- [ ] Unit tests (GPU-independent, host machine): `APP_CMD_*` →
      `PlatformEvent` mapping translation logic, isolated from
      `android_native_app_glue` where feasible.
- [ ] Clean Android build: Step 1's NDK/CMake configure+build succeeds with
      zero errors, `arm64-v8a`/API 29.
- [ ] Clean, unaffected Windows Debug/Release builds: confirm this Plan's
      Vulkan Backend/Platform/Runtime CMake changes do not break the
      existing Windows configuration — full existing `ctest` suite still
      passes on Windows.
- [ ] On-device/emulator manual verification (human-run): `integrated_showcase_demo`
      renders visibly, matching the existing Windows golden by eye; device
      rotation and window resize behave correctly; Activity pause/resume
      (switching away and back) does not crash.
- [ ] Vulkan Validation Layers: `adb logcat` captured and reviewed clean
      across the full manual verification session above.
- [ ] Image regression: explicitly **not** run against Android hardware
      (Spec 0034 Out of Scope) — the existing Windows-side goldens are
      unaffected and remain the pixel-exact baseline.
- [ ] Asset extraction: confirm the app-private-storage copy step correctly
      skips re-copying on a second launch (per
      [ADR-0080](../adr/0080-android-asset-delivery-and-composition-root-boundary.md)'s
      size-check skip condition) and correctly re-copies after an app
      reinstall/data-clear.
- [ ] `Atlantis Asset System`'s public API confirmed unmodified (a direct
      diff check, not just an assertion) — per
      [ADR-0080](../adr/0080-android-asset-delivery-and-composition-root-boundary.md)'s
      central boundary claim.

## Rollback Plan

Every change in this Plan is additive and platform-gated (`if(ANDROID)` /
new files under new directories) except the two genuine modifications to
existing shared files: `src/vulkan_backend/src/vulkan_instance.cpp` (new
constant plus conditional enablement) and
`src/vulkan_backend/CMakeLists.txt` (the `wsi/` source list becoming
conditional instead of flat). Both are small, mechanically reversible via a
plain `git revert` of the implementing commit(s) with no data migration, no
persistent state, and no effect on any already-shipped Windows binary.
Removing the new `android/` top-level directory and the new Android-gated
CMake targets/source files fully removes Android support with zero residual
effect on the Windows build.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas specific to this Plan:

- The general Definition of Done's CI-gating and automated-image-regression
  items do not apply to the Android target (no CI exists for either
  platform; Android image regression is explicitly Out of Scope per Spec
  0034) — Windows' existing CI-equivalent local/manual gates are unaffected
  and still apply in full to every change this Plan makes to shared
  (Vulkan Backend, Platform, Runtime CMake) files.
- "Vulkan Validation Layers clean" is verified via `adb logcat` review
  rather than the existing Windows verbose-test-output grep pattern — a
  different mechanism, the same bar, per Spec 0034's Testing & Verification
  Plan.
