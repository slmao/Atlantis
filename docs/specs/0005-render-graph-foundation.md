# Spec: Atlantis RenderGraph Foundation (GPU-Independent Graph Core)

- **Status:** Approved
- **Author:** Drafted by Claude Code at explicit human direction.
- **Created:** 2026-08-09
- **Human Review Approval:** slmao, 2026-08-09. All sixteen architectural
  decisions this Spec's four revisions settled were reviewed and accepted
  unchanged from this document's content (see Requirements, Proposed Design,
  and Architectural Impact for each). This was **not** a joint Spec + Plan
  review — approval authorizes drafting a Plan; implementation still requires
  that Plan's own (or a joint) Human Review.
- **Related Plan(s):** [Plan 0005](../plans/0005-render-graph-foundation.md)
  (`Approved`). Joint Spec + Plan Human Review completed 2026-08-09;
  implementation merged via [PR #18](https://github.com/slmao/Atlantis/pull/18).
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md)–[ADR-0004](../adr/0004-phase1-threading-baseline.md)
  and [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)–[ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md).
  Two new decisions filed as
  [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)
  (construction/compile layering and ownership) and
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  (dependency derivation and deterministic ordering); both `Accepted` alongside
  this Spec. Two ADRs an earlier Draft filed (resource-lifetime model,
  RenderGraph/RHI execution boundary) were **retracted** — see Architectural
  Impact.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  [PR #149](https://github.com/slmao/Atlantis/pull/149) Batch 2. Original scope
  and obligations retained.

## Summary

A minimal, **GPU-independent** RenderGraph core: declare passes and graph-local
logical resources, declare each pass's read/write usage of them under a
**single-producer-per-resource** model, derive producer-to-reader dependency
edges from that usage alone (no caller-authored dependency edge exists), detect
cycles arising purely from those derived edges, and compile a deterministic,
immutable, independently-owned description of a frame's pass execution order in
which every declared pass is retained. The builder that accumulates
declarations is non-copyable, non-movable, and the sole owner of its data; the
compiled result owns its own data and safely outlives the builder. It does
**not** execute GPU work, allocate any RHI resource, record or submit a
command, touch Vulkan, compute resource lifetime, model resource physical
properties, classify resources as imported vs. transient, or cull any pass — it
is scoped to exactly the pass/dependency/ordering/ownership logic unit-testable
without a Vulkan SDK or GPU.

### The sixteen settled decisions (Human Review, 2026-08-09)

1. GPU-independent graph-core scope for this Spec (no RHI resource/command
   extension bundled in).
2. Core-only module dependency this round (no RHI dependency).
3. The single-producer logical-resource model (producer→reader is the only
   derived edge kind).
4. Producer-less logical resources as a legal, ownership-free
   externally-provided input token.
5. An unconditional compile error for more than one producer of the same
   logical resource.
6. No same-resource in-place read/write (a programmer error; no
   read-modify-write concept this round).
7. No caller-authored explicit pass-to-pass dependency edge of any kind —
   producer-derived edges are the only ordering mechanism.
8. No automatic pass culling — every successfully declared pass is retained
   exactly once.
9. Declaration order used **only** as the deterministic tie-break for
   otherwise-unordered passes, never for hazard/version inference.
10. The builder is non-copyable, non-movable, and purely additive (no in-place
    declaration removal/editing API).
11. `compile()` is non-consuming and non-mutating on the builder; the resulting
    `CompiledGraph` independently owns its own data.
12. Builder-scoped handle provenance (default/invalid and cross-builder-while-
    live cases are guaranteed-detectable programmer errors); use-after-builder-
    destruction is an undetected lifetime precondition violation.
13. Non-unique, caller-provided diagnostic labels, and a deterministic
    dependency-cycle witness that identifies participating passes unambiguously
    even under duplicate labels.
14. Phase 1's single-logical-frame-thread contract, with no declared
    concurrent-access guarantee for the builder or `CompiledGraph`.
15. No resource-lifetime analysis, no physical resource binding/allocation, no
    RHI/GPU execution capability anywhere in this Spec's scope.
16. Exactly two new ADRs are required —
    [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md) and
    [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md) —
    no others.

## Motivation / Problem Statement

Spec 0003 delivered RHI's and Vulkan Backend's **non-frame** foundation only —
no `RenderTarget`, `Buffer`/`Texture`, command list, acquire/present, pipeline/
render-pass objects, queue submission, or resource-state/barrier API.
[AGENTS.md](../../AGENTS.md) makes RenderGraph the mandatory path for all GPU work,
but RenderGraph cannot yet execute anything (RHI has nothing to execute
against) and Renderer cannot be specced until RenderGraph exists. This Spec
resolves that sequencing by scoping RenderGraph Foundation to the slice
buildable and valuable **today**: the graph-description and compilation core,
backend-agnostic and GPU-independent. Execution, resource lifetime/versioning,
physical realization, any caller-authored ordering mechanism, and pass culling
are all left to future specs once real consumers exist to validate them.

Three revisions each fixed a distinct problem: (1) replaced an internally
inconsistent automatic WAR/RAW/WAW derivation model with the single-producer
model; (2) removed a caller-authored explicit pass-to-pass dependency edge with
no concrete use case; (3) fixed the builder's ownership/copy/move semantics,
handle provenance, and the compiled graph's independence from the builder,
which the first two left unstated. A fourth revision tightened cross-type
handle misuse to a compile-time type error. These are public ownership/lifetime
questions [AGENTS.md](../../AGENTS.md) does not allow to be settled implicitly —
see [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md).

## Goals

- A pass-declaration API surface (candidate semantics, not final spelling): a
  diagnostic-only identifier and a set of resource usages, backend-agnostic.
- A logical-resource concept: nothing more than a graph-local opaque identity
  connecting one producer to zero or more readers — decoupled from any RHI
  resource type, physical property, or versioning concept.
- Read/write usage declarations attaching a pass to a logical resource, under a
  single-producer-per-resource model.
- Producer-to-reader dependency edges derived automatically from declared
  resource usage — and **only** from it — with no separate caller-authored
  ordering mechanism, consistent with
  [render_graph/README.md](../render_graph/README.md)'s "ordering derived
  from the dependency graph rather than authored by hand per pass."
- An illegal multiple-producer declaration for the same resource as an
  **actually reachable** compile error.
- Detection of dependency cycles arising from producer-derived edges across two
  or more passes and resources, reported through an explicit Result/error type,
  never an exception or ill-defined behavior.
- A deterministic topological pass-execution order — the same graph description
  compiles to the same order every time, using declaration order strictly as a
  tie-break among otherwise-unordered passes.
- **Every successfully declared pass appears exactly once** in the compiled
  order — no dead-pass, unreferenced-pass, or output-root-based culling.
- A minimal, unambiguous ownership and lifetime contract for the builder, its
  handles, and the compiled graph (non-copyable/non-movable builder;
  builder-scoped, provenance-carrying handles; an independently-owned compiled
  graph that safely outlives the builder).
- An immutable, independently-owned compiled graph description as compile's
  success artifact, carrying exactly the data needed to verify this Spec's own
  behavior (pass order, dependency relations) and nothing more.
- `compile()` never mutating, consuming, or invalidating the builder, on
  success or failure — a caller can compile an unmodified builder repeatedly
  and always get the same result.
- All of the above as GPU-independent unit-testable logic runnable without a
  Vulkan SDK or GPU (testing-strategy.md layer 1).
- A clear, documented boundary for a future RHI resource/command spec, a future
  RenderGraph-execution spec, a future resource-lifetime/versioning spec, and a
  future spec adding any caller-authored ordering mechanism or pass culling.

## Non-Goals

- Atlantis Renderer, and any scene/mesh/camera/material concept.
- Shader compilation, reflection, or any Shader System concept.
- Graphics or compute pipeline object creation.
- Command list / command buffer implementation of any kind.
- Real GPU submission or queue scheduling. RenderGraph does not execute,
  submit, or simulate GPU work anywhere in this Spec's scope, never calls into
  any RHI command-recording surface (none exists), and never references Vulkan
  Backend — per [AGENTS.md](../../AGENTS.md)'s mandatory-RenderGraph-path rule and
  [ADR-0001](../adr/0001-rhi-backend-independence.md), both already `Accepted`;
  no separate ADR restates them.
- Vulkan barriers, image layout transitions, or any synchronization primitive.
- `Presentation` acquire/present, or any change to `Presentation`'s non-frame
  lifecycle contract ([Spec 0003](0003-rhi-vulkan-windowed-foundation.md)).
- `RenderTarget` creation, or any swapchain-image concept.
- RHI texture/buffer allocation, or a GPU memory allocator (unaffected by
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)).
- **Resource lifetime of any kind** — no first-use/last-use interval, no
  lifetime data on the compiled graph, no transient-resource aliasing or
  memory-reuse plan. No RHI resource type, physical realization, or consumer
  for lifetime data exists yet; a future spec may compute lifetime directly
  from a compiled graph's pass order and dependency data.
