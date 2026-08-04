# Spec: Atlantis Platform Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human direction;
  human authorship/ownership pending confirmation at Human Review.
- **Created:** 2026-08-02
- **Related Plan(s):** [plans/0002-platform-foundation.md](../plans/0002-platform-foundation.md)
  (`Approved / Ready for Implementation`, Windows portion).
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md),
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md), and
  [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) (all
  `Accepted`), and on [ADR-0010](../adr/0010-cmake-structure.md)
  (`Accepted`). See **ADRs Required Before Approval** below — the three
  decisions identified there were filed as
  [ADR-0011](../adr/0011-native-window-handle-representation.md),
  [ADR-0012](../adr/0012-application-lifecycle-and-event-model.md), and
  [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md), all
  `Accepted`. A fourth decision this spec originally identified (the
  Platform/Vulkan WSI header-visibility boundary) was resolved instead by
  amending [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md)
  — see Architecture / Design Constraints, below.

## Context

`docs/architecture/module_boundaries.md` already names **Atlantis
Platform** as a module — per-OS windowing/surface/lifecycle abstraction,
depended on only by Runtime, forbidden from knowing anything about
Vulkan — and explicitly flags that "the exact shape of the opaque
native-surface-handle type, and exact shape of lifecycle event delivery
... are not decided by this document." [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md)
established that Platform must generalize across Windows and Android (and
leave room for future iOS) without forking Renderer/RHI per OS, but it
also did not pin the concrete interface. This spec exists to close that
gap: define the actual, minimal, implementable Platform boundary, so a
future RHI/Vulkan spec can consume it without inventing windowing
concepts of its own, and a future Windows-rendering spec has something
concrete to build against.

`specs/0001-project-foundation.md` (Approved, implemented) established
`Atlantis Core` — logging, assertions, a `Result<T,E>` type — and the
project's first real CMake target structure
([ADR-0010](../adr/0010-cmake-structure.md)). This spec is the second
real module and is expected to depend on Core the same way every future
module will.

## Goals

- Define a minimal, stable Platform module boundary that lets a future
  RHI/Vulkan Backend obtain what it needs to create a Vulkan surface,
  without the Platform module knowing Vulkan exists.
- Define application lifecycle, window lifecycle, native window/surface
  access, window extent (logical vs. framebuffer), window events, event
  processing, monotonic timing, and platform identification — the
  smallest set of concepts the next Vulkan RHI and Windows-rendering
  specs actually need.
- Make Windows and Android's structurally different lifecycle/ownership
  models (synchronous message pump vs. asynchronous, framework-driven
  Activity/Surface lifecycle) fit through one shared abstraction, without
  forcing Android into Windows-shaped semantics that would misrepresent
  its actual window lifetime.
- Leave the door open for iOS (architecture-only; no iOS code).

## Scope

Defines the Platform layer for:

1. Application lifecycle
2. Application entry point
3. Window lifecycle
4. Native window handle access
5. Window size / framebuffer extent
6. Window events
7. Event processing
8. Application timing
9. Platform identification
10. The integration boundary a future graphics-surface (Vulkan) backend
    will consume — without designing that backend

Covers Windows and Android concretely; iOS architecturally only (no
implementation, no source files, no build configuration).

## Non-Goals

Explicitly out of scope for this spec:

- Vulkan, Vulkan surface creation, Vulkan swapchain
- RHI, Renderer, RenderGraph
- GPU resources, graphics synchronization
- Shader system
- Input system (keyboard, mouse, touch, controller, gestures) beyond the
  minimum events strictly required for lifecycle (window close, resize,
  focus, pause/resume, quit) — this is not a game-engine input system
- Audio
- Filesystem abstraction
- Asset system
- Android APK packaging
- Android NDK build implementation (this spec defines the interface
  Android's implementation must satisfy; it does not implement or build
  it, and does not add NDK toolchain configuration — see
  [specs/0001-project-foundation.md](0001-project-foundation.md)'s
  Non-Goals, which already excluded Android build support)
