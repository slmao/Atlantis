# Spec: Android Platform and Vulkan Presentation

- **Status:** Approved
- **Author:** Drafted by Claude Sonnet 5 at explicit human direction; human
  authorship/ownership confirmation pending.
- **Created:** 2026-09-12
- **Related Plan(s):** [Plan 0034](../plans/0034-android-platform-vulkan-presentation.md) (Draft).
- **Approval:** slmao, 2026-09-12 (chat confirmation), consistent with this
  repository's earliest ADRs ([ADR-0005](../adr/0005-platform-module-multi-os-windowing.md),
  [ADR-0011](../adr/0011-native-window-handle-representation.md)–[ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md)),
  which record human approval by date without a reviewing PR. No PR exists
  yet for this Spec/ADR set; **this approval authorizes drafting a Plan, not
  Implementation** — per [AGENTS.md](../../AGENTS.md), Implementation begins
  only after a distinct joint Spec+Plan Human Review.
- **Related ADR(s):** Builds on `Accepted` [ADR-0001](../adr/0001-rhi-backend-independence.md),
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
  [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) (amended),
  [ADR-0010](../adr/0010-cmake-structure.md), [ADR-0011](../adr/0011-native-window-handle-representation.md),
  [ADR-0012](../adr/0012-application-lifecycle-and-event-model.md),
  [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md),
  [ADR-0043](../adr/0043-asset-system-module-boundary.md),
  [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md).
  This Spec identifies four new decisions — see Architectural Impact — drafted
  alongside it as **`Proposed`** ADRs: [ADR-0077](../adr/0077-android-native-entry-point-and-process-model.md),
  [ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md),
  [ADR-0079](../adr/0079-android-native-window-reference-management.md),
  [ADR-0080](../adr/0080-android-asset-delivery-and-composition-root-boundary.md).
  None is `Accepted` yet; all four must reach `Accepted` before this Spec can
  be marked `Approved`, per [AGENTS.md](../../AGENTS.md#the-workflow-stage-by-stage).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
State each requirement once; link ADR rationale and map verification to the
requirements rather than adding duplicate acceptance or review-decision lists.
Keep implementation code, diffs, review transcripts, and execution logs in PRs.

## Summary

This Spec implements **Android as Atlantis's second target platform**: a
concrete `Atlantis Platform` Android backend, a Vulkan `VK_KHR_android_surface`
WSI path in `Atlantis Vulkan Backend`, the NDK/Gradle build and minimal APK
packaging needed to install and run on a device or emulator, and an Android
composition-root entry point that reuses the existing, unmodified `Atlantis
Renderer` → `Atlantis RenderGraph` → `Atlantis RHI` → `Atlantis Vulkan
Backend` stack and an existing selectable sample scene (Spec 0032) to put the
same rendered output Windows already produces onto a real Android screen. It
is Candidate Order 1 in [specs/README.md](README.md) Section B and Milestone 6
in [the blueprint](../project-blueprint.md), both of which name this as the
next-priority direction once every Windows-side rendering milestone (0001–
0033) closed with no open Windows-blocking work.

## Motivation / Problem Statement

Atlantis's target platforms are **Windows and Android (primary), iOS
(future)** per [AGENTS.md](../../AGENTS.md). Every module built so far — Core,
RHI, Vulkan Backend, RenderGraph, Renderer, Shader System, Asset System,
World, Runtime — was deliberately designed platform-agnostically
([ADR-0001](../adr/0001-rhi-backend-independence.md),
[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md)) specifically so
a second OS could be added without forking any of that code. That promise has
never been exercised: `Atlantis Platform` has only ever shipped a Windows
implementation ([Spec 0002](0002-platform-foundation.md)), and every ADR that
anticipated Android — [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md),
[ADR-0012](../adr/0012-application-lifecycle-and-event-model.md),
[ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md) — explicitly
left Android's concrete mechanism (native entry point, `ANativeWindow`
reference discipline, build/packaging) as an open question for "a future
Android Platform implementation spec." This is that spec. Until it lands,
Atlantis's "multi-OS" architecture is unverified in practice, and the primary
constraint list's "Windows and Android (primary)" wording is aspirational only
on the Android half.

## Goals

- A working `Atlantis Platform` Android backend satisfying the exact
  `initialize()`/`processEvents()`/`shouldQuit()`/`shutdown()`/
  `currentPlatform()` interface [Spec 0002](0002-platform-foundation.md)
  already defined — no change to that interface's shape.