- **Any imported/transient resource classification** — a logical resource is a
  single, uniform concept regardless of whether it has a producer.
- **Any resource-versioning model**, in-place read-modify-write, or multiple
  sequential writers to one logical resource — deferred per
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Alternatives Considered.
- **Any resource physical property** — size, format, usage flags, memory
  properties, physical ownership.
- **Any caller-authored pass-to-pass dependency edge or ordering override** —
  no explicit edge, `dependsOn`-shaped API, before/after relation, manual edge
  list, priority/order override, or integer sort key. Producer-derived,
  resource-usage-based edges are the only ordering mechanism.
- **Any automatic dead-pass, unreferenced-pass, or output-root-based culling.**
- Async compute or multi-queue scheduling.
- Multi-threaded graph construction, compilation, or recording; any job/task
  system. Phase 1's single-logical-frame-thread baseline
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)) is unchanged and no
  API for a future multi-threaded model is pre-embedded.
- **Pass or resource identifier uniqueness as a correctness mechanism** —
  diagnostic labels are never required to be unique and nothing depends on it.
- **Any handle generation counter, handle recycling, or cross-builder stable/
  serializable identity scheme** — there is no declaration-removal/reuse
  mechanism for a handle to need recycling from.
- **Any in-place declaration removal, replacement, or editing API on the
  builder** — a caller needing a different description constructs a new
  builder.
- **Copying or moving a builder instance** — non-copyable and non-movable this
  round.
