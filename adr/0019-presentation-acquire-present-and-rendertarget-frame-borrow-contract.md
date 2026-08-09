# ADR 0019: Presentation Acquire/Present Protocol and RenderTarget Frame-Borrow Contract

- **Status:** Proposed
- **Date:** 2026-08-09
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md)

## Context

[ADR-0016](0016-presentation-acquire-present-and-recreation-contract.md)
deliberately deferred an entire bundle of decisions "as one," because
fixing any one of them without the others risked a contract a future
RenderGraph spec would have to partially undo: the acquire API's public
shape, `RenderTarget`'s frame-ownership shape, acquire-complete
synchronization, graph-to-present synchronization, image layout handoff,
and `present()`'s own shape and out-of-date/suboptimal handling.

Spec 0005 (RenderGraph Foundation) has since shipped a GPU-independent
graph-compilation core with no RHI dependency at all — it does not, and
by its own Non-Goals could not, validate any part of this bundle against
a real acquire/present cycle. That gap is now the direct blocker for
[specs/0006](../specs/0006-rhi-render-graph-frame-execution-foundation.md)'s
own minimal-acceptance goal: acquire a frame target, execute at least one
GPU pass through RenderGraph, submit, and present a visible frame.

Two additional forces bear directly on this decision:

- [ADR-0002](0002-presentation-rendertarget-unification.md) already fixed
  that `RenderTarget` must be indistinguishable to Renderer/RenderGraph
  regardless of windowed-vs-headless origin, and
  [ADR-0003](0003-resource-rendertarget-ownership-model.md) already fixed
  that Renderer never owns a `RenderTarget` — it borrows one for a single
  frame's draw. Neither ADR fixed `RenderTarget`'s concrete C++ shape or
  what "borrowed for a single frame" means operationally (when the borrow
  begins, when it must end, what happens if it is dropped without being
  presented).
- [ADR-0016](0016-presentation-acquire-present-and-recreation-contract.md)'s
  own Negative/Trade-offs section flagged that `recreateIfNeeded()` being a
  separate, explicit entry point is "an integration detail" a future
  RenderGraph-driven implementation would need to resolve — either keep
  calling it explicitly, or fold it into acquire. That resolution belongs
  here.

## Decision

**`RenderTarget`** is introduced as a concrete RHI public type:

- It represents exactly one presentable color attachment — the acquired
  swapchain image and the resources (image view) needed to record work
  against it and to present it. It carries no depth/multi-attachment
  concept; that remains future Renderer scope.
- It is a **non-owning, frame-scoped borrow**, per
  [ADR-0003](0003-resource-rendertarget-ownership-model.md):
  `Presentation` continues to own every swapchain-backed resource behind
  it. A `RenderTarget` value is valid from the moment `acquireNextTarget()`
  returns it until it is consumed by exactly one matching `present()` call
  in the same frame — it must not be retained, copied for reuse, or
  presented more than once. Using one outside that window (across frames,
  or discarding it without presenting) is a lifetime precondition
  violation, the same tier [Spec 0005](../specs/0005-render-graph-foundation.md)'s
  Error Model already established for other borrowed-handle misuse in
  this codebase — not a case this contract claims to detect dynamically.
  **This precondition's consequence at `Presentation`/`Device`
  destruction is stated explicitly below**, because unlike Spec 0003 —
  where no image was ever acquired, so destruction had no synchronization
  precondition to satisfy — that is no longer true once real acquire
  exists.
- **`RenderTarget` is move-only: movable, not copyable.** Fixed
  explicitly, per AGENTS.md's Ownership and lifetime rules ("ownership
  transfer is expressed by moving an owning type... not implied by
  convention alone") — a copyable `RenderTarget` would let a caller
  produce two values referencing the same one-time acquire/present
  borrow, making "presented more than once" trivially easy to trigger by
  accident. `RenderTarget` also carries, opaquely (not as a
  caller-visible field), a reference to the acquire-complete signal
  `Device::submit()`
  ([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md))
  needs to wait on before GPU work touching this target begins — the
  caller never constructs, owns, or manages that signal directly.
