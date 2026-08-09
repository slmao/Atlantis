# Spec: Atlantis RenderGraph Foundation (GPU-Independent Graph Core)

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; content authored by the agent, reviewed and approved by a
  human per the Human Review Approval note below.
- **Created:** 2026-08-09
- **Human Review Approval (2026-08-09):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer for this branch) on 2026-08-09. All sixteen architectural
  decisions this spec's four revisions settled were reviewed and are
  **accepted as-is, unchanged from this document's own content**:

  1. GPU-independent graph-core scope for this spec (no RHI resource/
     command extension bundled in).
  2. Core-only module dependency for this round (no RHI dependency).
  3. The single-producer logical-resource model (producer→reader is the
     only derived edge kind).
  4. Producer-less logical resources as a legal, ownership-free
     externally-provided input token.
  5. An unconditional compile error for more than one producer of the
     same logical resource.
  6. No same-resource in-place read/write (rejected as a programmer
     error, no read-modify-write concept in this round).
  7. No caller-authored explicit pass-to-pass dependency edge of any
     kind — producer-derived edges are the only ordering mechanism.
  8. No automatic pass culling — every successfully declared pass is
     retained in the compiled result exactly once.
  9. Declaration order used **only** as the deterministic tie-break for
     otherwise-unordered passes, never for hazard/version inference.
  10. The builder is non-copyable, non-movable, and purely additive
      (append-only, no in-place declaration removal/editing API).
  11. `compile()` is non-consuming and non-mutating on the builder; the
      resulting `CompiledGraph` independently owns its own data.
  12. Builder-scoped handle provenance (default/invalid and cross-
      builder-while-live cases are guaranteed-detectable programmer
      errors) and the lifetime boundary (use-after-builder-destruction is
      an undetected lifetime precondition violation, not a guaranteed
      error).
  13. Non-unique, caller-provided diagnostic labels, and a deterministic
      dependency-cycle witness that identifies participating passes
      unambiguously even under duplicate labels.
  14. Phase 1's single-logical-frame-thread contract, with no declared
      concurrent-access guarantee for the builder or `CompiledGraph`.
  15. No resource-lifetime analysis, no physical resource
      binding/allocation, and no RHI/GPU execution capability anywhere in
      this spec's scope.
  16. Exactly two new ADRs are required for this spec —
      [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)
      and
      [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
      — no others.

  Following this approval: both ADRs above move to `Accepted` (see each
  ADR's own header) and this spec moves to `Approved`. **This is not a
  joint Spec + Plan Human Review** — no Plan exists yet, and none of the
  16 items above authorizes implementation. Per
  [AGENTS.md](../AGENTS.md), a Plan may now be drafted against this
  Approved spec, but implementation still requires that Plan to itself be
  written, reviewed, and pass its own (or a joint Spec+Plan) Human Review
  before any code is written.
- **Revision history:**
  - **2026-08-09 (revision 4):** Fixed a residual error-classification gap
    in revision 3: cross-type misuse among a pass handle, a logical
    resource handle, and a `CompiledGraph`-local identity was ambiguously
    worded as partly a runtime concern ("prevented statically... and
    otherwise this spec's stated policy for the remaining case"). This
    revision states unconditionally that these are three mutually
    distinct, strongly-typed concepts whose cross-type misuse is a
    **compile-time type error**, never a runtime condition and never an
    assertion; the guaranteed-detectable runtime programmer-error tier is
    now scoped to exactly two value/provenance cases *within* a single,
    already-correctly-typed handle (default/invalid; foreign, currently-
    live builder). The Error Model is now four tiers instead of three
    (compile-time type error; guaranteed-detectable runtime programmer
    error; lifetime precondition violation; recoverable compile error).
    No new ADR was filed — this extends
    [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md),
    consistent with revision 3.
  - **2026-08-09 (revision 3):** Fixed the builder/handle/`CompiledGraph`
    ownership model, which the prior revisions had left entirely
    unstated. The builder is now non-copyable and non-movable, and is the
    sole owner of its accumulated declarations. Pass handles and logical
    resource handles are distinct, strongly-typed, builder-scoped
    concepts; a default/invalid handle or a handle from a different,
    currently-live builder is a guaranteed-detectable programmer error,
    while using a handle after its originating builder has been destroyed
    is a lifetime precondition violation this spec does not claim to
    detect. `CompiledGraph` now independently owns its compiled-local
    data and is defined to safely outlive the builder that produced it;
    repeated `compile()` calls produce independent, equivalent values.
    Resolved the previously-open diagnostic-label question: non-unique,
    caller-provided diagnostic labels are included, used only for
    logging/error/test readability, never for identity or ordering; a
    cycle error's participating-pass witness is required to be
    deterministic. Fixed a minimal public thread-safety contract for the
    builder, its handles, and `CompiledGraph`, scoped to Phase 1's
    single-logical-frame-thread baseline. No new ADR was filed — all of
    this extends
    [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md),
    which already owns the construction/compile-layering decision this
    is part of.
  - **2026-08-09 (revision 2):** Removed the caller-authored explicit
    pass-to-pass dependency edge entirely — no current, approved use case
    justified shipping it, and it was a second scheduling-control surface
    alongside resource-usage derivation. Cycle detection is now explained
    and tested purely as a cross-resource property of producer-derived
    edges (see Proposed Design). Narrowed the post-failed-compile builder
    contract: the builder remains a valid, usable object, but this spec
    does not require or promise any in-place declaration removal/editing
    API. Fixed handle-misuse terminology to "invalid or foreign handle"
    (no generation counters, recycling, or cross-builder identity
    implied). Added an explicit "all declared passes are retained" pass-
    retention invariant and moved automatic pass culling to Non-Goals.
    Tightened producer-less/unused logical resource semantics.
  - **2026-08-09 (revision 1):** Replaced the original dependency-
    derivation model (which was internally inconsistent — see
    [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
    Context) with a single-producer logical resource model; removed
    lifetime-interval computation and the imported/transient resource
    classification as speculative, premature scope; and removed the
    standalone RenderGraph/RHI execution-boundary ADR in favor of stating
    that boundary directly against already-`Accepted`
    [AGENTS.md](../AGENTS.md) rules and
    [ADR-0001](../adr/0001-rhi-backend-independence.md). This spec's
    Architectural Impact requires exactly two ADRs
    ([ADR-0017](../adr/0017-render-graph-construction-compile-layering.md),
    [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md));
    the two additional ADRs the original Draft filed (lifetime model,
    execution boundary) were retracted, not merely deprioritized.
- **Related Plan(s):**
  [plans/0005-render-graph-foundation.md](../plans/0005-render-graph-foundation.md)
  (`Draft`) — not yet through a joint Spec + Plan Human Review; drafting
  this Plan is not itself an authorization of anything beyond drafting
  it.
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md),
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
  [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md),
  [ADR-0004](../adr/0004-phase1-threading-baseline.md), and
  [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)–[ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)
  (all `Accepted`). See **Architectural Impact** below — two new
  decisions are identified and drafted alongside this spec as
  [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)
  and
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  — both `Proposed`, neither yet `Accepted`.

## Summary

This spec proposes a minimal, **GPU-independent** Atlantis RenderGraph
Foundation: a backend-agnostic core for declaring passes and graph-local
logical resources, declaring each pass's read/write usage of those
resources under a **single-producer-per-resource** model, deriving
producer-to-reader dependency edges from that usage alone (no
caller-authored dependency edge exists), detecting cycles that arise
purely from those derived edges, and compiling a deterministic, immutable,
independently-owned description of a frame's pass execution order in
which every declared pass is retained. The builder that accumulates
declarations is non-copyable, non-movable, and the sole owner of its
data; the compiled result owns its own data and safely outlives the
builder. It does **not** execute GPU work, allocate any RHI resource,
record or submit a command, or touch Vulkan in any way, and it does
**not** compute resource lifetime, model resource physical properties,
classify resources as imported vs. transient, or cull any pass — it is
scope-limited to exactly the pass/dependency/ordering/ownership logic
that can be unit-tested without a Vulkan SDK or a GPU. This spec is now
**Approved**, per the Human Review Approval note above: a human has
reviewed and accepted all sixteen architectural decisions it settles.
Approval authorizes drafting a Plan against this spec; it does **not**
itself authorize implementation, which still requires that future Plan
to pass its own (or a joint Spec+Plan) Human Review.

## Motivation / Problem Statement

Spec 0003 delivered `Atlantis RHI`'s and `Atlantis Vulkan Backend`'s
**non-frame** foundation only: `Device` construction and `Presentation`'s
surface/swapchain lifecycle (`notifyResized`/`recreateIfNeeded`/metadata
queries). It deliberately does not define `RenderTarget`, any
`Buffer`/`Texture` resource type, any command list/command buffer
abstraction, acquire/present, pipeline/render-pass objects, queue
submission, or a resource-state/barrier API — see
[specs/0003-rhi-vulkan-windowed-foundation.md](0003-rhi-vulkan-windowed-foundation.md)'s
own Non-Goals and Out of Scope sections.

At the same time, [AGENTS.md](../AGENTS.md)'s Golden Rule and Architecture
Principles state that **RenderGraph is the mandatory path for all GPU
work** — no subsystem may submit ad hoc, hand-scheduled GPU work outside
it, and no code before it exists may invent an ad hoc path either. This
creates a real sequencing problem: RenderGraph cannot yet execute anything
against RHI, because RHI has nothing to execute against, but Renderer
cannot be specced (per AGENTS.md and
[docs/project-blueprint.md](../docs/project-blueprint.md) Milestone 3)
until RenderGraph exists.

This spec resolves that sequencing problem by scoping RenderGraph
Foundation to exactly the slice that is buildable and valuable **today**,
without inventing RHI's missing resource/command surface implicitly: the
**graph-description and compilation core** — pass declaration, logical
resource usage, dependency derivation, cycle detection, and deterministic
ordering — all backend-agnostic and GPU-independent. It leaves execution
(turning a compiled graph into real command recording and submission
against RHI) to a future spec, once RHI itself grows the resource/command
surface that execution requires, and it leaves resource lifetime,
physical realization, any resource-versioning model, and any pass-culling
mechanism to future specs as well, once real consumers exist to validate
those designs against.

Three revisions of this Draft have each fixed a distinct problem. The
first replaced an internally inconsistent dependency-derivation model
(automatic write-after-read/read-after-write/write-after-write edges,
plus a "legal if otherwise ordered" multiple-writer rule that its own
automatic ordering made unreachable) with a single-producer logical
resource model that is unambiguous by construction. The second removed
the caller-authored explicit pass-to-pass dependency edge that the first
revision had kept as an "escape hatch," since no concrete, approved use
case justified shipping a second pass-ordering control surface alongside
resource-usage derivation. This third revision fixes a gap the first two
left entirely unstated: the builder's ownership/copy/move semantics,
handle provenance (what makes a handle from a different builder
detectable as foreign), and whether the compiled graph independently owns
its data or dangerously depends on the builder staying alive. These are
public ownership/lifetime questions this repository's own conventions
(per [AGENTS.md](../AGENTS.md) Ownership and lifetime rules) do not allow
to be settled implicitly during implementation — see Proposed Design's
"Ownership and lifetime hazards this model must resolve" section for the
concrete problems this revision closes, and
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md) for
the full decision and rationale.

## Goals

- Define a pass-declaration API surface (candidate semantics, not final
  names/signatures — see Proposed Design) for describing a unit of work
  as backend-agnostic: an identifier (diagnostic only — see Requirements)
  and a set of resource usages.
- Define a logical-resource concept that is nothing more than a
  graph-local opaque identity connecting one producer to zero or more
  readers — decoupled from any RHI resource type, any physical property,
  and any resource-versioning concept.
- Define read and write usage declarations that attach a pass to a
  logical resource, under a single-producer-per-resource model.
- Derive producer-to-reader dependency edges automatically from declared
  resource usage — and **only** from declared resource usage — so pass
  authors describe *what* a pass touches, not *how* it must be ordered,
  with no separate, caller-authored ordering mechanism alongside it.
  Consistent with
  [docs/render_graph/README.md](../docs/render_graph/README.md)'s
  anticipated responsibility that ordering is "derived from the
  dependency graph rather than authored by hand per pass."
- Make an illegal multiple-producer declaration for the same logical
  resource an **actually reachable** compile error — not one that a
  same-round auto-ordering rule silently forecloses.
- Detect dependency cycles that arise from producer-derived edges across
  two or more passes and resources, and report them through an explicit
  Result/error type, never as an exception, silently-picked order, or
  ill-defined behavior.
- Produce a deterministic topological pass-execution order: the same
  graph description compiles to the same order, every time, using
  declaration order strictly as a tie-break among otherwise-unordered
  passes — never to infer resource hazard direction or producer/version
  semantics.
- Guarantee that **every successfully declared pass appears exactly once
  in the compiled pass order** — no dead-pass, unreferenced-pass, or
  output-root-based culling of any kind.
- Fix a minimal, unambiguous **ownership and lifetime contract** for the
  builder, its handles, and the compiled graph — non-copyable/non-movable
  builder; builder-scoped, provenance-carrying handles; an independently-
  owned compiled graph that safely outlives the builder — so this is not
  settled implicitly during implementation.
- Produce an **immutable, independently-owned compiled graph
  description** as compile's success artifact, carrying exactly the data
  needed to verify this spec's own behavior (pass order, dependency
  relations) — nothing more.
- Guarantee that `compile()` never mutates, consumes, or invalidates the
  builder it reads from, on either success or failure, so a caller can
  compile an unmodified builder repeatedly and always get the same
  result (success or failure).
- Ship all of the above as GPU-independent unit-testable logic that runs
  without a Vulkan SDK or a GPU, per
  [docs/process/testing-strategy.md](../docs/process/testing-strategy.md)
  layer 1.
- Leave a clear, documented boundary — not a silent gap — for a future
  RHI resource/command spec, a future RenderGraph-execution spec, a
  future resource-lifetime/versioning spec, and a future spec that adds
  any caller-authored ordering mechanism or pass culling, to build
  against.

## Non-Goals

Explicitly excluded from this spec's design and implementation:

- Atlantis Renderer, and any scene/mesh/camera/material concept.
- Shader compilation, reflection, or any Shader System concept.
- Graphics or compute pipeline object creation.
- Command list / command buffer implementation of any kind.
- Real GPU submission or queue scheduling of any kind. RenderGraph does
  not execute, submit, or simulate GPU work anywhere in this spec's
  scope: it does not call into any RHI command-recording surface (none
  exists yet per Spec 0003) and never references Vulkan Backend, per
  [AGENTS.md](../AGENTS.md)'s mandatory-RenderGraph-path rule and
  [ADR-0001](../adr/0001-rhi-backend-independence.md)'s backend-
  independence rule — both already `Accepted`; this spec does not need,
  and does not file, a separate ADR to restate them.
- Vulkan barriers, image layout transitions, or any synchronization
  primitive (semaphore/fence-equivalent).
- `Presentation` acquire/present, or any change to
  `Presentation`'s existing non-frame lifecycle contract
  ([Spec 0003](0003-rhi-vulkan-windowed-foundation.md)).
- `RenderTarget` creation, or any swapchain-image concept.
- RHI texture/buffer allocation of any kind.
- A GPU memory allocator (VMA or hand-rolled) — unaffected by, and does
  not resolve, [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md).
- **Resource lifetime of any kind** — no first-use/last-use interval, no
  lifetime data on the compiled graph, and no transient-resource aliasing
  or memory-reuse plan. There is currently no RHI resource type, no
  physical resource realization, and no consumer for lifetime data; a
  future spec may compute lifetime directly from a compiled graph's pass
  order and dependency data once it has a real reason to (see Out of
  Scope / Future Work), rather than this spec pre-computing it now.
- **Any imported/transient resource classification.** A logical resource
  in this spec's scope is a single, uniform concept regardless of whether
  it has a producer inside the graph — see Proposed Design's "Logical
  resources" section.
- **Any resource-versioning model**, in-place read-modify-write on a
  single resource identity, or multiple sequential writers to one
  logical resource — deferred to a future spec, per
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered.
- **Any resource physical property** — size, format, usage flags, memory
  properties, or physical ownership of any kind.
- **Any caller-authored pass-to-pass dependency edge or ordering
  override of any kind** — no explicit edge declaration, no
  `dependsOn`-shaped API, no before/after relation, no manual edge list,
  no priority/order override, and no integer sort key. The only ordering
  mechanism this spec provides is producer-derived, resource-usage-based
  edges — see
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered for why this is deferred to a future spec
  rather than included now.
- **Any automatic dead-pass, unreferenced-pass, or output-root-based pass
  culling.** Every successfully declared pass appears in the compiled
  pass order — see Proposed Design's "Pass retention" section and
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered.
- Async compute or multi-queue scheduling of any kind.
- Multi-threaded graph construction, compilation, or (future) recording;
  any job/task system. Phase 1's single-logical-frame-thread baseline
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)) is unchanged and
  this spec does not pre-embed any API for a future multi-threaded model.
