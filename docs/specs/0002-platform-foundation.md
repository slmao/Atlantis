# Spec: Atlantis Platform Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code at explicit human direction; the original
  author metadata left human authorship/ownership confirmation pending.
- **Created:** 2026-08-02
- **Related Plan(s):** [Plan 0002](../plans/0002-platform-foundation.md)
  (`Approved`, Windows portion).
- **Related ADR(s):** Builds on [ADR-0001](../adr/0001-rhi-backend-independence.md),
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
  [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) (amended), and
  [ADR-0010](../adr/0010-cmake-structure.md). The three decisions in **ADRs
  Required Before Approval** were filed as
  [ADR-0011](../adr/0011-native-window-handle-representation.md),
  [ADR-0012](../adr/0012-application-lifecycle-and-event-model.md), and
  [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md); all
  `Accepted`. A fourth decision this Spec originally identified (the Platform/
  Vulkan WSI header-visibility boundary) was resolved by amending
  [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) — see
  Architecture / Design Constraints.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  [PR #147](https://github.com/slmao/Atlantis/pull/147) Batch 1. Original scope
  and obligations retained.

## Context

`docs/architecture/module_boundaries.md` names **Atlantis Platform** — the
per-OS windowing/surface/lifecycle abstraction, depended on only by Runtime,
forbidden from knowing anything about Vulkan — and flags that the concrete
shape of the opaque native-surface handle and of lifecycle event delivery is
not decided there.
[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) established that
Platform must generalize across Windows and Android (leaving room for iOS)
without forking Renderer/RHI per OS, but did not pin the concrete interface.
This Spec closes that gap: a minimal, implementable Platform boundary so a
future RHI/Vulkan Spec can consume it without inventing windowing concepts, and
a future Windows-rendering Spec has something concrete to build against. It is
the second real module after `Atlantis Core` (Spec 0001) and depends on Core
the way every future module will.

## Goals

- A minimal, stable Platform module boundary that lets a future RHI/Vulkan
  Backend obtain what it needs to create a Vulkan surface, without Platform
  knowing Vulkan exists.
- Define application lifecycle, window lifecycle, native window/surface access,
  window extent (logical vs. framebuffer), window events, event processing,
  monotonic timing, and platform identification — the smallest set the next
  Vulkan RHI and Windows-rendering Specs need.
- Make Windows and Android's structurally different lifecycle/ownership models
  (synchronous message pump vs. asynchronous framework-driven Activity/Surface
  lifecycle) fit one shared abstraction, without forcing Android into
  Windows-shaped semantics.
- Leave the door open for iOS (architecture-only; no iOS code).

## Scope

Defines the Platform layer for: application lifecycle and entry point; window
lifecycle; native window handle access; window size / framebuffer extent;
window events; event processing; application timing; platform identification;
and the integration boundary a future graphics-surface (Vulkan) backend will
consume — without designing that backend.

Covers Windows and Android concretely; iOS architecturally only (no
implementation, source, or build configuration).

## Non-Goals

- Vulkan, Vulkan surface creation, Vulkan swapchain.
- RHI, Renderer, RenderGraph, GPU resources, graphics synchronization, Shader
  System.
- An input system (keyboard, mouse, touch, controller, gestures) beyond the
  minimum lifecycle events (close, resize, focus, pause/resume, quit) — this is
  not a game-engine input system.
- Audio, filesystem abstraction, asset system.
- Android APK packaging and NDK build implementation — this Spec defines the
  interface Android's implementation must satisfy; it does not build it or add
  NDK toolchain configuration ([Spec 0001](0001-project-foundation.md)
  Non-Goals already excluded Android build support).
- iOS implementation (architecture-only).
- Linux support (not a target platform — [AGENTS.md](../../AGENTS.md) Phase 1).
- A complete profiler or frame-pacing system (Application Timing defines only
  monotonic elapsed time).
- A multi-threaded task system (Threading assumes a single application thread,
  per the Phase 1 baseline).

## Terminology

- **Platform** — the Atlantis Platform module: a common interface plus one
  concrete implementation per OS.
- **Platform lifecycle state** — Platform's internal notion of whether a native
  window/surface currently exists and is valid, and whether the application is
  paused/active. Exposed to Runtime only through `PlatformEvent` delivery and
  accessors (e.g. `shouldQuit()`) — not a stateful window object.
- **Native window handle** — the OS-specific value needed to eventually create
  a graphics-API surface: `HWND`+`HINSTANCE` on Windows, `ANativeWindow*` on
  Android, a `CAMetalLayer`-equivalent (future) on iOS.
- **NativeWindowHandle** — this Spec's platform-neutral, opaque-`void*` wrapper
  around a native window handle (see Architecture / Design Constraints).
  Distinct from the OS-typed handle itself.
- **Application / Runtime** — the composition root that owns a Platform instance
  and drives the application loop. This Spec defines what Runtime needs from
  Platform; it does not redefine Runtime.
- **Logical size** — a window's size in OS UI coordinates (may differ from
  pixels under DPI scaling).
