# ADR 0021: RenderGraph/RHI Execution Integration and Dependency-to-Barrier Responsibility

- **Status:** Proposed
- **Date:** 2026-08-09
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md)

## Context

Spec 0005 shipped RenderGraph's graph-description/compilation core with
**no RHI dependency**, explicitly deferring execution: "it does not
execute, submit, or simulate GPU work anywhere in this spec's scope."
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
already lists RenderGraph as depending on "RHI, Core" — this was
anticipated, not decided by Spec 0005, which scoped itself to Core-only
for that one round.

[ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
now gives RHI a minimal `CommandList`/`transitionResource()` surface.
Someone must decide, as a reviewed architectural boundary rather than an
implementation-time improvisation, which module is responsible for
*deciding when* a resource needs a state transition (a question only
answerable from the compiled graph's dependency data) versus *performing*
one (a question only the Vulkan Backend can answer, per AGENTS.md's
"no direct `vkCmd*` calls outside the Vulkan Backend's `CommandList`
implementation" rule). Left unstated, this is exactly the kind of
module-boundary question the Golden Rule exists to prevent from being
settled implicitly by whichever code is written first.

A second, related gap: Spec 0005's logical resources are graph-local
opaque identities with **no RHI backing of any kind** — a producer-less
resource is documented as "an externally-provided input token" but no
mechanism exists to actually bind one to a real RHI object. This spec's
minimal acceptance bar (draw into a real, acquired `RenderTarget`)
requires that binding mechanism to exist for the first time.

## Decision

**RenderGraph gains a dependency on RHI**, realizing the boundary
`module_boundaries.md` already anticipated — no new module boundary is
introduced, this decision fills in the previously-unrealized part of an
existing one. RenderGraph's execution-phase code references only RHI's
backend-agnostic types (`CommandList`, `ResourceState`, `RenderTarget`);
it continues to never reference Vulkan Backend or any `Vk*` type, per
[ADR-0001](0001-rhi-backend-independence.md) — that rule is unchanged and
unaffected by this new dependency.

**Resource-usage declarations gain a `ResourceState` tag.**
`RenderGraphBuilder::writes()`/`reads()` (Spec 0005) are extended to
additionally accept the `ResourceState`
([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md))
a pass requires its resource usage to be in — e.g. a pass that clears a
`RenderTarget` declares a write usage tagged `ColorAttachmentWrite`. This
is **additive** to Spec 0005's model: the single-producer-per-resource
rule, producer→reader edge derivation, cycle detection, and deterministic
ordering ([ADR-0018](0018-render-graph-dependency-derivation-and-ordering.md))
are unchanged in every respect — the state tag participates in transition
bookkeeping only, never in dependency derivation or ordering.

**A pass gains an execution callback**, recorded at declaration time and
invoked during execution, in compiled order — the first time any pass
declaration in this codebase carries executable behavior, not just
metadata. The callback receives a `CommandList&` to record into and
whatever RHI resource(s) its declared usages were bound to (below).

**External resource binding.** Because a logical resource has no RHI
identity of its own, a caller must bind each producer-less logical
resource used by the graph to a concrete RHI object (here: a
`RenderTarget`) before execution — extending Spec 0005's "producer-less
resource = externally-provided input token" concept with its first real
binding mechanism. This binding is scoped to a single `execute()` call
(one frame), matching `RenderTarget`'s own frame-scoped borrow
([ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)) —
it is not a persistent registration on the builder or the compiled graph.

**Dependency-to-barrier responsibility split** — the core of this
decision:

- **RenderGraph decides *when* and *between what states* a transition is
  needed.** Walking the compiled pass order, for each resource usage
  RenderGraph tracks that resource's most-recently-recorded state; when a
  pass's declared usage state differs from it, RenderGraph calls
  `CommandList::transitionResource(resource, previousState, declaredState)`
  before invoking that pass's execution callback. This is pure bookkeeping
  over already-compiled dependency data — RenderGraph never itself
  constructs a barrier, never references a pipeline stage/access mask, and
  never touches a `Vk*` type.
- **RHI (concretely, the Vulkan Backend's `CommandList` implementation)
  decides *how* to perform that transition** — the `VkImageMemoryBarrier`
  and `vkCmdPipelineBarrier` call, including every access-mask/
  pipeline-stage/layout detail, are entirely private to that
  implementation, per
  [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md).
- **The final transition to `PresentSource`** is applied by RenderGraph's
  execution, not by `Presentation::present()`: any bound resource that is
  a presentable `RenderTarget` is considered to carry an implicit final
  required state of `ResourceState::PresentSource`; `execute()` inserts
  one trailing `transitionResource()` call to that state, after the last
  pass in compiled order that uses the bound `RenderTarget`, before
  returning control to the caller for submission. This keeps every
  transition — including the last one — going through the same single
  primitive and recorded before submission, rather than splitting
  transition responsibility between `CommandList` and `Presentation` (see
  [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
  Alternatives Considered).

**RenderGraph records; it does not submit or present.** `execute()` only
fills the caller-provided `CommandList`. `Device::submit()` and
`Presentation::present()`
([ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md),
[ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md))
remain explicit, separate calls the caller (Runtime-equivalent code)
makes after `execute()` returns. This narrows, rather than reopens, Spec
0005's "RenderGraph does not execute, submit, or simulate GPU work"
Non-Goal: this decision grants RenderGraph the capability to **record**
GPU work (superseding that specific clause of Spec 0005's Non-Goals, as
this ADR's own governing decision), while **submission and presentation
remain outside RenderGraph**, exactly as they were before. AGENTS.md's
"RenderGraph is the mandatory path for GPU work" is satisfied because
recording is the GPU-work step being gated — nothing records into a
`CommandList` outside a RenderGraph pass callback anywhere this spec
introduces.

## Consequences

### Positive

- Answers the module-boundary question this repository's Golden Rule
  requires be answered by review, not improvisation: RenderGraph owns
  scheduling *decisions*, RHI/Vulkan Backend owns *mechanism* — the same
  separation already established between RHI (interface) and Vulkan
  Backend (implementation) elsewhere in this codebase.
- Extending, rather than replacing, Spec 0005's usage-declaration and
  dependency-derivation model means none of that spec's sixteen
  Human-Review-approved decisions are reopened — this spec's execution
  layer builds strictly on top of them.
- A single, uniform transition primitive (including the final
  present-transition) keeps the barrier-recording code path in exactly
  one place to reason about and test, rather than split across two
  modules.

### Negative / Trade-offs

- RenderGraph's execution phase now needs a way to track "most-recently-
  recorded state per resource" across the whole compiled pass order —
  new bookkeeping state that did not exist in Spec 0005's pure-compilation
  scope; this is graph-execution-local, not persisted on the compiled
  graph itself, but is real added complexity this ADR accepts as necessary
  to make dependency-derived transitions automatic rather than
  caller-authored.
- The frame-scoped external-binding mechanism is intentionally minimal
  (bind one `RenderTarget` to one producer-less resource); it is not a
  general resource-import system, and a future spec adding real
  `Buffer`/`Texture` resources will likely need to extend or redesign it
  rather than reuse it unchanged.
- "RenderGraph records but does not submit" is a fine distinction a future
  reader could miss; this ADR states it explicitly and the spec's
  Acceptance Criteria enforce it by inspection, but it remains a boundary
  that must be actively maintained as the codebase grows.

## Alternatives Considered

- **Have `Presentation::present()` perform the final `PresentSource`
  transition itself**, since it already knows the target is about to be
  presented. Rejected — see
  [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
  Alternatives Considered: splitting transition responsibility between
  `CommandList` and `Presentation` means two places construct/trigger
  barriers instead of one, and `present()` would need its own
  `CommandList` to record into, complicating its otherwise simple
  wait-then-`vkQueuePresentKHR` contract.
- **Let each pass's execution callback call `transitionResource()`
  itself**, rather than have RenderGraph derive and insert transitions
  automatically. Rejected: this reintroduces exactly the kind of
  caller-authored, hand-scheduled GPU work AGENTS.md's mandatory-
  RenderGraph-path rule exists to prevent — a pass author could get the
  before/after states wrong, or forget a transition, with nothing checking
  it against the graph's actual dependency data.
- **Keep RenderGraph's execution entirely resource-state-agnostic and
  require the caller to insert transitions between `execute()` calls for
  each pass.** Rejected: this pushes the dependency-to-barrier
  responsibility onto the caller, defeating RenderGraph's whole purpose as
  "the central rendering abstraction" per AGENTS.md, and reintroduces a
  second, ad hoc scheduling surface alongside the compiled graph's own
  already-derived ordering.
- **Extend `RenderGraphBuilder`'s public resource-declaration API to
  accept a permanent, builder-scoped RHI binding at declaration time**,
  rather than a frame-scoped binding passed to `execute()`. Rejected:
  Spec 0005's builder is deliberately GPU-independent and reusable across
  `compile()` calls; permanently binding it to one frame's concrete
  `RenderTarget` would couple a construction-time object to a
  frame-lifetime resource, contradicting the independence Spec 0005's
  Human-Review-approved ownership model established.
- **Let RenderGraph submit and present internally, absorbing
  `Device::submit()`/`Presentation::present()` into `execute()`.**
  Rejected: this would make RenderGraph depend on `Presentation`, which
  [module_boundaries.md](../docs/architecture/module_boundaries.md) does
  not list as one of its dependencies, and would blur the line between
  "recording GPU work" (RenderGraph's job) and "frame orchestration"
  (Runtime's future job) that Spec 0005 and this spec both otherwise
  preserve.