- **Any global handle registry, global handle allocator, or shared/
  reference-counted graph registry** — provenance is per-builder and
  per-compiled-graph, never global mutable state.
- **Any cross-process, cross-frame, or serialization-stable identity** for a
  handle or a compiled-local identifier.
- **Any declared thread-safety guarantee** for concurrent access to a builder
  or `CompiledGraph` beyond Phase 1's single-thread baseline. `CompiledGraph`
  immutability is a mutation guarantee, not a concurrency guarantee.
- GPU-driven rendering, neural rendering/shading, 3D Gaussian Splatting, or any
  world-model workload — future phases per [AGENTS.md](../../AGENTS.md).
- Android, iOS, or Linux implementation. This Spec's logic is
  platform-independent by construction (no OS type in its scope) but is not
  tested on, or scoped to, any specific OS. Linux is not a target platform.
- Headless rendering and image regression infrastructure.
- Editor tooling or graph serialization.
- A second graphics backend, or any abstraction knob added "for" one.

## Requirements

### Functional

Candidate semantics only — concrete C++ type/method names and exact signatures
are left to the Plan; what follows fixes *behavior*.

**Logical resources**

- A logical resource is a graph-local opaque identity, nothing more: it
  connects one producing pass (if any) to zero or more reading passes within a
  single graph description. It references no RHI resource type; carries no
  size/format/usage-flag/memory data; implies no physical ownership.
- The builder vends a logical resource handle through a single creation
  operation. There is **no separate "import" or "transient" declaration** —
  every resource is created the same way; whether it ends up with a producer is
  emergent from later usage, not fixed at creation.
- **Producer-less resources are supported and are the intended way to represent
  an externally-provided input token.** A resource never given a write usage is
  valid and may still be read; it is a graph-compilation-time identity for
  "something provided from outside this graph." It binds to no real RHI object,
  transfers no ownership, and guarantees no future mapping to an "imported
  resource" concept. Without this, no graph could express a pass that only
  consumes graph-external state.
- **A resource declared but never given any usage (no producer, no reader) is a
  valid, harmless declaration**, not a compile error. It participates in no
  dependency relation. Whether the compiled graph surfaces unused resources at
  all is a Plan-stage detail.
- A resource's only externally-visible states in the compiled result are: no
  producer, or exactly one producer (more than one is a compile error, not an
  observable valid state).

**Handles and builder ownership** (full model:
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md))

- **The builder is non-copyable and non-movable, and is the sole, exclusive
  owner of its accumulated declarations.** Exactly one builder instance per
  graph description.
- **A pass handle, a logical resource handle, and a compiled graph's own
  compiled-local pass/resource identity are three mutually distinct,
  strongly-typed concepts.** Using a value of one where a different one is
  expected is a **compile-time type error** — never a runtime condition, never
  an assertion's responsibility. The type system prevents all cross-type
  misuse, unconditionally, with no residual runtime case.
- **Within a single handle type**, every handle is scoped to the builder
  instance that vended it. Exactly two *runtime*, value/provenance misuse cases
  exist for a handle of the correct type, both guaranteed-detectable at the
  point of use (see Error Model): a default/invalid handle, and a handle vended
  by a *different*, currently-live builder instance.
- **Using a handle after its originating builder is destroyed is a lifetime
  precondition violation, not a guaranteed-detectable error** — the same
  category as any other dangling reference under
  [AGENTS.md](../../AGENTS.md)'s ownership rules; distinct from the two cases
  above, and must not be conflated with them.
- **Handle values are ordinary copyable value tokens.** Copying transfers,
  shares, or duplicates no ownership — it produces another reference to the
  same builder-scoped identity, usable only within its owning builder's
  single-threaded call context.
- No handle is globally unique; no generation counter, handle recycling, UUID,
  or cross-builder/serialization-stable scheme is introduced.

**Graph construction**

- A builder/description object accumulates pass and logical-resource
  declarations. Nothing is derived, validated, or ordered while declarations
  accumulate (ADR-0017).
- A pass is declared with a diagnostic-only label and, at declaration time or
  via subsequent pass-scoped calls, a set of read/write usages against logical
  resource handles.
- Declaration order is meaningful only as the deterministic tie-break for
  passes the dependency graph does not otherwise order (ADR-0018). It plays no
  role in edge direction, producer/consumer status, or the legality of a
  resource declaration.
- The declaration API is purely additive: no operation removes, replaces, or
  edits an already-accumulated declaration.

**Diagnostic labels**

- A pass may carry a caller-provided diagnostic label; a resource may
  optionally carry one under the same rules.
- Labels exist **solely for logging, compile-error messages, and test/debugging
  readability.** They are never required to be unique and never participate in
  identity, dependency derivation, or ordering.
- The builder owns (copies) whatever label data it needs; it never borrows a
  caller-supplied temporary. Anything that later exposes a label (a
  `CompiledGraph`, a compile error) owns its own copy.
- A dependency-cycle compile error identifies its participating passes via a
  **deterministic witness**: compiled-local identities, owned diagnostic
  labels, or both. Both which cycle is reported (if more than one exists) and
  the order of passes within the witness are deterministic — never dependent on
  unordered container iteration, pointer values, or hash order. The concrete
  algorithm and container are left to the Plan.

**Dependency derivation** (full model:
[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md))

