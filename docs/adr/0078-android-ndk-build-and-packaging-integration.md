# ADR 0078: Android NDK Build and Packaging Integration

- **Status:** Accepted
- **Date:** 2026-09-12
- **Deciders:** slmao
- **Acceptance:** Human approval confirmed 2026-09-12 (chat confirmation);
  no reviewing PR yet — see [Spec 0034](../specs/0034-android-platform-vulkan-presentation.md)'s
  own Approval note.
- **Related Spec:** [specs/0034-android-platform-vulkan-presentation.md](../specs/0034-android-platform-vulkan-presentation.md)

## Context

[Spec 0001](../specs/0001-project-foundation.md) and
[Spec 0002](../specs/0002-platform-foundation.md) both explicitly excluded
"Android APK packaging and NDK build implementation" from their Non-Goals,
deferring it to whichever future spec actually builds Android. This
repository's existing build (`cmake -S . -B build` on Windows/MSVC, per
[CLAUDE.md](../../CLAUDE.md)) has no Android awareness at all today: no NDK
toolchain wiring, no `ANDROID_ABI`/`ANDROID_PLATFORM` handling, no APK
packaging of any kind. [Spec 0034](../specs/0034-android-platform-vulkan-presentation.md)
needs a concrete answer to actually produce an installable APK; this ADR is
that decision. Per [AGENTS.md](../../AGENTS.md)'s "what counts as
significant," any build-system or repository-structure change requires the
full Spec → Plan → ADR path — this is such a change.

## Decision

- **Target ABI: `arm64-v8a` only.** The dominant 64-bit ABI across current
  Android hardware and the Android NDK's own recommended default for new
  native projects. No `armeabi-v7a`, `x86`, or `x86_64` build is produced by
  this Spec (an emulator running an `x86_64` system image may still use
  `arm64-v8a` binaries via the emulator's own translation layer, or a
  matching `arm64-v8a` system image may be used directly — the choice of
  emulator image is a verification-time detail, not a second ABI target).
- **Minimum Android API level: API 29 (Android 10).** Chosen as a floor
  broadly compatible with Vulkan 1.0-capable devices (Android's own Vulkan
  support requirement floor is API 24, but API 29 has meaningfully broader
  driver-quality Vulkan support in practice and is a common baseline for
  Vulkan-targeting NDK samples) while remaining recent enough to avoid
  legacy `android_native_app_glue` quirks from very old API levels. This is
  a **floor for this Spec's own verification device/emulator choice**, not a
  claim about production device-coverage requirements — a future spec may
  lower it if a real compatibility need arises, with its own evidence.
- **CMake integration reuses this repository's existing top-level
  `CMakeLists.txt` directly**, via the NDK's own standard `android.toolchain.cmake`
  toolchain file, invoked the conventional way:
  `cmake -S . -B build-android -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 ...`.
  Atlantis does **not** invent its own toolchain-file mechanism or duplicate
  the NDK's — the existing `if(WIN32)` / `elseif(ANDROID)` source-selection
  pattern [Spec 0002](../specs/0002-platform-foundation.md)'s Build
  Integration section already anticipated (`src/platform/src/windows/` vs.
  `src/platform/src/android/`) is exactly what this toolchain-file-driven
  configure activates; no new CMake option beyond what `ANDROID` (a variable
  the NDK toolchain file itself defines) already provides is required in the
  top-level `CMakeLists.txt` for module source selection. `find_package(Vulkan
  REQUIRED)`'s existing call is compatible with an NDK build (the NDK ships
  its own Vulkan headers/loader stub) but its exact behavior under the
  Android toolchain file is a Plan-stage verification item, not asserted as
  already-working by this ADR.
