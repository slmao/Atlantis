# ADR 0012: Application Lifecycle and Event Model

- **Status:** Accepted
- **Date:** 2026-08-02
- **Deciders:** _Human approval confirmed 2026-08-04_
- **Related Spec:** [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)

## Context

`specs/0002-platform-foundation.md` requires a common application
lifecycle abstraction spanning Windows' synchronous, application-owned
message-pump model and Android's asynchronous, framework-driven Activity/
Surface lifecycle, without forcing Android into a Win32-shaped loop that
would misrepresent its actual ownership and lifetime semantics (an
explicit requirement repeated across that spec and
[ADR-0005](0005-platform-module-multi-os-windowing.md)). The spec
proposed a shared `initialize`/`processEvents`/`shouldQuit`/`shutdown`
interface and a closed `PlatformEvent` set, and named this decision —
application lifecycle and event model — as requiring its own ADR. This
ADR is that decision. Input (keyboard/mouse/touch/controller/gesture) is
explicitly out of scope, per the spec's Non-Goals.

## Decision

**Common lifecycle interface**, implemented identically in shape by every
Platform implementation:

```
initialize()      -> Result<void, PlatformError>
processEvents()    -> drains and delivers this call's PlatformEvents (non-blocking)
shouldQuit()       -> bool
shutdown()
```

**Per-OS entry point, shared Runtime frame/update path.** The *entry
point* differs by platform, and so may the exact control-flow structure
around it; this ADR does not require Android to literally execute the
same `while`-loop shape Windows uses. What must stay shared is the
**Runtime frame/update path** both eventually drive:
- **Windows:** Runtime owns a conventional loop —
  `while (!platform.shouldQuit()) { platform.processEvents(); /* update, render */ }`
  — called from a `main`/`WinMain`-style entry point. Windows Platform's
  `processEvents()` translates the Win32 message pump into
  `PlatformEvent`s.
- **Android:** the Android framework drives the process, not Atlantis.
  Android Platform's implementation adapts whatever native entry
  mechanism it uses (exact mechanism not decided by this ADR — see Open
  Questions in `specs/0002-platform-foundation.md`) so that it eventually
  reaches the **same shared Runtime frame/update path** Windows uses —
  not necessarily via an identical loop structure — with
  `processEvents()` draining whatever lifecycle/window/surface events
  the framework delivered since the last call, never blocking on a
  Windows-style message queue that doesn't exist on Android.
- **iOS (future, architecture-only):** the same shared Runtime
  frame/update path must remain reachable from a UIKit-driven entry point
  analogous to Android's; not designed further here.

**Closed, categorized `PlatformEvent` set** — four distinct categories,
kept separate because they mean different things to different future
consumers (Runtime for most of them, a future RHI's `Presentation` for
surface-availability specifically):

1. **Application lifecycle events:** `ApplicationPause`,
   `ApplicationResume`, `Quit`. **Android** may produce
   `ApplicationPause`/`ApplicationResume` (Activity pause/resume) and
   `Quit` (Activity finishing). **Windows does not use
   `ApplicationPause`/`ApplicationResume` at this stage** — Windows
   minimize/restore and focus transitions are represented entirely
   through `WindowResize`, `FocusGained`, and `FocusLost` (category 2,
   below), not through fabricated pause/resume semantics. A future ADR
   may define a genuine cross-platform application-pause concept if a
   real need for one emerges; this ADR does not invent new events to
   pre-empt that.
2. **Window events:** `WindowResize { logical, framebuffer }`,
   `WindowCloseRequested`, `FocusGained`, `FocusLost`.
3. **Surface availability events:** `SurfaceCreated { handle:
   NativeWindowHandle }`, `SurfaceDestroyed` — kept **distinct from
   `WindowResize`**. The Android-critical pair — see
   [ADR-0013](0013-platform-window-ownership-and-lifetime.md), which
   preserves the rule that a `SurfaceDestroyed` followed by a
   `SurfaceCreated` represents a **new** native surface identity, never
   merely a resize. Windows Platform may synthesize this pair once
   around window creation/destruction so Runtime-level code has one
   uniform contract regardless of OS.
4. **Input events:** explicitly **not defined by this ADR.** Keyboard,
   mouse, touch, controller, and gesture input are out of scope; if an
   input system is built later, it is expected to extend this same
   `PlatformEvent` mechanism with a fifth category, not replace it.