- **Pass or resource identifier uniqueness as a correctness mechanism.**
  Diagnostic labels this spec's API accepts are never required to be
  unique, and nothing in this spec's semantics depends on them being
  unique — see Requirements and Error Model.
- **Any handle generation counter, handle recycling, or cross-builder
  stable/serializable identity scheme.** This spec has no declaration-
  removal or declaration-reuse mechanism for handles to need recycling
  from — see Proposed Design's "Handles and builder ownership" section.
- **Any in-place declaration removal, replacement, or editing API on the
  builder.** A caller that needs a different graph description constructs
  a new builder — see Proposed Design's "Construction/compilation
  layering" section.
- **Copying or moving a builder instance.** A builder is non-copyable and
  non-movable in this round — see Proposed Design's "Handles and builder
  ownership" section.
- **Any global handle registry, global handle allocator, or shared/
  reference-counted graph registry.** Handle and compiled-identity
  provenance is resolved per-builder and per-compiled-graph, never
  through global mutable state — see Proposed Design's "Handles and
  builder ownership" section.
- **Any cross-process, cross-frame, or serialization-stable identity**
  for a handle or a compiled-local pass/resource identifier.
- **Any declared thread-safety guarantee for concurrent access to a
  builder or a `CompiledGraph` beyond Phase 1's single-logical-frame-
  thread baseline.** Immutability of `CompiledGraph` is a mutation
  guarantee, not a concurrency guarantee — see Proposed Design's
  "Threading" section.