- iOS implementation (architecture-only, per Goals)
- Linux support (Linux is not a target platform — see
  [AGENTS.md](../AGENTS.md) Phase 1 constraints)
- A complete profiler or frame-pacing system (Application Timing defines
  only monotonic elapsed time)
- A multi-threaded task system (Threading assumes a single application
  thread, per the existing Phase 1 baseline)

## Terminology

- **Platform** — the Atlantis Platform module: a common interface plus
  one concrete implementation per OS (Windows Platform, Android Platform,
  future iOS Platform).
- **Platform lifecycle state** — Platform's internal notion of whether a
  native window/surface currently exists and is valid, and whether the
  application is paused/active. Exposed to Runtime only through
  `PlatformEvent` delivery and the accessors this spec defines (e.g.
  `shouldQuit()`) — **not** through a dedicated, stateful window object.
- **Native window handle** — the OS-specific value needed to eventually
  create a Vulkan (or other graphics API) surface: `HWND`+`HINSTANCE` on
  Windows, `ANativeWindow*` on Android, a `CAMetalLayer`-equivalent
  (future) on iOS.
- **NativeWindowHandle** — this spec's platform-neutral, opaque-`void*`
  wrapper around a native window handle (see Architecture / Design
  Constraints). Distinct from the OS-typed handle itself.
- **Application / Runtime** — the composition root (Atlantis Runtime,
  per `docs/architecture/module_boundaries.md`) that owns a Platform
  instance and drives the application loop. This spec defines what
  Runtime needs from Platform; it does not (re)define Runtime itself.
- **Logical size** — a window's size in OS UI coordinates (may differ
  from pixels, e.g. under DPI scaling).
- **Framebuffer extent** — the pixel dimensions a graphics backend must
  actually render into. Not assumed equal to logical size.

## Requirements

### Application Lifecycle & Entry Point

- Platform exposes a lifecycle interface Runtime drives: `initialize()`,
  `processEvents()` (non-blocking, drains and delivers this iteration's
  `PlatformEvent`s), `shouldQuit()`, `shutdown()`. The shared abstraction
  every platform reaches through these calls is the **Runtime
  frame/update path** — not an identical control-flow structure around
  them.
- **Windows**: Runtime owns a conventional application loop —
  `while (!platform.shouldQuit()) { platform.processEvents(); ...; }` —
  called from a conventional `main`/`WinMain`-style entry point.
- **Android**: Android's framework drives the process, not Atlantis, and
  Android Platform **does not reproduce Windows' literal loop
  structure**. Android Platform's implementation adapts whatever native
  entry point/lifecycle callback mechanism Android requires (e.g.
  `android_main` via a native glue layer — exact mechanism not decided by
  this spec, see Open Questions) so that it reaches the **same shared
  Runtime frame/update path** Windows reaches — not via an identical
  loop, but via `processEvents()` draining whatever lifecycle/window/
  surface events the framework delivered since the last call. This spec
  does not force Android's asynchronous, callback-driven lifecycle into a
  synchronous Win32 shape.
- **iOS (future, architecture-only)**: the same shared Runtime
  frame/update path must remain reachable from a UIKit-driven entry point
  analogous to Android's framework-driven model, not necessarily via an
  identical loop either. Not designed further here.

### Window Lifecycle, Native Window Handle Access

- Platform manages exactly one native window/surface for Phase 1
  (multi-window is a Future Extension, not designed here).
- **Native-window validity** is tracked entirely through Platform
  lifecycle state and the `SurfaceCreated`/`SurfaceDestroyed` event pair
  (see Events, below) — **not** through a query method on a window
  object. A native window is valid from the point a `SurfaceCreated`
  event is observed until a matching `SurfaceDestroyed` is observed; this
  is the "no usable window" state between an Android `SurfaceDestroyed`
  event and its later `SurfaceCreated`.
