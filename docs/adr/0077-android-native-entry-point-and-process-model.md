# ADR 0077: Android Native Entry Point and Process Model

- **Status:** Accepted
- **Date:** 2026-09-12
- **Deciders:** slmao
- **Acceptance:** Human approval confirmed 2026-09-12 (chat confirmation);
  no reviewing PR yet — see [Spec 0034](../specs/0034-android-platform-vulkan-presentation.md)'s
  own Approval note.
- **Related Spec:** [specs/0034-android-platform-vulkan-presentation.md](../specs/0034-android-platform-vulkan-presentation.md)

## Context

[Spec 0002](../specs/0002-platform-foundation.md) defined a shared
`initialize()`/`processEvents()`/`shouldQuit()`/`shutdown()` lifecycle
interface and a closed `PlatformEvent` set
([ADR-0012](0012-application-lifecycle-and-event-model.md)), explicitly
requiring that Android's asynchronous, framework-driven Activity lifecycle
reach the same shared Runtime frame/update path Windows uses — without
forcing Android into a synchronous Win32-shaped loop. Both documents left the
*exact native entry-point mechanism* as an open question: `ADR-0012`'s own
Open Questions pointed at "`android_native_app_glue` vs. custom JNI bridge —
deferred to Android Platform's own future implementation." [Spec 0034](../specs/0034-android-platform-vulkan-presentation.md)
is that future implementation; this ADR makes the mechanism decision it
identified.

## Decision

**Android Platform's native entry point is `android_native_app_glue`**, the
static helper library shipped inside the Android NDK (`$NDK/sources/android/native_app_glue/`).