- GPU-driven rendering, neural rendering/shading, 3D Gaussian Splatting,
  or any world-model workload — per [AGENTS.md](../AGENTS.md), these are
  future phases that must not shape this spec's abstractions.
- Android, iOS, or Linux implementation of anything — Linux is not a
  target platform at all (per [AGENTS.md](../AGENTS.md)); this spec's
  logic is platform-independent by construction (no OS type anywhere in
  its scope) but is not tested on, or scoped to, any specific OS.
- Headless rendering and image regression testing infrastructure.
- Editor tooling or graph serialization of any kind.
- A second graphics backend, or any abstraction knob added "for" one.

## Requirements

### Functional

Candidate semantics only — concrete C++ type/method names and exact
signatures are left to the Plan, per the scoping direction for this
round; what follows fixes *behavior*, not spelling.

**Logical resources**

- A logical resource is a **graph-local opaque identity**, nothing more:
  it exists to connect one producing pass (if any) to zero or more
  reading passes within a single graph description. It does not
  reference, wrap, or presage any RHI resource type; it carries no size,
  format, usage-flag, or memory-property data; and it implies no physical
  ownership or allocation of any kind.
- The builder vends a logical resource handle through a single creation
  operation. There is **no separate "import" declaration and no
  "transient" declaration** — every logical resource is created the same
  way, and whether it ends up with a producer or not is an emergent
  property of how it is later used, not a property fixed at creation
  time.
- **Producer-less resources are supported and are the intended way to
  represent an externally-provided input token.** A logical resource
  that is never given a write usage by any pass in the graph is valid and
  may still be read; it is treated purely as a graph-compilation-time
  identity representing "something provided from outside this graph." It
  does not bind to any real RHI object, does not transfer or establish
  any ownership, and does not guarantee it will ever map to an "imported
  resource" concept a future spec might define — it introduces no such
  type itself. This capability is needed even at this spec's scope, since
  without it no graph could express a pass that only consumes
  graph-external state without itself producing anything first.
- **A logical resource that is declared but never given any usage at all
  (no producer, no reader) is a valid, harmless declaration**, not a
  compile error. It participates in no dependency relation. This spec
  does not require detecting, rejecting, or culling an unused resource
  declaration; whether the compiled graph's API surfaces unused resources
  at all is an implementation-shape detail left to the Plan.
- A logical resource's only externally-visible states, as far as this
  spec's compiled result is concerned, are: no producer, or exactly one
  producer (see Dependency Derivation below — more than one producer is
  a compile error, not a resource state a caller can observe as valid).

**Handles and builder ownership** (see
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)
for the full model and the reasoning behind it)

- **The builder is non-copyable and non-movable, and is the sole,
  exclusive owner of its accumulated declarations.** There is exactly one
  builder instance per graph description.
- **A pass handle, a logical resource handle, and a compiled graph's own
  compiled-local pass/resource identity (see Compilation below) are three
  mutually distinct, strongly-typed concepts.** No two of them are
  interchangeable. **Using a value of one of these types where a
  different one is expected is a compile-time type error — never a
  runtime condition, and never something an assertion is responsible for
  catching.** This is unconditional: the type system is required to
  prevent all cross-type misuse among these three concepts, with no
  residual case left for a runtime check.
- **Within a single handle type** (pass handle, or logical resource
  handle), every handle is scoped to the specific builder instance that
  vended it. Exactly two *runtime*, value/provenance-level misuse cases
  exist for a handle of the correct type, and both are guaranteed-
  detectable at the point of use — see Error Model:
  - a default/invalid handle, and
  - a handle vended by a *different*, currently-live builder instance.
- **Using a handle after the builder instance that vended it has been
  destroyed is a lifetime precondition violation, not a guaranteed-
  detectable error.** This spec does not require, and does not claim,
  that this case is reliably caught; it is the same category of caller
  obligation as using any other dangling reference under this
  repository's ownership rules ([AGENTS.md](../AGENTS.md) Ownership and
  lifetime rules), not a new or weaker guarantee invented for
  RenderGraph. This is distinct from, and must not be conflated with, the
  two guaranteed-detectable cases above.
- **Handle values are ordinary copyable value tokens.** Copying a handle
  does not transfer, share, or duplicate ownership of anything — it
  produces another reference to the same builder-scoped identity, valid
  under the same rules as the original, and usable only within its
  owning builder's single-threaded call context (see Threading).
- No handle is required to be globally unique, and this spec introduces
  no generation counter, handle recycling, UUID, or cross-builder/
  serialization-stable identity scheme — there is no declaration-removal
  or handle-reuse mechanism in this spec's scope for a handle to need
  recycling from.

**Graph construction**

- A builder/description object accumulates pass and logical-resource
  declarations. Nothing is derived, validated, or ordered while
  declarations accumulate — see
  [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md).
- A pass is declared with a diagnostic-only label (see "Diagnostic
  labels" below and Error Model) and, at declaration time or via
  subsequent calls scoped to that pass, a set of read/write usages
  against logical resource handles.
- Declaration order is meaningful only as the deterministic tie-break for
  passes the dependency graph does not otherwise order — see
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md).
  It plays no role in determining edge direction, producer/consumer
  status, or the legality of a resource declaration.
- The builder's declaration API is purely additive: there is no operation
  to remove, replace, or edit an already-accumulated pass or resource
  declaration — see Proposed Design's "Construction/compilation layering"
  section.

**Diagnostic labels**

- A pass may carry a caller-provided diagnostic label; a logical resource
  may optionally carry one too, under the same rules.
- Labels exist **solely for logging, compile-error messages, and test/
  debugging readability.** They are never required to be unique and never
  participate in identity, dependency derivation, or ordering — see
  Non-Goals.
- The builder owns (copies) whatever label data it needs; it never
  borrows a caller-supplied temporary string. Anything that later exposes
  a label — a `CompiledGraph`, or a compile error — likewise owns its own
  copy, never one borrowed from the builder or from caller-supplied
  temporary storage (consistent with `CompiledGraph`'s independent
  ownership — see Compilation below).
- A dependency-cycle compile error identifies its participating passes
  via a **deterministic witness**: their compiled-local identities, their
  owned diagnostic labels, or both. Both which cycle is reported (if a
  graph could be seen as containing more than one) and the order of
  passes within the reported witness are deterministic — never dependent
  on unordered container iteration, pointer values, or hash order. The
  concrete algorithm and container/type used for this is left to the
  Plan.