- **The current `NativeWindowHandle`** is the payload carried by the most
  recently observed `SurfaceCreated` event
  (`SurfaceCreated { handle: NativeWindowHandle }`) — obtained by
  observing that event, not by calling an accessor on a window object.
  Callers must not cache it independent of the event stream and assume it
  stays valid (see Ownership and Lifetime).
- **Current logical/framebuffer extent** is Platform state most recently
  reported via a `WindowResize` event (see Window Extent, below) — not
  queried via separate accessor methods on a window object.

### Window Extent

- `WindowExtent` is a `{width, height}` pair (unsigned integers). A
  `WindowResize` event carries **both** `logical` and `framebuffer`
  extents as independent fields, and they are **not assumed equal** —
  Android in particular may report them differently (e.g. display
  density scaling), and this spec does not assume Windows and Android
  agree on the relationship either.
- A `WindowExtent` of `{0, 0}` is a valid, representable state (window
  minimized, or Android surface momentarily unavailable) and must be
  distinguishable from any nonzero size — see Ownership and Lifetime for
  what callers must do about it.

### Events

- Platform delivers a minimal, closed set of events through
  `processEvents()` — a tagged `PlatformEvent`, not a full input system:
  - `WindowResize { logical: WindowExtent, framebuffer: WindowExtent }`
  - `WindowCloseRequested` (Windows: user closed the window; maps toward
    `shouldQuit()` becoming true)
  - `FocusGained` / `FocusLost`
  - `ApplicationPause` / `ApplicationResume` — primarily Android
    lifecycle events (Activity pause/resume). **Windows does not use or
    synthesize these events**; Windows represents minimize/restore and
    focus transitions entirely through `WindowResize`, `FocusGained`,
    and `FocusLost` instead.
  - `SurfaceCreated { handle: NativeWindowHandle }` / `SurfaceDestroyed`
    (the Android-critical pair — see Ownership and Lifetime; Windows
    implementation may synthesize these once around window
    creation/destruction so callers don't need OS-specific branching)
  - `Quit`
- Keyboard, mouse, touch, controller, and gesture events are explicitly
  **not** part of this event set (Non-Goals).

### Application Timing

- Platform (or, per Open Questions, possibly Core — see below) exposes a
  monotonic clock: a function returning elapsed time since an
  unspecified epoch, suitable for frame-delta computation. No wall-clock/
  calendar time, no profiling, no frame-pacing.

### Platform Identification

- An enum (`PlatformKind { Windows, Android, IOS }`) and an accessor
  (`currentPlatform()`) let code identify the running platform without
  testing OS preprocessor macros directly. Renderer-level and other
  cross-cutting code should never need `#ifdef _WIN32` / `#ifdef
  __ANDROID__`; that conditional compilation stays localized to Platform
  implementation files.

## Architecture / Design Constraints

**The Platform layer must not become a graphics API abstraction.** No
`VkInstance`, `VkSurfaceKHR`, `VkSwapchainKHR`, `VkPhysicalDevice`,
`VkDevice`, `VkQueue`, or any other Vulkan concept appears anywhere in
Platform. Platform exposes only the native information a graphics backend
needs; the future relationship is:

```
Platform  -->  PlatformEvent  -->  Runtime  -->  Runtime/Vulkan integration
  (Presentation orchestration)  -->  private Vulkan WSI  -->  VkSurfaceKHR
  -->  Presentation / generic RHI  -->  Renderer
```

Generic RHI's public API sits at the far end of this chain: it never
consumes `PlatformEvent`, never consumes `NativeWindowHandle`, and never
depends on Atlantis Platform. Only Runtime consumes/interprets
`PlatformEvent`; only a Runtime/Vulkan integration layer translates
relevant lifecycle/surface/resize events into `Presentation` operations;
only Vulkan Backend's private WSI boundary ever interprets
`NativeWindowHandle`.

**Native window handle representation.** `NativeWindowHandle` is a
small, tagged, copyable value type:

```
struct NativeWindowHandle {
  PlatformKind kind;
  // opaque platform-specific payload — see below
};
```

The payload is stored as **opaque pointer-sized values (`void*`), not the
OS-typed handles themselves** (not `HWND`, not `ANativeWindow*`), at this
struct's public-header boundary. This is the smallest abstraction that
satisfies every stated requirement simultaneously:

- The Platform module's *public* header needs zero Win32/Android NDK
  headers to define `NativeWindowHandle` — any consumer (in principle
  including a future RHI) can include it without pulling in OS SDK
  headers "unnecessarily," per this task's explicit requirement.
- Platform's own *implementation* (`.cpp`, compiled per-OS) populates the
  opaque fields from the real typed handles it owns.
- A future Vulkan Backend's *implementation* (also compiled per-OS)
  reinterprets the opaque fields back to `HWND`/`ANativeWindow*` to call
  the matching Vulkan WSI extension (`vkCreateWin32SurfaceKHR`,
  `vkCreateAndroidSurfaceKHR`) — this is Vulkan Backend doing Vulkan
  work, not Platform doing graphics work, and is unaffected by this
  design.
- **Renderer never sees `NativeWindowHandle` at all, and neither does
  generic RHI's public API.** Runtime reads it from Platform and
  transports it, without interpreting it, to **Vulkan Backend's private
  WSI boundary** — the only layer that ever interprets it, per
  [ADR-0011](../adr/0011-native-window-handle-representation.md). That
  boundary produces a `VkSurfaceKHR`, which is what generic RHI's
  `Presentation` is subsequently built from. This matches the existing
  rule in `docs/architecture/overview.md` and
  `docs/architecture/module_boundaries.md` that Renderer depends on
  neither Platform nor any native/surface type.

This was chosen over two alternatives considered and rejected:

- **A platform-owned fully opaque object with no public field access at
  all** (e.g. `void* opaqueHandle` with no tag) — rejected: a future
  Vulkan Backend would have no way to know *which* WSI extension to call
  without an out-of-band platform query, reintroducing exactly the kind
  of implicit coupling a tagged type avoids.
- **Exposing the real typed OS handles directly** (`HWND`/`ANativeWindow*`
  in the public struct) — rejected outright per the explicit requirement
  that the RHI must not need Win32/Android headers "unnecessarily," and
  because it would make `NativeWindowHandle`'s header itself
  platform-conditional, complicating any code that merely forwards the
  value without touching its contents (e.g. Runtime).

**Boundary clarification (resolved via amended ADR-0005):** Vulkan's own
WSI extension headers (`vulkan_win32.h`, `vulkan_android.h`) declare
functions taking `HWND`/`ANativeWindow*` by their real OS types, which
means a future Vulkan Backend's Windows/Android implementation
transitively needs `<windows.h>`/`<android/native_window.h>` type
visibility to call them.
[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) has been
amended to allow this through a **private WSI boundary inside Vulkan
Backend**, which may consume `NativeWindowHandle` (borrowed, not owned)
and include those OS headers strictly to produce a `VkSurfaceKHR` —
without those types ever reaching RHI's public API, Renderer, or
RenderGraph, and without Platform ever depending on Vulkan. See
[docs/architecture/platform-vulkan-wsi-boundary.md](../docs/architecture/platform-vulkan-wsi-boundary.md)
for the full analysis.

## Platform-specific Requirements

### Windows

Expected native concepts, **not implemented by this spec**: `HWND`
(the window), `HINSTANCE` (the module handle needed to create it). Both
remain entirely inside Windows Platform's implementation boundary — never
in Platform's public headers, never in Runtime, RHI, or Renderer.

### Android

**Not implemented by this spec.** Must account for, and this spec's
interface must not preclude:

- Android's Activity/application lifecycle (pause/resume, and process
  death — out of scope to fully solve here, but the event model must not
  assume the process runs uninterrupted).
