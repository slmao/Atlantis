# ADR 0013: Platform Window Ownership and Lifetime

- **Status:** Accepted
- **Date:** 2026-08-02
- **Deciders:** _Human approval confirmed 2026-08-04_
- **Related Spec:** [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)

## Context

`specs/0002-platform-foundation.md`'s Ownership and Lifetime section, and
[docs/architecture/platform-vulkan-wsi-boundary.md](../docs/architecture/platform-vulkan-wsi-boundary.md)'s
resulting amendment to [ADR-0005](0005-platform-module-multi-os-windowing.md),
already establish most of the substance this ADR formalizes: who creates,
owns, and destroys native windows/surfaces per platform, and that Vulkan
WSI may create/destroy `VkSurfaceKHR` without ever destroying the native
window itself. Spec 0002 named "platform ownership model for native
windows/surfaces" as requiring its own ADR because a future RHI's
`Presentation` recreation logic must honor this contract exactly, and
getting it wrong on Android specifically risks crashes or leaks against a
resource the application does not control. This ADR is that decision,
and additionally makes explicit a principle Spec 0002 implied but did not
name outright: native window lifetime, `VkSurfaceKHR` lifetime, and
`VkSwapchainKHR` lifetime are three **independent** lifetimes, not one.

## Decision

### Windows

- Atlantis (Windows Platform) **creates, owns, and destroys** the
  `HWND` (and its associated window class/`HINSTANCE`).
- Vulkan WSI (per [ADR-0005](0005-platform-module-multi-os-windowing.md),
  as amended) **may create and destroy `VkSurfaceKHR`** bound to that
  `HWND`.
- Vulkan WSI **must never destroy the `HWND`** — no `DestroyWindow` or
  equivalent call from within Vulkan Backend, ever.

### Android

- The **Android framework owns** the `Surface`/`ANativeWindow`'s
  lifetime completely. Atlantis (Android Platform) does not create it in
  the way Windows Platform creates an `HWND` — it **observes
  availability** via the `SurfaceCreated`/`SurfaceDestroyed` events
  defined in [ADR-0012](0012-application-lifecycle-and-event-model.md).
- **Atlantis Platform does not own or destroy** the `ANativeWindow`.
- **Vulkan WSI may temporarily retain an `ANativeWindow` reference when
  its implementation requires doing so.** Such a retained reference is a
  **lifetime/reference-management mechanism, not ownership of the native
  window** — holding a reference does not make Vulkan WSI (or anything
  else in Atlantis) the window's owner. **Any acquire must have a
  matching release**, following Android's own NDK acquire/release
  discipline — not a lifetime Atlantis invents for itself. A future
  Android Platform implementation must pair any retained reference with
  the matching release, exactly as Android's platform contract requires;
  this ADR does not itself design that mechanism, only mandates that it
  be followed.
- Vulkan WSI **may create and destroy `VkSurfaceKHR`** bound to the
  current `ANativeWindow*`.
- **Vulkan WSI must never perform an operation whose purpose is to
  destroy the framework-owned native window.** Releasing a temporarily
  retained reference is ordinary, expected bookkeeping and is not the
  same act as destroying the window — but no code path in Vulkan WSI may
  call a destroy/release specifically *in order to* end the window's
  life; that authority belongs to the Android framework alone.

### Critical rule: `SurfaceDestroyed` is not a resize

**`SurfaceDestroyed` must never be modeled merely as a resize event.** A
subsequent `SurfaceCreated` represents a **new, unrelated window/surface
identity** — not guaranteed to reference the same underlying object as
before — and therefore requires full graphics-surface **and** swapchain
recreation, never an in-place update. This applies even though, at the
`PlatformEvent` level, both a resize and a destroy/recreate cycle might
superficially look like "the window changed": the distinguishing signal
is which event fired (`WindowResize` vs. `SurfaceDestroyed` followed by
`SurfaceCreated`), and downstream consumers must branch on that, not
infer identity from extent alone.

### Three independent lifetimes

This ADR explicitly preserves three separate lifetimes with independent
recreation triggers and ownership — none is implied by, or bounded by,
another — while still being connected by a one-way dependency:
`VkSwapchainKHR` depends on `VkSurfaceKHR`, which depends on the native
window, and that dependency constrains *destruction order*, not lifetime
identity.

1. **Native window/surface lifetime** — governed by Platform (Windows)
   or the OS framework (Android); observed through platform lifecycle
   state and the `SurfaceCreated`/`SurfaceDestroyed` event pair (see
   [ADR-0012](0012-application-lifecycle-and-event-model.md)) — a
   currently-held `NativeWindowHandle` (per
   [ADR-0011](0011-native-window-handle-representation.md)) is valid
   exactly between a `SurfaceCreated` and its matching
   `SurfaceDestroyed`, never assumed valid indefinitely.