- A Vulkan Android WSI boundary inside `Atlantis Vulkan Backend`
  (`VK_KHR_android_surface`), mirroring the existing Win32 WSI boundary
  (`src/vulkan_backend/src/wsi/win32_surface.{h,cpp}`) file-for-file in
  responsibility, per [platform-vulkan-wsi-boundary.md](../architecture/platform-vulkan-wsi-boundary.md).
- Enough NDK/CMake/Gradle build integration to produce an installable,
  debuggable APK — not a production release pipeline.
- An Android composition-root entry point that reuses `atlantis_runtime_host`
  (Spec 0013) and an existing sample scene (Spec 0032) to draw the same
  rendered output already visible on Windows, on a real Android device or
  emulator, with Vulkan Validation Layers clean.
- Resolve the four concrete mechanisms [Spec 0002](0002-platform-foundation.md)
  and its ADRs explicitly deferred: native entry point, `ANativeWindow`
  acquire/release discipline, NDK build integration, and (newly identified by
  this Spec) cooked-asset delivery into a packaged APK.

## Non-Goals

- Touch, gesture, or any other input system — still out of scope, matching
  [Spec 0002](0002-platform-foundation.md)'s Non-Goals.
- Android process-death save/restore — still explicitly deferred, matching
  [ADR-0012](../adr/0012-application-lifecycle-and-event-model.md).
- Multi-window Android, split-screen, or foldable-specific handling.
- A production Play Store release pipeline: signing, obfuscation, app
  bundles (`.aab`), staged rollout, or store listing metadata. This Spec's
  packaging goal is "install and run on a device/emulator for verification,"
  nothing more.
- Multi-ABI universal APKs or 32-bit ABI support — one 64-bit ABI
  (`arm64-v8a`, see [ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md))
  is this Spec's only verified target.
- Any new rendering feature, material, or lighting behavior — this is a
  platform-parity milestone; the rendered output must match what the chosen
  sample scene already produces on Windows, not add to it.
- CI-enforced Android build/test gating — [docs/process/ci-strategy.md](../process/ci-strategy.md)
  already tracks the general GPU-in-CI gap; this Spec does not resolve it for
  either platform.
- iOS — architecture-only, unaffected, not designed further here.
- Any change to `Atlantis RHI`, `Atlantis RenderGraph`, or `Atlantis
  Renderer`'s existing public API. If implementation reveals one is actually
  needed, that is a stop-and-escalate finding, not something this Spec
  authorizes in advance.

## Requirements

### Functional

- **Android Platform module** (`src/platform/src/android/`, per
  [Spec 0002](0002-platform-foundation.md)'s already-declared, not-yet-built
  directory): implements the existing `atlantis::platform` interface;
  `currentPlatform()` returns `PlatformKind::Android`.
- **Native entry point and process model**: resolved by
  [ADR-0077](../adr/0077-android-native-entry-point-and-process-model.md) —
  proposes `android_native_app_glue`, mapping its `AInputEvent`-free app
  commands onto the *existing, closed* `PlatformEvent` set (no new variant;
  [ADR-0012](../adr/0012-application-lifecycle-and-event-model.md)'s four
  categories are unchanged):

  | `android_native_app_glue` command | `PlatformEvent` |
  |---|---|
  | `APP_CMD_INIT_WINDOW` | `SurfaceCreated { handle }` |
  | `APP_CMD_TERM_WINDOW` | `SurfaceDestroyed` |
  | `APP_CMD_WINDOW_RESIZED` / `APP_CMD_CONTENT_RECT_CHANGED` | `WindowResize` |
  | `APP_CMD_GAINED_FOCUS` | `FocusGained` |
  | `APP_CMD_LOST_FOCUS` | `FocusLost` |
  | `APP_CMD_PAUSE` | `ApplicationPause` |
  | `APP_CMD_RESUME` | `ApplicationResume` |
  | `APP_CMD_DESTROY` | `Quit` |

  `processEvents()` drains `android_native_app_glue`'s command/looper queue
  (`ALooper_pollAll`, non-blocking) each call, translating each pending
  command to its mapped `PlatformEvent`, consistent with the existing
  drained-per-call contract [Spec 0002](0002-platform-foundation.md) and
  [ADR-0012](../adr/0012-application-lifecycle-and-event-model.md) already
  specify.