- `ANativeWindow` is **not permanently valid**. It can be destroyed and
  later recreated by the framework independent of application/process
  lifetime (e.g. multi-window mode, some OEM backgrounding behavior), not
  only on final app exit. Atlantis code must never assume a
  `NativeWindowHandle` obtained once remains valid indefinitely — see
  Ownership and Lifetime.
- Native window availability must be observable through Platform
  lifecycle state, with transitions event-driven
  (`SurfaceCreated`/`SurfaceDestroyed`), not polled by guessing.

### iOS (future, architecture-only)

**No iOS source files. No iOS build configuration.** This spec documents
only the architectural requirements the Platform abstraction must not
prevent:

- A UIKit-driven application lifecycle (analogous to Android's
  framework-driven model, not Windows' Runtime-owned loop).
- Native drawable/surface ownership living entirely inside a future iOS
  Platform implementation.
- A `CAMetalLayer` (or equivalent) native surface object representable
  through the same tagged `NativeWindowHandle` shape (`PlatformKind::IOS`
  plus an opaque payload) without changing `NativeWindowHandle`'s public
  shape.

## Ownership and Lifetime

| Question | Windows | Android |
|---|---|---|
| Who creates the native window | Windows Platform, on request (Runtime calls into Platform during `initialize()`) | The Android framework; Windows Platform does not "create" it — it observes `SurfaceCreated` |
| Who owns it | Windows Platform (owns the `HWND` for its lifetime) | The Android framework owns the underlying `Surface`; Android Platform owns Atlantis's *tracking* of it |
| Who destroys it | Windows Platform (`DestroyWindow`, on Runtime shutdown or user close) | The Android framework destroys it; Atlantis code **must never** call a destroy operation on it — Android Platform only observes `SurfaceDestroyed` and invalidates its own state |
| How lifetime is represented | Platform lifecycle state + the `SurfaceCreated`/`SurfaceDestroyed` event pair, not an assumed-always-valid handle |||
| How the Renderer accesses it | It doesn't, ever (see Architecture / Design Constraints) |||
| How a graphics backend accesses it | Runtime transports the `NativeWindowHandle` (without interpreting it) to Vulkan Backend's private WSI boundary, which alone interprets it to build a `VkSurfaceKHR`; generic RHI's public API never receives the handle and never reaches into Platform itself (preserves the existing "Platform and RHI are siblings, Runtime composes them" rule) |||
| What happens on resize | Platform delivers `WindowResize` with both logical and framebuffer extents; Runtime is responsible for telling a future RHI `Presentation` to recreate — Renderer is uninvolved (matches [ADR-0002](../adr/0002-presentation-rendertarget-unification.md)) |||
| What happens when minimized | Framebuffer extent becomes `{0, 0}`. Runtime **must not** attempt to create or recreate a graphics surface/swapchain while extent is zero (Vulkan itself rejects a zero-extent swapchain) — it waits for the next nonzero `WindowResize` |||
| What happens when Android recreates the native window | `SurfaceDestroyed` then, at an indeterminate later time, `SurfaceCreated` with a **new** handle — not guaranteed to reference the same underlying object. Runtime must treat this as "the render target is entirely gone," not a resize: a future RHI must fully tear down and rebuild `Presentation`, not attempt an in-place update |||

**Explicit non-assumption, stated per this task's requirement:** this
spec does not assume a native window has the same lifetime as the
rendering device. A future RHI's `Device` may reasonably outlive one or
more Android surface destroy/recreate cycles; only `Presentation` and the
`RenderTarget`s it vends are tied to native window lifetime. This is not
new — it is what `docs/architecture/resource_lifetime.md`'s existing
Android section already anticipated; this spec is what makes it concrete
enough to implement against.

## Threading

- Application lifecycle, window operations, event processing, and native
  window access **all happen on a single application thread**, per the
  existing Phase 1 baseline in
  [docs/architecture/threading.md](../docs/architecture/threading.md) and
  [ADR-0004](../adr/0004-phase1-threading-baseline.md). This spec
  introduces no multi-threaded task system.
- On Android, "the application thread" means whatever thread ultimately
  calls into the shared Runtime loop (see Application Lifecycle &
  Entry Point) — for example, a native-glue-driven `android_main`'s own
  thread — not necessarily the Java UI thread. Exact mechanism is an
  Open Question, not decided here.
- **Assumption future Vulkan/RHI work must preserve:** RHI `Device`/
  `Presentation` creation and acquire/present calls happen on this same
  single application thread. `NativeWindowHandle` values must not be read,
  and Platform lifecycle state/events must not be observed, from any
  other thread in Phase 1.

## Error Handling

Follows existing Atlantis conventions, introduces no second error-handling
system:

- Platform initialization failure (e.g. window-class registration
  failure on Windows, a surface that never becomes available within
  whatever timeout a caller chooses on Android) is represented via
  `atlantis::Result<T, E>` (the type `specs/0001-project-foundation.md`
  already implemented), not exceptions — consistent with
  [ADR-0009](../adr/0009-assertion.md)'s "avoid exceptions as the default
  mechanism" and AGENTS.md's broader exception-free-core-modules rule.
- Programmer errors (e.g. querying Platform lifecycle state or
  native-window information before `initialize()`, or after
  `shutdown()`) use `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`, per the existing
  convention — not a new assertion mechanism.

## Build Integration

**Not implemented by this spec** — described for a future Plan:

- New module directory `src/platform/`, following
  [ADR-0010](../adr/0010-cmake-structure.md)'s established convention:
  - `include/atlantis/platform/*.h` — the common interface
    (`NativeWindowHandle`, `PlatformEvent`, `WindowExtent`,
    `PlatformKind`, timing, and the lifecycle functions
    `initialize`/`processEvents`/`shouldQuit`/`shutdown`). No OS-specific
    headers included here.
  - `src/windows/` — Windows Platform implementation, compiled only when
    targeting Windows.
  - `src/android/` — Android Platform implementation, compiled only when
    targeting Android. Per `specs/0001-project-foundation.md`'s
    Non-Goals, Android build/toolchain support does not exist yet; this
    directory's *existence* is defined by this spec, but nothing here
    unblocks actually compiling it — that is still gated on a future
    Android build-support spec.
  - `src/ios/` — **not created.** iOS is architecture-only.
- CMake target `atlantis_platform` (static library), alias
  `Atlantis::Platform`, depending on `Atlantis::Core` only — the same
  `atlantis_<module>` / `Atlantis::<Module>` pattern
  `specs/0001-project-foundation.md` established via
  `atlantis_core`/`Atlantis::Core`.
- Per-OS source selection via CMake `if(WIN32)` / `elseif(ANDROID)`
  conditionals. No Android NDK toolchain configuration is added by this
  spec (not strictly necessary to define the interface; deferred to the
  Android build-support spec, consistent with keeping this spec minimal).

## Acceptance Criteria

- [ ] `src/platform`'s public headers (once implemented) contain no
      `Vk*` type and no `#include <vulkan/...>` — Platform has no Vulkan
      dependency, verifiable by inspection/grep.
- [ ] No Renderer-level code (present or future) includes `<windows.h>`,
      `<android/native_window.h>`, or references `HWND`/`ANativeWindow`/
      `HINSTANCE` — enforced by the module boundary this spec and
      `docs/architecture/module_boundaries.md` both state.
- [ ] Windows native window ownership (creator/owner/destroyer) is
      stated unambiguously in this spec's Ownership and Lifetime table —
      satisfied by this document; re-verified against the future Plan.
- [ ] Android `ANativeWindow` lifetime is stated unambiguously, including
      that Atlantis never destroys it and must treat recreation as a new,
      unrelated handle — satisfied by this document's Ownership and
      Lifetime table and Android-specific Requirements.
- [ ] `WindowExtent` and the `WindowResize` event can represent both
      logical and framebuffer size independently, including `{0, 0}` —
      satisfied by this document's Window Extent section; verifiable once
      implemented via a unit test with no live window required.
- [ ] Windows' Runtime-owned-loop model and Android's framework-driven
      model are both satisfiable by the same `initialize`/
      `processEvents`/`shouldQuit`/`shutdown` interface, without forcing
      Android into a synchronous Win32-shaped loop — satisfied by this
      document's Application Lifecycle & Entry Point section.
- [ ] `NativeWindowHandle` can be passed from Platform through Runtime to
      Vulkan Backend's private WSI boundary without any generic RHI or
      Renderer code including a Win32/Android SDK header — satisfied by
      this document's Native Window Handle Representation design (opaque
      `void*` payload).