- Its only externally-visible state, as far as this decision fixes, is
  whatever a `CommandList::transitionResource()` call
  ([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md))
  and RenderGraph's execution binding
  ([ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
  need to reference it — no size/format mutation, no resizing API; extent
  and format are read-only, mirroring `Presentation`'s existing swapchain
  metadata query.

**`acquireNextTarget()`** is added to `Presentation`:

- **Folds `recreateIfNeeded()`'s recreation timing into acquire.**
  `acquireNextTarget()` calls the existing `recreateIfNeeded()` logic
  ([ADR-0016](0016-presentation-acquire-present-and-recreation-contract.md))
  as its first step, every call. `recreateIfNeeded()` itself is
  unchanged and remains callable directly (e.g. for a caller that wants
  to force recreation without acquiring); acquire simply becomes its
  primary caller for the frame loop, resolving ADR-0016's own flagged
  integration question.
- **Three-outcome result**, not a two-outcome one: `Err(AcquireError)` for
  a genuine unrecoverable failure (e.g. surface lost, device lost);
  `Ok(std::nullopt)` when the tracked extent is `{0, 0}` — "nothing to
  draw this frame," not an error, mirroring `recreateIfNeeded()`'s own
  zero-extent-is-not-an-error stance; `Ok(RenderTarget{...})` otherwise.
  The caller (Runtime-equivalent code) must skip RenderGraph execution and
  `present()` entirely for a `std::nullopt` outcome.
- If the underlying `vkAcquireNextImageKHR` call itself reports
  `VK_ERROR_OUT_OF_DATE_KHR`, `acquireNextTarget()` marks recreation
  needed and returns `Ok(std::nullopt)` for that call **without** retrying
  acquisition immediately within the same call — the next frame's
  `acquireNextTarget()` recreates (via its first-step
  `recreateIfNeeded()`) and then acquires against the new swapchain. See
  Alternatives Considered for why an immediate in-call retry was rejected.
  `VK_SUBOPTIMAL_KHR` is not treated as out-of-date: the acquired image is
  still usable, so acquisition proceeds normally, but recreation is marked
  needed so the *next* call's `recreateIfNeeded()` step recreates before
  acquiring again — the standard suboptimal-handling pattern.
- Acquire-complete synchronization is `Presentation`'s own concern: it
  owns whatever semaphore(s) are needed to signal "this image is ready to
  be recorded into," scoped to Phase 1's single-frame-in-flight baseline
  ([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)).
  The concrete semaphore/pool count is a Vulkan Backend implementation
  detail, not fixed by this ADR beyond "at least enough to avoid reusing a
  semaphore still in flight." This signal is never handed to the caller as
  a raw handle — it travels opaquely inside the returned `RenderTarget`
  value and is read only by `Device::submit()`
  ([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)),
  consistent with this ADR's decision that `RenderTarget` carries it
  internally (see "`RenderTarget`" above).

**`present(RenderTarget target, SubmissionSignal renderFinished)`** is
added to `Presentation`:

- Consumes `target` by value (move-only, per above), ending its borrow.
  `renderFinished` is the opaque `SubmissionSignal`
  [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
  `Device::submit()` returned for the submission that recorded work
  against `target` — `present()` waits on it before calling
  `vkQueuePresentKHR`. Presenting a `target` without having first
  submitted (and obtained a matching `SubmissionSignal` for) the work that
  transitions it to `ResourceState::PresentSource`
  ([ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
  is a caller precondition violation this ADR does not claim to detect —
  the same tier as every other cross-object precondition in this
  contract.
- `VK_ERROR_OUT_OF_DATE_KHR` or `VK_SUBOPTIMAL_KHR` from the present call
  itself marks recreation needed for the next `acquireNextTarget()` call
  and is **not** surfaced to the caller as a `Result::Err` — this is
  expected, routine swapchain aging, not a failure. Any other Vulkan error
  from present is a genuine `Result::Err`.

**Destruction with an outstanding acquired `RenderTarget`.** Spec 0003's
`Presentation` could be destroyed safely at any point because no image
was ever acquired under its contract — that structural guarantee no
longer holds once `acquireNextTarget()` exists. This ADR fixes the
resulting precondition explicitly rather than leaving it as a silent gap:
**a caller must not destroy `Presentation` (or the `Device` it was
constructed from) while a `RenderTarget` it vended has been acquired but
not yet consumed by a matching `present()` call**, and must not destroy
either while a `Device::submit()` this `RenderTarget` participated in has
not yet been waited on to completion. This is a lifetime precondition
violation, not a guaranteed-detectable error — consistent with this
codebase's existing handle-misuse tiering — but the Runtime-equivalent
verification composition this spec builds (see
[specs/0006](../specs/0006-rhi-render-graph-frame-execution-foundation.md)
Testing & Verification Plan) is required to demonstrate the discipline
that satisfies it: waiting for the device to go idle (e.g. via
`Device::submit()`'s own internal fence bookkeeping,
[ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md))
before tearing down `Presentation`/`Device` on every exit path, including
one that exits mid-frame (after `acquireNextTarget()` but before
`present()`).

**Image layout handoff** is resolved by a deliberate simplification: a
`RenderTarget`'s image is always treated by the Vulkan Backend as
entering its first transition of the frame from `ResourceState::Undefined`
(`VK_IMAGE_LAYOUT_UNDEFINED` as the barrier's `oldLayout`), regardless of
its actual previous layout from an earlier presented frame. This is valid
only because a `RenderTarget` is exclusively a **write** target in this
round — RenderGraph's usage model
([ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
never allows a pass to read from a bound `RenderTarget`'s prior contents,
so discarding whatever layout/content it held is always correct. A future
spec may track true prior layout to avoid a redundant discard-transition
once a real optimization need or a read-from-`RenderTarget` use case
appears; not designed here.

## Consequences

### Positive

- Resolves every item ADR-0016 deferred as a bundle, in one coherent pass,
  rather than compounding partial decisions made under pressure.
- `acquireNextTarget()` absorbing `recreateIfNeeded()` removes an
  integration seam ADR-0016 itself flagged as unresolved, without changing
  `recreateIfNeeded()`'s own existing contract or breaking Spec 0003's
  acceptance criteria (it remains independently callable and still never
  touches Vulkan at zero extent).
- The `Ok(std::nullopt)` / `Ok(RenderTarget)` / `Err` three-outcome shape
  lets a caller distinguish "nothing to draw" from "something went wrong"
  without overloading the error channel for a non-error condition,
  consistent with Spec 0005's existing "not an error" classification
  discipline.
- The always-`Undefined`-incoming-layout simplification avoids inventing
  cross-frame layout-tracking state this round has no consumer to justify,
  while remaining fully valid (and Validation-Layer-clean) under the
  write-only `RenderTarget` usage model this decision pairs it with.

### Negative / Trade-offs

- The always-`Undefined` incoming layout is a real (if typically
  negligible) missed optimization versus tracking true prior layout — a
  future spec revisiting cross-frame image state must treat this as a
  known, deliberate simplification to reconsider, not an oversight.
- Deferring the immediate-retry-on-out-of-date case to "handle it next
  frame" means a resize event can cost one dropped/skipped frame before a
  new image is actually acquired and presented — accepted as simplicity
  over latency, see Alternatives Considered.
- `RenderTarget` being write-only in this round means a pass cannot read
  back the frame it is drawing (e.g. no post-process-from-swapchain
  pattern) — acceptable because no such use case exists yet, but a real
  constraint any future spec building on this contract must know about.
- Real acquire genuinely reintroduces a synchronization precondition at
  `Presentation`/`Device` destruction that Spec 0003 was able to avoid
  entirely — this ADR fixes the precondition and requires this spec's
  verification composition to demonstrate satisfying it on every exit
  path, but it is a real new caller obligation Spec 0003's contract never
  had, not something this decision can eliminate.

## Alternatives Considered

- **Retry acquisition immediately, in-call, on `VK_ERROR_OUT_OF_DATE_KHR`.**
  Rejected: requires bounding the retry count to avoid an unbounded loop
  if the surface stays out-of-date (e.g. rapid resize), and the "handle it
  on the next frame" approach already produces a correct, eventually-
  consistent result with less code and no retry-count magic number to
  justify.
- **Keep `recreateIfNeeded()` as the caller's own explicit per-frame
  responsibility, do not fold it into acquire.** Rejected: this is exactly
  the integration ambiguity ADR-0016 flagged and left open; folding it in
  removes a footgun (a caller forgetting to call `recreateIfNeeded()`
  before acquiring) at no real cost, since acquiring without a valid
  swapchain makes no sense anyway.
- **Track true prior image layout across frames and transition from it
  instead of always discarding from `Undefined`.** Rejected for this
  round: adds cross-frame state (what was the layout after the last
  present completed) with no current correctness or performance need to
  justify it, given `RenderTarget` is write-only; revisit if a future spec
  needs to read a `RenderTarget`'s prior contents.
- **A two-outcome `Result<RenderTarget, AcquireError>` where zero-extent
  is folded into `AcquireError` as a distinguished variant.** Rejected:
  conflates "operation failed" with "operation correctly determined there
  is nothing to do," which the codebase's existing Error Model
  conventions (Spec 0005) already treat as a meaningfully different
  category — a caller checking only "is this an error" would have to
  special-case the zero-extent variant anyway, so it is clearer to keep it
  out of the error channel entirely.
- **Let `present()` return a hard error on out-of-date/suboptimal.**
  Rejected: this is the routine, expected way a swapchain ages (window
  moved between monitors, resized, etc.) — treating it as a hard failure
  would make ordinary resize interaction look like a crash-worthy
  condition, contradicting AGENTS.md's own framing of resize as a normal
  case to handle, not an error to propagate.