- **`NativeWindowHandle` payload**: `value0 = ANativeWindow*`, `value1`
  unused — already documented (not yet implemented) in
  [native_window_handle.h](../../src/platform/include/atlantis/platform/native_window_handle.h);
  no header change.
- **`ANativeWindow` reference discipline**: resolved by
  [ADR-0079](../adr/0079-android-native-window-reference-management.md) —
  satisfies [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md)'s
  explicitly deferred "a future Android Platform implementation must define
  the concrete acquire/release mechanism" obligation.
- **Vulkan Android WSI**: a new `src/vulkan_backend/src/wsi/android_surface.{h,cpp}`
  pair, structurally mirroring `win32_surface.{h,cpp}` — includes
  `<vulkan/vulkan_android.h>` (and therefore `<android/native_window.h>`) only
  in this file; consumes a borrowed `NativeWindowHandle` with
  `kind == PlatformKind::Android`; calls `vkCreateAndroidSurfaceKHR`; never
  creates, destroys, resizes, or otherwise manages the underlying
  `ANativeWindow`, per [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md).
- **`VkInstance` extension list**: conditionally enables
  `VK_KHR_android_surface` on Android builds, alongside the existing
  conditional `VK_KHR_win32_surface` on Windows builds — per-platform
  conditional compilation stays localized to Vulkan Backend, per
  [AGENTS.md](../../AGENTS.md) Vulkan-specific rules.
- **Surface destroy/recreate**: an Android `SurfaceDestroyed` →
  `SurfaceCreated` pair triggers full `VkSwapchainKHR` + `VkSurfaceKHR`
  teardown and rebuild against the new handle, per
  [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md)'s
  "`SurfaceDestroyed` is not a resize" rule — reusing `Presentation`'s
  existing recreation logic unmodified, since that logic was already written
  against this exact contract.
- **Build and packaging**: resolved by
  [ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md) —
  NDK CMake toolchain wiring, `arm64-v8a`-only initial ABI target, a minimal
  Gradle/APK shell, Vulkan Validation Layers bundling strategy, and an
  Android API level floor.
- **Composition root and asset delivery**: resolved by
  [ADR-0080](../adr/0080-android-asset-delivery-and-composition-root-boundary.md) —
  a new, thin `atlantis_runtime_android` native-activity entry point that
  drives `atlantis_runtime_host`'s existing, platform-neutral
  `RuntimeApplication::shouldContinue()`/`runFrame()`/`shutdown()` loop (Spec
  0013; already confirmed free of any Win32-specific assumption by direct
  inspection of `src/runtime/main.cpp`), plus a resolution for cooked-asset
  delivery given `Atlantis Asset System`'s current loader reads ordinary
  filesystem paths (`std::ifstream`, confirmed in `src/asset_system/src/load.cpp`),
  which an APK-packaged asset is not.
- **Verification scene**: one existing Spec 0032 sample scene (recommend
  `integrated_showcase_demo`, the default) renders visibly on a real Android
  device or emulator.

### Non-functional

- **Performance:** no formal frame-rate budget for this milestone — matches
  every prior Windows milestone, none of which set one either. "Runs at an
  interactively responsive rate on the verification device" is the only bar.
- **Memory:** no specific Android memory budget is set by this Spec; flagged
  as a Risk (see below), not a requirement, since Android devices vary far
  more widely in available memory than the single Windows development
  machine this repository has verified against so far.
- **Portability (within Vulkan-only Phase 1):** one Android ABI
  (`arm64-v8a`) and one minimum API level (see
  [ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md)) are
  this Spec's only verified configuration — consistent with this
  repository's existing single-GPU-vendor-verification pattern (every prior
  GPU-touching milestone discloses the same kind of narrow hardware
  coverage).
- **Other:** no new third-party dependency beyond what the Android NDK and
  the already-required Vulkan SDK already provide (`android_native_app_glue`
  ships inside the NDK; Vulkan Validation Layers for Android ship inside the
  Vulkan SDK/NDK). Gradle is a new *build-tool* dependency (Android's
  standard build system) but not a new *engine* dependency — see
  [ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md).

## Proposed Design