- [ ] Platform-specific code (Win32/Android NDK types, `#ifdef`s) remains
      isolated to `src/platform/src/windows/` and
      `src/platform/src/android/` — no such type or macro appears in
      `include/atlantis/platform/*.h` — verifiable by inspection once
      implemented.
- [ ] `PlatformKind` includes an `IOS` value and `NativeWindowHandle`'s
      tagged design has a documented (not implemented) iOS case — the
      design does not assume exactly two platforms — satisfied by this
      document's Platform Identification and iOS sections.
- [ ] No `src/platform/src/linux/` (or equivalent), no `PlatformKind`
      Linux value, no Linux-specific build configuration anywhere this
      spec touches — Linux is not a target platform.

## Verification Strategy

- **Unit tests** (once implemented, per
  [testing-strategy.md](../docs/process/testing-strategy.md) layer 1, no
  GPU/live window required): `WindowExtent` zero/nonzero and equality
  logic; `NativeWindowHandle`'s tag/payload construction and accessors;
  `PlatformEvent` construction/inspection; the monotonic clock wrapper's
  basic monotonicity property.
- **Manual verification**: actual window creation, resize, and minimize
  behavior on a real Windows machine (this spec does not require an
  Android device/emulator to exist yet, since Android isn't built).
- **Not applicable**: Vulkan Validation Layers, image regression — no
  rendering exists.

## Dependencies

`Atlantis Core` only (per `docs/architecture/module_boundaries.md`'s
already-established Platform dependency rule), plus each concrete
implementation's own OS headers (Win32 for Windows Platform; Android NDK
for Android Platform, though not built yet). **No third-party windowing
library** (no GLFW/SDL) — consistent with [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md)'s
decision to use native per-OS implementations instead, since GLFW/SDL fit
Android's Activity-lifecycle model poorly. No new dependency is added by
this spec.

