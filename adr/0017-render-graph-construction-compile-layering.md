# ADR 0017: RenderGraph Construction and Compilation Layering

- **Status:** Accepted
- **Date:** 2026-08-09
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
  Approval recorded 2026-08-09; see
  [specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md)'s
  Human Review Approval note for the full 16-item approval record this
  ADR's Decision is part of.
- **Related Spec:** [specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md) (`Approved`)

## Context

RenderGraph needs an internal phase structure before any pass/resource
declaration API can be designed, because that structure determines when
validation, dependency derivation, and ordering can happen, and whether a
caller can observe or mutate a graph mid-derivation. Left undecided, this
gets settled implicitly by whatever the first implementation attempt
assumes — precisely the kind of uncontrolled architectural decision
[AGENTS.md](../AGENTS.md)'s Golden Rule exists to prevent, and one that
would be awkward to change later once a (not-yet-specced) Renderer starts
depending on RenderGraph's shape.

A further complication: cycle detection and dependency-derived ordering
fundamentally require a whole-graph view — they cannot be decided
correctly by validating each declaration in isolation as it arrives. This
forces some kind of two-phase model (accumulate, then process) rather
than a single always-valid mutable object. An earlier draft of this ADR
left open whether a builder remains usable after a failed compile, while
simultaneously requiring compile to be a "pure, repeatable" function of
the builder's state — those two positions are in tension: a function
cannot be both pure/repeatable over a builder's state and free to
invalidate that same builder as a side effect of evaluating it. A prior
revision resolved that tension directly instead of leaving it as an open
question.

**This revision fixes a further gap: the prior revision left the
builder's ownership/copy/move semantics, handle provenance, and the
compiled graph's independence from the builder entirely unstated —
public ownership/lifetime questions this codebase's own conventions (see
[AGENTS.md](../AGENTS.md) Ownership and lifetime rules) do not allow to
be settled implicitly during implementation.** Left unstated, at least
three concrete hazards fall out of that gap:

- If a handle is represented as a plain, builder-local integer index with
  no further provenance, two different builder instances can readily
  produce colliding index values (e.g. both vend index `0` for their
  first declared pass). A foreign handle from a different, live builder
  would then be indistinguishable from a valid local one by value alone,
  which makes the "a foreign handle is detectable" requirement this
  spec's Error Model depends on impossible to satisfy with that
  representation.
- If a handle instead carries the builder's own address as its
  provenance, that provenance silently breaks the moment the builder is
  moved to a new address — a handle obtained before the move would
  compare against a stale address afterward, either falsely flagging a
  still-valid handle as foreign or (worse) doing the reverse.
- If the compiled graph produced by `compile()` borrows the builder's
  internal declaration storage (a view or reference into the builder)
  rather than owning its own data, destroying the builder that produced
  it leaves every compiled graph it vended dangling — directly
  contradicting this ADR's own "compiled artifact is an independent,
  immutable value" claim below.

None of these are ordinary internal implementation details a Plan can
safely improvise; each is a public ownership/lifetime contract this ADR
needs to fix now, the same way construction/compile phase separation
itself was fixed above.

## Decision

- RenderGraph exposes a builder/description type used to declare passes,
  logical resources, and their usages during construction. A separate
  `compile()`-shaped operation attempts to transform that accumulated
  description into a graph.
- Construction and compilation are decoupled: the builder only
  accumulates declarations. No dependency derivation, cycle detection, or
  ordering happens as a side effect of any declaration call — all of it
  happens inside `compile()`.
- **`compile()` never mutates, consumes, or invalidates the builder, on
  either success or failure.** It reads the builder's accumulated state
  and produces a result; the builder remains exactly as usable afterward
  as it was before the call. This holds unconditionally — there is no
  "the builder is spent after one compile" mode and no "the builder is
  spent only after a failed compile" mode.