**Dependency derivation** (see
[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
for the full model and the reasoning behind it)

- A logical resource has **at most one producer**: the single pass, if
  any, that declares a write usage against it.
- If a resource has a producer, a derived ordering edge runs from that
  producer to every pass that declares a read usage against the same
  resource. Two passes that both only read the same resource get no edge
  between them.
- **More than one producer for the same logical resource is
  unconditionally a compile error** — not legalized by declaration order
  and not legalized by any other mechanism, because no other
  ordering-constraint mechanism exists in this spec's scope (see below).
- A pass declaring both a read and a write usage against the same logical
  resource is an **unsupported declaration**, rejected per the Error
  Model below — this round's model has no read-modify-write concept.
- **There is no caller-authored pass-to-pass dependency edge of any
  kind.** Producer-derived edges from resource usage are the only
  ordering-constraint mechanism this spec provides — see Non-Goals and
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered for why this is deferred rather than included.
- Cycle detection runs over the producer-derived edge set. A cycle spans
  **two or more distinct passes**; a single-pass self-loop cannot arise,
  because a pass being simultaneously the producer and a reader of the
  same resource is already rejected at declaration time (see above) —
  compile-time cycle detection therefore never needs to special-case a
  degenerate one-pass cycle. A cycle is a compile error carrying a
  deterministic witness sufficient to identify the participating passes
  (see "Diagnostic labels" above).
- An isolated pass (no usage relationship to any other pass) is valid and
  participates in the same deterministic ordering as any other pass.

**Pass retention**

- Every pass that is successfully declared appears in the compiled pass
  order **exactly once**. This holds regardless of whether the pass has
  any producer/reader relationship to any other pass (an isolated pass is
  retained) and regardless of whether a pass's own produced resource is
  ever read by anything within the graph (a producer with no readers is
  retained).
- This spec performs **no automatic dead-pass, unreferenced-pass, or
  output-root-based culling of any kind** — see Non-Goals and
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered.

**Compilation** (see
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md))

- Construction (declaring passes/resources/usages) and compilation
  (deriving dependencies, detecting cycles, computing order) are distinct
  steps; compilation is not an implicit side-effect of any declaration
  call.
- **`compile()` never mutates, consumes, or invalidates the builder, on
  either success or failure.** A caller may compile an unmodified builder
  repeatedly and get an equivalent result every time — the same success,
  or the same failure — see
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  for the determinism guarantee this relies on.
- **This spec does not require, and does not provide, any in-place
  correction, removal, or editing of an already-accumulated declaration.**
  After a failed compile, the builder remains a valid object whose
  diagnostics the caller may inspect and which the caller may compile
  again, unmodified, to observe the same failure deterministically. A
  caller that wants a different graph description constructs a new
  builder with the corrected declarations; this spec does not promise
  that continuing to add declarations to the same builder will resolve a
  prior error (e.g. an illegal multiple-producer declaration is not fixed
  by adding an unrelated pass).
- **A successful compile's result — the compiled graph — is an
  independently-owned, immutable value, entirely distinct from the
  builder.** It owns its own compiled-local representation of pass
  identity, execution order, and dependency relations; it does not
  borrow, reference, or otherwise depend on the builder's internal
  declaration storage. Concretely:
  - The builder may be destroyed immediately after a successful compile
    without affecting the resulting compiled graph's validity or
    completeness in any way.
  - The builder may continue accepting further declarations after
    producing a compiled graph; doing so never affects any compiled graph
    already produced.
  - Two compiled graph values produced by separate `compile()` calls
    (whether on the same unmodified builder or after further
    declarations) are independent objects — destroying one has no effect
    on the other or on the builder.
  - The compiled graph is, at minimum, movable, so it can be returned by
    value inside a `Result` and have ownership transferred cleanly.
    Whether it is additionally copyable is left to the Plan, provided
    either choice preserves independent ownership and immutability.
  - The compiled-local pass/resource identifiers the compiled graph
    exposes are their own distinct, strongly-typed concept — not a
    builder pass handle, not a builder logical resource handle.
    Interpreting them never requires the originating builder to still be
    alive. Using one as a declaration handle on any builder (or a builder
    handle as a compiled-local identity) is a compile-time type error,
    not a runtime condition — see Error Model.
- Nothing in this spec's scope mutates a compiled graph after it is
  produced.
- Compilation returns an explicit `atlantis::Result`-shaped success/error
  outcome (per [AGENTS.md](../AGENTS.md)'s error-handling rules), never an
  exception, for every recoverable graph-description problem (see Error
  Model below). A failed compile does not vend a partial or otherwise
  usable compiled graph.

**Compiled graph output**

The compiled graph description carries only what is needed to verify
this spec's own behavior:

- A deterministic pass execution order containing every successfully
  declared pass exactly once.
- The producer-derived dependency relations (or a read-only
  representation sufficient to check them, e.g. an edge list).
- Whatever compiled-local pass/resource identity (and, where included,
  owned diagnostic label) is needed to interpret the above.

It does **not** carry, and this spec does not design: lifetime intervals,
a barrier plan, any physical resource mapping, any RHI handle, an
execution callback, a queue assignment, or any synchronization data. This
data is not presented as the "only" possible handoff surface for a future
executor — a future spec may extend, replace, or internally consume it,
subject to that spec's own review; this spec fixes only what this
round's compiled graph itself contains.

- RenderGraph's public headers contain no `Vk*` type, no Vulkan header
  include, and no Atlantis Platform type — matching the existing
  RHI/Renderer boundary rules
  ([ADR-0001](../adr/0001-rhi-backend-independence.md),
  [docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)).
  No RHI dependency of any kind is required by this round's scope — see
  Proposed Design's Module Boundary section.

### Non-functional

- **Performance:** not a goal beyond "compiles a graph of the size
  Phase-1-scale scenes plausibly need without pathological (worse than
  polynomial) behavior." No frame-budget or micro-benchmark target is
  introduced by this spec.
- **Memory:** no GPU memory is allocated or referenced anywhere in this
  spec's scope. Host-side allocation for the builder/compiled graph uses
  ordinary RAII and standard containers, consistent with
  [AGENTS.md](../AGENTS.md)'s ownership rules — no custom allocator is
  introduced.
- **Portability (within the Vulkan-only Phase 1 constraint):** this
  spec's entire scope is OS- and backend-independent by construction — no
  Windows, Android, or Vulkan type appears anywhere in it. Verified by
  inspection (no such type in any RenderGraph header or source file), not
  by testing on multiple platforms, since none of this spec's logic
  depends on the platform at all.
- **Other:** no new third-party dependency is introduced. Unit tests use
  the existing Catch2 v3 framework already adopted per
  [ADR-0007](../adr/0007-test-framework.md).

## Proposed Design

### Module boundary (unchanged; this spec fills in RenderGraph inside it)

This spec does not move or reinterpret any existing module boundary. It
is the first concrete content of the `Atlantis RenderGraph` module
already named in
[docs/architecture/overview.md](../docs/architecture/overview.md) and
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
(both still carrying their `PROPOSED` banner):

```
RenderGraph (this spec's scope)
  depends on: Core only — this round has no RHI resource/command
              dependency to consume; Device/Presentation's non-frame
              lifecycle has nothing a graph-compilation step would call
  depended on by: (future) Renderer — not specced yet

Compiled graph description (deterministic pass order, dependency
relations) — immutable, independently owned, GPU-independent
  --> (future spec, not designed here) RHI resource/command extension
  --> (future spec, not designed here) RenderGraph execution extension
  --> (future spec, not designed here) resource lifetime/versioning
      extension
  --> (future spec, not designed here) any caller-authored ordering
      mechanism or pass-culling extension
```

`docs/render_graph/README.md` already states RenderGraph is "Built on
RHI + Core only." This spec does not contradict that long-term boundary;
it simply does not yet *use* any RHI surface. A future execution-focused
spec is expected to be the first RenderGraph work that actually depends
on RHI's (then-extended) resource/command surface.

### Ownership and lifetime hazards this model must resolve

Two earlier revisions of this Draft fixed the dependency model but left
the builder's ownership/copy/move semantics, handle provenance, and the
compiled graph's independence from the builder entirely unstated. That
gap is not a harmless implementation detail — left unresolved, at least
three concrete hazards fall out of it:

- **Handle collision across builders.** If a handle is represented as a
  plain, builder-local integer index with no further provenance, two
  different builder instances can readily produce colliding index values
  (e.g. both vend index `0` for their first declared pass). A foreign
  handle from a different, live builder would then be indistinguishable
  from a valid local one by value alone, making the "a foreign handle is
  detectable" requirement this spec's Error Model needs impossible to
  satisfy with that representation.
- **Address-based provenance breaking under a move.** If a handle instead
  carries the builder's own address as its provenance, that provenance
  silently breaks the moment the builder is relocated — a handle obtained
  before the move would compare against a stale address afterward.
- **A compiled graph dangling on builder destruction.** If the compiled
  graph produced by `compile()` borrows the builder's internal
  declaration storage (a view or reference into the builder) rather than
  owning its own data, destroying the builder that produced it leaves
  every compiled graph it vended dangling — directly contradicting the
  "compiled artifact is an independent, immutable value" property this
  spec otherwise claims.

These are public ownership/lifetime contracts, not internal
implementation details a Plan can safely improvise — see "Handles and
builder ownership" and "Construction/compilation layering" below for how
this revision resolves each one, and
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md) for
the full decision and the alternatives considered and rejected.