## Risks

- Android's exact lifecycle-glue mechanism (a native glue library vs. a
  hand-rolled JNI bridge) is undecided; this spec defines the *event
  contract* Platform must deliver, not that mechanism, so the risk is
  contained to the future Android Platform implementation, not this
  interface.
- The Win32-header-visibility tension flagged in Architecture / Design
  Constraints is a real risk if left unresolved before RHI implementation
  begins — could force rework of the RHI's Vulkan Backend if decided
  incompatibly later.
- The "don't create a swapchain at zero extent" rule can only be mandated
  by this spec as an event contract; actual compliance depends on a
  future RHI/Presentation implementation honoring it.

## Open Questions

- Exact `PlatformEvent` C++ representation (tagged union/`std::variant`
  vs. a polymorphic event base) — this spec assumes a value-type,
  tagged-union style consistent with `atlantis::Result`'s existing
  design, but does not lock the exact mechanism; left to the Plan stage.
- Android's native-entry-point mechanism (`android_native_app_glue` vs.
  custom JNI bridge) — not decided here, deferred to Android Platform's
  own future implementation.
- **Whether Application Timing belongs in Platform or in `Atlantis
  Core`.** A monotonic clock (`std::chrono::steady_clock`) is portable
  standard-library functionality with no OS-specific implementation
  needed, unlike everything else in this spec's scope. It is specified
  here because the task driving this spec explicitly scoped timing under
  Platform, but it could equally live in Core without any interface
  change, and may be relocated there at Plan time — flagged rather than
  decided.