- Concretely, this means:
  - After a **successful** compile, the same (unmodified) builder may be
    compiled again and yields an equivalent compiled result — see
    [ADR-0018](0018-render-graph-dependency-derivation-and-ordering.md)
    for the determinism guarantee this relies on.
  - After a **failed** compile, the builder is still a valid object: the
    caller may inspect its diagnostics, and may compile it again,
    unmodified, to observe the same failure deterministically (compile's
    error outcome is as repeatable as its success outcome).
  - **This ADR does not require the builder to support removing,
    replacing, or editing an already-accumulated declaration in place.**
    "The builder remains valid" means it remains a well-defined object
    safe to keep using and to compile again — not that this spec provides
    an in-place correction/removal API. A caller that wants a different
    graph description is expected to discard the builder and construct a
    new one with the corrected declarations. Nothing here precludes a
    builder whose declaration API happens to still accept further
    additive declarations after a failed compile, but this ADR makes no
    claim that doing so will resolve a prior error — e.g. an illegal
    multiple-producer declaration is not fixed by adding an unrelated
    third pass.
- **The builder is the sole, exclusive owner of its accumulated
  declarations, and is non-copyable and non-movable.** There is exactly
  one builder instance per graph description; it cannot be duplicated,
  and it cannot be relocated to a new address once constructed. This
  directly resolves the address-based-provenance hazard above (an
  address that never changes for the object's whole lifetime is safe to
  use as provenance) and sidesteps having to define what a "copy" of an
  in-progress graph description would even mean for handle ownership.
- **A pass handle, a logical resource handle, and a `CompiledGraph`-local
  identity are three mutually distinct, strongly-typed concepts.** No two
  of them are interchangeable, and the type system prevents confusing one
  for another: **using a value of one of these types where a different
  one is expected is a compile-time type error, full stop — never a
  runtime condition, and never something a runtime assertion is
  responsible for catching.** This is a stronger, unconditional statement
  than "prevented where the type system can enforce it" — the type system
  is required to enforce it for all three types, with no residual runtime
  case left over.
- **Within a single handle type (pass handle, or logical resource
  handle), every handle is scoped to the specific builder instance that
  vended it.** Two — and only two — *runtime*, value/provenance-level
  misuse cases exist for a handle of the correct type, and both are
  guaranteed-detectable programmer errors (assertions):
  - a default/invalid handle (e.g. one that was never vended by any
    builder call), and
  - a handle vended by a *different* builder instance than the one the
    call is being made on, while that other builder instance is still
    alive.

  This is what makes the "foreign handle is detectable" requirement in
  [specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md)'s
  Error Model achievable, and is exactly what a bare, provenance-free
  local index (the first hazard above) cannot provide.
- **Using a handle after the builder instance that vended it has been
  destroyed is a lifetime precondition violation, not a guaranteed-
  detectable error.** This ADR does not require, and does not claim, that
  this case is reliably caught — it is the same category of caller
  obligation as using any other dangling reference under this
  repository's existing RAII ownership rules
  ([AGENTS.md](../AGENTS.md) Ownership and lifetime rules), not a new,
  weaker guarantee invented for RenderGraph specifically. This is
  distinct from, and must not be conflated with, the two guaranteed-
  detectable cases above (default/invalid handle; handle from a
  different, *currently live* builder).
- **Handle values are ordinary copyable value tokens.** Copying a handle
  does not transfer, share, or duplicate ownership of anything — it
  produces another reference to the same builder-scoped identity, valid
  under the same rules as the original.
- **A successful compile's result — the `CompiledGraph` — is an
  independently-owned, immutable value, entirely distinct from the
  builder.** It owns its own compiled-local representation of pass
  identity, execution order, and dependency relations; it does not
  borrow, reference, or otherwise depend on the builder's internal
  declaration storage. Concretely, this means:
  - The builder may be destroyed immediately after a successful
    `compile()` call without affecting the resulting `CompiledGraph`'s
    validity or completeness in any way — this directly resolves the
    borrowed-storage hazard above.
  - The builder may continue accepting further declarations after
    producing a `CompiledGraph`; doing so never affects any
    `CompiledGraph` already produced.
  - Two `CompiledGraph` values produced by separate `compile()` calls
    (whether on the same unmodified builder, per this ADR's own
    repeatability guarantee, or after further declarations) are
    independent objects — destroying one has no effect on the other or
    on the builder.
  - `CompiledGraph` is, at minimum, movable, so it can be returned by
    value from inside a `Result` and have ownership transferred cleanly.
    Whether it is additionally copyable is left to the Plan, provided
    either choice preserves this independent-ownership and immutability
    contract.
  - The compiled-local pass/resource identifiers `CompiledGraph` exposes
    are their own distinct, strongly-typed concept — not a builder pass
    handle, not a builder resource handle. Interpreting them never
    requires the originating builder to still be alive. Using one as a
    declaration handle on any builder (or vice versa) is, per the point
    above, a compile-time type error, not a runtime condition.
