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
state." Spec 0006's own Requirements state the same rule
unconditionally: "`execute()` inserts one trailing `transitionResource()`
call to `ResourceState::PresentSource` for **any** bound `RenderTarget`."

**Verified against the actual implementation** (`src/render_graph/src/execution.cpp`,
the trailing loop at the end of `execute()`): this is exactly what the
shipped code does — for every entry in the caller-supplied `bindings`
vector whose `target` field is non-null and was touched by at least one
usage, `execute()` unconditionally inserts a transition to
`ResourceState::PresentSource`, with **no parameter, flag, or code path**
that distinguishes a windowed `RenderTarget` from any other kind. Neither
[ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
nor Spec 0006 ever defined how `execute()` would tell a "presentable"
`RenderTarget` apart from a non-presentable one — because until
[ADR-0038](0038-headless-offscreen-rendertarget-construction-and-ownership.md),
every bound `RenderTarget` in this codebase *was* presentable, so the
distinction never had to be made operational.

[ADR-0002](0002-presentation-rendertarget-unification.md) forecloses the
obvious fix: "Renderer **and RenderGraph** consume only `RenderTarget`
and cannot observe which path produced it — no member, flag, or
capability query on `RenderTarget` exposes 'am I a swapchain image or an
offscreen image.'" `execute()` therefore cannot ask its bound
`RenderTarget` whether `PresentSource` is even the right final state —
for a headless target it is not: nothing ever presents it, and the state
a readback copy actually needs is `ResourceState::TransferSource`
([ADR-0040](0040-gpu-to-cpu-readback-rhi-capability.md)), a different
Vulkan image layout with a different meaning. Unconditionally inserting
`PresentSource` for a headless bound target would be a real correctness
defect, not a cosmetic one — and, critically, **this includes the target
Renderer itself draws into**, because `Renderer::drawFrame()` calls this
exact same shared `execute()` function for its own internal graph
(verified against `src/renderer/src/renderer.cpp`) — a fact an earlier
draft of this ADR and of Spec 0010 missed, incorrectly claiming
`Renderer::drawFrame()` needed no change. See
[ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
own Proposed Amendment (2026-08-15) for the companion decision that fixes
this on `Renderer`'s side — this ADR fixes RenderGraph's own generalized
mechanism that amendment relies on.

A second, related gap: a headless verification composition must call
both `Renderer::drawFrame()` (which records into its own internal graph)
**and** a second, caller-built graph recording the readback copy
([ADR-0040](0040-gpu-to-cpu-readback-rhi-capability.md)) — two separate
`execute()` calls sharing one `CommandList`, both touching the same
physical `RenderTarget`.
**Verified against the actual implementation**: `execute()`'s
per-resource state-tracking map (`currentState`, a local
`std::unordered_map` declared fresh at the top of `execute()`'s body) is
explicitly documented and implemented as "entirely local to this call" —
a second `execute()` call against the same physical resource has no way
to know what state the first call actually left it in, and would
incorrectly assume `ResourceState::Undefined`.
`VK_IMAGE_LAYOUT_UNDEFINED` as a barrier's `oldLayout` is Vulkan's
explicit "the driver need not preserve existing contents" signal — using
it here risks discarding the very pixels the copy is supposed to read
back, immediately before it reads them. This is confirmed, not
hypothetical: it is the second, independent way the current
implementation's assumptions break under headless rendering.

Both gaps are, at root, the same shape: `execute()`'s per-resource state
bookkeeping hardcodes assumptions (`Undefined` incoming, `PresentSource`
final) that were correct for every case that has existed so far,
precisely because so far there has only ever been one case. This ADR
resolves both without reopening
[ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
actual dependency-to-barrier responsibility split (RenderGraph decides
*when*/*between what states*; RHI/Vulkan Backend decides *how*) — only
the two hardcoded state values that split currently assumes.

## Decision

**Each `ResourceBinding` entry whose `target` field is non-null**
(`src/render_graph/include/atlantis/render_graph/execution.h`) **gains
two additional fields**, meaningful only for `target`-shaped entries —
ignored, exactly like the existing `colorClear` field, for a
`depthTexture`-shaped entry:

- **`incomingState` (`ResourceState`, defaults to `Undefined`).** Seeds
  `execute()`'s per-resource tracking for this resource instead of the
  current hardcoded `Undefined` constant. **The default (`Undefined`) is
  correct and safe for exactly one case: a resource being bound to its
  first `execute()` call within its current `CommandList`/frame** — the
  overwhelmingly common case, and the only case that exists in this
  codebase's shipped code today. **The default is not safe, and must be
  explicitly overridden, for a resource that has already been touched by
  an earlier `execute()` call sharing the same `CommandList`** — using the
  default there is a caller precondition violation this decision does not
  claim to detect (the same lifetime-precondition-violation tier as every
  other cross-object precondition already in this codebase's Error
  Model): it produces a structurally-valid but semantically-wrong
  transition that silently discards the resource's real prior contents,
  exactly the defect described in Context above. This spec's own headless
  composition is the first, and so far only, caller required to supply a
  non-default value — see Requirements' worked example in
  [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md).
- **`finalState` (`std::optional<ResourceState>`, no default — every
  `target`-shaped binding entry must supply one explicitly).**
  `std::nullopt` means "no trailing transition beyond whatever the last
  pass that uses this resource already leaves it in." A concrete
  `ResourceState` value means `execute()` inserts one trailing
  `transitionResource()` call to it, after the last pass that uses this
  resource, exactly as today's hardcoded-`PresentSource` mechanism
  already does — generalized only in *which* state, not in *whether* or
  *when*. Requiring an explicit value (rather than defaulting silently to
  `PresentSource`) is deliberate: every caller constructing a
  `target`-shaped binding must decide this per binding rather than
  inheriting a windowed-shaped default that would be silently wrong for
  a non-presentable target.

`execute()`'s existing algorithm is otherwise **completely unchanged**:
it still walks compiled pass order, tracks each bound resource's
most-recently-recorded state (now seeded from `incomingState` instead of
a hardcoded constant), and calls `transitionResource()` whenever a pass's
declared usage state differs from it. The trailing step is unchanged in
mechanism: if `finalState` is present and differs from whatever state the
resource was left in by the last pass that used it, `execute()` inserts
one `transitionResource()` call to it (unchanged from today, just
parameterized); if `finalState` is `std::nullopt`, no trailing call is
inserted at all — a strict generalization of today's "if it was never
used, no transition" short-circuit, not a new kind of check. **If a
resource's tracked state (seeded from `incomingState`, updated by every
pass's transition) already equals its own `finalState` value, no
redundant transition is inserted** — same "insert only on an actual state
change" rule `execute()` already applies everywhere else.

**Every existing call site that constructs a `target`-shaped
`ResourceBinding` must be mechanically updated to supply an explicit
`finalState`** — there are exactly three today, not two as an earlier
draft of this ADR stated:

1. **`src/renderer/src/renderer.cpp`** (`Renderer::drawFrame()`'s
   internal `ResourceBinding` construction) — supplies
   `finalState = finalColorState`, the new parameter
   [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
   Proposed Amendment adds to `Renderer::drawFrame()`'s own public
   signature. `incomingState` is left at its default (`Undefined`) here —
   Renderer's own internal draw pass is always the first usage of the
   color target within its `CommandList`, in both the windowed and
   headless case.
2. **`examples/frame_execution_demo/main.cpp`** — its own, direct
   (non-`Renderer`) `execute()` call must supply
   `finalState = ResourceState::PresentSource` explicitly, preserving its
   exact existing, verified windowed behavior.
3. **`examples/minimal_renderer_demo`'s verification composition** —
   must pass `ResourceState::PresentSource` as the new
   `finalColorState` argument to `Renderer::drawFrame()`
   ([ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
   Proposed Amendment), preserving its exact existing, verified windowed
   behavior.

All three updates are **mechanical and non-behavioral** for the windowed
path — each supplies exactly the value `execute()`'s old hardcoded
behavior already produced. None of the three is optional or deferred to
a later spec; all three are required by this spec's future Plan before
any headless code is written, because `ResourceBinding`'s `finalState`
field has no default and none of the three would otherwise compile.

**Vulkan Backend impact — the one new state-transition pair this
decision requires.** `src/vulkan_backend/src/resource_state_mapping.cpp`'s
`planTransition()` function is a closed, exhaustively-enumerated lookup
table over `(before, after)` `ResourceState` pairs — verified by
inspection: any pair not explicitly listed triggers
`ATLANTIS_CHECK_MSG(false, ...)`, an assertion failure, not a computed
fallback. Given this decision's actual usage (Renderer's internal draw
pass always declares `ColorAttachmentOutput`; a headless caller supplies
`finalColorState = ResourceState::TransferSource`;
[ADR-0040](0040-gpu-to-cpu-readback-rhi-capability.md)'s copy pass
supplies `incomingState = TransferSource`, matching exactly what
`Renderer::drawFrame()` was told to leave the target in, so no further
transition is inserted for the copy pass itself), **exactly one new
`planTransition()` entry is required: `ColorAttachmentOutput →
TransferSource`.** No other new pair is required — the windowed path
continues to use the already-existing `ColorAttachmentOutput →
PresentSource` entry unchanged, and no `PresentSource`-involving pair is
ever needed for a headless target under this design (see
[ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
Proposed Amendment for why). This is named explicitly here, rather than
left purely implicit, so a future Plan does not have to rediscover it.

**Legal values and error semantics for `incomingState`/`finalState`.**
Both fields are the same backend-agnostic `ResourceState` enum used
everywhere else in RHI/RenderGraph's public surface — neither field's
*type* restricts which values a caller may supply, and this decision
introduces no new, general state-validation system to do so at the
RenderGraph level either. A value that does not correspond to an entry
in the Vulkan Backend's `planTransition()` table — for either field, for
any resource kind (`target` or `depthTexture`), now or as this decision
is extended in the future — is a **programmer error**: it produces a
guaranteed-detectable assertion failure (`ATLANTIS_CHECK_MSG`, per
[ADR-0009](0009-assertion.md)) at the point `execute()` would otherwise
call `CommandList::transitionResource()` with it, **not a compile-time
restriction and not a recoverable `Result`-typed error**. This is not a
new failure mode this decision invents — it is the same,
already-existing, unchanged behavior `planTransition()`'s closed table
already produces for any unlisted pair; this decision only widens which
values a caller can *choose to try*, it does not add any new validation
layer ahead of that existing mechanism, and does not promise that every
syntactically-legal `ResourceState` value is semantically accepted.

**This ADR does not change:**

- Guard 1 (every `ResourceState`-tagged usage must have a binding) or
  Guard 2 (no declared `reads()` usage on a bound `RenderTarget`) —
  both continue to apply exactly as
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
  fixed them, regardless of which incoming/final states a binding
  specifies.
- The bound depth `Texture`'s binding
  ([ADR-0026](0026-render-graph-multi-attachment-draw-pass-integration.md)),
  which never receives a trailing transition and does not use either new
  field.
- `RenderGraph`'s "decides when/between what states, never how" boundary
  — the Vulkan Backend's `CommandList::transitionResource()`
  implementation is untouched; this ADR only changes which state values
  RenderGraph's own bookkeeping starts and ends from, and adds one new
  entry to the Vulkan Backend's own closed transition table (named
  above).
- `Renderer`'s own draw-pass usage declarations
  (`ColorAttachmentOutput`/`DepthAttachmentReadWrite`) — unchanged;
  [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  Proposed Amendment only changes what `Renderer` passes as `finalState`
  for its own internal binding, never what its draw pass itself declares.

## Consequences

### Positive

- Resolves the `PresentSource`-for-headless correctness defect — for
  every bound `RenderTarget`, including the one `Renderer::drawFrame()`
  itself draws into — without giving RenderGraph any way to observe a
  `RenderTarget`'s origin: the discriminator lives entirely in the
  caller-supplied `finalState`/`incomingState` values, never in anything
  `RenderTarget` itself exposes, preserving
  [ADR-0002](0002-presentation-rendertarget-unification.md)'s unification
  promise to the letter.
- Resolves the cross-`execute()`-call state-continuity gap with a small,
  explicit, caller-supplied override rather than a shared, threaded-
  through mutable state object — no change to `RenderGraphBuilder`'s or
  `CompiledGraph`'s own contracts.
- Strictly generalizes, rather than replaces, `execute()`'s existing
  algorithm — for the single-`execute()`-call windowed case (100% of
  shipped code today), behavior is bit-for-bit identical once the three
  named call sites are updated; there is no regression risk to the
  windowed path from this decision's mechanism itself.
- Names, explicitly, the exact one new Vulkan Backend transition-table
  entry this decision requires, closing off a class of "discovered
  mid-implementation" surprise this repository's process exists to
  avoid.
- Keeps the "who decides when a transition happens" (RenderGraph) versus
  "who decides how one is performed" (Vulkan Backend) split from
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
  completely intact.

### Negative / Trade-offs

- Three existing call sites, not one, must be mechanically touched
  (`src/renderer/src/renderer.cpp`, `frame_execution_demo`,
  `minimal_renderer_demo`) to keep working — a real, if small and
  non-architectural, implementation cost this spec's Plan must account
  for.
- The binding's public shape grows from "resource → `RenderTarget`" to
  "resource → (`RenderTarget`, incoming state, final state)" — more
  parameters for a Plan to design a concrete C++ representation for, and
  more surface for a future caller to get wrong. Supplying an
  `incomingState` override that doesn't match a resource's true prior
  state is a caller precondition violation this decision does not claim
  to detect — the same tier as every other cross-object precondition
  already in this codebase's Error Model, but a real, disclosed
  fragility: each new chained-`execute()`-call caller must correctly
  know and manually state the resource's true prior state. A future spec
  introducing a third or fourth chained call within one frame should
  revisit whether this manual, per-call-site discipline still scales, or
  whether automated cross-call tracking (considered and rejected here —
  see Alternatives Considered — specifically because it would have
  required changing `Renderer`'s public API for no benefit beyond what
  this narrower mechanism already provides) becomes worth its added
  complexity at that point.

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
  requires fixing. A future spec may still make this case if the
  trailing-transition mechanism itself, not just its hardcoded target,
  turns out to need to go.
- **Thread a single, shared, caller-owned "execution state" object
  through every `execute()` call in a frame, replacing each call's own
  private bookkeeping map, so cross-call continuity happens automatically
  without any caller-supplied incoming-state override.** Considered —
  this would avoid requiring the caller to know
  `Renderer::drawFrame()`'s exact `finalColorState` value it itself
  supplied. Rejected for this round: it requires changing
  `Renderer::drawFrame()`'s own public signature to accept and thread
  through that shared object regardless (since Renderer's own internal
  `execute()` call would need to participate too) — no smaller than the
  [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)
  Proposed Amendment's own signature change, and strictly larger in
  surface area, for no benefit this narrower mechanism does not already
  provide (the caller only ever needs to remember a value it itself
  chose one line earlier, not infer a hidden `Renderer`-internal fact).
- **Let the headless verification composition perform its final
  transition directly via `CommandList::transitionResource()`, called
  from outside any RenderGraph pass callback.** Rejected — this is
  exactly the "caller-authored, hand-scheduled GPU work outside
  RenderGraph" pattern
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
  own Alternatives Considered already rejected for the same reason;
  `transitionResource()` remains recordable only from inside a pass
  callback, unchanged.
