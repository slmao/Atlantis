# ADR 0038: Headless Offscreen RenderTarget Construction and Ownership

- **Status:** Proposed
- **Date:** 2026-08-15
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md) (`In Review`)

## Context

[ADR-0002](0002-presentation-rendertarget-unification.md) fixed, from the
start of this repository's architecture, that `RenderTarget` must serve
two origins — windowed (`Presentation`-vended) and headless (an
"explicitly-requested offscreen target... no `Presentation` involved") —
without Renderer or RenderGraph ever being able to observe which one
produced a given value. [resource_lifetime.md](../docs/architecture/resource_lifetime.md)'s
Headless path section restates the same shape at a principle level: "The
caller... explicitly creates an offscreen `RenderTarget` through RHI and
owns it for as long as needed... No `Presentation` object is involved."
Neither document fixes a concrete construction API, an ownership type, or
how the non-owning, frame-scoped, move-only `RenderTarget` contract
[ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
already fixed for the windowed case is vended without a `Presentation`
object to vend it.

[specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md)'s
acceptance target — draw the same Minimal Renderer mesh headlessly and
read its pixels back — requires exactly this construction path to exist
for the first time. [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)
already resolved [ADR-0015](0015-vulkan-memory-allocation-deferred.md)'s
named GPU-memory-allocation blocker for `Buffer`/`Texture`, with an
explicit migration boundary; this ADR must state plainly whether the new
offscreen color resource this decision introduces falls under that same
policy, rather than leaving the relationship implicit.

A second, narrower question: Spec 0007's depth `Texture` is already
caller-owned, constructed via `Device::createTexture()`, and lives outside
`Presentation` entirely — nothing about its ownership model is
windowed-specific. This ADR must decide whether headless needs its own,
different depth-resource story, or can reuse Spec 0007's mechanism
verbatim.

## Decision

**A new RHI type, `OffscreenTarget`, is introduced as the headless
counterpart to `Presentation` — scoped to exactly one owned color
resource, nothing else:**

- `Device` gains a creation method (exact name left to the Plan, e.g.
  `createOffscreenTarget(OffscreenTargetCreateParams)`) returning
  `atlantis::Result<std::unique_ptr<OffscreenTarget>, ResourceCreateError>`
  (or an equivalent error type — exact naming left to the Plan). Creation
  parameters are expressed in RHI-level terms only: a fixed extent and a
  fixed color format, both **caller-specified at construction and never
  changed for that instance's lifetime** — there is no resize/recreation
  concept for an offscreen target, unlike `Presentation`'s swapchain,
  because nothing analogous to a window being resized by the user exists
  headlessly.
- `OffscreenTarget` owns exactly one color image (its view, and its
  backing device memory) for its entire lifetime, constructed once and
  destroyed once — no swapchain, no image count beyond one, no present
  mode, no acquire-complete semaphore pool of the kind `Presentation`
  needs to avoid racing a display engine. Nothing races an offscreen
  image's availability the way a display engine races a swapchain image,
  so `OffscreenTarget` needs no equivalent synchronization machinery
  beyond what `Device::submit()`'s existing single-frame-in-flight
  fence-wait already provides.
- `OffscreenTarget` vends a `RenderTarget` borrow via a method conceptually
  parallel to `Presentation::acquireNextTarget()` (exact name left to the
  Plan) — but with a **two-outcome**, not three-outcome, result:
  `Err(...)` for a genuine unrecoverable failure (e.g. a prior borrow was
  never consumed — a precondition violation surfaced as a checked error
  rather than silently permitted), or `Ok(RenderTarget{...})` otherwise.
  **There is no `Ok(std::nullopt)` zero-extent case** — unlike a window, an
  offscreen target's extent is fixed at construction and can never become
  `{0, 0}` mid-lifetime, so
  [ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)'s
  tri-state outcome would carry a permanently-unreachable branch here; a
  two-outcome result is not a reduced contract, it is the correctly-sized
  one for an origin that structurally cannot produce that case.
- The vended `RenderTarget` value is **exactly** the type
  [ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
  already defined — non-owning, frame-scoped, move-only, write-only within
  a single graph's declared usages. No field, method, or behavior is added
  to `RenderTarget` itself by this decision; only a second vendor of the
  same type is introduced, exactly as
  [ADR-0002](0002-presentation-rendertarget-unification.md) anticipated.
  The opaque acquire-complete signal `RenderTarget` carries internally for
  `Device::submit()`'s `WaitOn` parameter is trivial/pre-satisfied for an
  `OffscreenTarget`-vended value (there is no image-acquisition wait to
  perform) — `Device::submit()` requires no headless-specific branch to
  handle this; a signal that is always-already-satisfied is a valid value
  of the same opaque type, not a new case `Device` must recognize.
- **`OffscreenTarget` has no `present()` counterpart.** The borrow it
  vends is instead consumed by the readback operation
  [ADR-0040](0040-gpu-to-cpu-readback-rhi-capability.md) defines — this
  ADR fixes only that the borrow must be consumed by exactly one such
  call before a new one may be acquired (the same "one acquire, one
  consuming call" discipline `Presentation` already enforces between
  `acquireNextTarget()` and `present()`); ADR-0040 fixes what that
  consuming call actually does.
- **The same `OffscreenTarget` instance may be acquired-and-consumed more
  than once across its lifetime** — e.g. once per test case in a future
  Image Regression Testing harness. Each cycle is independent and follows
  Phase 1's existing single-frame-in-flight discipline
  ([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md));
  no new frame-in-flight or pooling semantics are introduced.
- **Destruction preconditions mirror `Presentation`'s exactly**
  ([ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)):
  a caller must not destroy `OffscreenTarget` (or the `Device` it was
  constructed from) while a `RenderTarget` it vended has been acquired
  but not yet consumed, or while a submission that `RenderTarget`
  participated in has not yet completed. Same lifetime-precondition tier,
  same `Device::waitIdle()`-satisfies-it mechanism, no new concept.
- **Allocation: `OffscreenTarget`'s color image is allocated under
  exactly [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s
  existing direct, unpooled, Vulkan-Backend-private policy** — its own
  individual `vkAllocateMemory` call, its own individual `vkFreeMemory`
  call at destruction, no suballocation, no shared `VkDeviceMemory` block.
  This is a **narrow, explicit extension of ADR-0023's already-Accepted
  scope to one more resource kind**, not a new allocation decision and not
  a resolution of [ADR-0015](0015-vulkan-memory-allocation-deferred.md)'s
  general deferral beyond what ADR-0023 already resolved — this ADR
  introduces no VMA dependency, no hand-rolled suballocator, and no
  RHI/Renderer signature shaped around any particular allocation strategy,
  consistent with ADR-0023's own migration-boundary language. A future
  Image Regression Testing harness's own resource count (repeated
  `OffscreenTarget`s across many test cases, if not reused) is exactly the
  kind of consumer ADR-0023's migration boundary already names as a future
  trigger to revisit — not a reason to decide differently now.
- **Depth attachment handling is unchanged and out of this decision's
  scope entirely.** A headless verification composition constructs its
  depth `Texture` via the existing, `Accepted`
  [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  `Device::createTexture()` path — the exact same mechanism Spec 0007's
  windowed composition already uses — sized to `OffscreenTarget`'s fixed
  extent. `OffscreenTarget` does not own, create, or reference a depth
  resource; there is nothing windowed-specific about the existing depth
  `Texture` ownership model for this decision to change.

## Consequences

### Positive

- Realizes [ADR-0002](0002-presentation-rendertarget-unification.md)'s
  originally-anticipated headless path concretely, for the first time,
  without adding any capability-query or origin flag to `RenderTarget`
  itself — the unification promise is kept exactly as designed.
- Reuses [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s
  allocation policy verbatim rather than inventing a second one, keeping
  the GPU-memory-allocation question answered in exactly one place.
- The two-outcome (not three-outcome) acquire result is honest about
  headless's actually-simpler contract rather than forcing a windowed
  shape that would carry a dead branch.
- Zero change to the depth `Texture` ownership model — one fewer decision
  this ADR has to make, and one fewer place headless and windowed code
  paths could silently diverge.

### Negative / Trade-offs

- `OffscreenTarget` and `Presentation` are two distinct RHI types with
  parallel but not identical acquire contracts (three-outcome vs.
  two-outcome) — a caller writing origin-agnostic code (if one ever
  wants to) must still branch on which vendor it is holding, even though
  the `RenderTarget` values themselves are indistinguishable. This is
  accepted because nothing in this repository's approved architecture
  requires a single unified acquire call across both origins — only the
  vended `RenderTarget` and the Renderer/RenderGraph path consuming it
  need to be unified, and this decision preserves exactly that.
- Introducing a second acquire-vending type is new RHI public surface
  that must be kept in sync with `Presentation`'s contract wherever the
  two are conceptually parallel (destruction preconditions, single-borrow
  discipline) — a future spec changing one must consider whether the
  other needs a matching change, a maintenance cost this decision accepts
  as the price of not forcing headless through `Presentation`'s
  swapchain-shaped contract.

## Alternatives Considered

- **Reuse `Presentation` itself for headless, gated by a construction-time
  flag or a null native-surface-handle.** Rejected:
  [ADR-0002](0002-presentation-rendertarget-unification.md) already
  states "`Presentation` exists solely to manage swapchain-specific
  concerns... and is never seen by Renderer," and headless explicitly has
  "no `Presentation` object... involved" — a flag-gated `Presentation`
  would reintroduce exactly the kind of origin-observability ADR-0002
  forbids, just moved one level up from `RenderTarget` to `Presentation`
  itself, and would carry swapchain-shaped concepts (present modes,
  resize) that make no sense for a fixed-extent offscreen target.
- **Give `OffscreenTarget` the same tri-state acquire outcome as
  `Presentation`, including `Ok(std::nullopt)` for symmetry.** Rejected:
  an offscreen target's extent cannot become `{0, 0}` after construction —
  there is no window to minimize — so the branch would be permanently
  dead code every caller would still have to handle, contradicting this
  codebase's preference for a contract that reflects what can actually
  happen over one that reflects surface-level symmetry with a different
  origin's genuinely different failure modes.
- **Have `OffscreenTarget` also own the depth resource (bundle color +
  depth into one owning type).** Rejected: this breaks symmetry with the
  already-`Accepted`, already-implemented windowed contract (Spec 0007
  keeps depth `Texture` caller-owned, entirely separate from
  `Presentation`), reusing none of that existing, working lifecycle code,
  and gains nothing — depth's resize-driven recreation logic doesn't even
  apply to a fixed-extent offscreen target, so there is no shared-lifecycle
  argument for bundling here that didn't already apply, and was already
  rejected, for the windowed case.
- **Decide a general GPU memory suballocation strategy (VMA or
  hand-rolled) now, since headless plus a future Image Regression Testing
  harness could plausibly create many `OffscreenTarget`s.** Rejected: this
  spec's own scope creates at most a small, fixed number of offscreen
  color allocations (mirroring
  [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s own
  "genuinely narrow claim" reasoning) — no concrete need exists yet:
  reusing ADR-0023's existing migration boundary, unmodified, is the
  correct place for that future decision to land once a real need
  materializes, not here.