### Handles and builder ownership

The builder is non-copyable and non-movable, and is the sole owner of its
accumulated declarations — resolving the address-based-provenance hazard
above, since an address that never changes for the object's whole
lifetime is safe to use as provenance, and sidestepping the question of
what a "copy" of an in-progress graph description would mean for handle
ownership. Pass handles and logical resource handles are distinct,
strongly-typed, builder-scoped concepts; each carries enough provenance
that, while its originating builder is still alive, a handle vended by a
*different* builder instance is reliably distinguishable from one of its
own — resolving the handle-collision hazard above. Using a handle after
its originating builder has been destroyed is a lifetime precondition
violation this spec does not claim to detect, not a third kind of
guaranteed-detectable misuse — see Functional Requirements' "Handles and
builder ownership" subsection and Error Model. The full model and
rationale, including the alternatives considered (a copyable/movable
builder; a bare local-index handle; a global handle registry with
generation counters) and why each was rejected, is recorded in
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md).

### Logical resources and dependency derivation

A logical resource has at most one producer (see Functional Requirements
above). The only derived relationship is producer → reader; there is no
caller-authored dependency edge of any kind in this spec's scope.

**Cycle detection is still fully meaningful under this model, purely from
producer-derived edges across two or more resources.** For example:

```
Pass A: writes Resource X, reads Resource Y
Pass B: writes Resource Y, reads Resource X

Derived edges: A -> B (through X: A produces X, B reads X)
               B -> A (through Y: B produces Y, A reads Y)
```

`A` and `B` form a cycle with no caller-authored edge involved at all —
it arises entirely from each pass being the other's producer for a
different resource. The same pattern generalizes to three or more passes
each producing one resource and reading another in a cyclic chain. A
single pass can never form a degenerate self-loop, because a pass cannot
be declared as both producer and reader of the same resource (rejected at
declaration time — see Error Model); compile-time cycle detection
therefore only ever needs to find cycles spanning two or more distinct
passes.

The full model and the reasoning behind it — including why the
single-producer model was chosen over the original, internally
inconsistent draft, why the caller-authored dependency edge from a prior
revision was removed, and why a versioned-resource model and pass culling
are both deferred — is recorded in
[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md).

### Pass retention

Every successfully declared pass appears in the compiled pass order
exactly once, whether or not it participates in any dependency relation.
This spec performs no dead-pass, unreferenced-pass, or output-root-based
culling — see Functional Requirements' "Pass retention" subsection and
[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
Alternatives Considered for why culling is deferred rather than included.

### Construction/compilation layering

The builder accumulates declarations additively; `compile()` reads that
accumulated state and never mutates, consumes, or invalidates the
builder, on either success or failure. A successful compile produces a
separate, **independently-owned** immutable compiled graph value — it
does not borrow the builder's internal storage, so the builder may be
destroyed immediately afterward, or may keep accumulating further
declarations, without affecting any compiled graph already produced (see
"Ownership and lifetime hazards this model must resolve" above). This
spec does not provide an in-place declaration removal/editing API on the
builder — a caller that needs a different graph description constructs a
new builder with the corrected declarations; the existing builder remains
valid to inspect or to compile again unmodified, but is not required to
support edits to what it has already accumulated. The rationale for this
layering, including why a non-consuming, non-mutating `compile()` was
chosen over letting compile consume the builder or mutate it into its own
compiled form, and why the compiled graph independently owns its data
rather than borrowing the builder's, is recorded in
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md).

### Threading

Single logical frame thread, per
[ADR-0004](../adr/0004-phase1-threading-baseline.md): graph construction
and compilation happen on the same thread that will (in a future spec)
own frame orchestration. This spec fixes the following minimal public
thread-safety contract:

- **The builder is not thread-safe.** All declaration calls and all
  `compile()` calls on it happen on the single Phase 1 logical frame
  thread.
- **Builder handles are ordinary copyable value tokens**, but a handle
  must only be used within its owning builder's single-threaded call
  context; copying a handle does not transfer ownership or grant any
  cross-thread usage right.
- **The compiled graph is not declared thread-safe for concurrent access
  in this round**, including concurrent reads. Its immutability is a
  *mutation* guarantee (no public API mutates it after it is produced),
  not a *concurrency* guarantee — this spec does not reason about, and
  does not claim, that its underlying representation is safe to read from
  multiple threads simultaneously. A future spec may add an explicit
  thread-safety upgrade if a real need appears, backed by its own
  reasoning about the representation chosen at that point; this round
  makes no such claim and no such promise.
- No mutex, atomic, job/task system, or lock-free structure is introduced
  anywhere in this spec's scope. No public type introduced by this spec
  is documented as safe for concurrent use from multiple threads.

### Error model

Per [AGENTS.md](../AGENTS.md)'s error-handling rules, this spec fixes the
following classification — not merely categories, but which side of the
line each named case falls on, so this is not left to the Plan to decide.
There are four tiers: a compile-time type error, guaranteed-detectable
runtime programmer errors, a lifetime precondition violation this spec
does not claim to detect, and recoverable compile errors. The first tier
is deliberately listed separately from, and is not a kind of, the second
— see below.

**Compile-time type error (not a runtime condition of any kind, and not
an assertion):**

- **Using a pass handle, a logical resource handle, or a compiled graph's
  compiled-local pass/resource identity where a different one of those
  three is expected.** These are three mutually distinct, strongly-typed
  concepts (see Proposed Design's "Handles and builder ownership"); the
  type system is required to reject any cross-type usage among them at
  compile time, unconditionally. This case never reaches runtime, so it
  is never something an assertion, a `Result`, or any other runtime
  mechanism is responsible for catching.

**Guaranteed-detectable runtime programmer error (assertion, e.g.
`ATLANTIS_CHECK`/`ATLANTIS_ASSERT`)** — misuse that is fully judgable from
the information available at a single API call's own entry point, while
every builder instance involved is still alive, **for a handle already
known (by its type) to be a pass handle or a logical resource handle**:

- **A default/invalid handle**, or **a handle vended by a different,
  currently-live builder instance** than the one the call is being made
  on. See Proposed Design's "Handles and builder ownership" for why this
  is reliably detectable by construction, unlike the case below. These
  are the *only* two runtime misuse cases this spec defines for a handle
  of the correct type — there is no third, cross-type runtime case, since
  that is fully handled by the compile-time tier above.
- A pass declaring both a read and a write usage against the same logical
  resource — checkable against that one pass's own accumulated usage
  state at the moment the conflicting usage is added, regardless of which
  usage (read or write) was declared first.

**Lifetime precondition violation (not a guaranteed-detectable error —
out of this spec's defined-behavior guarantees):**

- **Using a handle after the builder instance that vended it has been
  destroyed.** This spec does not require, and does not test for, this
  case being caught; it is the same category of caller obligation as
  using any other dangling reference under this repository's ownership
  rules ([AGENTS.md](../AGENTS.md) Ownership and lifetime rules). This is
  a deliberately different tier from the guaranteed-detectable cases
  above and must not be conflated with them.

**Recoverable compile error (explicit `atlantis::Result`, never an
exception)** — conditions that can only be determined by observing the
graph as a whole, across more than one pass's declarations:

- More than one producer (write usage) declared for the same logical
  resource, across two or more different passes.
- A dependency cycle spanning two or more passes, arising from
  producer-derived edges (the only edge kind this spec has — see
  Dependency Derivation).

**Not an error:**

- An empty graph (no passes declared) compiling successfully to an empty
  compiled result — a graph legitimately having nothing to do in a given
  frame is not a defect.
- An isolated pass, a producer with no readers, or a producer-less
  logical resource used only for reads — all retained, per Pass
  Retention above.
- A logical resource that is declared but never given any usage at all
  (no producer, no reader).
- A duplicate diagnostic label on two different passes or resources —
  labels are diagnostic-only and are never required to be unique; nothing
  in this spec's semantics depends on uniqueness (see Non-Goals). Passes
  and resources are distinguished by the builder-scoped handle identity
  the builder vends (or, for a compiled graph, its own compiled-local
  identity), never by their diagnostic label.
- **The builder being destroyed after producing a compiled graph.** A
  compiled graph does not depend on its originating builder's continued
  existence, so builder destruction is never a compiled-graph error
  condition of any kind — see Compilation above.

The exact enumeration of error cases (as a concrete enum or equivalent)
is **not fixed by this spec** — this spec fixes the semantics and the
four-tier classification above in full; the Plan stage fixes only the
concrete type/spelling.

## Architectural Impact

This spec introduced architecture and required **two** new ADRs before it
could move from `Draft`/`In Review` to `Approved`, per
[AGENTS.md](../AGENTS.md). Neither was decided by this spec's prose
alone — each was filed as its own ADR, and both reached `Accepted`
alongside this spec's own approval (see the Human Review Approval note
above):

1. **RenderGraph construction/compilation layering and ownership** — how
   graph declaration (a non-copyable, non-movable, purely-additive
   builder) and compilation (pure, repeatable, Result-returning, and
   never destructive to the builder on either success or failure, with
   no in-place declaration-editing API) are separated; why the builder
   and its handles have the ownership/provenance model they do; and why
   the compiled artifact is immutable and independently owned, safe to
   outlive the builder that produced it. Filed as
   [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md).
2. **Dependency derivation rules and deterministic ordering** — the
   single-producer logical resource model, the producer-to-reader
   derivation rule, the unconditional multiple-producer compile error,
   cross-resource cycle detection, the declaration-order tie-break, the
   all-passes-retained invariant, and why no caller-authored dependency
   edge or pass culling is included in this round. Filed as
   [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md).

This spec's ownership/handle/`CompiledGraph`-independence decisions (this
revision's own additions) extend ADR-0017 rather than requiring a new
ADR: they are part of the same construction/compile-layering decision
ADR-0017 already owns, not a separate architectural surface.

**This round retracts the two additional ADRs an earlier revision of this
Draft filed** (a resource-lifetime-model ADR and a RenderGraph/RHI
execution-boundary ADR). Both are removed, not merely deprioritized,
because on review neither represents a new architectural decision this
round actually needs to make:

- Resource lifetime is now entirely out of this round's scope (see
  Non-Goals) — there is nothing to record a decision about until a
  future spec actually computes and consumes lifetime data.