- How process-death-and-restart on Android (not just pause/resume)
  should be represented, if at all, in Phase 1 — not resolved; may be
  legitimately out of scope until an actual Android build-support spec
  exists.

## Future Extensions

- iOS Platform implementation (UIKit lifecycle, `CAMetalLayer`).
- Multi-window support (this spec assumes exactly one native
  window/surface).
- An input system (keyboard/mouse/touch) layered on top of the event
  model this spec defines, not replacing it.
- Multi-threaded event processing or Platform access, if a future spec
  ever revisits the Phase 1 single-thread baseline.

## ADRs Required Before Approval

None of the following are decided by this spec — each names a decision
that materially affects architecture and was required to reach `Accepted`
before this spec could move past `In Review`. (Not numbered here — see
`specs/0001-project-foundation.md`'s experience: informal numbering in
spec prose has previously drifted from the ADRs' real, sequentially
assigned file numbers.) All three were filed and are now `Accepted`; see
the ADR link appended to each item below.

1. **Native window handle representation.** Decision: the tagged,
   opaque-`void*`-payload `NativeWindowHandle` design described in
   Architecture / Design Constraints. Matters because every future
   RHI/Vulkan spec's `Presentation` interface depends on this shape being
   fixed; changing it later means touching every backend. Alternatives to
   weigh: a fully untagged opaque object (rejected here, reasoning
   given); exposing real typed OS handles directly (rejected here,
   reasoning given); a `std::variant`-based payload instead of raw
   `void*` fields (not evaluated in depth by this spec). Filed as
   [ADR-0011](../adr/0011-native-window-handle-representation.md)
   (`Accepted`).