2. **`VkSurfaceKHR` lifetime** — owned by Vulkan Backend (its WSI
   boundary creates it), built from a currently-valid
   `NativeWindowHandle`. It must not outlive the native window it was
   created from. It has its **own recreation trigger, independent of
   `VkSwapchainKHR`'s** — an ordinary resize does not recreate it (see
   Resize, minimize, zero extent, below) — but **a `VkSurfaceKHR` must
   remain alive for as long as any `VkSwapchainKHR` depends on it.**
   Destroying a `VkSurfaceKHR` therefore requires every dependent
   `VkSwapchainKHR` to already have been destroyed first.
3. **`VkSwapchainKHR` lifetime** — owned by Vulkan Backend's
   `Presentation` implementation (per
   [ADR-0002](0002-presentation-rendertarget-unification.md)), built atop
   a `VkSurfaceKHR` and **depending on it**. Has its own recreation
   trigger, independent of `VkSurfaceKHR`'s: resize or an
   out-of-date/suboptimal present result recreate the swapchain alone,
   with no `VkSurfaceKHR` change required. When the native window is
   destroyed, teardown must happen in dependency order (swapchain, then
   surface — see SurfaceDestroyed / SurfaceCreated ordering, below), and
   neither may be recreated until a new native window (and thus a new
   `VkSurfaceKHR`) exists.

A native window can outlive many swapchain recreations (ordinary
resizing); a `VkSurfaceKHR` cannot outlive its native window; a
`VkSwapchainKHR` cannot outlive its `VkSurfaceKHR`. Collapsing any two of
these into one lifetime is exactly the mistake this ADR exists to
prevent — most importantly, treating "the window resized" and "the
window was destroyed and a new one created" as the same case.

### SurfaceDestroyed / SurfaceCreated ordering

Because `VkSwapchainKHR` depends on `VkSurfaceKHR`, and `VkSurfaceKHR`
depends on the native window, teardown and recreation follow strict
dependency order:

- **On `SurfaceDestroyed`:**
  1. Destroy the dependent `VkSwapchainKHR`, if one exists.
  2. Destroy the `VkSurfaceKHR`.
  3. Wait for a new `SurfaceCreated` before attempting to recreate
     either.
- **On `SurfaceCreated`:**
  1. Create a new `VkSurfaceKHR` from the new `NativeWindowHandle`.
  2. Create a new `VkSwapchainKHR` from it, but only when the current
     framebuffer extent is non-zero (see Resize, minimize, zero extent,
     below).

### Resize, minimize, zero extent

- **Resize:** native window lifetime unaffected. On an ordinary
  `WindowResize`, `VkSurfaceKHR` **normally remains alive** (a Vulkan
  surface is not inherently tied to a specific extent); `VkSwapchainKHR`
  **is recreated** at the new extent (already established by
  [ADR-0002](0002-presentation-rendertarget-unification.md)). Resize
  never triggers the destroy/recreate sequence above — that sequence is
  reserved for an actual `SurfaceDestroyed`/`SurfaceCreated` pair.
- **Minimize / zero extent:** per `specs/0002-platform-foundation.md`,
  Runtime must not attempt to create or recreate a swapchain while
  framebuffer extent is `{0, 0}`. This ADR locates that rule precisely:
  it applies at the **swapchain** layer — the native window, and even
  the `VkSurfaceKHR`, may continue to exist validly while minimized;
  Vulkan itself is what rejects a zero-extent swapchain, not any rule
  about the window or surface being gone.

## Rationale

Platform-correct, per-OS ownership rules were chosen over any uniform
model because uniformity here would be a fiction: Windows genuinely
grants the application explicit `CreateWindowEx`/`DestroyWindow` control,
while Android genuinely does not grant equivalent control over
`Surface`/`ANativeWindow` — pretending otherwise on either platform
produces either an inaccurate model (Windows abdicating control it
actually has) or a crash risk (Android code destroying something the
framework still owns). Naming the three lifetimes independently, rather
than nesting them, is what makes the "`SurfaceDestroyed` is not a resize"
rule enforceable: if `VkSurfaceKHR`/`VkSwapchainKHR` lifetime were
implicitly assumed to track window lifetime 1:1, there would be no
vocabulary left to describe "the window is gone and came back as a
different one," which is precisely Android's actual behavior.

## Alternatives Considered

- **Atlantis owns all platforms uniformly** (Atlantis always
  creates/destroys the native window/surface, Android included).
  Rejected: factually false on Android — the framework owns
  `Surface`/`ANativeWindow` creation and destruction, not application
  code; forcing this model would mean either lying about what Atlantis
  actually controls, or attempting an illegal destroy call against a
  framework-owned resource.