```
android_native_app_glue (android_main)
  --commands (APP_CMD_*)-->
Android Platform (src/platform/src/android/)
  --PlatformEvent-->
atlantis_runtime_android (new, thin native-activity entry, .so)
  --drives-->
atlantis_runtime_host (Spec 0013, unmodified frame loop)
  --RenderTarget request-->
Atlantis RHI Presentation (unmodified public API)
  --NativeWindowHandle, kind=Android-->
Atlantis Vulkan Backend / Vulkan Android WSI (new: android_surface.{h,cpp})
  --vkCreateAndroidSurfaceKHR-->
VkSurfaceKHR --> VkSwapchainKHR --> RenderTarget
  --unmodified-->
Atlantis RenderGraph / Atlantis Renderer (unmodified)
```

Everything below `Atlantis RHI`'s public API and above the new Android
Platform/WSI boundary is byte-for-byte the same code Windows already
exercises — this Spec's actual surface area is narrow: one new Platform
backend, one new WSI file pair, one new thin composition-root entry point,
and the build/packaging/asset-delivery plumbing to get all three onto a
device. See the four `Proposed` ADRs for the specific mechanism chosen at
each point above; this section only shows how they compose.

## Architectural Impact

Yes — four new decisions, each drafted alongside this Spec as a `Proposed`
ADR (none `Accepted` yet):

1. **Android native entry point and process model** — `android_native_app_glue`
   vs. a hand-rolled JNI bridge, and the `APP_CMD_*` → `PlatformEvent` mapping
   table above. [ADR-0077](../adr/0077-android-native-entry-point-and-process-model.md).
2. **Android NDK build and packaging integration** — CMake/NDK toolchain
   wiring, ABI/API-level floor, Gradle shell, Vulkan Validation Layer
   packaging. A build-system change, itself "significant" per
   [AGENTS.md](../../AGENTS.md)'s "what counts as significant" list.
   [ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md).
3. **Android `ANativeWindow` reference management** — the concrete
   acquire/release mechanism [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md)
   explicitly deferred to this Spec.
   [ADR-0079](../adr/0079-android-native-window-reference-management.md).
4. **Android asset delivery and composition-root boundary** — how cooked
   assets reach a packaged APK without `Atlantis Asset System` gaining an
   Android dependency (preserving its Core-only boundary,
   [ADR-0043](../adr/0043-asset-system-module-boundary.md)), and the shape of
   the new `atlantis_runtime_android` entry point relative to
   [ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md)'s
   existing host/executable split.
   [ADR-0080](../adr/0080-android-asset-delivery-and-composition-root-boundary.md).

No change is proposed to any existing `Accepted` ADR's decision; ADR-0013's
Android items are *fulfilled* by ADR-0079, not altered.

## Alternatives Considered

- **A hand-rolled JNI bridge instead of `android_native_app_glue`.** See
  [ADR-0077](../adr/0077-android-native-entry-point-and-process-model.md)'s
  own Alternatives Considered.