- A logical resource has **at most one producer**: the single pass, if any,
  that declares a write usage against it.
- If a resource has a producer, a derived ordering edge runs from that producer
  to every pass that declares a read usage against the same resource. Two
  passes that both only read the same resource get no edge between them.
- **More than one producer for the same logical resource is unconditionally a
  compile error** — not legalized by declaration order or any other mechanism,
  because no other ordering-constraint mechanism exists in this Spec's scope.
- A pass declaring both a read and a write usage against the same logical
  resource is an **unsupported declaration**, rejected per the Error Model —
  this round's model has no read-modify-write concept.
- **There is no caller-authored pass-to-pass dependency edge of any kind.**
  Producer-derived edges are the only ordering-constraint mechanism.
- Cycle detection runs over the producer-derived edge set. A cycle spans **two
  or more distinct passes**; a single-pass self-loop cannot arise (a pass being
  simultaneously producer and reader of the same resource is rejected at
  declaration time). A cycle is a compile error carrying a deterministic
  witness sufficient to identify the participating passes.
- An isolated pass (no usage relationship to any other pass) is valid and
  participates in the same deterministic ordering as any other pass.

**Pass retention**

- Every successfully declared pass appears in the compiled pass order
  **exactly once** — whether or not it participates in any dependency relation
  (isolated pass retained), and whether or not its own produced resource is
  ever read (producer with no readers retained).
- This Spec performs **no automatic dead-pass, unreferenced-pass, or
  output-root-based culling of any kind.**

**Compilation** (see
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md))

- Construction (declaring passes/resources/usages) and compilation (deriving
  dependencies, detecting cycles, computing order) are distinct steps;
  compilation is not an implicit side-effect of any declaration call.
- **`compile()` never mutates, consumes, or invalidates the builder, on success
  or failure.** A caller may compile an unmodified builder repeatedly and get
  an equivalent result — the same success, or the same failure — every time.
- **This Spec does not require or provide any in-place correction, removal, or
  editing of an already-accumulated declaration.** After a failed compile the
  builder remains a valid object whose diagnostics the caller may inspect and
  which the caller may compile again, unmodified, to observe the same failure
  deterministically. A caller wanting a different description constructs a new
  builder; adding an unrelated pass does not fix a prior error.
- **A successful compile's result — the compiled graph — is an
  independently-owned, immutable value, entirely distinct from the builder.**
  It owns its own compiled-local representation of pass identity, execution
  order, and dependency relations; it does not borrow, reference, or depend on
  the builder's declaration storage. Concretely:
  - The builder may be destroyed immediately after a successful compile without
    affecting the compiled graph.
  - The builder may continue accepting declarations after producing a compiled
    graph; doing so never affects any compiled graph already produced.
  - Two compiled graphs from separate `compile()` calls are independent objects
    — destroying one has no effect on the other or on the builder.
  - The compiled graph is at minimum movable (so it can be returned by value in
    a `Result`). Whether it is additionally copyable is left to the Plan,
    provided either choice preserves independent ownership and immutability.
  - The compiled-local pass/resource identifiers it exposes are their own
    distinct, strongly-typed concept — not a builder handle. Interpreting them
    never requires the originating builder to be alive. Cross-type misuse
    (builder handle ↔ compiled-local identity) is a compile-time type error.
- Nothing in this Spec's scope mutates a compiled graph after it is produced.
- Compilation returns an explicit `atlantis::Result`-shaped success/error
  outcome (per [AGENTS.md](../../AGENTS.md)), never an exception, for every
  recoverable graph-description problem. A failed compile does not vend a
  partial or otherwise usable compiled graph.

**Compiled graph output**

The compiled graph description carries only what is needed to verify this
Spec's behavior: a deterministic pass execution order containing every
successfully declared pass exactly once; the producer-derived dependency
relations (or a read-only representation sufficient to check them, e.g. an edge
list); and whatever compiled-local pass/resource identity (and, where included,
owned label) is needed to interpret those. It does **not** carry, and this Spec
does not design: lifetime intervals, a barrier plan, any physical resource
mapping, an RHI handle, an execution callback, a queue assignment, or any
synchronization data. A future spec may extend, replace, or internally consume
this data subject to its own review; this Spec fixes only what this round's
compiled graph itself contains.

RenderGraph's public headers contain no `Vk*` type, no Vulkan header include,
and no Atlantis Platform type — matching
[ADR-0001](../adr/0001-rhi-backend-independence.md) and
[module_boundaries.md](../architecture/module_boundaries.md). No RHI
dependency is required this round.

### Non-functional

- **Performance:** compiles a Phase-1-scale frame's pass count without
  pathological (worse than polynomial) behavior. No frame-budget or
  micro-benchmark target.
- **Memory:** no GPU memory allocated or referenced. Host-side allocation uses
  ordinary RAII and standard containers; no custom allocator.
- **Portability:** OS- and backend-independent by construction — no Windows,
  Android, or Vulkan type anywhere. Verified by inspection, not multi-platform
  testing.
- **Other:** no new third-party dependency. Unit tests use the existing Catch2
  v3 framework ([ADR-0007](../adr/0007-test-framework.md)).

## Proposed Design

### Module boundary