- **Framebuffer extent** — the pixel dimensions a graphics backend renders
  into. Not assumed equal to logical size.

## Requirements

### Application Lifecycle & Entry Point

- Platform exposes a lifecycle interface Runtime drives: `initialize()`,
  `processEvents()` (non-blocking; drains and delivers this iteration's
  `PlatformEvent`s), `shouldQuit()`, `shutdown()`. The shared abstraction every
  platform reaches through these calls is the **Runtime frame/update path** —
  not an identical control-flow structure.
- **Windows:** Runtime owns a conventional loop —
  `while (!platform.shouldQuit()) { platform.processEvents(); ... }` — from a
  conventional `main`/`WinMain`-style entry point.
- **Android:** the framework drives the process. Android Platform adapts
  whatever native entry point / lifecycle callback mechanism Android requires
  (exact mechanism not decided here — see Open Questions) so it reaches the
  **same shared Runtime frame/update path**, via `processEvents()` draining
  whatever lifecycle/window/surface events the framework delivered since the
  last call. This Spec does not force Android's asynchronous, callback-driven
  lifecycle into a synchronous Win32 shape.
- **iOS (future, architecture-only):** the same shared path must remain
  reachable from a UIKit-driven entry point analogous to Android's model. Not
  designed further here.

### Window Lifecycle, Native Window Handle Access

- Platform manages exactly one native window/surface for Phase 1 (multi-window
  is a Future Extension).
- **Native-window validity** is tracked entirely through Platform lifecycle
  state and the `SurfaceCreated`/`SurfaceDestroyed` event pair — not a query
  method on a window object. A native window is valid from an observed
  `SurfaceCreated` until a matching `SurfaceDestroyed`; the interval between an
  Android `SurfaceDestroyed` and its later `SurfaceCreated` is the "no usable
  window" state.
- **The current `NativeWindowHandle`** is the payload of the most recently
  observed `SurfaceCreated` event — obtained by observing that event, not an
  accessor. Callers must not cache it independent of the event stream (see
  Ownership and Lifetime).
- **Current logical/framebuffer extent** is Platform state most recently
  reported via a `WindowResize` event — not separate accessor methods.

### Window Extent

- `WindowExtent` is a `{width, height}` pair (unsigned). A `WindowResize` event
  carries **both** `logical` and `framebuffer` extents as independent fields,
  **not assumed equal** — Android in particular may report them differently
  (e.g. density scaling), and this Spec does not assume Windows and Android
  agree on the relationship.
- A `WindowExtent` of `{0, 0}` is a valid, representable state (minimized, or
  Android surface momentarily unavailable) and must be distinguishable from any
  nonzero size — see Ownership and Lifetime.

### Events