- The execution boundary (RenderGraph does not execute GPU work) is a
  direct, unmodified application of two already-`Accepted` rules —
  [AGENTS.md](../AGENTS.md)'s mandatory-RenderGraph-path principle and
  [ADR-0001](../adr/0001-rhi-backend-independence.md)'s backend-
  independence rule — to a module that currently has nothing to execute
  against. Restating an existing Accepted rule as a new ADR would not add
  information; it is stated instead as this spec's own Non-Goals, exactly
  as Spec 0003 stated many of its own boundaries as Non-Goals without a
  dedicated ADR for each one.

No existing `Accepted` ADR's conclusions are restated, reopened, or
modified by this spec or by the two ADRs above — each new ADR references
and builds on the existing ones (particularly ADR-0001, ADR-0003, and
ADR-0004) without altering them. **Architectural Impact for this spec is
not "None"** — RenderGraph is a new module boundary with a new public API
surface, exactly the kind of change AGENTS.md's "What counts as
significant" section requires the full Spec → Plan → Human Review path
for.

## Alternatives Considered

- **Scope this spec to the GPU-independent graph core only (recommended,
  adopted).** Chosen because it is fully buildable and testable today
  without inventing any RHI surface ahead of its own review, and because
  it gives a future RHI-resource/command spec a settled, reviewed
  contract (the compiled graph description) to build execution against,
  rather than requiring both to be designed under the same pressure at
  once.
- **Extend this spec to also add the minimal RHI resource/command API
  RenderGraph would need to execute something real (evaluated, not
  adopted).** Considered and rejected: it would require new RHI public
  API (a command-list abstraction, a resource-state/barrier
  representation, and a resource type) that has never had its own
  spec/ADR review, all of which
  [specs/0003](0003-rhi-vulkan-windowed-foundation.md) deliberately left
  undesigned; it would compound ownership/lifetime/threading decisions
  that belong to their own spec/ADR (extending
  [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md) and
  [ADR-0004](../adr/0004-phase1-threading-baseline.md)); and bundling
  does not make that future work easier, only less reviewable, since
  reviewers would be asked to approve RenderGraph's graph-core design and
  RHI's next major surface expansion in the same pass.
- **The dependency-derivation model this Draft originally proposed**
  (automatic WAR/RAW/WAW edge derivation, multiple writers legal if
  "otherwise ordered"). Rejected as internally inconsistent — see
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Context for the specific contradiction and Alternatives Considered for
  why the single-producer model was chosen instead.