This Spec does not move any existing boundary. It is the first concrete content
of the `Atlantis RenderGraph` module already named in
[overview.md](../architecture/overview.md) and
[module_boundaries.md](../architecture/module_boundaries.md): depends on
Core only this round (no RHI resource/command dependency to consume); depended
on by a future Renderer. The compiled graph description feeds future (not
designed here) RHI resource/command, RenderGraph-execution, resource
lifetime/versioning, and ordering/culling extensions.
[render_graph/README.md](../render_graph/README.md)'s long-term "Built on
RHI + Core only" boundary is not contradicted — this Spec simply does not yet
*use* any RHI surface; a future execution spec is expected to be the first
RenderGraph work that depends on RHI's then-extended surface.

### Ownership and lifetime hazards this model resolves

Two earlier revisions fixed the dependency model but left the builder's
ownership/copy/move semantics, handle provenance, and the compiled graph's
independence unstated. Left unresolved, at least three hazards fall out:

- **Handle collision across builders.** A bare builder-local integer index
  gives two builders colliding values (both vend index `0` first), making "a
  foreign handle is detectable" impossible.
- **Address-based provenance breaking under a move.** A handle carrying the
  builder's address breaks the moment the builder is relocated.
- **A compiled graph dangling on builder destruction.** A compiled graph that
  borrows the builder's storage dangles when the builder is destroyed,
  contradicting the "independent, immutable value" property.

The builder is non-copyable and non-movable and the sole owner of its
declarations — making the builder's own address safe to use as handle
provenance (an object neither copied nor moved never changes address), and
sidestepping what a "copy" of an in-progress description would mean. Pass and
resource handles are distinct, strongly-typed, builder-scoped concepts carrying
enough provenance that, while the originating builder is alive, a handle from a
*different* builder is reliably distinguishable. Use-after-builder-destruction
is an undetected lifetime precondition violation. Full model, alternatives
(copyable/movable builder; bare local-index handle; global registry with
generation counters), and why each was rejected:
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md).

### Dependency derivation and cycles

A logical resource has at most one producer; the only derived relationship is
producer → reader. Cycle detection is still fully meaningful purely from
producer-derived edges across two or more resources — e.g. pass A writes X and
reads Y, pass B writes Y and reads X: derived edges A→B (through X) and B→A
(through Y) form a cycle with no caller-authored edge, generalizing to chains
of three or more. A single pass can never form a self-loop (producer+reader of
the same resource is rejected at declaration time). Full model and rationale
(single-producer over the inconsistent original draft; removal of the
caller-authored edge; deferral of versioned resources and culling):
[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md).

### Construction/compilation layering

The builder accumulates declarations additively; `compile()` reads that state
and never mutates, consumes, or invalidates the builder. A successful compile
produces a separate, independently-owned immutable value that does not borrow
the builder's storage — so the builder may be destroyed immediately after, or
keep accumulating, without affecting any compiled graph already produced. No
in-place declaration removal/editing API. Rationale (non-consuming
non-mutating `compile()` over letting compile consume or mutate the builder;
independent ownership over borrowing):
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md).

### Threading

Single logical frame thread
([ADR-0004](../adr/0004-phase1-threading-baseline.md)). Minimal public
thread-safety contract: the builder is not thread-safe (all declaration and
`compile()` calls on the single frame thread); builder handles are copyable
value tokens usable only within their owning builder's single-threaded context;
the compiled graph is **not declared thread-safe for concurrent access,
including concurrent reads** — its immutability is a mutation guarantee, not a
concurrency guarantee. No mutex, atomic, job/task system, or lock-free
structure is introduced; no public type is documented as safe for concurrent
use. A future spec may add an explicit thread-safety upgrade with its own
reasoning.

### Error model

Four tiers, with which side of the line each named case falls on fixed here
(not left to the Plan):

**Compile-time type error (not a runtime condition, not an assertion):** using
a pass handle, a logical resource handle, or a compiled-local pass/resource
identity where a different one of those three is expected. The type system
rejects all cross-type usage among them at compile time, unconditionally.

**Guaranteed-detectable runtime programmer error (assertion, e.g.
`ATLANTIS_CHECK`/`ATLANTIS_ASSERT`)** — misuse fully judgable at a single API
call's entry point while every builder involved is alive, **for a handle
already known by its type to be a pass or logical resource handle**:

- A default/invalid handle, or a handle vended by a different, currently-live
  builder instance. These are the *only* two runtime misuse cases for a
  correctly-typed handle — no third cross-type runtime case exists.
- A pass declaring both a read and a write usage against the same logical
  resource — checkable against that pass's own accumulated usage state at the
  moment the conflicting usage is added, regardless of order.

