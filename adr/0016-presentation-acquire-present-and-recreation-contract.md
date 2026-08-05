# ADR 0016: Presentation Construction, Recreation, and Destruction Lifecycle Contract

- **Status:** Accepted
- **Date:** 2026-08-06
- **Deciders:** _Human approval confirmed 2026-08-06_
- **Related Spec:** [specs/0003-rhi-vulkan-windowed-foundation.md](../specs/0003-rhi-vulkan-windowed-foundation.md)

## Context

An earlier draft of this ADR defined `acquireNextTarget()` as a real,
implemented, and independently-verifiable operation — vending a
`RenderTarget` and lazily recreating the swapchain on the next call after
a resize — while explicitly deferring only `present()` and the image
transition it requires.

That draft does not hold up: `vkAcquireNextImageKHR` does not record a
command buffer, but it **does** take ownership of a swapchain image and
incur a synchronization obligation (a semaphore/fence the caller must
eventually wait on before reusing or destroying that image). A swapchain
has a small, fixed number of images (typically 2–3). Calling
`acquireNextTarget()` repeatedly without ever presenting exhausts that
pool — later acquires block or fail — and destroying a `Presentation`
while an image remains acquired-but-unpresented is exactly the kind of
undefined-synchronization shortcut this repository's ownership rules
([AGENTS.md](../AGENTS.md) "RAII by default," "borrowed access never
implies ownership transfer") exist to prevent. A verification story built
on repeated un-presented acquires is not a safe substitute for the real
acquire/present contract — it is a different, quietly-invented contract
that would need to be undone.

Per [AGENTS.md](../AGENTS.md), RenderGraph is the mandatory, sole path for
GPU frame work, and no subsystem may create an alternate acquire → present
path — bare, "empty," or otherwise — outside it. The frame-level contract
(acquire's public shape, `RenderTarget` frame ownership, acquire-complete
synchronization, graph-to-present synchronization, image layout handoff,
and `present()`'s own shape and implementation) is therefore **not**
decided piecemeal here. This ADR instead fixes exactly what `Presentation`
*can* do safely without ever handing out a swapchain image: construction,
swapchain (re)creation and destruction, resize- and zero-extent-driven
recreation bookkeeping, and swapchain metadata queries.

## Decision

`Presentation`'s contract in this phase covers construction, recreation,
and destruction only. It never acquires, vends, or tracks a swapchain
image, and therefore never incurs a per-image synchronization obligation.

- **Construction** (via [ADR-0014](0014-rhi-device-presentation-construction-boundary.md)'s
  factory API, from a `Device` and a `NativeWindowHandle`): creates the
  `VkSurfaceKHR` through the private WSI boundary
  ([ADR-0005](0005-platform-module-multi-os-windowing.md), as amended)
  and constructs the `Presentation` object. **It does not create a
  swapchain.** The object starts in a "recreation needed" state,
  regardless of the initial framebuffer extent — swapchain creation and
  swapchain recreation are the same code path (below), not two.
- **`notifyResized(WindowExtent)`** — the caller (this spec's
  verification demo; Runtime, once it exists) calls this whenever it
  observes a `WindowResize` `PlatformEvent`. Updates `Presentation`'s
  tracked extent and marks "recreation needed." **Makes no Vulkan call.**
- **`recreateIfNeeded()`** — the only operation that ever creates,
  recreates, or destroys the `VkSwapchainKHR`. A pure lifecycle
  operation: it does not acquire, return, or reference a swapchain image
  or a `RenderTarget`, and carries no frame-level semantics whatsoever.
  Behavior, in order:
  1. **If the tracked framebuffer extent is `{0, 0}`, return
     immediately, without calling any Vulkan swapchain-creation,
     -recreation, or -destruction function.** This is the same
     structural guarantee the earlier draft applied only to
     "recreation" — here it applies uniformly to *first* creation too,
     since both go through this one path. If a swapchain already exists
     from a prior non-zero extent, it is left untouched; this phase does
     not decide whether a future revision should eagerly release it
     while minimized (not needed without acquire — see Risks).
  2. Otherwise, if "recreation needed" is not currently flagged, this
     call is a no-op (the swapchain already matches the tracked extent).
  3. Otherwise (non-zero extent, recreation flagged, including the
     first call after construction): destroy the previous swapchain, if
     any, then create a new one at the current tracked extent. Query and
     cache its image count, format, and extent for the metadata queries
     below. Clear the "recreation needed" flag. Every `VkResult` is
     checked; a genuine failure is surfaced through `atlantis::Result`,
     never discarded, and leaves "recreation needed" set so the next
     call retries.
  4. `notifyResized` reinforces the zero-extent rule by construction: a
     resize down to zero simply leaves "recreation needed" set (or
     clears it without acting, per step 1) until a later non-zero resize
     arrives — there is no code path from a zero extent to a Vulkan
     call, at any point.
- **Swapchain metadata queries** (e.g. current image count, format,
  extent) — read-only, reflect the most recently successfully (re)created
  swapchain. **No image handle, no `RenderTarget`, and no per-image
  resource of any kind is ever handed to the caller by these queries or
  by anything else in this contract.**
- **Destruction** — destroys the swapchain (if one exists) and the
  `VkSurfaceKHR`, then any Vulkan-Backend-internal WSI state, at any
  point in `Presentation`'s lifetime. Because no image is ever acquired
  or vended under this contract, **there is no outstanding
  acquired-image synchronization concern to resolve at destruction
  time** — this directly removes the hazard the earlier draft's
  verification story ran into.
- **Android surface destruction remains explicitly out of
  `Presentation`'s own API**, unchanged from the earlier draft: per
  [ADR-0013](0013-platform-window-ownership-and-lifetime.md), a new
  `SurfaceCreated` handle is never guaranteed to reference the same
  underlying object. The caller is responsible for destroying its
  `Presentation` instance entirely on `SurfaceDestroyed` and constructing
  a brand-new one on a later `SurfaceCreated`. `Presentation` has no
  "recover from a destroyed surface" method. (Not implemented or tested
  by Spec 0003, since Android isn't implemented — the principle is
  preserved for when it is.)

## Deferred as One Bundle to a Future RenderGraph Specification

The following are **not** decided here, individually or otherwise, and
are recorded together because they are mutually interdependent — fixing
any one without the others risks a contract the future RenderGraph spec
would have to partially undo:

- **The public API for acquiring a frame** (an `acquireNextTarget()`-
  shaped operation or otherwise) — not sketched here, not even
  illustratively, to avoid prematurely defining a future API this ADR
  has no way to validate against a real consumer.
- **`RenderTarget`'s frame-ownership shape** — whether/how a future
  acquire vends one, its lifetime, and whether `Presentation` retains any
  bookkeeping about "currently acquired" images.
- **Acquire-complete synchronization** — semaphore/fence ownership,
  creation, and destruction.
- **Graph-to-present synchronization** — how a future RenderGraph's
  completed work signals readiness for `present()`.
- **Image layout handoff** — how a future consumer learns the current
  layout of a swapchain image in order to record a correct transition.
- **`present()`'s own API shape and Vulkan Backend implementation** —
  the `vkQueuePresentKHR` call itself and its out-of-date/suboptimal
  frame-completion handling.

**The only path this ADR anticipates for ever reaching a presented frame
is `acquire → graph-recorded work/synchronization → present`,** decided
in full by that future specification — never a bare acquire-then-present,
never an empty/no-render present, and never any other bypass of
RenderGraph.

## Consequences

### Positive

- Eliminates the acquire-without-present hazard entirely: `Presentation`
  never hands out an image or incurs a per-image synchronization
  obligation under this contract, so there is no exhaustion risk and no
  undefined-synchronization destruction scenario to reason about.
- `recreateIfNeeded()` and the metadata queries are fully idempotent and
  safe to call any number of times, in any order relative to zero-extent,
  resize, and destruction — a genuinely safe, repeatable verification
  surface, not a careful-not-to-call-it-too-often one.
- Still operationalizes [ADR-0002](0002-presentation-rendertarget-unification.md),
  [ADR-0003](0003-resource-rendertarget-ownership-model.md), and
  [ADR-0013](0013-platform-window-ownership-and-lifetime.md)'s existing
  principles (swapchain-concern ownership, no hidden caching, the
  zero-extent and Android-teardown rules) without changing any of their
  conclusions.
- Bundling every frame-level detail into one future deferral, rather than
  fixing some now, avoids compounding a partially-wrong early decision
  that the future RenderGraph spec would have to unwind.

### Negative / Trade-offs

- Spec 0003's own verification cannot demonstrate an actual visible frame
  or prove a swapchain image is genuinely presentable — only that the
  swapchain *object* lifecycle (creation, recreation-at-new-extent,
  destruction) is correct. End-to-end proof that something appears on
  screen waits for the future RenderGraph spec.
- The future RenderGraph spec must design the entire acquire/present/
  synchronization package in one pass rather than incrementally — a
  larger single decision surface for that spec, accepted here as the
  cost of not inventing pieces of it now.
- `recreateIfNeeded()` is an additional, non-obvious entry point (versus
  the earlier draft's "recreation happens automatically inside acquire")
  that a future RenderGraph-driven implementation will need to either
  keep calling explicitly or fold into its own acquire path — a Plan- or
  future-ADR-stage integration detail, not decided here.

## Alternatives Considered

- **Keep `acquireNextTarget()`, but cap verification to fewer repetitions
  than the swapchain's image count, destroying and reconstructing
  `Presentation` between test iterations to "reset."** Rejected: still
  leaves at least one genuinely acquired-but-unpresented image at the
  moment of each destruction — exactly the undefined-synchronization
  shortcut this ADR exists to avoid — and is brittle, since it depends on
  an image count not robustly known before creation.
- **Acquire, then call `vkQueuePresentKHR` immediately with no
  transition, accepting the resulting validation warning.** Rejected
  outright: [AGENTS.md](../AGENTS.md) treats a validation warning as a
  build/test failure, not acceptable output, and this is precisely the
  "acquire → direct present" RenderGraph bypass this task explicitly
  forbids.
- **Expose a "dummy" acquire returning a fabricated `RenderTarget` not
  backed by a real swapchain image, purely to test the recreation
  trigger.** Rejected: this invents a parallel, fictitious API shape now
  that would still need reconciling with the real future
  RenderGraph-driven acquire, and risks becoming a de facto frame path in
  its own right — the same "quietly becomes the first path" failure mode
  already rejected for the bare-barrier alternative.
- **Keep "the next acquire" as the recreation trigger, but never actually
  call acquire in verification — rely on code inspection alone.**
  Rejected: leaves "resize-driven recreation" practically unverifiable,
  and still requires sketching an acquire-shaped method now.
  `recreateIfNeeded()` gives an equally lazy, but genuinely frame-free and
  actually testable, trigger instead — without pre-defining any part of
  the future acquire API.
- **Eagerly destroy the swapchain the instant extent becomes zero**
  (inside `notifyResized` or `recreateIfNeeded`), rather than leaving a
  prior swapchain untouched until a later non-zero resize. Rejected for
  now: without acquire, there is no active use of that swapchain to stop;
  eagerly tearing it down would be optimization with no current benefit,
  and is left open (see Risks in Spec 0003) rather than decided here.