- The application's native entry point is `android_main(struct android_app*
  app)`, supplied by linking against `android_native_app_glue` and declaring
  it via the standard `ANativeActivity_onCreate` hook (wired automatically by
  the glue library's own `main.c`, unmodified).
- `android_main` calls into the new `atlantis_runtime_android` composition
  root (see [ADR-0080](0080-android-asset-delivery-and-composition-root-boundary.md)),
  which owns an `android_app*` for the process's lifetime and drives it
  through `ALooper_pollAll` inside the same shared Runtime frame/update loop
  shape every platform ultimately reaches, per
  [ADR-0012](0012-application-lifecycle-and-event-model.md).
- `android_native_app_glue` posts lifecycle/window transitions as integer
  **app commands** (`APP_CMD_*`) through its own command pipe, drained by
  `android_app_poll_source`'s `process` callback wired to the `app->cmd`
  looper identifier. Android Platform's `processEvents()` calls
  `ALooper_pollAll(0, ...)` (non-blocking, matching the existing non-blocking
  drain contract) once per invocation, translating every pending `APP_CMD_*`
  into the **existing, closed** `PlatformEvent` set — no fifth category and
  no new variant is introduced; [ADR-0012](0012-application-lifecycle-and-event-model.md)'s
  four-category closed set stands unmodified:

  | `android_native_app_glue` command | `PlatformEvent` | Notes |
  |---|---|---|
  | `APP_CMD_INIT_WINDOW` | `SurfaceCreated { handle }` | `handle.value0 = app->window` (an `ANativeWindow*`); see [ADR-0079](0079-android-native-window-reference-management.md) for its lifetime treatment. |
  | `APP_CMD_TERM_WINDOW` | `SurfaceDestroyed` | Fired *before* the glue library invalidates `app->window` — Android Platform must translate and deliver this event in the same `processEvents()` call that observes the command, never deferred. |
  | `APP_CMD_WINDOW_RESIZED` / `APP_CMD_CONTENT_RECT_CHANGED` | `WindowResize { logical, framebuffer }` | Both extents are read from `ANativeWindow_getWidth`/`getHeight` at translation time; Android reports no separate logical-vs-framebuffer distinction in Phase 1, so both fields carry the same value — matching Windows' own Phase 1 simplification (`docs/plans/0002-platform-foundation.md` Section 7). |
  | `APP_CMD_GAINED_FOCUS` | `FocusGained` | |
  | `APP_CMD_LOST_FOCUS` | `FocusLost` | |
  | `APP_CMD_PAUSE` | `ApplicationPause` | The Android-shaped event `ADR-0012` reserved specifically for this case. |
  | `APP_CMD_RESUME` | `ApplicationResume` | |
  | `APP_CMD_DESTROY` | `Quit` | Maps `shouldQuit()` to `true`, mirroring `WindowCloseRequested`'s effect on Windows. |
  | `APP_CMD_INPUT_CHANGED`, `APP_CMD_CONFIG_CHANGED`, `APP_CMD_LOW_MEMORY`, `APP_CMD_SAVE_STATE`, `APP_CMD_START`, `APP_CMD_STOP` | *(none — observed and discarded)* | Out of scope: input, configuration change beyond resize, low-memory handling, and state save/restore are all explicitly Non-Goals of [Spec 0002](../specs/0002-platform-foundation.md) and [Spec 0034](../specs/0034-android-platform-vulkan-presentation.md). Discarding rather than asserting/erroring on them keeps `processEvents()` forward-compatible with commands this Spec doesn't yet act on. |

- **No custom JNI bridge is written.** All Java/Kotlin-side code needed to
  host a native activity is the standard, boilerplate `android.app.NativeActivity`
  (declared in `AndroidManifest.xml`, see
  [ADR-0078](0078-android-ndk-build-and-packaging-integration.md)) — Atlantis
  contributes zero Java/Kotlin source.
- This ADR does not change `atlantis::platform`'s public interface shape in
  any way — `initialize()`/`processEvents()`/`shouldQuit()`/`shutdown()`/
  `currentPlatform()` keep the exact signatures [Spec 0002](../specs/0002-platform-foundation.md)
  already fixed. Everything above is internal to Android Platform's
  implementation file(s) under `src/platform/src/android/`.

## Consequences

### Positive

- Reuses a battle-tested, NDK-shipped mechanism instead of hand-rolling JNI
  glue code that would duplicate what `android_native_app_glue` already
  solves correctly (window/lifecycle event delivery, looper integration,
  correct interaction with `ANativeActivity`'s own threading model).
- The `APP_CMD_*` → `PlatformEvent` mapping table is small, closed, and
  directly traceable to [ADR-0012](0012-application-lifecycle-and-event-model.md)'s
  existing four categories — no redesign of the event model this Spec
  inherits.
- Keeps Atlantis's Android surface area to pure C/C++ — no JNI marshaling
  code, no Java/Kotlin build toolchain knowledge required beyond the minimal
  Gradle shell [ADR-0078](0078-android-ndk-build-and-packaging-integration.md)
  defines.

### Negative / Trade-offs

- `android_native_app_glue` is itself a small, separately-versioned
  third-party source drop bundled with the NDK (not a system library) —
  Atlantis takes on tracking whichever NDK version it builds against,
  though this is materially smaller than owning an equivalent hand-rolled
  bridge.
- The glue library dictates its own threading model (a dedicated native
  thread separate from the Java main thread) — Android Platform's
  single-application-thread assumption ([ADR-0004](0004-phase1-threading-baseline.md))
  applies to *that* thread, which implementers must not confuse with the
  Java UI thread `NativeActivity` itself still runs.
- Commands not yet acted on (`APP_CMD_CONFIG_CHANGED`, `APP_CMD_SAVE_STATE`,
  etc.) are silently discarded rather than surfaced in any way — acceptable
  for this Spec's Non-Goals, but a future input or process-death-handling
  spec must revisit this table rather than assume it is exhaustive forever.

## Alternatives Considered

- **A hand-rolled JNI bridge** (a custom `Activity` subclass in Kotlin/Java,
  manually forwarding lifecycle callbacks and a `Surface` object across JNI
  to native code). Rejected for this Spec: strictly more implementation and
  maintenance surface than `android_native_app_glue` for no compensating
  benefit at the foundation stage — every lifecycle/window event this Spec
  needs is already exactly what the glue library delivers. Revisiting this
  choice later (e.g. if a future spec needs Java-side platform APIs the glue
  library doesn't expose, such as certain sensor or permission APIs) remains
  open and is not foreclosed by this decision — it would be a superseding
  ADR at that time, not a retrofit of this one.
- **GLFW/SDL for Android.** Already rejected repository-wide by
  [ADR-0005](0005-platform-module-multi-os-windowing.md)'s Alternatives
  Considered; not re-evaluated here.
- **Forcing Android into a synchronous, Windows-shaped `while` loop** by
  busy-polling `android_native_app_glue`'s command queue from `main()`
  instead of using its own `android_main` entry convention. Rejected: this
  is exactly the "fake Win32-style lifecycle" [ADR-0012](0012-application-lifecycle-and-event-model.md)
  already forbade: `android_native_app_glue`'s own `android_main` thread
  model is the idiomatic, framework-correct way to reach the shared frame
  loop, and fighting it would reintroduce the exact risk ADR-0012 flagged
  (missing or mishandling a pause/surface-destroyed transition).