- **Splitting this Spec into two, mirroring Windows' original Spec
  0002 → Spec 0003 sequencing** (a narrower "Android Platform Foundation"
  delivering only Platform + Vulkan WSI + a cleared-color swapchain,
  matching Milestone 1's original Windows scope, followed by a *second*,
  later spec that wires up the composition root and visible mesh output,
  matching Milestone 3/4/10's later Windows scope). **This is presented as
  an open scope question for the human reviewer, not decided by this
  draft.** The case for the narrower slice: it mirrors precedent exactly and
  produces a smaller, easier single Plan. The case for the single, larger
  slice this draft actually proposes: unlike when Windows started, `RHI`,
  `RenderGraph`, `Renderer`, `Shader System`, `Asset System`, and `World` are
  now all already built and already platform-agnostic — the *only* new work
  Android needs is Platform + WSI + build/packaging + a thin composition-root
  entry point, which is considerably narrower than what Windows' original
  four-milestone sequence had to build from scratch. If the reviewer prefers
  the narrower slice anyway (e.g. to de-risk the untested NDK/Gradle
  toolchain before committing to asset delivery and the composition root in
  the same Plan), this Spec should be split before approval — see Risks &
  Open Questions.
- **A generic cross-platform windowing library (GLFW/SDL) for Android
  instead of a native `android_native_app_glue`-based Platform
  implementation.** Already rejected repository-wide by
  [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md)'s
  Alternatives Considered; not reopened here.

## Testing & Verification Plan

- **Unit tests (GPU-independent, host machine, not the Android device):**
  the `APP_CMD_*` → `PlatformEvent` mapping table's own translation logic,
  isolated from `android_native_app_glue` itself where feasible (matching
  how [Spec 0002](0002-platform-foundation.md)'s Windows event-construction
  logic was unit-tested without a live window) — exact test shape is Plan-stage
  work.
- **On-device/emulator manual verification (human-run, not automated):** no
  equivalent of `atlantis_runtime`'s existing Win32 message-injection
  automation ([Spec 0013](0013-runtime-host-foundation.md)/[Spec 0014](0014-world-scene-foundation.md))
  exists for Android in this repository. Consistent with this repository's
  existing disclosed-limitation pattern for windowed visual checks (Milestone
  10/11 in [the blueprint](../project-blueprint.md)), verification here is a
  human installing the APK on a real device or a configured emulator and
  confirming: the selected scene renders visibly and matches the existing
  Windows golden by eye; rotation/resize behaves correctly; Activity
  pause/resume (switching away and back) does not crash; `adb logcat`-captured
  Vulkan Validation Layer output is clean throughout.
- **Image regression:** [Spec 0011](0011-image-regression-testing-foundation.md)'s
  harness is not run on Android hardware by this Spec — out of scope (see
  Out of Scope / Future Work). The existing Windows-side goldens remain the
  only pixel-exact regression baseline; Android's output is checked only by
  human visual comparison against them.
- **Build verification:** a clean Android build (the ABI/API-level
  combination [ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md)
  settles) succeeds and installs, alongside the existing, unaffected clean
  Windows Debug/Release builds — an Android build must never break the
  Windows build or vice versa.

## Risks & Open Questions

- **No CI, no Android device farm.** Verification depends entirely on a
  human with a physical device or emulator — the same kind of gap
  [docs/process/ci-strategy.md](../process/ci-strategy.md) already discloses
  for Windows GPU testing, now extended to a second platform with no
  mitigation proposed here.
- **Android GPU/driver fragmentation.** Real-device Vulkan support varies far
  more than the single Windows GPU vendor this repository has verified
  against so far; any verification claim from this Spec's eventual
  implementation is necessarily narrow (one device or one emulator
  configuration), matching this repository's existing single-GPU-vendor
  disclosure pattern (e.g. Spec 0010/0011's own disclosed limitations).
- **Vulkan Validation Layers availability on Android** is not yet confirmed
  in this repository — [ADR-0078](../adr/0078-android-ndk-build-and-packaging-integration.md)
  proposes a bundling strategy, but if the layers cannot be cleanly loaded on
  the actual verification device/emulator, this Spec's "Validation Layers
  clean" bar may need to be weakened *for this milestone specifically* — that
  would need to be called out explicitly in the implementing PR, never
  silently dropped, per [AGENTS.md](../../AGENTS.md)'s Definition of Done.
- **Spec scope/splitting**, per Alternatives Considered above — a genuine
  open decision for the human reviewer, not resolved here.
- **Memory budget on constrained Android devices** is unset (see
  Non-functional) — a real risk if the verification device is
  memory-constrained, not designed around here.
- **Whether `atlantis_runtime_host`'s frame loop is truly free of hidden
  Windows coupling** beyond what is already isolated behind Platform/Vulkan
  WSI. This Spec's own inspection of `src/runtime/main.cpp` found no
  Win32-specific code in the entry point itself, but a full audit of
  `RuntimeApplication`'s internals is Plan-stage work, not completed by this
  Draft — see [ADR-0080](../adr/0080-android-asset-delivery-and-composition-root-boundary.md).

## Out of Scope / Future Work

- A production Play Store release pipeline (signing, `.aab`, staged
  rollout).
- Touch/gesture input system (a future spec, per
  [Spec 0002](0002-platform-foundation.md)'s own Future Extensions, now
  doubly motivated once Android is real).
- Android process-death save/restore.
- Multi-ABI universal APKs, 32-bit ABI support.
- CI-enforced Android build/test gating.
- Running [Spec 0011](0011-image-regression-testing-foundation.md)'s image
  regression harness against Android hardware.
- iOS Platform (architecture-only; unaffected by this Spec).
- Any Editor/Tool Connection Protocol work that might eventually target
  Android as a runtime deployment surface (Candidate Order 2 in
  [specs/README.md](README.md) Section B — unrelated, not unblocked or
  blocked by this Spec).

This Spec, once implemented, unblocks: genuine multi-OS verification of every
existing rendering milestone (0001–0033) rather than Windows-only coverage,
and establishes the concrete pattern a future iOS Platform spec can follow by
direct analogy.