- **Retaining a caller-authored explicit pass-to-pass dependency edge**
  (an earlier revision of this Draft's design). Deferred, not permanently
  foreclosed: there is no current, approved use case for it, and it would
  be a second pass-ordering control surface alongside resource-usage
  derivation with no concrete need to validate its shape against. If a
  real workload later needs to express an ordering constraint resource
  usage cannot capture, a future spec should decide the mechanism against
  that real case — see
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered.
- **A versioned/history resource model**, allowing multiple sequential
  writers and in-place read-modify-write. Not rejected as wrong — deferred
  to a future spec once a real consumer motivates the version-identity
  and binding-rule design it needs; see
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered.
- **Automatic dead-pass / unreferenced-pass / output-root-based pass
  culling.** Deferred, not adopted: culling requires a real notion of a
  graph's "output" or "root," which this spec does not define — there is
  no Renderer yet to say what a frame's actual outputs are. This spec
  retains every declared pass unconditionally instead; see
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered.
- **A copyable and/or movable builder.** Rejected for this round: copying
  raises an immediate handle-provenance question (does a handle from the
  original also identify the corresponding declaration in the copy?) with
  no current use case to motivate an answer; moving breaks address-based
  handle provenance. Neither cost is paid for any actual need in this
  spec's own scope. See
  [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)
  Alternatives Considered.
- **A plain, builder-local integer index as a handle's sole
  representation, with no cross-builder provenance.** Rejected: this
  cannot distinguish a foreign handle from a different, live builder from
  a valid local one when the two builders' indices collide — see
  [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)
  Alternatives Considered.
- **A global handle registry, generation counters, or handle recycling.**
  Rejected: this spec has no declaration-removal mechanism for a handle
  to ever need recycling from, and a global registry introduces global
  mutable state this codebase's ownership rules do not permit without a
  stated exception — see
  [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)
  Alternatives Considered.
- **A compiled graph that borrows the builder's internal declaration
  storage** instead of owning independent data. Rejected: this leaves
  every compiled graph dangling the moment its builder is destroyed,
  contradicting this spec's own "independently-owned, immutable value"
  requirement — see
  [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)
  Alternatives Considered.
- **Computing resource lifetime (first/last-use intervals) in this
  round.** Rejected in this revision: there is no RHI resource type, no
  physical realization, and no consumer for that data yet; a future spec
  can derive it directly from a compiled graph's pass order and
  dependency data once one exists, without this round needing to
  pre-commit to a specific lifetime representation now.
- **A dedicated ADR for the RenderGraph/RHI execution boundary.** Rejected
  in this revision in favor of stating the boundary as this spec's own
  Non-Goals: the boundary is a direct application of already-`Accepted`
  [AGENTS.md](../AGENTS.md) and [ADR-0001](../adr/0001-rhi-backend-independence.md)
  rules, not a new decision — see Architectural Impact.
- **Copy an existing commercial/open-source RenderGraph API's shape
  wholesale.** Rejected as a design method: Atlantis's own module
  boundaries (no RHI resource/command surface yet, Result-based error
  model, no exceptions, single-frame-thread baseline) differ enough from
  typical reference implementations that copying a concrete API shape
  would either not fit or would silently import assumptions this
  repository's own ADRs haven't made. The *behavior* this spec proposes
  is a common pattern in the field, but the concrete API is Atlantis's
  own, left to the Plan.
- **Non-deterministic or "any valid topological order" compilation.**
  Rejected — see
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered.
- **Explicit-edges-only dependency model (no usage-derived
  dependencies at all).** Rejected — see
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered.
- **Letting `compile()` consume the builder, or mutate it in place into
  the compiled result (no separate compiled-graph value).** Rejected —
  see [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)
  Alternatives Considered for the comparison of all three options and why
  a non-consuming, non-mutating `compile()` was chosen.

## Testing & Verification Plan

- **Unit tests:** the entirety of this spec's scope is GPU-independent
  logic and must be exercised by unit tests that run without a Vulkan
  device, per
  [docs/process/testing-strategy.md](../docs/process/testing-strategy.md)
  layer 1, using the existing Catch2 v3 framework
  ([ADR-0007](../adr/0007-test-framework.md)). At minimum, tests must
  cover:
  - An empty graph compiles successfully to an empty compiled result.
  - A single-pass graph compiles successfully.
  - One producer, one reader: the derived edge and resulting order are
    correct.
  - One producer, multiple readers (fan-out): every reader is ordered
    after the producer; readers are not ordered relative to each other.
  - Multiple independent producer/reader pairs on unrelated resources
    (no shared resource usage) compile to a deterministic order driven by
    declaration order between the unrelated groups.
  - Multiple readers of the same resource do **not** produce an edge
    between those readers (verified by a case that would fail an
    ordering assertion if it wrongly did).
  - Multiple producers declared for the same logical resource is
    rejected as a compile error, unconditionally.
  - A single pass declaring both a read and a write usage against the
    same logical resource is rejected per the Error Model (programmer
    error).
  - A **two-pass, two-resource cycle formed entirely from producer-
    derived edges** (pass A writes X and reads Y; pass B writes Y and
    reads X) is detected and reported as a compile error, with a
    deterministic witness identifying both passes.
  - A longer cycle spanning three (or more) passes and resources, formed
    entirely from producer-derived edges, is likewise detected and
    reported as a compile error.
  - Repeated compilation of the same cyclic graph description reports an
    equivalent (deterministic) witness every time.
  - A dependency cycle whose participating passes carry **duplicate
    (non-unique) diagnostic labels** still produces a deterministic,
    unambiguously identifiable witness (e.g. via compiled-local identity,
    independent of the label text) — duplicate labels must not make the
    reported witness ambiguous about which passes actually participate.
  - An isolated pass (no usage relationship to any other pass) compiles
    successfully, is retained in the compiled result, and participates in
    the deterministic tie-break like any other pass.
  - A producer pass whose resource is never read by anything in the graph
    is retained in the compiled result (no dead-pass culling).
  - A logical resource that is declared but never used by any pass (no
    producer, no reader) is a valid declaration and produces no
    dependency relation.
  - Every successfully declared pass appears in the compiled pass order
    exactly once, across all of the above graph shapes.
  - Declaration order determines compiled order only among otherwise-
    unordered passes (a test that would fail if declaration order were
    instead influencing edge direction or producer legality).
  - Repeated compilation of an unmodified, already-accumulated graph
    description yields an identical result (same order, same dependency
    relations) every time.
  - A failed compile does not vend a partial or otherwise usable compiled
    graph.
  - After a failed compile, the same builder remains a valid object and
    can be compiled again, unmodified, to observe the same failure
    deterministically. (This spec does not test, and does not require, an
    in-place fix-and-retry workflow on the same builder — a corrected
    graph is tested by constructing a new builder with the corrected
    declarations.)
  - The builder type is not copy-constructible and not move-constructible
    (a compile-time property, verified as such — e.g. via a
    `std::is_copy_constructible`/`std::is_move_constructible`-style static
    check — not a runtime test).
  - A pass handle type, a logical resource handle type, and a compiled
    graph's compiled-local pass/resource identity type are three
    mutually distinct types with no implicit conversion between any pair
    (a compile-time property, verified as such — not a runtime test,
    since this spec defines no runtime case for cross-type misuse at
    all).
  - A default-constructed (or otherwise never-vended) handle triggers the
    programmer-error/assertion policy when used.
  - A handle vended by one builder instance, used on a *different*,
    concurrently-alive builder instance, triggers the programmer-error/
    assertion policy.
  - Handles created by a builder remain valid and usable for that
    builder's entire lifetime (ordinary further declarations never
    invalidate an earlier handle).
  - After a successful compile, destroying the builder leaves the
    resulting compiled graph fully and correctly queryable (pass order,
    dependency relations, and diagnostic content, if any, all remain
    intact).
  - After a successful compile, continuing to add declarations to the
    (still-alive) builder does not change the already-produced compiled
    graph.
  - Two `compile()` calls on the same unmodified builder produce two
    independent compiled-graph values that compare as equivalent (same
    order, same dependency relations).
  - Destroying one compiled graph does not affect another compiled graph
    produced from the same or a different builder, nor the builder
    itself.
  - Duplicate diagnostic labels on two different passes (or resources)
    are legal and do not affect compiled identity, dependency relations,
    or order.
  - No global handle registry, global handle allocator, or other global
    mutable graph-related state exists anywhere in this spec's
    implementation (verifiable by inspection).
  - RenderGraph's public headers contain no `Vk*` type, no
    `#include <vulkan/...>` type, and no RHI resource type — verifiable
    by inspection/grep, mirroring Spec 0003's equivalent RHI acceptance
    criterion.

  **Explicitly not tested:** using a handle after the builder instance
  that vended it has been destroyed. This is a lifetime precondition
  violation (see Error Model), not a defined-behavior case this spec
  guarantees detection for — a dynamic test exercising it would be
  exercising undefined behavior, which this spec does not require or
  sanction, consistent with how this repository's existing RAII/ownership
  conventions are tested elsewhere.
- **Headless integration tests:** not applicable — this spec's scope
  performs no GPU work of any kind, so there is nothing for a headless
  integration test (per
  [testing-strategy.md](../docs/process/testing-strategy.md) layer 2) to
  exercise.
- **Image regression tests:** not applicable — nothing is rendered.
- **Vulkan Validation Layers:** not applicable — no Vulkan call is made
  anywhere in this spec's scope.
- **Manual verification:** not required for this spec's scope, since
  every behavior it defines is exercisable and observable through unit
  tests alone; unlike Spec 0003, there is no windowed/interactive
  component to this spec.

## Acceptance Criteria

- [ ] RenderGraph's public headers contain no `Vk*` type, no
      `#include <vulkan/...>`, no Atlantis Platform type, and no RHI
      resource type — verifiable by inspection/grep.
- [ ] No Vulkan call, and no RHI command-recording call (none exists to
      call), is made anywhere in this spec's implementation.
- [ ] No GPU-required test exists anywhere in this spec's test suite —
      every test runs without a Vulkan device.
- [ ] No rendering output, image, or `RenderTarget` is produced,
      referenced, or asserted on anywhere in this spec's implementation
      or tests.
- [ ] An empty graph compiles successfully.
- [ ] A single pass compiles successfully.
- [ ] A single-producer, single-reader graph and a single-producer,
      multiple-reader (fan-out) graph each compile to the order the
      producer/reader relationship implies.
- [ ] Multiple readers of the same resource produce no edge between those
      readers.
- [ ] Independent producer/reader groups compile to a deterministic,
      declaration-order-driven order between the groups.
- [ ] Declaring more than one producer for the same logical resource is
      rejected as a compile error in every case, unconditionally.
- [ ] A pass declaring both a read and a write usage against the same
      logical resource is rejected as a programmer error.
- [ ] A two-pass, two-resource cycle formed entirely from producer-
      derived edges is detected and reported as a compile error with a
      deterministic participating-pass witness.
- [ ] A longer (three-or-more-pass) producer-derived cycle is likewise
      detected and reported as a compile error.
- [ ] A cycle whose participating passes carry duplicate diagnostic
      labels still produces a deterministic, unambiguously identifiable
      witness — duplicate labels never make a reported witness ambiguous.
- [ ] An isolated pass, and a producer pass with no readers, are each
      retained in the compiled result — no dead-pass or
      unreferenced-pass culling occurs anywhere in this spec's
      implementation.
- [ ] A declared-but-unused logical resource (no producer, no reader) is
      accepted without error and produces no dependency relation.
- [ ] Every successfully declared pass appears in the compiled pass order
      exactly once, across every tested graph shape.
- [ ] The builder type is non-copyable and non-movable (a compile-time
      property).
- [ ] Pass handles, logical resource handles, and compiled-local pass/
      resource identities are three mutually distinct types that cannot
      be used interchangeably at runtime — cross-type misuse is a
      compile-time error, never a runtime condition and never an
      assertion (a compile-time property).
- [ ] A default/invalid handle, and a handle vended by a different,
      currently-live builder instance of the *same* handle type, each
      trigger the programmer-error/assertion policy — no generation
      counter, handle recycling, or cross-builder identity scheme is
      implemented or required to achieve this.