Platform delivers a minimal, closed set through `processEvents()` — a tagged
`PlatformEvent`, not a full input system:

- `WindowResize { logical: WindowExtent, framebuffer: WindowExtent }`
- `WindowCloseRequested` (Windows: user closed the window; maps toward
  `shouldQuit()` becoming true)
- `FocusGained` / `FocusLost`
- `ApplicationPause` / `ApplicationResume` — primarily Android Activity
  pause/resume. **Windows does not use or synthesize these**; it represents
  minimize/restore and focus transitions through `WindowResize`, `FocusGained`,
  and `FocusLost`.
- `SurfaceCreated { handle: NativeWindowHandle }` / `SurfaceDestroyed` — the
  Android-critical pair (see Ownership and Lifetime); the Windows
  implementation may synthesize these once around window creation/destruction
  so callers need no OS-specific branching.
- `Quit`

Keyboard, mouse, touch, controller, and gesture events are explicitly not part
of this set (Non-Goals).

### Application Timing

Platform (or, per Open Questions, Core) exposes a monotonic clock: a function
returning elapsed time since an unspecified epoch, suitable for frame-delta
computation. No wall-clock time, profiling, or frame-pacing.

### Platform Identification

An enum (`PlatformKind { Windows, Android, IOS }`) and an accessor
(`currentPlatform()`) let code identify the running platform without testing OS
preprocessor macros directly. `#ifdef _WIN32` / `#ifdef __ANDROID__`
conditional compilation stays localized to Platform implementation files.

## Architecture / Design Constraints

**Platform must not become a graphics API abstraction.** No `VkInstance`,
`VkSurfaceKHR`, `VkSwapchainKHR`, `VkPhysicalDevice`, `VkDevice`, `VkQueue`, or
any other Vulkan concept appears anywhere in Platform. Platform exposes only the
native information a graphics backend needs. The future chain:

```
Platform --PlatformEvent--> Runtime --> Runtime/Vulkan integration
  --> private Vulkan WSI --> VkSurfaceKHR --> Presentation / generic RHI --> Renderer
```

Generic RHI's public API sits at the far end: it never consumes `PlatformEvent`
or `NativeWindowHandle` and never depends on Atlantis Platform. Only Runtime
consumes `PlatformEvent`; only a Runtime/Vulkan integration layer translates
lifecycle/surface/resize events into `Presentation` operations; only Vulkan
Backend's private WSI boundary ever interprets `NativeWindowHandle`.

**Native window handle representation** ([ADR-0011](../adr/0011-native-window-handle-representation.md)).
`NativeWindowHandle` is a small, tagged, copyable value type:

```
struct NativeWindowHandle {
  PlatformKind kind;
  // opaque platform-specific payload
};
```

The payload is stored as **opaque pointer-sized values (`void*`), not the
OS-typed handles** (`HWND`, `ANativeWindow*`), at the public-header boundary.
This is the smallest abstraction satisfying every requirement: Platform's
public header needs zero Win32/Android NDK headers; Platform's per-OS
implementation populates the opaque fields from the real typed handles; a
future Vulkan Backend's per-OS implementation reinterprets them back to call
the matching WSI extension (`vkCreateWin32SurfaceKHR`,
`vkCreateAndroidSurfaceKHR`); Renderer and generic RHI's public API never see
`NativeWindowHandle` at all. Runtime transports it, uninterpreted, to Vulkan
Backend's private WSI boundary, which alone interprets it to produce a
`VkSurfaceKHR`.

Two alternatives were rejected: a fully untagged opaque object (a future Vulkan
Backend could not know which WSI extension to call without an out-of-band
query); exposing the real typed OS handles directly (would need Win32/Android
headers "unnecessarily" and make `NativeWindowHandle`'s header
platform-conditional).