- Nothing in this spec's scope can mutate a `CompiledGraph` after it is
  produced. See
  [ADR-0018](0018-render-graph-dependency-derivation-and-ordering.md) for
  what data it carries.
- Compile returns an explicit `atlantis::Result`-shaped success/error
  outcome, consistent with [AGENTS.md](../AGENTS.md)'s error-handling
  rules (recoverable runtime errors are Result-typed, not exceptions). A
  failed compile never vends a partial or otherwise usable compiled
  graph — only `Ok(CompiledGraph)` or `Err(CompileError)`.
- This ADR does not attach any execution capability to the compiled
  graph. RenderGraph does not execute, submit, or simulate GPU work in
  this spec's scope — see
  [specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md)
  Non-Goals, which state this boundary directly against
  [AGENTS.md](../AGENTS.md)'s existing mandatory-RenderGraph-path rule and
  [ADR-0001](0001-rhi-backend-independence.md)'s existing backend-
  independence rule; no separate ADR is filed for it, because it
  introduces no new architectural decision beyond applying those two
  already-`Accepted` rules to a module that currently has nothing to
  execute against.

## Consequences

### Positive

- Clean phase separation lets GPU-independent unit tests exercise
  construction and compilation without any executor existing yet.
- A non-destructive, side-effect-free `compile()` needs no special
  recovery API — the builder simply keeps working, and a caller who wants
  a corrected graph can always build a new one from scratch.
- Immutability of the compiled result prevents a future Renderer from
  mutating a graph mid-frame, and makes the compiled graph safe to pass
  around/inspect without defensive copying.
- Non-copyable/non-movable builder plus builder-scoped, provenance-
  carrying handles close all three ownership hazards identified in
  Context: no cross-builder index collision, no address-based provenance
  breaking under a move that can no longer happen, and no compiled graph
  left dangling by borrowed storage.
- A `CompiledGraph` that independently owns its data enables the natural
  calling pattern of building, compiling, and immediately discarding the
  builder — e.g. `auto graph = builder.compile().value();` at the end of
  a scope that destroys `builder` — without the caller having to reason
  about the builder's lifetime relative to the graph's.
- Using `atlantis::Result` instead of an exception matches the same
  convention already established in Core, RHI, and Vulkan Backend
  ([specs/0001](../specs/0001-project-foundation.md),
  [specs/0003](../specs/0003-rhi-vulkan-windowed-foundation.md)), so
  RenderGraph does not introduce a second error-handling idiom.

### Negative / Trade-offs

- A two-phase model adds an extra step and an extra type (the builder vs.
  the compiled result) compared to a single mutable graph object —
  slightly more API surface for a caller to learn.
- Guaranteeing `compile()` never mutates the builder likely means compile
  computes derived data (dependency edges, order) into a separate
  structure rather than annotating the builder's own storage in place —
  a minor implementation cost, left to the Plan.
- A non-copyable, non-movable builder cannot be stored by value in a
  container or returned by value from a factory function that requires
  copyability/movability. Nothing in this spec's own scope needs to
  relocate an in-progress builder, so this is not a cost paid here — but
  a future caller with a real composition need (e.g. building a graph
  description across multiple function calls that each take builder
  ownership) would need a different pattern (e.g. heap allocation behind
  a smart pointer), which this ADR does not design.
- `CompiledGraph` independently owning everything it needs (rather than
  referencing builder storage) means `compile()` deep-copies pass/
  resource identity and dependency data into the compiled result instead
  of pointing back at the builder — a real allocation/copy cost, accepted
  as the price of a value that is genuinely safe to outlive the builder
  that produced it.
