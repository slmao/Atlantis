# ADR 0038: Headless Offscreen RenderTarget Construction and Ownership

- **Status:** Accepted
- **Date:** 2026-08-15
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
  Approval recorded 2026-08-16; see
  [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md)'s
  Human Review Approval note for the full, three-round approval record
  this ADR's Decision — including the round-3 correction separating the
  borrow wrapper's lifetime from `OffscreenTarget`'s own — is part of.
- **Related Spec:** [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md) (`Approved`)

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
  `Err(...)` for a genuine unrecoverable, environmental failure (e.g. a
  Vulkan allocation/device-lost condition, mirroring
  `PresentationError`'s own non-precondition variants), or `Ok(...)`
  with a non-null `RenderTarget` otherwise. **There is no zero-extent
  `Ok(nullptr)` case** — unlike a window, an offscreen target's extent is
  fixed at construction and can never become `{0, 0}` mid-lifetime, so
  `Presentation::acquireNextTarget()`'s tri-state outcome
  (`src/rhi/include/atlantis/rhi/presentation.h`'s own documented
  `Err`/`Ok(nullptr)`/`Ok(non-null)` contract) would carry a permanently-
  unreachable branch here; a two-outcome result is not a reduced
  contract, it is the correctly-sized one for an origin that structurally
  cannot produce that case. **Calling `acquireTarget()` while a
  previously-vended borrow is still outstanding is a guaranteed-
  detectable programmer error** (`ATLANTIS_CHECK`/`ATLANTIS_ASSERT`, per
  [ADR-0009](0009-assertion.md)), not part of this `Result::Err` channel
  — see "Borrow lifetime contract" below for why this is checkable and
  why it is an assertion, not a recoverable error.
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
- **`OffscreenTarget` has no `present()` counterpart, and no other
  explicit "give the borrow back" public method — the borrow ends via
  ordinary RAII.** An earlier draft of this ADR stated the borrow "is
  instead consumed by the readback operation
  [ADR-0040](0040-gpu-to-cpu-readback-rhi-capability.md) defines" — this
  was incorrect and is corrected here:
  [ADR-0040](0040-gpu-to-cpu-readback-rhi-capability.md)'s
  `copyRenderTargetToBuffer(RenderTarget&, Buffer&)` only *borrows* a
  reference to record a command; it neither takes ownership of, nor
  ends, the `RenderTarget` borrow, and defines no consuming call of any
  kind. See "Borrow lifetime contract" below for the actual mechanism.
- **The same `OffscreenTarget` instance may be acquired-and-borrowed
  more than once across its lifetime** — e.g. once per test case in a
  future Image Regression Testing harness. Each cycle is independent and
  follows Phase 1's existing single-frame-in-flight discipline
  ([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md));
  no new frame-in-flight or pooling semantics are introduced.

**Lifetime contract — two distinct, non-equivalent lifetimes, not one.**
This subsection is the full, authoritative answer to "how does a vended
`RenderTarget` borrow end, when may `OffscreenTarget` itself be safely
destroyed, and what may a caller do around both," replacing an earlier
draft's incorrect claim that
[ADR-0040](0040-gpu-to-cpu-readback-rhi-capability.md) would define the
former. **A second-round Human Review of this ADR conflated these two
lifetimes into a single rule; a third-round Human Review found that gap
by checking `Device::submit()`'s actual implementation and
`VulkanPresentation`'s actual destructor — the correction below keeps
them explicitly separate:**

1. **The borrow wrapper** (`std::unique_ptr<RenderTarget>`,
   `VulkanOffscreenRenderTarget`) — a thin, non-owning, per-cycle C++
   handle.
2. **`OffscreenTarget` itself and the Vulkan objects it owns** (the color
   `VkImage`, its `VkDeviceMemory`, its `VkImageView`) — the actual GPU
   resources every borrow, every frame, ultimately points at.

**1. Borrow wrapper lifetime.**

- **Ending the borrow.** A caller ends its current borrow by destroying
  or resetting the `std::unique_ptr<RenderTarget>` `acquireTarget()`
  returned — ordinary RAII, exactly like every other owned-handle type
  in this codebase (`Buffer`/`Texture`/`Pipeline`), and **not** an
  explicit method call. There is no real side effect to trigger
  explicitly here (unlike `Presentation::present()`, which must also
  call `vkQueuePresentKHR`) — `VulkanOffscreenRenderTarget` is non-owning
  and destroying it releases no Vulkan object; its destructor's only
  responsibility is to notify its owning `VulkanOffscreenTarget`
  (privately, not via any public API) that the borrow has ended, clearing
  the "borrow outstanding" tracking state that also backs the
  guaranteed-detectable double-acquire check below. Adding a public
  `release()`/`consume()` method would duplicate what destruction/
  `reset()` already does, for no behavioral gain.
- **Minimum required lifetime: only through `Device::submit()`'s
  return.** The borrow must remain alive from `acquireTarget()`'s return
  until at least the return of the `Device::submit()` call whose
  recorded `CommandList` references it — because
  `Device::submit(std::unique_ptr<CommandList>, const RenderTarget&)`
  itself takes a `const RenderTarget&`
  (`src/rhi/include/atlantis/rhi/device.h`), and because every
  `CommandList` recording call that touches this target (via
  `Renderer::drawFrame()`, `transitionResource()`,
  `copyRenderTargetToBuffer()`) captures/dereferences it during
  recording, which happens before `submit()`. **The borrow does *not*
  need to remain alive through `Device::waitIdle()`, through reading the
  readback `Buffer`'s contents, or until the referencing GPU work has
  actually finished executing.** This reflects how Vulkan command
  recording actually works: `vkCmdCopyImageToBuffer` and every other
  recorded command bake the underlying `VkImage`/`VkBuffer` *handles*
  into the command buffer at record time; the GPU, when it later
  executes those commands, never dereferences the C++ `RenderTarget`
  wrapper object again. **This is a claim about the wrapper only** — it
  says nothing about when the *owner* (`OffscreenTarget`) or the
  underlying Vulkan objects may be destroyed; see Part 2 below, which
  this claim does not relax in any way.
- **The borrow must not outlive its `OffscreenTarget` owner.** Acquiring
  a second `RenderTarget` from an `OffscreenTarget` while a previously-
  vended borrow is still outstanding, or destroying `OffscreenTarget`
  while a borrow it vended is still outstanding, are each a
  **guaranteed-detectable programmer error** (`ATLANTIS_CHECK`/
  `ATLANTIS_ASSERT`) — checkable because `OffscreenTarget` tracks
  "borrow outstanding" as a simple boolean, cleared by the borrow's own
  destructor (see "Ending the borrow" above). **This check answers only
  "is a borrow wrapper currently alive," a question about Part 1 above —
  it does not, and cannot, answer "has the GPU finished executing work
  that referenced this `OffscreenTarget`'s resources," which is Part 2's
  concern and requires a separate rule, below.** Conflating the two —
  treating "no borrow outstanding" as sufficient justification to
  destroy `OffscreenTarget` — is exactly the mistake this ADR's own
  second-round revision made and this third-round revision corrects.

**2. `OffscreenTarget` (owner and backing resources) lifetime.**

- **`OffscreenTarget`, and the color `VkImage`/`VkDeviceMemory`/
  `VkImageView` it owns, must remain alive until every GPU submission
  that referenced them has finished executing — independently of
  whether the C++ borrow wrapper that referenced them in that submission
  has already been destroyed.** Destroying `OffscreenTarget` (or the
  `Device` it was constructed from) while GPU work that referenced its
  backing resources is still in flight is a **lifetime precondition
  violation — the same, undetectable tier as the identical existing
  rule for `Presentation`** ([ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md);
  verified directly against `VulkanPresentation::~VulkanPresentation()`,
  `src/vulkan_backend/src/vulkan_presentation.cpp`, whose own comment
  states plainly: "This destructor does not itself wait; that discipline
  lives on the caller side... an undetected lifetime precondition, not
  something this destructor re-verifies"). This ADR deliberately does
  **not** introduce anything to make this specific condition
  guaranteed-detectable: no new assertion, no `Result`-typed error, no
  implicit wait inside any destructor, and no per-submission resource-
  tracking system — Phase 1's `Device` exposes only a single, coarse
  "wait for everything" operation (`Device::waitIdle()`), with no
  finer-grained, per-resource completion query to check against, and
  inventing one is explicitly out of this ADR's scope (see Alternatives
  Considered). This is not a lesser design than the "borrow outstanding"
  check above — it protects a genuinely different, and genuinely
  undetectable-with-Phase-1's-current-tools, condition.
- **How a caller satisfies this precondition in practice: call
  `Device::waitIdle()` after the last `Device::submit()` call that
  referenced this `OffscreenTarget`, before destroying (or reusing for a
  new cycle whose safety does not otherwise already follow from
  `Device::submit()`'s own fence-wait — see below) `OffscreenTarget`.**
  `Device::waitIdle()` (`src/rhi/include/atlantis/rhi/device.h`,
  implemented via `vkDeviceWaitIdle()`,
  `src/vulkan_backend/src/vulkan_device.cpp`) is the only mechanism
  Phase 1's `Device` exposes for a caller to establish "the GPU is done
  with everything I have submitted" — the same mechanism
  [ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
  already requires before destroying `Presentation`/`Device` with an
  outstanding acquired `RenderTarget` or unwaited submission. Note this
  is coarser than a per-`OffscreenTarget` wait (it drains the whole
  `Device`, not just work touching this one resource) — accepted,
  consistent with Phase 1's single-frame-in-flight baseline
  ([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md))
  having no finer-grained wait primitive to offer instead.
- **`VulkanDevice`'s own destructor already drains outstanding GPU work
  before destroying `VulkanDevice`'s own Vulkan objects**
  (`~VulkanDevice()`, `src/vulkan_backend/src/vulkan_device.cpp`: calls
  its internal `waitAndReleaseRetainedSubmission()` then
  `vkDeviceWaitIdle()`) — **but this protects only `Device`'s own
  teardown, not `OffscreenTarget`'s.** In the ordinary case where a
  caller destroys `OffscreenTarget` *before* destroying the `Device` it
  was constructed from (the expected, natural teardown order — see
  "Destruction order" below), `Device`'s own destructor-time drain runs
  strictly *after* `OffscreenTarget`'s backing resources are already
  gone, and does not protect them. A caller must not rely on `Device`'s
  destructor to make destroying `OffscreenTarget` early safe.
- **GPU-in-flight safety for repeated *acquire* cycles against the
  *same, still-alive* `OffscreenTarget` comes entirely from
  `Device::submit()`'s existing single-frame-in-flight fence-wait**
  (verified: `VulkanDevice::submit()`,
  `src/vulkan_backend/src/vulkan_device.cpp`, calls its internal
  `waitAndReleaseRetainedSubmission()` — a blocking `vkWaitForFences()`
  on the *previous* retained submission — before issuing the *new*
  `vkQueueSubmit()`) — **this is a separate claim from, and does not
  substitute for, the destruction precondition above.** A caller may
  validly destroy/reset the borrow and immediately call
  `acquireTarget()` again against the *same, not-yet-destroyed*
  `OffscreenTarget` with no intervening `Device::waitIdle()` call — the
  new cycle's own `Device::submit()` call will transparently wait on the
  previous submission's fence before the GPU actually begins the new
  cycle's work, exactly as it already does for consecutive windowed
  frames today. This claim is only about *reusing* a live
  `OffscreenTarget` for another cycle — it says nothing about, and does
  not relax, when `OffscreenTarget` itself may be *destroyed*.

**3. Destruction order (avoiding the exact inversion a naive reading
could produce).** The correct order is: establish GPU completion first
(`Device::waitIdle()`), *then* destroy `OffscreenTarget` (releasing its
`VkImage`/`VkDeviceMemory`/`VkImageView`), *then*, whenever the caller is
done with it, destroy `Device`. **Destroying `OffscreenTarget`'s backing
resources first and relying on `Device`'s own destructor to "catch up"
and wait afterward is the wrong order and is not safe** — by the time
`Device`'s destructor runs, `OffscreenTarget`'s `VkImage`/
`VkDeviceMemory`/`VkImageView` have already been released by
`OffscreenTarget`'s own (non-waiting) destructor,
regardless of whether GPU work referencing them had actually finished.

**Recommended flow (satisfies both lifetimes without exploiting either
minimum):** `acquireTarget()` → record (draw, then copy) → `submit()` →
`waitIdle()` → read the readback `Buffer` → drop/reset the borrow →
destroy or reuse `OffscreenTarget` (both now safe: the borrow's minimum
lifetime — Part 1 — was already satisfied at `submit()`'s return, and
the owner's precondition — Part 2 — is satisfied by the `waitIdle()`
call that already happened). This is this spec's own worked example's
flow (see
[specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md)'s
Proposed Design) — not the only legal ordering (the borrow may still be
dropped as early as right after `submit()` returns, per Part 1, and a
live `OffscreenTarget` may still be reused for another `acquireTarget()`
cycle without an intervening `waitIdle()`, per Part 2's repeated-cycle
claim), but the simplest one to reason about, and the one a caller must
follow, in substance, before *destroying* `OffscreenTarget` specifically.

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
- The borrow-wrapper lifetime contract is RAII-based, adds no new public
  method, and makes the double-acquire and destroy-while-borrow-
  outstanding misuse cases guaranteed-detectable programmer errors — a
  real, disclosed improvement over `Presentation`'s equivalent,
  currently-undetectable precondition for that specific case, achieved
  at no added public-API cost. **This improvement is scoped precisely:
  it answers "is a borrow wrapper alive," not "has the GPU finished
  referencing this resource"** — see the Negative/Trade-offs entry below
  for why the latter remains, correctly, at the same undetectable tier
  as `Presentation`'s equivalent.
- The minimum-borrow-lifetime clarification (through `submit()`, not
  through GPU completion) is not merely a convenience — it correctly
  reflects that Vulkan command recording bakes handles, not C++ object
  references, into a `CommandList`, avoiding a stricter-than-necessary
  contract that could otherwise mislead a future reader into believing
  the borrow's C++ lifetime has GPU-synchronization significance it does
  not have.
- Explicitly separating the borrow wrapper's lifetime from
  `OffscreenTarget`'s own (backing-resource) lifetime, and stating the
  latter's `Device::waitIdle()`-based precondition at the same tier as
  `Presentation`'s identical, existing rule, closes a real gap a second-
  round revision of this ADR introduced by conflating the two — found
  and corrected by a third-round Human Review that checked the claim
  against `VulkanDevice::submit()`'s and `VulkanPresentation`'s actual
  implementations rather than accepting the second-round text at face
  value.

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
- The borrow's C++ lifetime being decoupled from GPU execution
  completion (see "Minimum required lifetime: only through
  `Device::submit()`'s return" above) is a subtle point that a future
  reader could get wrong in either direction (assuming the borrow must
  be held longer than required, or — more dangerously — assuming
  dropping it early says anything at all about whether
  `OffscreenTarget`'s *backing resources* are safe to destroy). This ADR
  states the borrow's minimum lifetime and `OffscreenTarget`'s own,
  separate destruction precondition as two independent rules
  specifically to prevent that conflation; any future documentation
  referencing this contract must preserve that separation rather than
  collapsing it back into a single, vaguer rule — exactly the collapse a
  second-round revision of this ADR itself made and a third-round Human
  Review had to catch and correct.
- `OffscreenTarget`'s own destruction precondition (Part 2 of the
  lifetime contract above) is, by explicit, deliberate choice, **not**
  guaranteed-detectable — a real, if unavoidable-given-Phase-1's-tools,
  gap relative to the borrow-outstanding check's stronger tier. A caller
  that destroys `OffscreenTarget` without first calling
  `Device::waitIdle()` after its last relevant `submit()` call gets no
  assertion, no `Result::Err`, and no other diagnostic — only, at best,
  a Vulkan Validation Layer error, or at worst silent corruption/a driver
  crash. This is the same risk profile
  [ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
  already accepts for the identical `Presentation`/`Device` case, not a
  new or larger one this ADR introduces.

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
- **Give `OffscreenTarget` an explicit `release()`/`consume()` public
  method the caller must call to end a borrow, mirroring
  `Presentation::present()`'s explicit-call shape.** Considered — this
  round's second-round Human Review specifically asked whether RAII
  alone can safely express the relevant lifecycle/GPU-in-flight
  constraints; the analysis above (see "Borrow lifetime contract")
  answers yes. **Rejected**: unlike `present()`, there is no real side
  effect (`vkQueuePresentKHR` or equivalent) an explicit method would
  need to trigger — its only job would be clearing the same
  outstanding-borrow flag that a destructor can clear just as correctly,
  for free, via RAII. Adding a public method here would be pure
  ceremony: a second way to do exactly what destruction already does,
  inconsistent with every other owned RHI handle in this codebase
  (`Buffer`/`Texture`/`Pipeline`, none of which has an explicit
  `release()` method either) and adding public surface with no
  behavioral payoff.
- **Have `acquireTarget()`'s double-acquire case return `Result::Err`
  instead of firing a guaranteed-detectable assertion**, matching an
  earlier draft of this ADR. Reconsidered and rejected during this
  round's Human Review for consistency with this codebase's own Error
  Model (AGENTS.md, Error handling: "Programmer errors are assertions,
  not error returns... Recoverable runtime errors use explicit result/
  error types"): a caller acquiring twice without returning the first
  borrow is a caller mistake, not an environmental failure — exactly the
  same category Guard 1/Guard 2
  ([ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
  already treat as `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`, not `Result::Err`.
  Once the outstanding-borrow tracking needed to detect this case exists
  at all (which it must, to support the destruction-time check above),
  routing it through the assertion channel is both more consistent with
  precedent and enables the same tracking to serve both checks uniformly.
- **Make `OffscreenTarget`'s GPU-completion destruction precondition
  (Part 2 of the lifetime contract above) guaranteed-detectable too** —
  e.g. by having `Device` track, per outstanding submission, which
  `OffscreenTarget`(s) it referenced, and have `OffscreenTarget`'s
  destructor (or an explicit query) check against that; or by having
  `OffscreenTarget`'s destructor itself call an internal, `Device`-level
  wait before releasing its Vulkan objects, mirroring `VulkanDevice`'s
  own destructor's drain. **Rejected for this round, by explicit
  instruction confirmed during this ADR's third-round Human Review**:
  Phase 1's `Device`/`ADR-0020` single-frame-in-flight baseline
  deliberately exposes only one, coarse, whole-`Device` completion
  signal (`Device::waitIdle()`/`vkDeviceWaitIdle()`) — no per-resource or
  per-submission tracking of "which `OffscreenTarget`s does this
  submission touch" exists anywhere in this codebase today, and adding
  one would be new bookkeeping infrastructure introduced *specifically*
  for this one precondition, not something this spec's own acceptance
  target needs for any other reason — exactly the kind of scope creep
  [AGENTS.md](../AGENTS.md)'s "no speculative abstraction" principle
  warns against. An implicit wait inside `OffscreenTarget`'s own
  destructor was also considered and rejected for the same reason
  [VulkanPresentation](../src/vulkan_backend/src/vulkan_presentation.cpp)'s
  own destructor already rejected it (see that file's own comment,
  quoted above): it would silently stall an unrelated caller-visible
  operation (destruction) with GPU-wait latency the caller did not
  explicitly ask for, diverging from this codebase's established
  "destructors do not themselves wait; the caller establishes completion
  explicitly" convention. This precondition therefore remains,
  deliberately, at the same undetectable tier
  [ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
  already accepts for `Presentation`'s identical case — a future spec
  motivated by a real, concrete need (not this one) may reconsider a
  finer-grained completion-tracking mechanism, at which point it could
  also make this specific precondition detectable, but this ADR does not
  design or scaffold for that ahead of such a need.