**Vulkan WSI header-visibility boundary (resolved via amended ADR-0005).**
Vulkan's WSI extension headers declare functions taking `HWND`/`ANativeWindow*`
by their real OS types, so a future Vulkan Backend's per-OS implementation
transitively needs `<windows.h>` / `<android/native_window.h>` visibility.
[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) was amended to
allow this through a **private WSI boundary inside Vulkan Backend** that may
consume `NativeWindowHandle` (borrowed, not owned) and include those OS headers
strictly to produce a `VkSurfaceKHR` — without those types reaching RHI's
public API, Renderer, or RenderGraph, and without Platform depending on Vulkan.
See [platform-vulkan-wsi-boundary.md](../architecture/platform-vulkan-wsi-boundary.md).

## Platform-specific Requirements

### Windows

Expected native concepts, **not implemented by this Spec**: `HWND`,
`HINSTANCE`. Both stay inside Windows Platform's implementation boundary — never
in public headers, Runtime, RHI, or Renderer.

### Android

**Not implemented by this Spec.** The interface must account for, and not
preclude:

- Android's Activity/application lifecycle (pause/resume, and process death —
  out of scope to fully solve, but the event model must not assume the process
  runs uninterrupted).
- `ANativeWindow` is **not permanently valid** — the framework can destroy and
  later recreate it independent of application/process lifetime, not only on
  final exit. Code must never assume a `NativeWindowHandle` obtained once
  remains valid (see Ownership and Lifetime).
- Native window availability is observable through Platform lifecycle state,
  event-driven (`SurfaceCreated`/`SurfaceDestroyed`), not polled.

### iOS (future, architecture-only)