2. **Application lifecycle / event-model abstraction.** Decision: the
   shared `initialize`/`processEvents`/`shouldQuit`/`shutdown` interface
   plus the closed `PlatformEvent` set (`WindowResize`,
   `WindowCloseRequested`, `FocusGained`/`Lost`,
   `ApplicationPause`/`Resume`, `SurfaceCreated`/`Destroyed`, `Quit`), and
   how Android's framework-driven entry point adapts into it without
   faking a Win32-style loop. Matters because it's the seam every future
   Runtime-level code and the eventual Android Platform implementation
   are built against. Alternatives to weigh: callback/observer
   registration instead of a polled event queue; a richer event set vs.
   this spec's deliberately minimal one. Filed as
   [ADR-0012](../adr/0012-application-lifecycle-and-event-model.md)
   (`Accepted`).

3. **Platform ownership model for native windows/surfaces.** Decision:
   the per-OS creator/owner/destroyer split and the "native window
   lifetime ≠ device lifetime" principle in the Ownership and Lifetime
   table, including the zero-extent-means-don't-render rule and the
   treat-Android-recreation-as-full-teardown rule. Matters because it's
   the contract a future RHI's `Presentation` recreation logic must
   honor exactly, and getting it wrong on Android specifically risks
   crashes or leaks against a framework-owned resource Atlantis doesn't
   control. Alternatives to weigh: whether Windows should also emit
   `SurfaceCreated`/`SurfaceDestroyed` (this spec proposes yes, for a
   uniform Runtime-side contract) vs. Windows-only using `WindowResize`
   with a zero extent to represent "unavailable." Filed as
   [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md)
   (`Accepted`).

A fourth decision — the boundary between "only Platform includes Win32/
Android NDK headers" and Vulkan Backend's necessary use of Vulkan's own
WSI headers — was originally listed here. It is **resolved**: amending
[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) in place
(a private WSI boundary inside Vulkan Backend, consuming
`NativeWindowHandle`) fully addresses it, and no new ADR is required. See
[docs/architecture/platform-vulkan-wsi-boundary.md](../docs/architecture/platform-vulkan-wsi-boundary.md).