- **Gradle is the outer packaging layer**, using Android Gradle Plugin's
  `externalNativeBuild` (CMake mode) pointed at this repository's existing
  top-level `CMakeLists.txt` with the arguments above — Gradle does not get
  its own, second, forked copy of the build graph; it drives the same CMake
  project every other configuration already uses. A new, minimal Gradle
  project (`android/` — a new top-level directory, sibling to `src/`,
  `tests/`, `examples/`) contains only: a `build.gradle`/`settings.gradle`
  pair wiring `externalNativeBuild`; an `AndroidManifest.xml` declaring a
  single `android.app.NativeActivity` (per
  [ADR-0077](0077-android-native-entry-point-and-process-model.md), zero
  Java/Kotlin source); and whatever resource stub (an app icon, a label)
  Android's manifest schema requires at minimum. This directory is new
  top-level structure, consistent in spirit with how
  [ADR-0010](0010-cmake-structure.md) introduced `examples/` as a new
  top-level directory for a comparable reason (packaging/composition
  concerns that don't belong inside `src/`).
- **Cooked assets and shaders are packaged into the APK's `assets/`
  directory** as a Gradle-time copy step sourced from this repository's
  existing CMake-cooked output directories (the same artifacts
  `atlantis_runtime`'s Windows build already produces via the existing asset
  cooker / shader compiler pipeline, unmodified) — see
  [ADR-0080](0080-android-asset-delivery-and-composition-root-boundary.md)
  for how the packaged assets are then read back at runtime.
- **Vulkan Validation Layers**: bundled as the prebuilt `.so` layer binaries
  the Android NDK/Vulkan SDK already ships (`libVkLayer_khronos_validation.so`
  for the target ABI), placed under the Gradle project's `jniLibs/arm64-v8a/`
  so the OS loader can find them, and enabled at Vulkan instance-creation
  time exactly the way `RuntimeApplication`'s existing `enableValidationLayers`
  flag ([bootstrap_config.h](../../src/runtime/include/atlantis/runtime/bootstrap_config.h))
  already does on Windows — no new enable mechanism, only a new binary
  placement/discovery step. If this bundling proves not to work cleanly on
  the actual verification device/emulator, [Spec 0034](../specs/0034-android-platform-vulkan-presentation.md)'s
  Risks section already requires that gap be disclosed explicitly in the
  implementing PR rather than silently weakening the "Validation Layers
  clean" bar.
- **No CI integration.** This ADR produces a build a human runs locally
  (`./gradlew installDebug` or equivalent); it does not add an Android job to
  any CI pipeline, consistent with [docs/process/ci-strategy.md](../process/ci-strategy.md)'s
  existing, still-open GPU-in-CI gap, now simply not extended to Android
  either.

## Consequences

### Positive

- Reuses the existing CMake project and module structure wholesale — no
  parallel Android-specific build graph to keep in sync with the Windows
  one.
- `ANDROID_ABI`/`ANDROID_PLATFORM` selection happens entirely at
  configure-invocation time (via the NDK's own toolchain file), so the
  top-level `CMakeLists.txt` needs no Android-specific branching beyond what
  the toolchain file already implies through the existing `if(WIN32)` /
  `elseif(ANDROID)` pattern.
- Validation Layer enablement reuses the exact existing
  `enableValidationLayers` code path — no second, Android-specific
  validation toggle to maintain.

### Negative / Trade-offs

- Introduces Gradle as a genuinely new build-tool dependency for anyone
  building the Android target — a real new tool to install and version,
  even though it is standard for Android and adds no *engine* dependency.
- Single-ABI (`arm64-v8a`), single-API-level-floor verification is
  deliberately narrow; this ADR does not attempt to characterize behavior
  across Android's actual device/OS-version fragmentation, matching this
  repository's existing single-GPU-vendor disclosure pattern but now also
  applied to OS-version and ABI coverage.
- `find_package(Vulkan REQUIRED)`'s exact behavior under the NDK toolchain
  file (whether it correctly resolves the NDK's bundled Vulkan headers
  rather than expecting a desktop Vulkan SDK install) is asserted as a
  Plan-stage verification item, not proven here — a real risk if it does not
  "just work."
- No CI coverage means every future PR touching a shared module (Core,
  RHI, RenderGraph, Renderer, Asset System, World) can silently break the
  Android build without anyone noticing until the next manual Android
  build/run — the same category of risk Windows already accepts without CI,
  now doubled across two platforms with no automated cross-check between
  them.

## Alternatives Considered

- **A fully separate Android-specific CMake project/build graph**, isolated
  from the shared top-level `CMakeLists.txt`. Rejected: would require
  duplicating every module's `add_subdirectory()`/dependency wiring in a
  second file, directly risking silent drift between the two build graphs —
  exactly the kind of fork this repository's platform-agnostic module design
  exists to avoid.
- **A raw `ndk-build`/`Android.mk`-based build instead of CMake.** Rejected:
  this repository is CMake-only per [AGENTS.md](../../AGENTS.md) Phase 1
  constraints ("Build: CMake"); introducing a second build system for one
  platform would directly contradict that.
- **Skipping Gradle entirely, invoking `cmake`+`adb install` by hand for a
  minimal APK.** Considered: technically possible (a hand-assembled APK from
  raw CMake output plus `aapt2`/`apksigner` invoked directly), but rejected
  as strictly more manual, undocumented tooling to maintain than the
  standard, well-supported Gradle `externalNativeBuild` path every Android
  NDK project uses — the cost of adopting Gradle is smaller than the cost of
  reinventing what it already does correctly.
- **A lower API-level floor (e.g. API 24, Android's Vulkan-support minimum)
  for maximum device coverage.** Rejected for this foundation milestone:
  broader device coverage is not a stated goal (see
  [Spec 0034](../specs/0034-android-platform-vulkan-presentation.md)
  Non-Goals — no production release pipeline), and a higher floor reduces
  the chance of hitting early-Vulkan-driver-era bugs unrelated to Atlantis's
  own code during initial bring-up.