- If a future Renderer reconstructs its graph description every frame
  (likely, since frame content changes frame to frame), construction and
  compilation cost recurs every frame; this ADR does not address
  performance, which is left to the Plan/future profiling.

## Alternatives Considered

- **Single mutable graph object with incremental/implicit validation.**
  Rejected: cycle detection and dependency-derived ordering need a
  whole-graph view, so per-call incremental validation cannot correctly
  detect a cycle introduced by a later declaration, and blurs the
  "compiled artifact is immutable" property future multi-consumer/
  executor work is expected to rely on.
- **Eager per-declaration validation with no separate compile step.**
  Rejected for the same whole-graph-view reason above — a multi-producer
  conflict or a cycle may only become apparent once every declaration is
  in, not at the moment any single declaration call is made.
- **Compile consumes the builder** (on success, on failure, or both),
  requiring a fresh builder for any subsequent compile. Rejected: this
  directly contradicts the "compile is a pure, repeatable read over
  builder state" property — a consumed builder cannot be compiled a
  second time to verify the same result or the same failure recurs, which
  this spec's own determinism/repeated-compile Acceptance Criteria depend
  on. Consumption is also a hidden state transition triggered by a call
  that, on its face, looks like a read (`compile()` reading a
  description), which is a surprising contract for callers to discover.
- **Compile mutates the builder in place into its own compiled form** (no
  separate result type — the builder *becomes* the compiled graph).
  Rejected: this makes "was this compiled already, and with what result"
  a stateful question about the builder's own identity rather than a
  structural property of holding (or not holding) a separate,
  immutable compiled-graph value, and it is a second, different kind of
  hidden state transition from the "compile consumes the builder"
  alternative above — still incompatible with repeatable, side-effect-
  free compilation.
- **Non-consuming compile: the builder and the compiled graph are
  distinct values, and `compile()` never mutates the builder (this ADR's
  choice).** Adopted because it is the only option of the three that
  keeps `compile()` a pure, repeatable read with no hidden state
  transition, and keeps the compiled graph a genuinely separate,
  immutable value rather than a relabeling of the builder itself.
- **A copyable and/or movable builder.** Rejected for this round: copying
  raises an immediate handle-provenance question this ADR would otherwise
  have to answer (does a handle obtained from the original also identify
  the corresponding declaration in the copy? are they the same
  declaration or two independent ones?) with no current use case to
  motivate a specific answer; moving breaks address-based handle
  provenance unless a more elaborate stable-identity scheme is designed
  instead (see below). Neither cost is paid for any actual need in this
  spec's own scope, which only ever constructs one builder and compiles
  it in place. A future spec may revisit this if a real composition use
  case (e.g. assembling a graph description across ownership boundaries)
  appears.
- **A plain, builder-local integer index as a handle's sole
  representation, with no cross-builder provenance check.** Rejected:
  two different builder instances can readily produce colliding index
  values, making a foreign handle from a different, live builder
  indistinguishable by value alone from a valid local one — directly
  incompatible with this spec's "a foreign handle is detectable"
  requirement.
- **A global handle registry, generation counters, or handle
  recycling.** Rejected: this spec has no declaration-removal mechanism
  for a handle to ever need recycling from, and a global registry would
  introduce global mutable state this codebase's ownership rules
  ([AGENTS.md](../AGENTS.md)) do not permit without a stated, narrow
  exception — RenderGraph is not that exception.
- **`CompiledGraph` borrowing the builder's internal declaration storage**
  (a view or reference into the builder, rather than owning independent
  data). Rejected: this leaves every `CompiledGraph` a builder produced
  dangling the moment that builder is destroyed, directly contradicting
  the "compiled artifact is an independent, immutable value" property
  this ADR fixes above.
- **`CompiledGraph` reusing builder handles as its own compiled-pass/
  resource identity**, instead of a distinct compiled-local identity
  concept. Rejected: this would make interpreting a `CompiledGraph`
  implicitly depend on the originating builder's continued existence (or
  at least its handle-provenance data), reintroducing a lifetime coupling
  this ADR's independent-ownership decision above exists to remove.
