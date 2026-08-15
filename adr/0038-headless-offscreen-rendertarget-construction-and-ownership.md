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
Neither document fixes a concrete construction API, ownership type, or
how the non-owning, frame-scoped, move-only `RenderTarget` contract
[ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
already fixed for the windowed case is vended without a `Presentation`
object to vend it.

**What this ADR's own type must not be, verified against the actual
implementation, not assumed:** the existing `Texture` type
(`src/rhi/include/atlantis/rhi/texture.h`) is depth-only **at the type
level**, not merely by convention — its `format()` accessor returns
`DepthFormat`, a distinct type from `RenderTarget::format()`'s `Format`,
and `TextureCreateParams` (`src/rhi/include/atlantis/rhi/types.h`) has no
field capable of expressing a color format or a color-attachment/
transfer-source usage. An earlier draft of this ADR left ambiguous
whether the new offscreen color resource reuses `Texture`; it cannot,
without reopening
[ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s
already-`Accepted` decision to scope `Texture` "exclusively as a depth
attachment... this round." This ADR resolves that ambiguity explicitly
(see Decision).

A second question this ADR must resolve explicitly, also left ambiguous
in an earlier draft: **how `OffscreenTarget` (a long-lived owner) relates
to the `RenderTarget` values it vends (each a short-lived, frame-scoped
borrow)** — the existing precedent (`VulkanPresentation` owning the
swapchain, vending non-owning `VulkanRenderTarget` borrows per acquire —
`src/vulkan_backend/src/vulkan_render_target.h`'s own header comment:
"Non-owning: the swapchain image... belong[s] to the `VulkanPresentation`
that vended this object") is the model this ADR follows, made explicit
below rather than left for an implementer to infer.

A third question: whether this decision forces an early, general GPU
memory allocator strategy.
[ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md) already
resolved [ADR-0015](0015-vulkan-memory-allocation-deferred.md)'s named
GPU-memory-allocation blocker for `Buffer`/`Texture`, with an explicit
migration boundary; this ADR must state plainly whether the new
offscreen color resource this decision introduces falls under that same
policy, rather than leaving the relationship implicit.

## Decision

**A new RHI type, `OffscreenTarget`, is introduced as the headless
counterpart to `Presentation` — scoped to exactly one owned color
resource, nothing else:**

- `Device` gains a creation method (exact name left to the Plan, e.g.
  `createOffscreenTarget(OffscreenTargetCreateParams)`) returning
  `atlantis::Result<std::unique_ptr<OffscreenTarget>, ResourceCreateError>`
  (or an equivalent error type — exact naming left to the Plan, matching
  this codebase's existing per-resource-kind error-enum precedent, e.g.
  `BufferCreateError`/`TextureCreateError`). Creation parameters are
  expressed in RHI-level terms only: a fixed extent and a fixed color
  `atlantis::rhi::Format` (see "Format reuse" below), both
  **caller-specified at construction and never changed for that
  instance's lifetime** — there is no resize/recreation concept for an
  offscreen target, unlike `Presentation`'s swapchain, because nothing
  analogous to a window being resized by the user exists headlessly.
- `OffscreenTarget` owns exactly one color image (its view, and its
  backing device memory) for its entire lifetime, constructed once and
  destroyed once — no swapchain, no image count beyond one, no present
  mode, no acquire-complete semaphore pool of the kind `Presentation`
  needs to avoid racing a display engine. Nothing races an offscreen
  image's availability the way a display engine races a swapchain image,
  so `OffscreenTarget` needs no equivalent synchronization machinery
  beyond what `Device::submit()`'s existing single-frame-in-flight
  fence-wait already provides.

**Ownership/lifetime split — `OffscreenTarget` (long-lived owner) vs. its
vended `RenderTarget` (short-lived, frame-scoped borrow):**

- `OffscreenTarget` vends a `RenderTarget` borrow via a method
  conceptually parallel to `Presentation::acquireNextTarget()` (exact
  name left to the Plan), returning
  `atlantis::Result<std::unique_ptr<RenderTarget>, Err>` — the same
  `unique_ptr`-held shape `Presentation::acquireNextTarget()`/`present()`
  already use (`src/rhi/include/atlantis/rhi/presentation.h`), not a
  value type — but with a **two-outcome**, not three-outcome, result:
  `Err(...)` for a genuine unrecoverable failure (e.g. a prior borrow was
  never consumed — a precondition violation surfaced as a checked error
  rather than silently permitted), or `Ok(...)` with a non-null
  `RenderTarget` otherwise. **There is no zero-extent
  `Ok(nullptr)` case** — unlike a window, an offscreen target's extent is
  fixed at construction and can never become `{0, 0}` mid-lifetime, so
  `Presentation::acquireNextTarget()`'s tri-state outcome
  (`src/rhi/include/atlantis/rhi/presentation.h`'s own documented
  `Err`/`Ok(nullptr)`/`Ok(non-null)` contract) would carry a permanently-
  unreachable branch here; a two-outcome result is not a reduced
  contract, it is the correctly-sized one for an origin that structurally
  cannot produce that case.
- The vended `RenderTarget` value is **exactly** the abstract type
  [ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
  already defines (`atlantis::rhi::RenderTarget` — pure-virtual
  `extent()`/`format()`, no other public member). No field, method, or
  behavior is added to `RenderTarget`'s public interface by this
  decision; only a **second concrete implementation** of it is
  introduced (see "Vulkan Backend implementation shape" below), exactly
  as [ADR-0002](0002-presentation-rendertarget-unification.md)
  anticipated. The opaque acquire-complete signal `RenderTarget` carries
  internally for `Device::submit()`'s wait requirement is trivial/
  pre-satisfied for an `OffscreenTarget`-vended value (there is no
  image-acquisition wait to perform) — `Device::submit()` requires no
  headless-specific branch to handle this; a signal that is
  always-already-satisfied is a valid value of the same opaque
  mechanism, not a new case `Device` must recognize.
- **`OffscreenTarget` has no `present()` counterpart.** The borrow it
  vends is instead consumed by the readback operation
  [ADR-0040](0040-gpu-to-cpu-readback-rhi-capability.md) defines — this
  ADR fixes only that the borrow must be consumed by exactly one such
  call before a new one may be acquired (the same "one acquire, one
  consuming call" discipline `Presentation` already enforces between
  `acquireNextTarget()` and `present()`); ADR-0040 fixes what that
  consuming call actually does.
- **The same `OffscreenTarget` instance may be acquired-and-consumed
  more than once across its lifetime** — e.g. once per test case in a
  future Image Regression Testing harness. Each cycle is independent and
  follows Phase 1's existing single-frame-in-flight discipline
  ([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md));
  no new frame-in-flight or pooling semantics are introduced.
- **Destruction preconditions mirror `Presentation`'s exactly**
  ([ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)):
  a caller must not destroy `OffscreenTarget` (or the `Device` it was
  constructed from) while a `RenderTarget` it vended has been acquired
  but not yet consumed, or while a submission that `RenderTarget`
  participated in has not yet completed. Same lifetime-precondition tier,
  same `Device::waitIdle()`-satisfies-it mechanism, no new concept.

**Vulkan Backend implementation shape (named explicitly, resolving this
ADR's own earlier ambiguity):**

- A new, Vulkan-Backend-private, **owning** type — e.g.
  `VulkanOffscreenTarget` (exact name left to the Plan) — implements the
  new RHI `OffscreenTarget` interface. It owns the color `VkImage`, its
  `VkDeviceMemory` (see "Allocation" below), and its `VkImageView` for
  its entire lifetime, following exactly the same "one owning Vulkan
  Backend type per public RHI resource type" shape `VulkanBuffer`/
  `VulkanTexture` already establish for `Buffer`/`Texture`
  ([ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)).
- A second, distinct, Vulkan-Backend-private, **non-owning** type — e.g.
  `VulkanOffscreenRenderTarget` (exact name left to the Plan) —
  implements `atlantis::rhi::RenderTarget`, vended by the owning
  `VulkanOffscreenTarget` on each acquire cycle. It borrows the image/
  view from the owning `VulkanOffscreenTarget` exactly as the existing
  `VulkanRenderTarget` (`src/vulkan_backend/src/vulkan_render_target.h`)
  already borrows from `VulkanPresentation` — **this is a deliberate,
  direct mirror of that existing, working pattern, not a new ownership
  shape**: `VulkanOffscreenTarget` is to `VulkanOffscreenRenderTarget`
  exactly what `VulkanPresentation` is to `VulkanRenderTarget` today.
  `VulkanOffscreenRenderTarget` is a **second, distinct concrete
  implementation of `atlantis::rhi::RenderTarget`**, alongside the
  existing `VulkanRenderTarget` — both implement the same unchanged
  public interface (`extent()`/`format()` only), so RenderGraph/Renderer
  cannot and do not need to distinguish them, exactly as
  [ADR-0002](0002-presentation-rendertarget-unification.md) requires.

**Format reuse — `atlantis::rhi::Format`, explicitly re-scoped:**

- `OffscreenTarget`'s color image uses the existing
  `atlantis::rhi::Format` enum (`Bgra8Unorm`/`Bgra8Srgb`/`Rgba8Unorm`/
  `Rgba8Srgb`) as its creation-time format parameter — the same enum
  `Presentation::metadata()` already exposes for the swapchain's
  currently-selected surface format. This ADR **explicitly re-scopes**
  `Format`'s documented role: its current header comment
  (`src/rhi/include/atlantis/rhi/types.h`) reads "Describes only the
  currently-selected swapchain surface format for this spec's read-only
  metadata query — not a general resource-format system." This ADR
  widens `Format`'s role to additionally serve as an offscreen color
  resource's creation-time format parameter — **this is still not a
  general resource-format system**: `Format`'s four values remain
  color-attachment-shaped values only; `Texture`'s separate
  `DepthFormat` enum is untouched, and this decision does not give
  `Buffer`, `Texture`, or `Pipeline` any new format concept. The
  implementation that resumes from this ADR must update `types.h`'s
  comment on `Format` to reflect this widened (but still narrow, still
  not general) scope — leaving the old, now-inaccurate "only... swapchain
  surface format" comment unedited alongside code that also uses `Format`
  for a non-swapchain purpose would itself violate this codebase's
  "update or remove a comment in the same change that makes it stale"
  rule (AGENTS.md, Documentation and code comments).
- **Depth attachment handling is unchanged and out of this decision's
  scope entirely.** A headless verification composition constructs its
  depth `Texture` via the existing, `Accepted`
  [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  `Device::createTexture()` path — the exact same mechanism Spec 0007's
  windowed composition already uses — sized to `OffscreenTarget`'s fixed
  extent. `OffscreenTarget` does not own, create, or reference a depth
  resource; `Texture`/`DepthFormat` are entirely untouched by this ADR.

**Allocation: `OffscreenTarget`'s color image is allocated under exactly
[ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s
existing direct, unpooled, Vulkan-Backend-private policy** — its own
individual `vkAllocateMemory` call, its own individual `vkFreeMemory`
call at destruction, no suballocation, no shared `VkDeviceMemory` block —
mirroring exactly how `VulkanBuffer`/`VulkanTexture` already allocate.
This is a **narrow, explicit extension of ADR-0023's already-`Accepted`
scope to one more resource kind**, not a new allocation decision and not
a resolution of [ADR-0015](0015-vulkan-memory-allocation-deferred.md)'s
general deferral beyond what ADR-0023 already resolved — this ADR
introduces no VMA dependency, no hand-rolled suballocator, and no RHI/
Renderer signature shaped around any particular allocation strategy. A
future Image Regression Testing harness's own resource count (repeated
`OffscreenTarget`s across many test cases, if not reused) is exactly the
kind of consumer ADR-0023's migration boundary already names as a future
trigger to revisit — not a reason to decide differently now.

## Consequences

### Positive

- Realizes [ADR-0002](0002-presentation-rendertarget-unification.md)'s
  originally-anticipated headless path concretely, for the first time,
  without adding any capability-query or origin flag to `RenderTarget`
  itself.
- Resolves the type-level ambiguity an earlier draft left open by
  explicitly ruling out `Texture` reuse (verified type-incompatible) and
  explicitly naming the two-class (owning `VulkanOffscreenTarget` +
  non-owning `VulkanOffscreenRenderTarget`) Vulkan Backend shape,
  directly mirroring the existing, working `VulkanPresentation`/
  `VulkanRenderTarget` pattern rather than inventing a new one.
- Reuses [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s
  allocation policy verbatim rather than inventing a second one.
- The two-outcome (not three-outcome) acquire result is honest about
  headless's actually-simpler contract rather than forcing a windowed
  shape that would carry a dead branch.
- Zero change to the depth `Texture` ownership model.

### Negative / Trade-offs

- `OffscreenTarget` and `Presentation` are two distinct RHI types with
  parallel but not identical acquire contracts (three-outcome vs.
  two-outcome) — a caller writing origin-agnostic code (if one ever
  wants to) must still branch on which vendor it is holding, even though
  the `RenderTarget` values themselves are indistinguishable. Accepted
  because nothing in this repository's approved architecture requires a
  single unified acquire call across both origins — only the vended
  `RenderTarget` and the Renderer/RenderGraph path consuming it need to
  be unified, and this decision preserves exactly that.
- A second concrete `atlantis::rhi::RenderTarget` implementation
  (`VulkanOffscreenRenderTarget`, alongside the existing
  `VulkanRenderTarget`) is new Vulkan Backend surface that must be kept
  behaviorally consistent with the first wherever the two are
  conceptually parallel (destruction preconditions, single-borrow
  discipline) — a maintenance cost this decision accepts as the price of
  not forcing headless through `Presentation`'s swapchain-shaped
  contract.
- `Format`'s documented scope widens from "swapchain metadata query
  only" to "also an offscreen color creation parameter" — a real,
  if narrow and explicitly-justified, re-scoping of an existing type's
  stated purpose that the implementation must carry through to that
  type's own doc comment, not just to this ADR's text.

## Alternatives Considered

- **Reuse `Presentation` itself for headless, gated by a construction-
  time flag or a null native-surface-handle.** Rejected:
  [ADR-0002](0002-presentation-rendertarget-unification.md) already
  states "`Presentation` exists solely to manage swapchain-specific
  concerns... and is never seen by Renderer," and headless explicitly has
  "no `Presentation` object... involved" — a flag-gated `Presentation`
  would reintroduce exactly the kind of origin-observability ADR-0002
  forbids, just moved one level up, and would carry swapchain-shaped
  concepts (present modes, resize) that make no sense for a fixed-extent
  offscreen target.
- **Widen the existing `Texture` type to support a color, transfer-
  source-capable usage, instead of introducing a new owning `RenderTarget`
  implementation.** Rejected: `Texture::format()`'s return type
  (`DepthFormat`) and `TextureCreateParams`' single `DepthFormat` field
  are a type-level, not conventional, restriction —
  [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  scoped `Texture` "exclusively as a depth attachment... this round" as
  an explicit, reviewed, `Accepted` decision; widening it now would
  silently reopen that decision rather than extend it, and would also
  require every existing `Texture` consumer (the depth-attachment path,
  unchanged by this spec) to reason about a new, unrelated usage kind.
  The existing precedent for "an owning long-lived resource vending
  short-lived, non-owning `RenderTarget` borrows" — `Presentation`/
  `VulkanRenderTarget` — already fits this need exactly; reusing that
  shape (this ADR's actual Decision) is strictly smaller than widening
  `Texture`.
- **Give `OffscreenTarget` the same tri-state acquire outcome as
  `Presentation`, including a zero-extent case, for surface-level
  symmetry.** Rejected: an offscreen target's extent cannot become
  `{0, 0}` after construction — there is no window to minimize — so the
  branch would be permanently dead code every caller would still have to
  handle.
- **Have `OffscreenTarget` also own the depth resource (bundle color +
  depth into one owning type).** Rejected: breaks symmetry with the
  already-`Accepted`, already-implemented windowed contract (Spec 0007
  keeps depth `Texture` caller-owned, entirely separate from
  `Presentation`), reusing none of that existing, working lifecycle code,
  and gains nothing — depth's resize-driven recreation logic doesn't even
  apply to a fixed-extent offscreen target.
- **Decide a general GPU memory suballocation strategy (VMA or
  hand-rolled) now.** Rejected: this spec's own scope creates at most a
  small, fixed number of offscreen color allocations (mirroring
  [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s own
  "genuinely narrow claim" reasoning) — reusing ADR-0023's existing
  migration boundary, unmodified, is the correct place for that future
  decision to land once a real need materializes, not here.