**`NativeWindowHandle` transport.** The `SurfaceCreated` variant carries
a `NativeWindowHandle` (per
[ADR-0011](0011-native-window-handle-representation.md)). Runtime
**transports** this value — receiving it from `processEvents()` and
forwarding it onward — but **must not interpret** `value0`/`value1`.
Interpretation is reserved exclusively for the active graphics backend's
private WSI boundary (e.g. Vulkan Backend's WSI layer); Runtime's role
here is limited to moving the value, unchanged, from Platform to that
boundary. Generic RHI's public API, Renderer, and RenderGraph never see
this value at all.

**Android process-death is explicitly out of scope for this ADR.**
Android can terminate the process entirely under memory pressure
(distinct from `ApplicationPause`), with system-managed state
save/restore. This ADR does not solve state restoration; future
Android-specific work must not assume `shutdown()` always runs cleanly on
Android, but designing that handling is deferred — Android build support
does not exist yet (per `specs/0001-project-foundation.md` and
`specs/0002-platform-foundation.md` Non-Goals), and solving it now would
be speculative ahead of a real implementation to validate against.

## Rationale

A polled, drained-per-call event queue (rather than callback/observer
registration) matches the single-application-thread model
([ADR-0004](0004-phase1-threading-baseline.md)) most simply: there is no
re-entrancy concern about a callback firing mid-frame, and it mirrors how
both Win32's message pump and an Android native-glue event loop already
work at their core — "check what's pending, hand it to the caller."
Splitting events into four categories (rather than one flat list) exists
because their consumers differ: Runtime handles lifecycle and window
events directly, while surface-availability events are what a future
RHI's `Presentation` recreation logic specifically keys off of — keeping
that category distinct avoids forcing `Presentation`-adjacent code to
filter a mixed event stream.

## Alternatives Considered

- **A fully platform-neutral event loop that also makes Android
  synchronous** (Atlantis owns `main()` and busy-polls or blocks on
  Android the way it does on Windows). Rejected: this is exactly the
  "fake Win32-style lifecycle" `specs/0002-platform-foundation.md` and
  this task explicitly forbid — it misrepresents Android's actual
  framework-driven, asynchronous model and risks missing or
  mishandling a pause/surface-destroyed transition.
- **Platform-specific lifecycle with only a thin common interface**
  (mostly separate Windows/Android code paths sharing little). Rejected:
  goes too far the other direction — Runtime-level code and a future
  RHI's `Presentation` need one shared event vocabulary to react to
  regardless of OS, or every consumer needs per-OS branches, which is
  exactly what Atlantis Platform exists to prevent (per
  [ADR-0005](0005-platform-module-multi-os-windowing.md)).
- **Callback/observer registration instead of a polled queue.**
  Considered, not adopted for Phase 1: viable, but adds re-entrancy
  surface area a polled model avoids for no current benefit; could be
  layered on top later without contradicting this decision.
- **Forcing all platforms into a Windows-style loop.** Equivalent to the
  first alternative; rejected for the same reason.

The chosen design — shared interface and event vocabulary, per-OS entry
point, polled/drained delivery — is the option that best preserves
correct Android lifecycle semantics, per this task's explicit guidance,
while still giving Windows nothing more complex than it already needs.

## Consequences

### Positive

- Runtime-level code, and a future RHI's `Presentation` recreation logic,
  have one event vocabulary regardless of OS.
- Android's asynchronous, framework-driven semantics are represented
  accurately rather than distorted to fit Windows' shape.
- The four-category split gives future specs (input system, Android
  implementation, RHI) a clear place to plug in without redesigning the
  event model itself.

### Negative / Trade-offs

- The interface must generalize correctly before Android — its second
  and structurally hardest consumer — actually exists to validate it
  against (the same "generalize before the second consumer can confirm
  it" cost already accepted in [ADR-0002](0002-presentation-rendertarget-unification.md)
  and [ADR-0005](0005-platform-module-multi-os-windowing.md)).
- Android process-death/state-restoration is a real, deferred gap, not a
  solved problem — a future Android implementation spec must still
  address it.
- Windows contributes no `ApplicationPause`/`ApplicationResume` events at
  this stage; if a genuine cross-platform pause concept is needed later,
  it requires its own future ADR rather than retrofitting these two
  Android-shaped events onto Windows.

## Constraints on Future Specs

- A future Android Platform implementation spec must adapt its actual
  native entry point so it reaches the shared Runtime frame/update path
  via `processEvents()` — it must not invent a separate path or bypass
  the shared interface, though it need not replicate Windows' literal
  loop structure to do so.
- No future spec may have Runtime interpret `NativeWindowHandle`'s
  `value0`/`value1` fields — only the active graphics backend's private
  WSI boundary may do so (per [ADR-0011](0011-native-window-handle-representation.md)).
- Platform **produces** `PlatformEvent`s; Runtime **consumes and
  interprets** them. A future Runtime/Vulkan integration layer —
  Presentation orchestration living alongside Runtime, not inside generic
  RHI — is responsible for translating the relevant `SurfaceCreated`,
  `SurfaceDestroyed`, and `WindowResize` events into concrete
  `Presentation` lifecycle/recreation operations. **Generic RHI's public
  API must not directly consume `PlatformEvent`, must not depend on
  Atlantis Platform, and must not expose `NativeWindowHandle` or any
  OS-specific type** (see
  [ADR-0011](0011-native-window-handle-representation.md)) — no generic
  RHI interface method accepts, returns, or is defined in terms of any of
  these, and Runtime is never required to invent additional
  platform-specific signals beyond what this ADR defines. Vulkan
  Backend's private WSI boundary remains the only layer that interprets
  `NativeWindowHandle`.
- A future input-system spec must extend `PlatformEvent` with a new,
  separate category rather than overloading window/lifecycle/surface
  events to also carry input data.
- No future spec may assume Android's `shutdown()` runs deterministically
  on process termination without first addressing process-death handling
  explicitly.

## Related Specs

- [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)

## Related ADRs

- [ADR-0004](0004-phase1-threading-baseline.md) — the single-application-
  thread baseline this event model is designed against.
- [ADR-0005](0005-platform-module-multi-os-windowing.md) — introduces
  Atlantis Platform and the requirement that it generalize across
  Windows/Android/future iOS without forking Renderer/RHI.
- [ADR-0011](0011-native-window-handle-representation.md) — defines the
  `NativeWindowHandle` carried by the `SurfaceCreated` event.
- [ADR-0013](0013-platform-window-ownership-and-lifetime.md) — defines
  exactly what `SurfaceCreated`/`SurfaceDestroyed` mean for window
  ownership and lifetime.