- [ ] This spec does not claim, test, or require detection of a handle
      being used after its originating builder was destroyed — that case
      is documented as a lifetime precondition violation, not a tested
      behavior.
- [ ] A successful compile's resulting compiled graph remains fully valid
      and queryable after the builder that produced it is destroyed.
- [ ] Continuing to add declarations to a builder after a successful
      compile never changes a compiled graph already produced by that
      builder.
- [ ] Two `compile()` calls on the same unmodified builder produce
      independent, equivalent compiled-graph values; destroying one never
      affects the other.
- [ ] Duplicate diagnostic labels are legal and never affect identity,
      dependency relations, or compiled order.
- [ ] Repeated compilation of an unmodified graph description is
      deterministic — identical order and dependency relations every
      time.
- [ ] A failed compile never vends a partial compiled graph, and never
      invalidates the builder: the same builder can be compiled again,
      unmodified, to observe the same failure deterministically. No
      in-place declaration removal/editing API is implemented or required
      by this spec.
- [ ] No mutation of a compiled graph description is possible through any
      public API this spec introduces.
- [ ] No caller-authored pass-to-pass dependency edge, `dependsOn`-shaped
      API, before/after relation, manual edge list, priority/order
      override, or integer sort key is implemented anywhere in this
      spec's scope — the only ordering mechanism is producer-derived,
      resource-usage-based edges.
- [ ] No mutex, atomic, job/task system, or lock-free structure is
      introduced anywhere in this spec's implementation, and no public
      type is documented as thread-safe for concurrent use beyond Phase
      1's single-logical-frame-thread baseline.
- [ ] No global handle registry, global handle allocator, or other global
      mutable graph-related state exists anywhere in this spec's
      implementation.
- [ ] No lifetime interval, imported/transient resource classification,
      or resource physical property (size/format/usage/memory) appears
      anywhere in this spec's public API or compiled graph output.
- [ ] No `src/renderer/`, Shader System, or RHI resource/command source
      is created or modified by this spec's implementation.
- [x] Both ADRs listed in Architectural Impact
      ([ADR-0017](../adr/0017-render-graph-construction-compile-layering.md),
      [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md))
      reach `Accepted` before this spec is marked `Approved` — satisfied
      2026-08-09 (see Human Review Approval note above); this checkbox
      gates spec approval, not implementation, and every other checkbox
      in this section still describes a property the future
      implementation must satisfy, not one already verified.

## Risks & Open Questions

**Resolved by Human Review Approval (2026-08-09)** — see the Human Review
Approval note above for the full 16-item record. These were architectural
questions this spec surfaced for a human to decide; a human has now
decided each of them, and none is reopened by this section:

- **The single-producer logical resource model is accepted** as this
  round's dependency model, over adopting a versioned/history resource
  model now — see checklist item 3 and
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered for the deferred alternative.
- **Producer-less logical resources (externally-provided input tokens
  with no producer pass) are accepted** as a capability at this round's
  scope — see checklist item 4 and Proposed Design's "Logical resources"
  Functional Requirements subsection.
- **The GPU-independent-graph-core scope boundary is accepted** — this
  spec does not extend RHI with a resource/command surface; that remains
  a future spec's work — see checklist item 1 and Alternatives Considered
  above.
- **The non-copyable, non-movable builder is accepted** as this round's
  ownership model, over a more elaborate stable-identity scheme that
  would allow a movable or copyable builder — see checklist item 10 and
  [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)
  Alternatives Considered.

**Remaining open — Plan-level detail, not an architectural question
requiring further Human Review:**

- **Whether `CompiledGraph` should additionally be copyable**, beyond the
  minimum movability this spec requires. Left to the Plan, provided
  whatever is chosen preserves independent ownership and immutability —
  see Functional Requirements' Compilation subsection.
- **Concrete error enumeration** — this spec fixes error *semantics* and
  the four-tier classification in full (see Error Model) but leaves the
  concrete enum/type spelling to the Plan.
- **Test target naming and file layout** — left to the Plan stage; this
  round does not fix `tests/render_graph/` internal structure beyond
  noting it should mirror the existing `tests/rhi/`,
  `tests/vulkan_backend/` CMake pattern
  (`add_executable`/`catch_discover_tests`, per
  [ADR-0007](../adr/0007-test-framework.md) and
  [ADR-0010](../adr/0010-cmake-structure.md)).
- **Exact public API shapes** (handle representation, the compiled-local
  identity representation, the diagnostic-label storage type, exact
  method names) are left to the Plan in full — this spec fixes behavior,
  not spelling.

The following were open questions in an earlier revision of this Draft
and are **no longer open** — settled by this round's own Decision, not
deferred further:

- Whether a builder remains usable after a failed compile: **yes,
  unconditionally**, on both success and failure — but without any
  in-place correction/removal API; see
  [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md).
- The overall assertion-vs-Result classification principle, and which
  named cases fall on each side: **fixed in full** in this spec's Error
  Model above, including the lifetime-precondition-violation tier.
- Whether resource lifetime belongs in this spec: **no** — removed
  entirely as a Non-Goal (see Non-Goals and Out of Scope / Future Work).
- Whether this round's RenderGraph depends on RHI: **no** — Core only
  (see Proposed Design's Module Boundary section).
- Whether explicit/caller-authored pass-to-pass dependency edges are
  included in this round: **no** — removed entirely; the only ordering
  mechanism is producer-derived edges from resource usage. See Non-Goals
  and [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered. A future spec may introduce a different
  mechanism if a concrete use case appears.
- Whether this round performs any dead-pass/unreferenced-pass/output-root
  culling: **no** — every successfully declared pass is retained
  unconditionally. See Non-Goals and Proposed Design's "Pass retention"
  section.
- Whether pass/resource names need to be caller-visible stable
  identifiers: **no** — this round includes non-unique, caller-provided
  **diagnostic labels** only (never identity, never ordering); see
  Functional Requirements' "Diagnostic labels" subsection.
- Whether the builder is copyable or movable: **no to both** — the
  builder is non-copyable and non-movable; see Functional Requirements'
  "Handles and builder ownership" subsection and
  [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md).
- Whether the compiled graph depends on the builder that produced it
  remaining alive: **no** — it independently owns its own data and is
  safe to outlive the builder; see Functional Requirements' Compilation
  subsection.
- Whether a handle vended by a different, currently-live builder is
  reliably detectable as foreign: **yes** — see Error Model. Whether a
  handle used after its originating builder was destroyed is reliably
  detectable: **no, and this spec does not require it to be** — see Error
  Model's lifetime-precondition-violation tier.
- What this round's public thread-safety contract is for the builder, its
  handles, and the compiled graph: **fixed** — see Proposed Design's
  "Threading" section. The compiled graph's immutability is not, by
  itself, a concurrency claim.

## Out of Scope / Future Work

A future RHI resource/command spec (not numbered or designed here) is
expected to extend RHI with `RenderTarget`, `Buffer`/`Texture` resources,
a command list/command buffer abstraction, and a resource-state/barrier
API. A future RenderGraph-execution spec (also not numbered or designed
here) is expected to extend or complement this spec's compiled graph
description to actually record and submit GPU work, consuming both that
new RHI surface and this spec's compiled pass order and dependency
relations. A future resource-lifetime/versioning spec is expected to
design first/last-use computation, transient aliasing, and any
multi-writer/versioned-resource model — informed by, but not committed to
reusing, this round's compiled graph shape, since that data may need to
change once a real consumer exists to validate it against; this round
makes no promise about what that future spec will or must reuse. A future
spec may introduce a caller-authored ordering mechanism (an explicit
dependency edge, a side-effect resource/token, or some other constrained
mechanism) once a concrete workload need appears, and a future spec may
introduce pass culling once a real notion of graph output/root exists;
neither is designed or anticipated in any particular shape by this
spec. A future spec may also revisit builder copyability/movability, or
add an explicit thread-safety upgrade for the compiled graph, if a real
composition or concurrency need appears — neither is anticipated or
designed here. Physical resource realization and any GPU memory allocator
strategy remain future work's concern, per
[ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md). Atlantis
Renderer (Milestone 3), Shader System (Milestone 4), Android Platform
(Milestone 5), headless rendering (Milestone 6), and image regression
testing (Milestone 7) all remain later, separately-specced work per
[docs/project-blueprint.md](../docs/project-blueprint.md) and are not
advanced, designed, or unblocked by this spec beyond satisfying
RenderGraph Foundation as their shared dependency.