**Lifetime precondition violation (not guaranteed-detectable — outside this
Spec's defined-behavior guarantees):** using a handle after the builder that
vended it has been destroyed. Not required or tested to be caught; the same
category as any other dangling reference under
[AGENTS.md](../../AGENTS.md)'s ownership rules. A deliberately different tier from
the guaranteed-detectable cases.

**Recoverable compile error (explicit `atlantis::Result`, never an
exception)** — determinable only by observing the graph as a whole: more than
one producer for the same logical resource (across two or more passes); a
dependency cycle spanning two or more passes from producer-derived edges.

**Not an error:** an empty graph compiling to an empty result; an isolated
pass, a producer with no readers, or a producer-less resource used only for
reads (all retained); a resource declared but never used; a duplicate
diagnostic label on two different passes or resources; the builder being
destroyed after producing a compiled graph.

The exact enumeration of error cases (as a concrete enum) is not fixed by this
Spec — it fixes the semantics and the four-tier classification in full; the
Plan fixes the concrete type/spelling.

## Architectural Impact

Introduces architecture; required **two** new ADRs before `Approved`, neither
decided by this Spec's prose. Both reached `Accepted` alongside this Spec:

| Decision | ADR |
|---|---|
| RenderGraph construction/compilation layering and ownership — how a non-copyable/non-movable, purely-additive builder and a pure, repeatable, Result-returning, builder-non-destructive `compile()` (no in-place declaration editing) are separated; the builder/handle ownership-and-provenance model; why the compiled artifact is immutable and independently owned, safe to outlive the builder | [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md) |
| Dependency derivation rules and deterministic ordering — the single-producer model, producer-to-reader derivation, the unconditional multiple-producer compile error, cross-resource cycle detection, the declaration-order tie-break, the all-passes-retained invariant, and why no caller-authored dependency edge or pass culling is included | [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md) |

The ownership/handle/`CompiledGraph`-independence decisions extend ADR-0017
rather than requiring a new ADR — they are part of the same
construction/compile-layering decision.

**This round retracts the two additional ADRs an earlier Draft filed** (a
resource-lifetime-model ADR and a RenderGraph/RHI execution-boundary ADR) —
removed, not deprioritized: resource lifetime is entirely out of scope (nothing
to record a decision about yet), and the execution boundary is a direct,
unmodified application of already-`Accepted`
[AGENTS.md](../../AGENTS.md) and [ADR-0001](../adr/0001-rhi-backend-independence.md)
rules, stated as this Spec's own Non-Goals (as Spec 0003 stated many of its
boundaries without a dedicated ADR for each).

No existing `Accepted` ADR's conclusions are restated, reopened, or modified.
Architectural Impact is **not "None"** — RenderGraph is a new module boundary
with a new public API surface, the kind of change
[AGENTS.md](../../AGENTS.md)'s "What counts as significant" section requires the
full Spec → Plan → Human Review path for.

## Alternatives Considered

- **GPU-independent graph core only (adopted).** Fully buildable and testable
  today without inventing any RHI surface ahead of its own review; gives a
  future RHI-resource/command spec a settled contract to build execution
  against.
- **Also add the minimal RHI resource/command API RenderGraph would need to
  execute something real.** Rejected: requires new, unreviewed RHI public API
  (command list, resource-state/barrier representation, a resource type) Spec
  0003 deliberately left undesigned; compounds ownership/lifetime/threading
  decisions belonging to their own spec/ADR; bundling makes both less
  reviewable, not easier.
- **The originally-proposed automatic WAR/RAW/WAW derivation model** (multiple
  writers legal if "otherwise ordered"). Rejected as internally inconsistent —
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  Context.
- **Retaining a caller-authored explicit pass-to-pass dependency edge.**
  Deferred, not permanently foreclosed — no current approved use case, and it
  would be a second pass-ordering control surface with no concrete need to
  validate its shape against.
- **A versioned/history resource model** (multiple sequential writers, in-place
  read-modify-write). Deferred to a future spec once a real consumer motivates
  the version-identity and binding-rule design.
- **Automatic dead-pass / unreferenced-pass / output-root-based culling.**
  Deferred — culling requires a real notion of a graph's "output"/"root",
  which this Spec does not define (no Renderer yet).
- **A copyable and/or movable builder.** Rejected this round — copying raises
  an unmotivated handle-provenance question; moving breaks address-based
  provenance.
- **A bare builder-local integer index as a handle's sole representation.**
  Rejected — cannot distinguish a foreign live-builder handle from a valid
  local one on index collision.
- **A global handle registry, generation counters, or handle recycling.**
  Rejected — no declaration-removal mechanism to recycle from, and a global
  registry is global mutable state the ownership rules do not permit without a
  stated exception.
- **A compiled graph that borrows the builder's declaration storage.**
  Rejected — leaves every compiled graph dangling on builder destruction.
- **Computing resource lifetime this round.** Rejected — no RHI resource type,
  physical realization, or consumer yet; a future spec can derive it from the
  compiled graph's data.
- **A dedicated ADR for the RenderGraph/RHI execution boundary.** Rejected in
  favor of stating it as Non-Goals — a direct application of already-`Accepted`
  rules, not a new decision.
- **Copy an existing commercial/open-source RenderGraph API's shape
  wholesale.** Rejected as a design method — Atlantis's own boundaries (no RHI
  resource/command surface yet, Result-based error model, no exceptions,
  single-frame-thread baseline) differ enough that copying a concrete API would
  import unmade assumptions. The *behavior* is a common field pattern; the
  concrete API is Atlantis's own, left to the Plan.
- **Non-deterministic or "any valid topological order" compilation** — rejected
  ([ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)).
- **Explicit-edges-only dependency model (no usage-derived dependencies)** —
  rejected ([ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)).
- **Letting `compile()` consume the builder or mutate it in place into the
  compiled result** — rejected
  ([ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)).

## Testing & Verification Plan

