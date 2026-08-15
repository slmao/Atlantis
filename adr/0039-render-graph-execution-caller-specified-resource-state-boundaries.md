# ADR 0039: RenderGraph Execution — Caller-Specified Incoming and Final Resource States

- **Status:** Proposed
- **Date:** 2026-08-15
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md) (`In Review`)

## Context

[ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
fixed `execute()`'s trailing-transition behavior as: "any bound resource
that is a **presentable** `RenderTarget` is considered to carry an
implicit final required state of `ResourceState::PresentSource`;
`execute()` inserts one trailing `transitionResource()` call to that
state." Spec 0006's own Requirements state the same rule unconditionally:
"`execute()` inserts one trailing `transitionResource()` call to
`ResourceState::PresentSource` for **any** bound `RenderTarget`." Neither
document defines how `execute()` would ever distinguish a "presentable"
`RenderTarget` from any other kind — because until
[ADR-0038](0038-headless-offscreen-rendertarget-construction-and-ownership.md),
every bound `RenderTarget` in this codebase *was* presentable, so the
distinction never had to be made operational.

[ADR-0002](0002-presentation-rendertarget-unification.md) forecloses the
obvious fix: "Renderer **and RenderGraph** consume only `RenderTarget` and
cannot observe which path produced it — no member, flag, or capability
query on `RenderTarget` exposes 'am I a swapchain image or an offscreen
image.'" `execute()` therefore cannot ask its bound `RenderTarget`
whether `PresentSource` is even the right final state — for a headless
target, it is not: nothing ever presents it, and the state a readback copy
actually needs is `ResourceState::TransferSource`
([ADR-0040](0040-gpu-to-cpu-readback-rhi-capability.md)), a different
Vulkan image layout with a different, semantically-wrong-if-swapped
meaning. Unconditionally inserting `PresentSource` for a headless bound
target would be a real correctness defect, not a cosmetic one.

A second, related gap surfaces once a headless verification composition
is actually built: [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)
fixes `Renderer::drawFrame()` as a sealed call that builds, compiles, and
executes its **own internal** `RenderGraphBuilder` graph, exposing no way
for a caller to inject an additional pass into it. A headless composition
that wants to both draw (via `Renderer::drawFrame()`) and then copy the
drawn image out ([ADR-0040](0040-gpu-to-cpu-readback-rhi-capability.md))
must therefore issue **two separate `execute()` calls sharing one
`CommandList`** — Renderer's own internal call, then the caller's own,
second, small copy-pass graph — both against the *same* `RenderTarget`
value (borrowed by reference into `Renderer::drawFrame()`, never
consumed by it, so the caller still holds it afterward). Spec 0007
already generalized `execute()`'s incoming-state assumption to "every
bound resource... is treated as entering **each** `execute()` call from
`ResourceState::Undefined`" — worded per-call because, until now, exactly
one `execute()` call had ever touched a given physical resource within one
frame. A second, chained `execute()` call against the *same* physical
image would, under that existing rule, incorrectly assume the image's
prior contents (in this case, the mesh Renderer just drew) don't need
preserving — `VK_IMAGE_LAYOUT_UNDEFINED` as a barrier's `oldLayout` is
Vulkan's explicit "the driver need not preserve existing contents" signal.
Using it here would risk discarding the very pixels the copy is supposed
to read back, immediately before it reads them.

Both gaps are, at root, the same shape: `execute()`'s per-resource state
bookkeeping currently hardcodes assumptions (`Undefined` incoming,
`PresentSource` final) that were correct for every case that has existed
so far, precisely because so far there has only ever been one case. This
ADR resolves both without reopening
[ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
actual dependency-to-barrier responsibility split (RenderGraph decides
*when*/*between what states*; RHI/Vulkan Backend decides *how*) — only the
two hardcoded state values that split currently assumes.

## Decision

**Each entry in `execute()`'s existing frame-scoped external resource
binding** (introduced by
[ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md),
its concrete representation already left to the Plan) **gains two
additional, caller-supplied pieces of information, conceptually attached
to the `resource → RenderTarget` pair, not to `RenderTarget` itself:**

- **An assumed incoming `ResourceState`, defaulting to `Undefined` when
  not specified.** This preserves every existing windowed call site's
  behavior exactly, with zero required change, for the overwhelmingly
  common case of a `RenderTarget` used by exactly one `execute()` call per
  frame. A caller chaining a second `execute()` call against a
  `RenderTarget` an earlier call in the same frame already transitioned
  (this spec's own headless readback composition is the first such case)
  overrides this to the resource's true last-known state — which, for the
  specific case of a `RenderTarget` that just came out of
  `Renderer::drawFrame()`, is the fixed, already-public
  `ResourceState::ColorAttachmentOutput`
  ([ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)) —
  a documented, stable fact about what Renderer's one draw-pass usage
  always declares, not an internal detail the caller has to guess at or
  that could silently change without a spec revision.
- **A required (no default) final `ResourceState`, expressed as
  `std::optional<ResourceState>`** — `std::nullopt` meaning "no trailing
  transition beyond whatever the last pass that uses this resource already
  left it in." Every existing windowed call site must now pass
  `ResourceState::PresentSource` explicitly where it previously received
  this behavior implicitly — a mechanical, non-behavioral update
  ([frame_execution_demo](../examples/frame_execution_demo),
  [minimal_renderer_demo](../examples/minimal_renderer_demo)), required by
  this spec's Plan, with **zero observable change** to either demo's
  actual output or Vulkan calls. Requiring an explicit value (rather than
  defaulting silently to `PresentSource`) is deliberate: a caller must
  decide this per binding rather than inheriting a windowed-shaped
  default that would be silently wrong for the next non-presentable
  binding kind a future spec introduces.

`execute()`'s existing algorithm is otherwise **completely unchanged**:
it still walks compiled pass order, tracks each bound resource's
most-recently-recorded state (now seeded from the caller-supplied incoming
state instead of a hardcoded constant), and calls `transitionResource()`
whenever a pass's declared usage state differs from it. The only new step
is a **trailing pass, after the last pass that uses a bound resource**:
if that resource's caller-supplied final state is present and differs
from whatever state it was left in, `execute()` inserts one
`transitionResource()` call to it (unchanged mechanism from today);
if `std::nullopt`, no trailing call is inserted at all — a strict
generalization of today's "if it was never used, no transition"
short-circuit, not a new kind of check.

**This ADR does not change:**

- Guard 1 (every `ResourceState`-tagged usage must have a binding) or
  Guard 2 (no declared `reads()` usage on a bound `RenderTarget`) —
  both continue to apply exactly as
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
  fixed them, regardless of which incoming/final states a binding
  specifies.
- The bound depth `Texture`'s binding
  ([ADR-0026](0026-render-graph-multi-attachment-draw-pass-integration.md)),
  which never receives a trailing transition and is unaffected by this
  decision.
- `RenderGraph`'s "decides when/between what states, never how" boundary
  — the Vulkan Backend's `CommandList::transitionResource()`
  implementation is untouched; this ADR only changes which state values
  RenderGraph's own bookkeeping starts and ends from.
- `Renderer`'s public API
  ([ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)).
  `Renderer::drawFrame()` continues to build, compile, and execute its own
  internal graph exactly as today, with its own private, per-call state
  map, unaware of and unmodified by anything this ADR introduces. Only the
  caller's own, separate, second `execute()` call (for the copy pass) uses
  this ADR's new incoming-state override — the caller supplies the fixed
  `ColorAttachmentOutput` value itself, based on Renderer's already-public
  contract, not on any new signal Renderer emits.

## Consequences

### Positive

- Resolves the `PresentSource`-for-headless correctness defect without
  giving RenderGraph any way to observe a `RenderTarget`'s origin,
  preserving [ADR-0002](0002-presentation-rendertarget-unification.md)'s
  unification promise to the letter — the discriminator lives in the
  caller-supplied binding, not in `RenderTarget`.
- Resolves the cross-`execute()`-call state-continuity gap without adding
  any shared, threaded-through mutable state object, and without changing
  `Renderer`'s already-`Accepted`, already-implemented public API at all —
  the fix is entirely confined to the caller's own second graph's binding.
- Strictly generalizes, rather than replaces, `execute()`'s existing
  algorithm — for the single-`execute()`-call windowed case (100% of
  shipped code today), behavior is bit-for-bit identical once call sites
  are updated to pass `PresentSource` explicitly; there is no regression
  risk to the windowed path from this decision's mechanism itself.
- Keeps the "who decides when a transition happens" (RenderGraph) versus
  "who decides how one is performed" (Vulkan Backend) split from
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
  completely intact — this decision only widens which values seed and
  terminate that bookkeeping.

### Negative / Trade-offs

- Every existing windowed call site must be mechanically touched
  (`frame_execution_demo`, `minimal_renderer_demo`) to keep working — a
  real, if small and non-architectural, implementation cost this spec's
  Plan must account for, not a zero-diff change to already-shipped code.
- The binding's public shape grows from "resource → `RenderTarget`" to
  "resource → (`RenderTarget`, optional incoming state, optional final
  state)" — more parameters for a Plan to design a concrete C++
  representation for, and more surface for a future caller to get wrong
  (e.g. supplying an incoming-state override that doesn't match the
  resource's true prior state is a caller precondition violation this
  decision does not claim to detect, the same tier as every other
  cross-object precondition already in this codebase's Error Model).
- A caller building a chained, multi-`execute()`-call frame (this spec's
  own headless readback composition) must know and correctly state the
  fixed `ResourceState` a prior call left a resource in — a real, if
  narrow and documented, coupling between the caller's own graph and
  Renderer's public `ColorAttachmentOutput` contract that a
  single-`execute()`-call frame never has to reason about.

## Alternatives Considered

- **Add a second, distinct `execute()`-family entry point for headless
  (e.g. `executeHeadless()`) that skips the trailing transition entirely,
  leaving windowed's `execute()` untouched.** Rejected: this forks
  RenderGraph's execution entry point by origin — exactly what
  [ADR-0002](0002-presentation-rendertarget-unification.md) exists to
  prevent — doubling the surface to test and maintain for what is, in
  substance, a two-field difference in one already-Plan-deferred binding
  structure.
- **Remove the trailing-transition mechanism entirely; require every
  graph, windowed or headless, to declare its own true final-state usage
  as an explicit last pass.** Considered — this is a real, arguably
  cleaner simplification (no synthetic insertion at all). Rejected for
  this round: it is a larger, behavior-visible edit to
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
  already-shipped, already-verified windowed contract than this gap
  requires fixing — every existing pass declaration in
  `frame_execution_demo`/`minimal_renderer_demo` would need a new,
  synthetic final-state pass added, not just one already-deferred binding
  field populated. A future spec may still make this case if the trailing-
  transition mechanism itself, not just its hardcoded target, turns out to
  need to go.
- **Thread a single, shared, caller-owned "execution state" object through
  every `execute()` call in a frame, replacing each call's own private
  bookkeeping map, so cross-call continuity happens automatically without
  any caller-supplied incoming-state override.** Considered — this would
  avoid requiring the caller to know Renderer's fixed
  `ColorAttachmentOutput` contract. Rejected for this round: it requires
  changing `Renderer::drawFrame()`'s own public signature to accept and
  thread through that shared object (since Renderer's own internal
  `execute()` call would need to participate too), reopening
  [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  already-`Accepted`, already-implemented contract for a capability only
  this spec's own second, caller-built graph actually needs — a
  meaningfully larger blast radius than an explicit, documented
  incoming-state override on the caller's own binding, which touches
  nothing inside `Renderer`.
- **Let the headless verification composition perform its final
  transition directly via `CommandList::transitionResource()`, called
  from outside any RenderGraph pass callback.** Rejected — this is exactly
  the "caller-authored, hand-scheduled GPU work outside RenderGraph"
  pattern
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
  own Alternatives Considered already rejected for the same reason;
  `transitionResource()` remains recordable only from inside a pass
  callback, unchanged.