**No iOS source or build configuration.** Documented architectural requirements
the abstraction must not prevent: a UIKit-driven lifecycle (analogous to
Android's model); native drawable/surface ownership living entirely inside a
future iOS Platform implementation; a `CAMetalLayer` (or equivalent)
representable through the same tagged `NativeWindowHandle` shape
(`PlatformKind::IOS` plus an opaque payload) without changing that type's
public shape.

## Ownership and Lifetime

| Question | Windows | Android |
|---|---|---|
| Who creates the native window | Windows Platform, on request during `initialize()` | The Android framework; Windows Platform observes `SurfaceCreated` |
| Who owns it | Windows Platform (owns the `HWND` for its lifetime) | The framework owns the underlying `Surface`; Android Platform owns Atlantis's *tracking* of it |
| Who destroys it | Windows Platform (`DestroyWindow`, on shutdown or user close) | The framework; Atlantis code **must never** destroy it — Android Platform only observes `SurfaceDestroyed` and invalidates its own state |
| How lifetime is represented | Platform lifecycle state + the `SurfaceCreated`/`SurfaceDestroyed` pair, not an assumed-always-valid handle | |
| How the Renderer accesses it | It doesn't, ever | |
| How a graphics backend accesses it | Runtime transports the `NativeWindowHandle`, uninterpreted, to Vulkan Backend's private WSI boundary, which alone interprets it to build a `VkSurfaceKHR`; generic RHI's public API never receives the handle | |
| On resize | Platform delivers `WindowResize` with both extents; Runtime tells a future RHI `Presentation` to recreate — Renderer is uninvolved ([ADR-0002](../adr/0002-presentation-rendertarget-unification.md)) | |
| When minimized | Framebuffer extent becomes `{0, 0}`. Runtime **must not** create/recreate a graphics surface/swapchain while extent is zero — it waits for the next nonzero `WindowResize` | |
| When Android recreates the native window | `SurfaceDestroyed` then, at an indeterminate later time, `SurfaceCreated` with a **new** handle. Runtime treats this as "the render target is entirely gone" — a future RHI fully tears down and rebuilds `Presentation`, not an in-place update | |

This Spec does not assume a native window has the same lifetime as the
rendering device: a future RHI `Device` may outlive Android surface
destroy/recreate cycles; only `Presentation` and the `RenderTarget`s it vends
are tied to native window lifetime. This is what
`docs/architecture/resource_lifetime.md`'s Android section already anticipated;
this Spec makes it concrete enough to implement against.

## Threading

- Application lifecycle, window operations, event processing, and native window
  access **all happen on a single application thread**, per
  [threading.md](../architecture/threading.md) and
  [ADR-0004](../adr/0004-phase1-threading-baseline.md). No multi-threaded task
  system is introduced.
- On Android, "the application thread" is whatever thread ultimately calls into
  the shared Runtime loop (e.g. a native-glue-driven `android_main`'s thread),
  not necessarily the Java UI thread. Exact mechanism is an Open Question.
- **Assumption future Vulkan/RHI work must preserve:** RHI `Device`/
  `Presentation` creation and acquire/present calls happen on this same single
  thread. `NativeWindowHandle` values and Platform lifecycle state/events must
  not be read from any other thread in Phase 1.

## Error Handling

- Platform initialization failure (e.g. window-class registration failure on
  Windows; a surface that never becomes available within a caller-chosen
  timeout on Android) is represented via `atlantis::Result<T, E>`, not
  exceptions — consistent with [ADR-0009](../adr/0009-assertion.md) and
  AGENTS.md's exception-free-core-modules rule.
- Programmer errors (e.g. querying Platform state before `initialize()` or
  after `shutdown()`) use `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`.

## Build Integration

**Not implemented by this Spec** — described for a future Plan:

- New module directory `src/platform/`, following
  [ADR-0010](../adr/0010-cmake-structure.md): `include/atlantis/platform/*.h`
  for the common interface (no OS-specific headers); `src/windows/` compiled
  only for Windows; `src/android/` compiled only for Android (its *existence*
  is defined here, but compiling it stays gated on a future Android
  build-support Spec); no `src/ios/`.
- CMake target `atlantis_platform` (static library), alias `Atlantis::Platform`,
  depending on `Atlantis::Core` only — the same `atlantis_<module>` /
  `Atlantis::<Module>` pattern Spec 0001 established.
- Per-OS source selection via CMake `if(WIN32)` / `elseif(ANDROID)`. No Android
  NDK toolchain configuration is added by this Spec.

## Acceptance Criteria

Original obligations remain unmarked; this editorial revision does not certify
historical execution.

- [ ] `src/platform`'s public headers contain no `Vk*` type and no
      `#include <vulkan/...>` — verifiable by inspection.
- [ ] No Renderer-level code (present or future) includes `<windows.h>`,
      `<android/native_window.h>`, or references `HWND`/`ANativeWindow`/
      `HINSTANCE`.
- [ ] Windows native window ownership (creator/owner/destroyer) is stated
      unambiguously in the Ownership and Lifetime table.
- [ ] Android `ANativeWindow` lifetime is stated unambiguously, including that
      Atlantis never destroys it and must treat recreation as a new, unrelated
      handle.
- [ ] `WindowExtent` and the `WindowResize` event can represent logical and
      framebuffer size independently, including `{0, 0}`.
- [ ] Windows' Runtime-owned-loop model and Android's framework-driven model
      are both satisfiable by the same `initialize`/`processEvents`/
      `shouldQuit`/`shutdown` interface, without forcing Android into a
      synchronous Win32-shaped loop.
- [ ] `NativeWindowHandle` can pass from Platform through Runtime to Vulkan
      Backend's private WSI boundary without any generic RHI or Renderer code
      including a Win32/Android SDK header.
- [ ] Platform-specific code (Win32/Android NDK types, `#ifdef`s) is isolated to
      `src/platform/src/windows/` and `src/platform/src/android/` — none in
      `include/atlantis/platform/*.h`.
- [ ] `PlatformKind` includes an `IOS` value and `NativeWindowHandle`'s tagged
      design has a documented (not implemented) iOS case.
- [ ] No `src/platform/src/linux/`, no `PlatformKind` Linux value, no
      Linux-specific build configuration anywhere this Spec touches.

## Verification Strategy

- **Unit tests** (no GPU / live window required): `WindowExtent` zero/nonzero
  and equality; `NativeWindowHandle` tag/payload construction and accessors;
  `PlatformEvent` construction/inspection; the monotonic clock wrapper's
  monotonicity property.
- **Manual verification:** window creation, resize, and minimize behavior on a
  real Windows machine (no Android device/emulator required — Android isn't
  built).
- **Not applicable:** Vulkan Validation Layers, image regression — no rendering.

## Dependencies

`Atlantis Core` only, plus each concrete implementation's own OS headers (Win32
for Windows; Android NDK for Android, not built yet). **No third-party
windowing library** (no GLFW/SDL) — consistent with
[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md)'s decision to use
native per-OS implementations, since GLFW/SDL fit Android's Activity-lifecycle
model poorly. No new dependency is added.

## Risks

- Android's exact lifecycle-glue mechanism (native glue library vs. hand-rolled
  JNI bridge) is undecided; this Spec defines the *event contract*, not that
  mechanism, so the risk is contained to the future Android implementation.