The entire scope is GPU-independent logic exercised by unit tests that run
without a Vulkan device (testing-strategy.md layer 1, Catch2 v3). At minimum,
tests must cover:

- An empty graph compiles to an empty result; a single-pass graph compiles.
- One producer / one reader: the derived edge and order are correct. One
  producer / multiple readers (fan-out): every reader is ordered after the
  producer; readers are not ordered relative to each other.
- Multiple independent producer/reader pairs on unrelated resources compile to
  a deterministic, declaration-order-driven order between the groups.
- Multiple readers of the same resource produce **no** edge between them.
- Multiple producers for the same logical resource is rejected as a compile
  error, unconditionally.
- A single pass declaring both a read and a write against the same resource is
  rejected per the Error Model (programmer error).
- A two-pass, two-resource cycle formed entirely from producer-derived edges is
  detected and reported with a deterministic witness identifying both passes; a
  longer cycle spanning three or more passes/resources likewise. Repeated
  compilation of the same cyclic graph reports an equivalent witness every
  time. A cycle whose passes carry **duplicate** diagnostic labels still
  produces a deterministic, unambiguously identifiable witness (via
  compiled-local identity, independent of label text).
- An isolated pass compiles, is retained, and participates in the tie-break. A
  producer pass whose resource is never read is retained (no dead-pass
  culling). A resource declared but never used produces no dependency relation.
- Every successfully declared pass appears in the compiled order exactly once,
  across all shapes above. Declaration order determines compiled order only
  among otherwise-unordered passes (a test that would fail if declaration order
  instead influenced edge direction or producer legality).
- Repeated compilation of an unmodified graph yields an identical result. A
  failed compile vends no partial/usable compiled graph; the same builder
  compiles again, unmodified, to observe the same failure deterministically (no
  in-place fix-and-retry workflow is tested or required).
- The builder type is not copy-constructible and not move-constructible (a
  compile-time property, verified as such). A pass handle, a logical resource
  handle, and a compiled-local identity are three mutually distinct types with
  no implicit conversion (compile-time).
- A default-constructed (or never-vended) handle, and a handle vended by one
  builder used on a *different* concurrently-alive builder, each trigger the
  programmer-error/assertion policy. Handles remain valid for their builder's
  entire lifetime (further declarations never invalidate an earlier handle).
- After a successful compile: destroying the builder leaves the compiled graph
  fully queryable; continuing to add declarations does not change it; two
  `compile()` calls produce independent, equivalent values; destroying one
  compiled graph does not affect another or the builder.
- Duplicate diagnostic labels are legal and do not affect compiled identity,
  dependency relations, or order.
- No global handle registry, allocator, or other global mutable graph-related
  state exists (by inspection). RenderGraph's public headers contain no `Vk*`
  type, no `#include <vulkan/...>`, and no RHI resource type (by
  inspection/grep).

**Explicitly not tested:** using a handle after its originating builder has
been destroyed — a lifetime precondition violation; a dynamic test exercising
it would be exercising undefined behavior.

- **Headless integration / image regression / Vulkan Validation:** not
  applicable — no GPU work, nothing rendered, no Vulkan call.
- **Manual verification:** not required — every behavior is exercisable through
  unit tests alone; there is no windowed/interactive component.

## Acceptance Criteria

- [ ] RenderGraph's public headers contain no `Vk*` type, no
      `#include <vulkan/...>`, no Atlantis Platform type, and no RHI resource
      type.
- [ ] No Vulkan call, and no RHI command-recording call, is made anywhere in
      this Spec's implementation.
- [ ] No GPU-required test exists anywhere in this Spec's test suite.
- [ ] No rendering output, image, or `RenderTarget` is produced, referenced, or
      asserted on anywhere in this Spec's implementation or tests.
- [ ] An empty graph compiles successfully.
- [ ] A single pass compiles successfully.
- [ ] A single-producer/single-reader graph and a single-producer/multiple-
      reader (fan-out) graph each compile to the implied order.
- [ ] Multiple readers of the same resource produce no edge between those
      readers.
- [ ] Independent producer/reader groups compile to a deterministic,
      declaration-order-driven order between the groups.
- [ ] Declaring more than one producer for the same logical resource is
      rejected as a compile error, unconditionally.
- [ ] A pass declaring both a read and a write usage against the same logical
      resource is rejected as a programmer error.
- [ ] A two-pass, two-resource producer-derived cycle is detected and reported
      with a deterministic participating-pass witness; a longer
      (three-or-more-pass) producer-derived cycle likewise.
- [ ] A cycle whose passes carry duplicate diagnostic labels still produces a
      deterministic, unambiguously identifiable witness.
- [ ] An isolated pass, and a producer pass with no readers, are each retained
      — no dead-pass or unreferenced-pass culling occurs anywhere.
- [ ] A declared-but-unused logical resource is accepted without error and
      produces no dependency relation.
- [ ] Every successfully declared pass appears in the compiled pass order
      exactly once, across every tested graph shape.
- [ ] The builder type is non-copyable and non-movable (a compile-time
      property).
- [ ] Pass handles, logical resource handles, and compiled-local pass/resource
      identities are three mutually distinct types that cannot be used
      interchangeably at runtime — cross-type misuse is a compile-time error
      (a compile-time property).