- **OS/framework owns all platforms uniformly** (even on Windows, never
  explicitly destroy the window). Rejected: Windows genuinely provides
  explicit create/destroy control via `CreateWindowEx`/`DestroyWindow`;
  abdicating it would be an unnecessarily indirect, inaccurate model for
  a platform that doesn't require it.
- **Platform-specific ownership rules** (the chosen model). Adopted:
  matches what is actually true per OS, per this task's explicit
  guidance to choose the platform-correct model over forcing uniformity.
- **A reference-counted common ownership model** spanning both platforms
  (e.g. a shared, refcounted handle wrapping the native window, teardown
  triggered by the last reference dropping). Rejected for Phase 1: this
  ADR's model has exactly one owner per platform (Platform on Windows,
  the OS framework on Android), with Vulkan WSI at most temporarily
  retaining a reference under Android's own acquire/release rules and
  never taking ownership — there is no multiple-simultaneous-owner
  scenario to justify a shared refcounted ownership model, and
  introducing one risks obscuring the "Vulkan WSI must never destroy the
  native window" rule behind an accidental refcount-reaches-zero
  teardown.

## Consequences

### Positive

- Matches each platform's actual capabilities and constraints instead of
  a fictional uniform model, removing an entire class of "why did this
  crash on Android but not Windows" bugs rooted in ownership confusion.
- The three-independent-lifetimes framing gives a future RHI/Vulkan spec
  an unambiguous rule for exactly when to recreate `VkSurfaceKHR` versus
  `VkSwapchainKHR` versus doing nothing (ordinary resize).
- The `SurfaceDestroyed`-is-not-a-resize rule directly prevents a
  plausible, serious bug class: attempting to "resize" a swapchain built
  on a `VkSurfaceKHR` whose underlying window no longer exists.

### Negative / Trade-offs

- Two different ownership stories (Windows vs. Android) is more to
  document and implement correctly than one uniform story would be —
  accepted because the uniform alternatives are each wrong for one
  platform.
- Android's acquire/release reference discipline for `ANativeWindow` is
  mandated by reference (must follow Android's own semantics) but not
  designed in this ADR — a real, deferred implementation detail for
  Android Platform's own future spec.
- Enforcing "Vulkan WSI must never destroy the native window" is a
  documentation/code-review discipline, not something this ADR can make
  the compiler enforce — a real, acknowledged limitation.

## Constraints on Future Specs

- A future RHI/Vulkan spec's `Presentation` recreation logic must treat
  every new `SurfaceCreated` handle as requiring full `VkSurfaceKHR` +
  `VkSwapchainKHR` recreation — it must never assume handle stability
  across a destroy/recreate boundary, and must never conflate
  `SurfaceDestroyed`/`SurfaceCreated` with `WindowResize`.
- No future spec's Vulkan Backend implementation may call a
  native-window-destroying or -releasing operation (`DestroyWindow`,
  `ANativeWindow_release` used as a destroy trigger, or equivalent) —
  that authority belongs to Platform (or the OS framework) alone.
- A future Android Platform implementation spec must define the concrete
  acquire/release mechanism satisfying Android's reference semantics;
  this ADR mandates that it exist and be followed, not what it looks
  like.
- A future RHI/Vulkan spec must not create a swapchain while framebuffer
  extent is `{0, 0}`, per `specs/0002-platform-foundation.md`, and must
  treat that constraint as applying to `VkSwapchainKHR` specifically, not
  to `VkSurfaceKHR` or the native window.

## Related Specs

- [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)

## Related ADRs

- [ADR-0002](0002-presentation-rendertarget-unification.md) — establishes
  `Presentation`/`VkSwapchainKHR` recreation on resize, which this ADR
  refines by distinguishing resize from destroy/recreate.
- [ADR-0003](0003-resource-rendertarget-ownership-model.md) — the general
  RHI resource/`RenderTarget` ownership model this ADR's `VkSurfaceKHR`/
  `VkSwapchainKHR` lifetime rules sit alongside.
- [ADR-0005](0005-platform-module-multi-os-windowing.md) — as amended,
  establishes the Vulkan WSI boundary this ADR's "Vulkan WSI may
  create/destroy `VkSurfaceKHR` but never the native window" rule
  depends on.
- [ADR-0011](0011-native-window-handle-representation.md) — defines the
  `NativeWindowHandle` this ADR's ownership rules govern access to.
- [ADR-0012](0012-application-lifecycle-and-event-model.md) — defines
  the `SurfaceCreated`/`SurfaceDestroyed` events this ADR's critical rule
  depends on.