- The Win32-header-visibility tension is a real risk if left unresolved before
  RHI implementation begins — could force rework of the RHI's Vulkan Backend.
- The "don't create a swapchain at zero extent" rule can only be mandated here
  as an event contract; compliance depends on a future RHI/Presentation
  implementation honoring it.

## Open Questions

- Exact `PlatformEvent` C++ representation (tagged union / `std::variant` vs. a
  polymorphic base) — left to the Plan; a value-type, tagged-union style
  consistent with `atlantis::Result` is assumed but not locked.
- Android's native-entry-point mechanism (`android_native_app_glue` vs. custom
  JNI bridge) — deferred to Android Platform's own future implementation.
- Whether Application Timing belongs in Platform or `Atlantis Core` — a
  monotonic clock is portable standard-library functionality with no
  OS-specific implementation; specified under Platform because the driving task
  scoped it there, but it could live in Core without any interface change and
  may be relocated at Plan time.
- How process-death-and-restart on Android should be represented, if at all, in
  Phase 1 — may be legitimately out of scope until an Android build-support
  Spec exists.

## Future Extensions

- iOS Platform implementation (UIKit lifecycle, `CAMetalLayer`).
- Multi-window support.
- An input system layered on top of the event model this Spec defines.
- Multi-threaded event processing or Platform access, if a future Spec revisits
  the Phase 1 single-thread baseline.

## ADRs Required Before Approval

None was decided by this Spec — each names a decision that materially affects
architecture and had to be `Accepted` before this Spec could pass `In Review`.
All are `Accepted`.

| Decision | ADR |
|---|---|
| Native window handle representation — the tagged, opaque-`void*`-payload `NativeWindowHandle` design in Architecture / Design Constraints | [ADR-0011](../adr/0011-native-window-handle-representation.md) |
| Application lifecycle / event-model abstraction — the shared `initialize`/`processEvents`/`shouldQuit`/`shutdown` interface plus the closed `PlatformEvent` set, and how Android's framework-driven entry point adapts into it without faking a Win32-style loop | [ADR-0012](../adr/0012-application-lifecycle-and-event-model.md) |
| Platform ownership model for native windows/surfaces — the per-OS creator/owner/destroyer split, the "native window lifetime ≠ device lifetime" principle, the zero-extent-means-don't-render rule, and the treat-Android-recreation-as-full-teardown rule | [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md) |

A fourth decision — the boundary between "only Platform includes Win32/Android
NDK headers" and Vulkan Backend's necessary use of Vulkan's own WSI headers —
was resolved by amending
[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) in place (a
private WSI boundary inside Vulkan Backend consuming `NativeWindowHandle`); no
new ADR is required. See
[platform-vulkan-wsi-boundary.md](../architecture/platform-vulkan-wsi-boundary.md).