- [ ] A default/invalid handle, and a handle vended by a different,
      currently-live builder instance of the same handle type, each trigger the
      programmer-error/assertion policy — no generation counter, handle
      recycling, or cross-builder identity scheme is implemented or required.
- [ ] This Spec does not claim, test, or require detection of a handle being
      used after its originating builder was destroyed — documented as a
      lifetime precondition violation.
- [ ] A successful compile's resulting compiled graph remains fully valid and
      queryable after the builder that produced it is destroyed.
- [ ] Continuing to add declarations to a builder after a successful compile
      never changes a compiled graph already produced.
- [ ] Two `compile()` calls on the same unmodified builder produce independent,
      equivalent compiled-graph values; destroying one never affects the other.
- [ ] Duplicate diagnostic labels are legal and never affect identity,
      dependency relations, or compiled order.
- [ ] Repeated compilation of an unmodified graph description is deterministic.
- [ ] A failed compile never vends a partial compiled graph and never
      invalidates the builder: the same builder can be compiled again,
      unmodified, to observe the same failure. No in-place declaration
      removal/editing API is implemented or required.
- [ ] No mutation of a compiled graph description is possible through any public
      API this Spec introduces.
- [ ] No caller-authored pass-to-pass dependency edge, `dependsOn`-shaped API,
      before/after relation, manual edge list, priority/order override, or
      integer sort key is implemented anywhere — the only ordering mechanism is
      producer-derived, resource-usage-based edges.
- [ ] No mutex, atomic, job/task system, or lock-free structure is introduced
      anywhere, and no public type is documented as thread-safe for concurrent
      use beyond Phase 1's single-logical-frame-thread baseline.
- [ ] No global handle registry, global handle allocator, or other global
      mutable graph-related state exists anywhere.
- [ ] No lifetime interval, imported/transient resource classification, or
      resource physical property (size/format/usage/memory) appears anywhere in
      this Spec's public API or compiled graph output.
- [ ] No `src/renderer/`, Shader System, or RHI resource/command source is
      created or modified.
- [x] Both ADRs in Architectural Impact
      ([ADR-0017](../adr/0017-render-graph-construction-compile-layering.md),
      [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md))
      reach `Accepted` before this Spec is marked `Approved` — satisfied
      2026-08-09; this checkbox gates Spec approval, not implementation.

## Risks & Open Questions

**Resolved by Human Review (2026-08-09)** — see the sixteen-decision record in
Summary; none is reopened here: the single-producer model, producer-less
resources, the GPU-independent-graph-core scope boundary, and the
non-copyable/non-movable builder are each accepted as this round's respective
design.

**Remaining open — Plan-level detail, not an architectural question:**

- Whether `CompiledGraph` should additionally be copyable (beyond the minimum
  movability), provided independent ownership and immutability are preserved.
- Concrete error enumeration — semantics and the four-tier classification are
  fixed; the concrete enum/type spelling is the Plan's.
- Test target naming and `tests/render_graph/` internal layout — should mirror
  the existing `tests/rhi/`/`tests/vulkan_backend/` CMake pattern.
- Exact public API shapes (handle representation, compiled-local identity
  representation, diagnostic-label storage type, method names) — the Plan's in
  full; this Spec fixes behavior, not spelling.

The following were open in an earlier revision and are **no longer open** —
settled by this round's own Decision: a builder remains usable after a failed
compile (yes, unconditionally, but with no in-place correction API); the
assertion-vs-Result classification and which cases fall on each side (fixed in
the Error Model, including the lifetime-precondition tier); resource lifetime in
this Spec (no — a Non-Goal); whether this round depends on RHI (no — Core
only); explicit/caller-authored pass-to-pass edges (no — removed); dead-pass
culling (no — all passes retained); caller-visible stable identifiers (no —
non-unique diagnostic labels only); builder copyability/movability (no to
both); whether the compiled graph depends on the builder staying alive (no —
independent ownership); whether a foreign live-builder handle is reliably
detectable (yes) and a handle after builder destruction (no, and not required
to be); the public thread-safety contract (fixed — see Threading; immutability
is not a concurrency claim).

## Out of Scope / Future Work

A future RHI resource/command spec is expected to extend RHI with
`RenderTarget`, `Buffer`/`Texture`, a command list abstraction, and a
resource-state/barrier API. A future RenderGraph-execution spec is expected to
extend or complement this Spec's compiled graph description to record and submit
GPU work, consuming both that new RHI surface and this Spec's compiled pass
order and dependency relations. A future resource-lifetime/versioning spec is
expected to design first/last-use computation, transient aliasing, and any
multi-writer/versioned-resource model — informed by, but not committed to
reusing, this round's compiled graph shape. A future spec may introduce a
caller-authored ordering mechanism once a concrete workload need appears, and a
future spec may introduce pass culling once a real notion of graph output/root
exists — neither designed or anticipated in any particular shape here. A future
spec may also revisit builder copyability/movability or add an explicit
thread-safety upgrade for the compiled graph. Physical resource realization and
any GPU memory allocator strategy remain future work per
[ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md). Renderer, Shader
System, Android Platform, headless rendering, and image regression testing
remain later, separately-specced work per
[docs/project-blueprint.md](../project-blueprint.md), not advanced by this
Spec beyond satisfying RenderGraph Foundation as their shared dependency.
