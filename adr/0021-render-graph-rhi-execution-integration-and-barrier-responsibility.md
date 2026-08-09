# ADR 0021: RenderGraph/RHI Execution Integration and Dependency-to-Barrier Responsibility

- **Status:** Accepted
- **Date:** 2026-08-09
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
  Approval recorded 2026-08-09; see
  [specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md)'s
  Human Review Approval note for the full approval record this ADR's
  Decision is part of, including the explicit confirmation that the two
  `execute()`-time guard checks (unbound `ResourceState`-tagged usage;
  a bound `RenderTarget` with a declared read usage) are adopted as
  specified.
- **Related Spec:** [specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md) (`Approved`)

## Context

Spec 0005 shipped RenderGraph's graph-description/compilation core with
**no RHI dependency**, explicitly deferring execution: "it does not
execute, submit, or simulate GPU work anywhere in this spec's scope."
Spec 0005 itself, in its own `Approved` Out of Scope / Future Work
section, anticipates exactly this: "A future RenderGraph-execution spec...
is expected to extend or complement this spec's compiled graph
description to actually record and submit GPU work, consuming both that
new RHI surface and this spec's compiled pass order and dependency
relations." This is the primary authority this ADR relies on for treating
RenderGraph's new RHI dependency as an anticipated continuation rather
than a boundary invented from nothing.
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
also already lists RenderGraph as depending on "RHI, Core" — corroborating,
but secondary, evidence: that document is explicitly marked "PROPOSED —
pending spec/ADR approval. Not as-built," so it is not, by itself,
authoritative under AGENTS.md's "one authoritative source" documentation
rule. This ADR is what actually makes the RenderGraph → RHI dependency a
reviewed decision, informed by both.

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

**RenderGraph gains a dependency on RHI**, realizing the dependency Spec
0005 itself already anticipated (see Context) — no *new* module boundary
concept is introduced (RenderGraph does not gain a dependency on Vulkan
Backend, Atlantis Platform, or Runtime; every existing forbidden-
dependency rule is unchanged), but this ADR, not a prior document, is
what makes RenderGraph's RHI dependency an actual, reviewed decision
rather than a still-open anticipation. RenderGraph's execution-phase code
references only RHI's
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

**Binding validity is checked, not assumed, at `execute()` time — two
rules, both guaranteed-detectable programmer errors:**

- **Every logical resource that participates in any `ResourceState`-tagged
  usage in the compiled graph must have a binding supplied to `execute()`.**
  A `ResourceState`-tagged usage against an unbound resource (one whose
  binding was omitted, or one that is not producer-less at all — see
  below) is a programmer error, checked when `execute()` is called, since
  that is the first point both the compiled graph's full usage set and the
  binding map are simultaneously available. Spec 0005's plain, *untagged*
  producer/reader logical resources (used purely for ordering, with no
  `ResourceState` and no RHI backing) remain fully legal and require no
  binding — they participate in no transition, exactly as in Spec 0005.
- **Binding a `RenderTarget` to a logical resource that has any declared
  read usage anywhere in the compiled graph is a programmer error.** This
  is what makes [ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)'s
  "a `RenderTarget` is exclusively a write target, so discarding from
  `ResourceState::Undefined` is always correct" claim an *enforced*
  property of this round's implementation rather than a documented
  assumption nothing actually checks — without this rule, nothing would
  stop a caller from declaring a `reads()` usage against the same logical
  resource a `RenderTarget` is bound to, silently invalidating that
  claim's premise. `execute()` can check this because binding happens
  after `compile()`, when the full, final usage set for every resource is
  already known.

Both checks use `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`, consistent with
[ADR-0009](0009-assertion.md) and this codebase's existing programmer-
error tiering — not a `Result`-typed recoverable error, since both
conditions are fully determinable from the caller's own inputs at the
call site.

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
fills the caller-provided `CommandList` (a borrowed reference — ownership
of the `CommandList` transfers to `Device` only later, at `submit()`, per
[ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)).
`Device::submit()` and `Presentation::present()`
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
- The two binding-validity checks (every `ResourceState`-tagged usage is
  bound; no bound `RenderTarget` has a read usage) turn
  [ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)'s
  write-only/always-`Undefined` premise from a documented assumption into
  something `execute()` actually enforces, closing a gap an earlier
  draft of this decision left implicit.

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
- The two binding-validity checks add a real, if small, `execute()`-time
  validation pass over the compiled graph's usage set that did not exist
  in Spec 0005's own scope — accepted as necessary, not optional, given
  what they protect (see Positive above).

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
- **Leave binding validity as an undocumented caller obligation, with no
  `execute()`-time check** (rely on manual verification/inspection alone,
  the way `RenderTarget`'s destruction precondition is handled). Rejected
  specifically for the "no read usage on a bound `RenderTarget`" rule: the
  entire always-`Undefined`-incoming-layout simplification
  ([ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md))
  depends on this being true, and `execute()` has everything it needs
  (the compiled graph's full usage set, the binding map) to check it
  cheaply — leaving a check this easy to add as an undetected precondition
  would be an unforced gap, unlike genuinely undetectable cases (e.g.
  handle-use-after-builder-destruction) where no reasonable check exists.
- **Let RenderGraph submit and present internally, absorbing
  `Device::submit()`/`Presentation::present()` into `execute()`.**
  Rejected: this would make RenderGraph depend on `Presentation`, which
  [module_boundaries.md](../docs/architecture/module_boundaries.md) does
  not list as one of its dependencies, and would blur the line between
  "recording GPU work" (RenderGraph's job) and "frame orchestration"
  (Runtime's future job) that Spec 0005 and this spec both otherwise
  preserve.

## Clarification (2026-08-09, not a change in conclusion)

Plan 0006's own review surfaced that this ADR's Decision text — "a
caller must bind each **producer-less** logical resource used by the
graph to a concrete RHI object" — reads ambiguously against the minimal
acceptance scenario this Decision exists to enable, where a pass **writes**
(clears) the bound `RenderTarget`, which gives that logical resource a
producer under Spec 0005's model. This clarification resolves the
ambiguity without changing this ADR's Decision or Consequences in any
way — it states what "producer-less" was always meant to describe, per
Human Review confirmation recorded in
[plans/0006-rhi-render-graph-frame-execution-foundation.md](../plans/0006-rhi-render-graph-frame-execution-foundation.md)'s
"Human Review Confirmations Received" section:

- **"Producer-less" describes the bound *physical* RHI object's origin**
  — a `RenderTarget` is supplied by `Presentation`, never created by
  RenderGraph itself, unlike a hypothetical future resource type
  RenderGraph might someday allocate on its own. It is not a constraint
  on the *logical* resource's producer count within the graph.
- **The logical resource a `RenderTarget` is bound to may have exactly
  one write producer** (e.g. the pass that clears it), governed by
  Spec 0005's single-producer rule
  ([ADR-0018](0018-render-graph-dependency-derivation-and-ordering.md))
  exactly as it governs every other logical resource in this codebase —
  unchanged, not reopened by this clarification.
- **The one additional, structurally-enforced constraint a bound
  resource carries is exactly [ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)'s
  own guard: no read usage anywhere in the graph.** This was always the
  operative rule; this clarification makes it the *only* stated
  constraint on binding, removing the "producer-less" phrase's potential
  to be read as an additional, conflicting requirement.

This is a documentation clarification of already-Accepted intent, not a
new decision, a superseding ADR, or a reopening of Human Review — the
`execute()` design this ADR's Decision section already describes
(dependency-to-barrier responsibility split, the two `execute()`-time
guard checks) is unaffected.
