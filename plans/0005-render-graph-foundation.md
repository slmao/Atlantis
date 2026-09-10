# Plan: Atlantis RenderGraph Foundation (GPU-Independent Graph Core)

- **Spec:** [Spec 0005](../specs/0005-render-graph-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code at explicit human direction; approved at
  joint Spec 0005 + Plan 0005 Human Review on 2026-08-09. The reviewer accepted
  all nineteen Plan-stage dispositions (§7), the `CompileError` variant
  representation (§2), the deterministic error-selection priority rule (§6),
  `CompiledGraph`'s finalized move/view contract (§4), Sections 1–13, the
  Verification Checklist, the Rollback Plan, and the Acceptance Criteria Mapping
  as written — none of the nineteen changes Spec 0005/ADR-0017/ADR-0018's own
  decisions; Human Review blockers surfaced by drafting this Plan: none.
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  [PR #147](https://github.com/slmao/Atlantis/pull/147) Batch 2. Original scope,
  ordered work, and verification retained. Candidate C++ headers/algorithms
  drafted here, and the multi-round revision history and 16-item Consistency
  Review, are preserved in this PR's and
  [PR #18](https://github.com/slmao/Atlantis/pull/18)'s history, not narrated
  here.

Every C++ type, signature, algorithm, and file name below was a candidate shape
for Human Review at drafting time and is now the approved basis for
implementation — Spec 0005 and
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)/[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
fix *behavior*; this Plan proposes the concrete C++. Per
[AGENTS.md](../AGENTS.md), a forced deviation during implementation is called
out in the PR; an architecture-changing deviation returns to Spec review.

## Objective

Turn [Spec 0005](../specs/0005-render-graph-foundation.md) into an ordered,
reviewable set of concrete changes: a new, GPU-independent **Atlantis
RenderGraph** module (`RenderGraphBuilder`, strongly-typed pass/resource
handles, a single-producer dependency model, deterministic compilation into an
independently-owned `CompiledGraph`), per
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)
(construction/compilation layering and ownership) and
[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
(dependency derivation and deterministic ordering).

## Architectural boundaries (preserved, not re-decided)

- RenderGraph's public headers: zero `Vk*` types, zero OS-specific types, zero
  RHI resource types. Only `Atlantis::Core` is a dependency (Spec 0005
  Functional Requirements).
- No caller-authored pass-to-pass dependency edge of any kind — the only
  ordering mechanism is a producer-derived edge from resource usage (ADR-0018).
- No pass culling — every successfully declared pass appears in the compiled
  order exactly once (ADR-0018).
- The builder is the sole, exclusive, non-copyable, non-movable owner of its
  accumulated declarations; handles are builder-scoped value tokens (ADR-0017).
- `compile()` never mutates, consumes, or invalidates the builder, on success
  or failure; a successful `CompiledGraph` independently owns its own data
  (ADR-0017).
- Single Phase 1 logical frame thread
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)); no declared
  concurrent-access guarantee for the builder or `CompiledGraph` beyond that
  baseline.

## Non-Goals (matching Spec 0005)

Does not implement, sketch, or pre-declare: any RHI resource/command type or
call; any GPU/Vulkan call; a `Renderer`, `RenderTarget`, or Shader System; a
caller-authored pass-to-pass dependency edge (`dependsOn`, before/after
relation, manual edge list, priority/order override, integer sort key);
automatic pass culling; resource lifetime intervals, an imported/transient
classification, or any resource physical property; a resource-versioning model
or in-place read-modify-write; a global handle registry, generation counter,
handle recycling, or cross-builder/serialization-stable identity; any declared
thread-safety guarantee beyond the Phase 1 baseline; a job/task system, mutex,
atomic, or lock-free structure; Android/iOS/Linux support; a second graphics
backend. §10 is the verification-checkable version.

## 1. Module and CMake target

One new module, following
[ADR-0010](../adr/0010-cmake-structure.md)'s pattern, matching
`src/rhi/CMakeLists.txt`'s current form:

| Module | Directory | Target / alias / namespace | Links |
|---|---|---|---|
| Atlantis RenderGraph | `src/render_graph/` | `atlantis_render_graph` / `Atlantis::RenderGraph` / `atlantis::render_graph` | `Atlantis::Core` (PUBLIC) only |

No dependency on `Atlantis::RHI`, `Atlantis::Platform`,
`Atlantis::VulkanBackend`, or the Vulkan SDK.

### File-level layout (expected — not created by this Plan)

```
src/render_graph/
  CMakeLists.txt
  include/atlantis/render_graph/
    handles.h              PassHandle, ResourceHandle (public, opaque)
    render_graph_builder.h RenderGraphBuilder (declareResource, declarePass, reads, writes, compile)
    compiled_graph.h        CompiledGraph, CompiledPassId, CompiledDependencyEdge
    compile_error.h         PassDiagnostic, ResourceDiagnostic, MultipleProducersError,
                            DependencyCycleError, CompileError (variant of the two)
  src/
    render_graph_builder.cpp  method bodies; provenance checks (§5); compile() delegates to detail::compile()
    compile_algorithm.{h,cpp} private detail::compile(): the entire algorithm in §6, over plain
                              Builder-internal record types — directly unit-testable without a full builder
    compiled_graph.cpp        CompiledGraph method bodies, incl. the assertion-fallback returns (§5)

tests/render_graph/
  handle_ownership_tests.cpp            non-copyable/non-movable static checks; distinct handle/id types;
                                        default/foreign handle assertion policy, via the public API
  compile_algorithm_tests.cpp          white-box tests of detail::compile() against hand-built RawPass
                                        fixtures — the only file with the PRIVATE include-path addition (§11)
  dependency_derivation_tests.cpp      producer→reader edges, RAR no-edge, multiple-producer, same-pass
                                        read+write, producer-less/unused resources — public API only
  cycle_detection_tests.cpp            two-pass and longer derived cycles (incl. a downstream-but-not-in-cycle
                                        pass with a smaller declarationIndex), deterministic witness selection,
                                        witness under duplicate labels, repeated-compile equivalence
  pass_retention_and_ordering_tests.cpp empty/single-pass graphs, isolated pass retention, producer-with-no-
                                        readers retention, exactly-once invariant, declaration-order tie-break
  ownership_lifetime_tests.cpp         builder-destroyed-before/after-CompiledGraph, repeated-compile
                                        independence, failed-compile builder validity, no public mutation,
                                        CompiledPassId default/out-of-range assertion-fallback behavior
```

### Files to modify

`CMakeLists.txt` (`add_subdirectory` for `src/render_graph` and, inside the
existing `ATLANTIS_BUILD_TESTS` block, `tests/render_graph` — **both land in
Implementation Order step 1**, not the Documentation step); `src/README.md` and
`tests/README.md` (add the module/test entries; `tests/render_graph/` is
entirely GPU-independent — no `gpu` CTest label — and runs under the existing
`-LE gpu` command); `README.md` only if the project-status paragraph needs a
one-line update. No new external dependency.

No file under `src/core`, `src/platform`, `src/rhi`, `src/vulkan_backend`, any
other `tests/` directory, or any `examples/` directory is modified — purely
additive. No `src/renderer/`, RHI resource/command source, or Shader System
directory is created.

## 2. Value types and handles

- **`PassHandle` / `ResourceHandle`** (`handles.h`) — separate classes, no
  shared base, no conversion operator between them, no free function accepting
  one where the other is declared. Each is a trivially-copyable value token
  holding `const void* owner_` (the vending builder's address; `nullptr` when
  default-constructed) and `std::size_t index_` (declaration position;
  `static_cast<std::size_t>(-1)` sentinel when default-constructed). Only
  `RenderGraphBuilder` may construct or interpret them. The "three mutually
  distinct types" requirement (the third being `CompiledPassId`, §4) is
  satisfied by the type system alone.
- **`CompileError`** (`compile_error.h`) —
  `using CompileError = std::variant<MultipleProducersError, DependencyCycleError>`,
  a closed sum type, not a `{kind, optional-payload}` struct.
  `MultipleProducersError { ResourceDiagnostic resource; std::vector<PassDiagnostic> producers; }`
  — `resource` always populated; `producers` always ≥2 entries, ascending by
  `declarationIndex`. `DependencyCycleError { std::vector<PassDiagnostic> passes; }`
  — the deterministic cycle witness; **no `resource` field at all** (not an
  always-empty one), because no single resource identifies a cycle.
  `PassDiagnostic`/`ResourceDiagnostic` each hold a plain `std::size_t
  declarationIndex` (position among all pass / all resource declarations at the
  time `compile()` was called) and an owned `std::string label` copy — **never
  a `PassHandle`/`ResourceHandle`**, because a handle is only valid for use
  with its originating builder, and a `CompileError` a caller inspects after
  discarding the builder must carry no builder-lifetime dependency. The variant
  makes every illegal combination (a `DependencyCycle` payload carrying a
  resource, a `MultipleProducers` payload missing one) unrepresentable rather
  than a runtime convention. Consumers branch with
  `std::holds_alternative`/`std::get_if`/`std::visit`. This corrects this
  Plan's own earlier `{kind, optional}` shape (§7 item 10) — not a Spec/ADR
  change.

## 3. `RenderGraphBuilder`

`render_graph_builder.h` — non-copyable and non-movable (deleted copy/move
special members), the sole exclusive owner of its accumulated declarations,
purely additive (no operation removes, replaces, or edits an accumulated
declaration). Not thread-safe. Public surface:

- `[[nodiscard]] ResourceHandle declareResource(std::string_view label = {})` —
  `label` copied into owned storage immediately, never borrowed. A resource
  never given a write usage is valid and may still be read.
- `[[nodiscard]] PassHandle declarePass(std::string_view label = {})` — same
  label rules.
- `void reads(PassHandle, ResourceHandle)` / `void writes(PassHandle,
  ResourceHandle)` — declare a read / write usage. Declaring the same
  (pass, resource) read or write more than once is legal and idempotent (a
  double write does not count as a second distinct producer). A read **and** a
  write of the same resource on the same pass is a programmer error
  (`ATLANTIS_CHECK`), checked against that pass's accumulated usage state at
  the point of the call. A default/invalid or foreign-live-builder handle is a
  programmer error (§5).
- `[[nodiscard]] atlantis::Result<CompiledGraph, CompileError> compile() const`
  — reads the accumulated state and never mutates, consumes, or invalidates the
  builder, on success or failure (enforced by `const`, not only documented).
  Repeatable: compiling an unmodified builder again yields an equivalent
  result. Algorithm: §6.

Internal records: `PassRecord { std::string label; std::vector<ResourceUsage>
usages; }`, `ResourceRecord { std::string label; }`,
`ResourceUsage { std::size_t resourceIndex; UsageKind kind; }`. A pass's/
resource's position in `passes_`/`resources_` **is** both its handle index and
its `declarationIndex` — one field, no separate `declarationIndex` member.

**No `PassBuilder`-shaped proxy type** (a prior revision had one): returning a
proxy bound to one `PassHandle` from `declarePass()` made a default or foreign
`PassHandle` untestable through the public API (there was no way to obtain a
proxy wrapping an invalid handle). The direct `reads`/`writes` signatures above
close that gap with less API surface, no borrowed-reference type, and no loss
of append-only-ness.

## 4. `CompiledGraph`

`compiled_graph.h` — the immutable, independently-owned result of a successful
`compile()`. Owns its own compiled-local pass identity, order, labels, and
dependency relations; never borrows the builder's storage; no shared/
reference-counted state (no two instances share data). Move-only: both move
construction and move assignment kept (`= default` each, `= delete` copy). The
originating builder may be destroyed, or keep accumulating, without affecting
any `CompiledGraph` already produced. **Not declared thread-safe for concurrent
access, including concurrent reads** — immutability is a mutation guarantee,
not a concurrency guarantee.

Public accessors: `std::size_t passCount()`; `CompiledPassId passOrder(std::size_t
position)` (position 0 executes first); `std::string_view label(CompiledPassId)`;
`std::size_t dependencyCount()`; `CompiledDependencyEdge dependency(std::size_t i)`.
Out-of-range position/id/index is a programmer error (`ATLANTIS_CHECK`); under a
non-terminating handler the fallback return is a default-constructed
(invalid-sentinel) `CompiledPassId` / empty `std::string_view` / a
`CompiledDependencyEdge` with both endpoints invalid-sentinel — never
mistakable for a real result (except `label()`'s empty view, which is
documented as not perfectly distinguishable from a legitimately empty label but
harmless for a diagnostic-only accessor). **No resource identity or
resource-level query at all** — Spec 0005's "Compiled graph output" fixes the
minimum content and does not require resources to be independently queryable;
every Acceptance Criterion is checkable from pass order and pass-to-pass edges
alone.

- **`CompiledPassId`** — a compiled-local identity distinct from `PassHandle`
  and `ResourceHandle`. `index()` is the pass's zero-based compiled-order
  position (one field for both position and identity, since every pass appears
  exactly once). Default-constructed to `static_cast<std::size_t>(-1)` —
  **never `0`**, which is a legitimate position. A default or out-of-range
  `CompiledPassId` passed to a `CompiledGraph` accessor **is**
  guaranteed-detectable. A `CompiledPassId` from a *different* `CompiledGraph`
  whose index happens to be in range for the one it is passed to is **not**
  claimed detectable — a graph-scoped identity precondition violation (Spec
  0005 Error Model's lifetime-precondition tier), because `CompiledGraph` is
  movable and so has no stable address to tag identity with (unlike the
  non-movable builder, §5); manufacturing one would require the
  generation-counter/registry machinery Spec 0005 forbids. Carries no owner
  pointer, generation counter, or global registry.
- **`CompiledDependencyEdge { CompiledPassId from; CompiledPassId to; }`** —
  producer → reader; both endpoints interpreted against the same `CompiledGraph`
  that produced the edge.
- **`label()` borrow-lifetime contract:** the returned `std::string_view` is
  valid only while (a) the `CompiledGraph` instance is alive, (b) it has not
  since been the source of a move (construction or assignment), and (c) it has
  not since been the destination of a move-assignment that replaced its
  content. Move construction of one instance has no effect on any other,
  independent instance. Self-move-assignment gets no stronger guarantee than
  the defaulted operator. A caller needing the label to outlive the
  `CompiledGraph` must copy it into an owned `std::string`.
  `static_assert(std::is_move_constructible_v<CompiledGraph>)` and
  `std::is_move_assignable_v` are both in the test matrix.

## 5. Ownership, provenance, and lifetime — implementation strategy

The concrete mechanism behind
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)'s
decision; it implements, not changes, that decision.

- **Handle provenance = the builder's own address.** `PassHandle`/
  `ResourceHandle` store `owner_ = static_cast<const void*>(this)` at vend time.
  This is safe *because* the builder is non-copyable and non-movable — an
  object's address never changes across its own lifetime once neither copied
  nor moved, so `handle.owner_ == this` inside a builder method is a stable
  provenance check. It resolves both guaranteed-detectable cases: a
  default-constructed handle (`owner_ == nullptr`, never equal to a live
  builder's non-null `this`) and a handle from a different, live builder (a
  different live address). `owns(handle)` also checks `handle.index_ <
  passes_.size()` / `resources_.size()`. No global registry, generation
  counter, or handle recycling — there is no removal/reuse mechanism for a
  handle to recycle from.
- **Use-after-builder-destruction is not claimed detectable** — if builder `A`
  is destroyed and a later builder `B` is allocated at the same address, a
  stale handle from `A` would compare equal to `B`'s `this`. Preventing this
  would require the machinery Spec 0005 excludes. This is why Spec 0005
  classifies it as an undetected lifetime precondition violation and §9's test
  matrix has no dynamic test for it.
- **The check-then-early-return pattern.** `ATLANTIS_CHECK` calls the installed
  `AssertFailureHandler` and then **falls through** — only the *default*
  handler aborts; `tests/core/assert_tests.cpp` relies on a recording
  non-terminating replacement. So every provenance/bounds check **computes its
  condition exactly once into a local `const bool`, passes that local to
  `ATLANTIS_CHECK`, then branches on it explicitly and returns** a safe,
  non-misleading fallback (for non-`void` methods) — never re-evaluating the
  condition, never falling through into an out-of-range index. In production
  the fallback is unreachable (the default handler aborts first); under a test
  handler it keeps the failure-handler-replaced path memory-safe. It does not
  turn a programmer error into recoverable caller-facing behavior. Applied
  uniformly across `reads()`/`writes()` and every `CompiledGraph` accessor.
- **`CompiledGraph` independent ownership.** `compile()`'s success path
  deep-copies each pass's `label` into `CompiledGraph`'s own storage and builds
  edges from plain `CompiledPassId` (plain `std::size_t` positions, not
  pointers back into the builder) — nothing inside a `CompiledGraph` references
  the builder. So the builder may be destroyed immediately after, or keep
  accumulating; two `compile()` calls each produce their own independent deep
  copy.
- **Failed-compile builder validity.** `compile()` never writes
  `passes_`/`resources_` on any path — every step reads that state into local
  algorithm-scoped variables. A failed compile leaves the builder exactly as it
  was, compilable again unmodified to observe the same `CompileError`.

## 6. Dependency derivation and compilation algorithm

`compile() const` delegates to a private free function over plain data —
directly unit-testable without a full `RenderGraphBuilder` (mirrors
`plans/0003`'s `detail::decideRecreateAction()` precedent):
`detail::compile(const std::vector<RawPass>& passes, std::size_t resourceCount)
-> Result<CompiledGraphData, CompileError>`, over
`RawPass { std::string label; std::vector<RawUsage> usages; }` /
`RawUsage { std::size_t resourceIndex; UsageKind kind; }`. `CompiledGraphData`
is a plain intermediate (the two parallel vectors used to construct the public
`CompiledGraph`), not a public type.

1. **Verify at most one producer per resource.** Iterate resource index `r` in
   **ascending order** (== ascending `declarationIndex`). For each `r`, scan
   every pass's usages in ascending pass-index order; collect the *distinct*
   pass indices declaring a `Write` against `r` (a `std::vector`, pushed in
   ascending order only if not already present — a linear scan, not a set,
   since duplicate writes by one pass collapse to one entry). If more than one
   distinct pass is found for this `r`, **stop the entire check immediately**
   (do not scan later resources) and return
   `Err(MultipleProducersError{ .resource = {r's declarationIndex, r's label},
   .producers = those distinct producers, ascending by declarationIndex })`.
   This check **runs to completion (or fails) before any edge is derived or any
   ordering is attempted** — see "Error selection priority" below.
2. **Derive producer → reader edges.** Every resource now has zero or one
   producer. Scan every pass's `Read` usages; for each read of resource `r`
   whose producer (from step 1) is pass `p` (never the reading pass itself —
   structurally impossible, checked anyway), record the ordered pair
   `(p, reader)`.
3. **De-duplicate edges** via a `std::set<std::pair<std::size_t,std::size_t>>`
   (tree-ordered by value, **not** `std::unordered_set`) — the resulting deduped
   sequence is deterministic, ascending by `(from, to)`, with no hash-bucket /
   insertion-order / pointer dependency. Copy into a `std::vector` (`edges`).
4. **Deterministic topological order (Kahn's algorithm, declaration-order
   tie-break).** Compute `inDegree[p]` from `edges`. Repeat `passes.size()`
   times: linearly scan passes in ascending index order for the first
   not-yet-output pass with `inDegree == 0`; if none, stop early (step 6).
   Output it, mark it output, decrement `inDegree` for every pass it has an
   edge to (iterating `edges` in its already-sorted order). **Deliberately
   `O(passes²)`** instead of a priority-queue `O(passes·log passes)` — at Phase
   1 scale the "always scan ascending from zero, first hit wins" rule is
   obviously deterministic by inspection and sidesteps a `std::priority_queue`'s
   comparator subtleties.
5. **Success.** If `compiledOrder.size() == passes.size()`, build the returned
   `CompiledGraphData` from `compiledOrder` (this *is* the compiled position
   order) and `edges` (translated to `CompiledPassId` pairs), copying each
   pass's `label` into owned storage. Return `Ok(...)`.
6. **Cycle witness (only if step 4 stopped early) — three-color DFS over
   exactly the remaining (not-output) subgraph, not "every not-output pass is
   in a cycle."** A pass that merely *depends* on a cycle without being in it
   also never reaches `inDegree == 0`, so the remaining set can be strictly
   larger than the actual cycle. Iterate remaining passes in ascending
   `declarationIndex`; for each still-`Unvisited` one, DFS from it (an explicit
   stack, not recursion — keeps traversal state inspectable). Mark `Visiting`,
   push onto the path stack, visit outgoing edges restricted to remaining-
   subgraph targets in ascending target-`declarationIndex` order (the order
   `edges` is already sorted in): an edge to `Unvisited` recurses; an edge to
   `Visiting` is a **back edge** — the cycle is exactly the path-stack suffix
   from that pass to the current top, closed by this edge (**not** the whole
   remaining subgraph, and **not** any pass below that suffix); an edge to
   `Finished` is skipped. The **first** back edge in this fixed order
   determines the reported cycle. **Canonical rotation:** rotate the cycle
   sequence to begin at its own smallest-`declarationIndex` member, preserving
   edge-following direction — so the witness is independent of which pass the
   DFS was visiting when it found the back edge. Every adjacent pair in the
   witness (including the wrap-around) corresponds to a real derived edge.
   Return `Err(DependencyCycleError{ .passes = witness })` (each entry:
   `declarationIndex` + owned `label` copy); no `.resource` field exists.
7. **The builder is never mutated** — `compile()` is `const`; every step reads
   the caller-supplied `RawPass` vector into local state.
8. **No partial `CompiledGraph` is ever constructed** — its constructor is
   invoked only from step 5's success path; every other path returns `Err`
   before it is reachable.

**Error selection priority and determinism.** A single graph can exhibit both a
multiple-producer conflict and a dependency cycle at once (among unrelated
resources/passes). Fixed: **multiple-producer validation (step 1) always runs
to completion/failure before cycle detection (steps 2–6) ever starts** — steps
2–6 are literally not reached on a step-1 failure. A graph with both problems is
**always** reported as `MultipleProducersError`, and when more than one
resource independently qualifies, the smallest-`declarationIndex` one is
reported (a direct consequence of step 1's ascending iteration + stop-at-first).
None of this depends on unordered/hash iteration, pointer values, or thread
scheduling — repeated `compile()` on the same unmodified multi-problem graph
reports the identical `MultipleProducersError` every time (ADR-0018's
determinism requirement applied to the priority rule).

**Complexity / determinism:** steps 1–2 `O(passes·usages)`; step 3
`O(edges·log edges)` (`std::set`, tree-ordered); step 4 `O(passes² + edges)`;
step 6 (failure only, at most once) `O(remaining passes + edges)`. Every
ordering decision is driven by `declarationIndex` or `std::set`'s
value-ordering — never `std::unordered_map`/`std::unordered_set` iteration, a
pointer/address comparison used for *ordering* (as opposed to provenance
identity in §5), or any thread-scheduling/wall-clock effect.

## 7. Plan-stage details closed by this round

Per Spec 0005's "concrete C++ type/method names and exact signatures are left
to the Plan." None changes the ownership, dependency-derivation, threading, or
error-classification model Spec 0005/ADR-0017/ADR-0018 fix. Key dispositions:

- Diagnostic label type: `std::string_view` parameter (default `{}`), copied
  into an owned `std::string` immediately. Resource labels optional under the
  same rule.
- Duplicate read declaration: legal, idempotent no-op (edge derivation
  de-duplicates). Duplicate write declaration: legal — one pass counted as one
  distinct producer, never triggering `MultipleProducers`.
- Whether a declared-but-unused resource appears in `CompiledGraph`'s query
  surface: it does not — `CompiledGraph` exposes no resource-level query.
- Producer-less resource: no special representation — a `ResourceRecord` with
  zero recorded `Write` usages across all passes *is* one.
- Pass/resource identity and declaration order: one `std::size_t` position
  serves as both the handle index and the declaration order.
- Builder provenance: the builder's own `this` pointer, safe because
  non-copyable/non-movable (§5).
- Cycle-witness selection: three-color DFS over the remaining subgraph,
  smallest-`declarationIndex`-first root and edge order, canonical rotation
  (§6).
- Error payload: `std::variant<MultipleProducersError, DependencyCycleError>`
  (§2) — corrects this Plan's own prior `{kind, optional}` shape; illegal
  combinations become compile errors, not runtime possibilities.
- `CompiledGraph` copy/move: move-only, both move construction **and** move
  assignment kept — `Result` plumbing needs only move construction, but caller
  code re-running `compile()` and replacing a held `CompiledGraph` is a normal
  pattern this Plan does not force into an `std::optional` wrapper.
- Empty graph: `Ok(CompiledGraph)` with `passCount() == 0` /
  `dependencyCount() == 0`, no special-casing (step 4's loop runs zero times,
  step 5's `0 == 0` holds).
- Assertion testing without real UB: the check-then-early-return pattern,
  condition computed exactly once (§5), grounded in `assert.h`'s actual
  non-`[[noreturn]]` `ATLANTIS_CHECK` behavior.
- `CompiledPassId` invalid sentinel: `static_cast<std::size_t>(-1)`, never `0`.
- `CompiledPassId` cross-`CompiledGraph` misuse when the index is coincidentally
  in range: not claimed detectable — an accepted, documented limitation
  (`CompiledGraph` is movable, so no stable address to tag).
- Error selection priority when both a multiple-producer conflict and a cycle
  exist: multiple-producer always wins, deterministically (§6).

**Human Review blockers surfaced by drafting this Plan: none** — every item is
a Plan-stage implementation-shape decision with no public ownership/dependency/
threading/error-classification weight beyond what Spec 0005 and
ADR-0017/ADR-0018 fix.

## 8. Error model implementation

Directly implements Spec 0005's four-tier Error Model, no reopening:

- **Compile-time type error:** `PassHandle`/`ResourceHandle`/`CompiledPassId`
  are distinct types with no conversion between any pair — a misuse fails to
  compile; nothing for runtime code to check.
- **Guaranteed-detectable runtime programmer error (`ATLANTIS_CHECK`):** every
  method accepting a handle or index calls `owns(...)`/a bounds check via the
  check-then-early-return pattern (§5). Covers: default/invalid handle, a
  handle from a different currently-live builder, a pass's own read-then-write
  or write-then-read of the same resource (inside `declareUsage()`, called from
  both `reads()` and `writes()`), and `CompiledGraph::passOrder()`/`label()`/
  `dependency()` with an out-of-range argument.
- **Lifetime/identity precondition violation (not detectable, not tested):**
  using a handle after its originating builder is destroyed; using a
  `CompiledPassId` against a different `CompiledGraph` when its index is
  coincidentally in range. Both accepted as documented limitations (no stable
  address to tag identity without inventing state Spec 0005 forbids). §9 states
  explicitly that no dynamic test exercises either.
- **Recoverable compile error (`atlantis::Result`):** `CompileError` (§2),
  returned from `compile()`/`detail::compile()` — never an exception.

No new assertion macro, `Result`-like type, or third error mechanism —
`ATLANTIS_CHECK` (ADR-0009) and `atlantis::Result` (Spec 0001) are reused
exactly as they exist.

## 9. Testing strategy

Every test is GPU-independent, runs with no Vulkan device and no window, and
belongs to the single `atlantis_render_graph_tests` Catch2 v3 executable — **no
CTest `gpu` label** is introduced or needed (Spec 0005 has no GPU-touching
scope). Run via `ctest --test-dir <build> -C Debug -LE gpu --output-on-failure`
and the `-C Release` equivalent.

Two layers: `compile_algorithm_tests.cpp` exercises `detail::compile()`
directly (white-box, plain `RawPass` data, no builder) — but
`RenderGraphBuilder::compile()` (production code, §6/§12 step 7) calls the same
function; every other file exercises the full public API exclusively
(black-box). See the file table in §1 for each file's coverage; at minimum it
covers every Acceptance-Criterion behavior in §13, including the cycle-detection
downstream-pass and multiple-cycle cases and the multiple-producer-beats-cycle
priority.

**Explicitly not tested** (Spec 0005 Acceptance Criteria; §5/§8 design): using
a `PassHandle`/`ResourceHandle` after its builder is destroyed; using a
`CompiledPassId` against a different `CompiledGraph` when its index is
coincidentally in range. A dynamic test of either would be exercising undefined
behavior.

Compile-time properties (non-copyable/non-movable builder; pairwise-distinct
handle/id types) are verified via `static_assert`, which fails the *build* if
violated — stronger than a runtime `TEST_CASE`.

**On CTest's `gpu` label, precisely:** this module registers no `gpu`-labeled
test (no `PROPERTIES LABELS "gpu"` anywhere in
`tests/render_graph/CMakeLists.txt`). This Plan does **not** claim `ctest -L
gpu` returns zero tests repository-wide — `atlantis_vulkan_backend_gpu_tests`
(Plan 0003, merged) is real, existing, unrelated infrastructure this Plan does
not touch. The checkable claim: `ctest --test-dir <build> -N -L gpu` count is
identical before and after this Plan's implementation.

## 10. Explicit prohibitions (verification-checkable)

None may appear anywhere in `src/render_graph/` or `tests/render_graph/`:

| Prohibited | Check |
|---|---|
| Any `Vk*` type or `#include <vulkan/...>` | grep across `src/render_graph/` |
| Any Atlantis Platform type (`NativeWindowHandle`, `PlatformEvent`, …) | grep for `atlantis/platform` includes |
| Any RHI type (`Device`, `Presentation`, `Extent2D`, …) | grep for `atlantis/rhi` includes |
| A caller-authored dependency edge (`dependsOn`, `before`, `after`, priority/order override, integer sort key) | grep those identifiers (case-insensitive) across new headers |
| Any pass-culling logic | code review that `compiledOrder.size()` on success always equals the input pass count |
| Any lifetime interval, imported/transient classification, or resource physical property field | grep `lifetime`, `imported`, `transient`, `format`, `usage.*flag`, `memory` across the public headers |
| A generation counter, handle-recycling field, or global handle registry | grep `generation`, `recycle`, any `static`/global mutable container across `src/render_graph/` |
| A mutex, atomic, or lock-free structure | grep `std::mutex`, `std::atomic`, `std::lock_guard` |
| A `gpu`-labeled test registration | grep `LABELS` in `tests/render_graph/CMakeLists.txt` — no match |
| `ATLANTIS_CHECK`/`ATLANTIS_ASSERT` misuse that skips the early-return pattern, or re-evaluates its condition instead of reusing one local `bool` | code review of every provenance/bounds check against §5 |

## 11. Build integration

Root `CMakeLists.txt` gains `add_subdirectory(src/render_graph)` and, inside the
existing `ATLANTIS_BUILD_TESTS` block, `add_subdirectory(tests/render_graph)` —
**both in Implementation Order step 1**. `tests/render_graph/CMakeLists.txt`
builds one `atlantis_render_graph_tests` executable from the six source files
(§1), links `Atlantis::RenderGraph` + `Catch2::Catch2WithMain` +
`atlantis_compiler_warnings`, and adds a **`PRIVATE` include path**
(`${CMAKE_SOURCE_DIR}/src/render_graph/src`) so `compile_algorithm_tests.cpp`
can `#include` the private `compile_algorithm.h` — never added to
`atlantis_render_graph`'s own PUBLIC interface. `catch_discover_tests` with
**no** `PROPERTIES LABELS "gpu"` (§9). No `find_package` addition — no new
external dependency.

## 12. Implementation order

Each step ends with a build-and-test action. None has been executed by this
Plan. Exactly **10** steps.

1. **Module/CMake skeleton** (§1, §11): `src/render_graph/CMakeLists.txt`; root
   `CMakeLists.txt`'s two `add_subdirectory` additions (**both here**);
   `tests/render_graph/CMakeLists.txt` created listing only
   `handle_ownership_tests.cpp` initially (later steps append their file to the
   same `add_executable`) with the `PRIVATE` include path already added; empty
   `include/` headers with forward declarations only. `atlantis_render_graph`
   builds (Debug + Release), linking only `Atlantis::Core`.
2. **`handles.h`** (§2) + `handle_ownership_tests.cpp`'s compile-time
   `static_assert`s: build + `ctest -LE gpu` (trivial pass).
3. **`compile_error.h`** (§2): build only (pure data types).
4. **`compiled_graph.h`/`.cpp`** (§4), including the
   `passOrder()`/`label()`/`dependency()` assertion-fallback bodies (§5),
   private constructor with no caller yet: build only.
5. **`render_graph_builder.h`**'s declaration surface (§3) —
   `declareResource()`, `declarePass()`, `reads()`, `writes()`, the `owns()`
   checks and the check-then-early-return pattern (§5); `compile()` declared,
   not defined: build; `handle_ownership_tests.cpp`'s runtime assertion-policy
   cases and `dependency_derivation_tests.cpp`'s same-pass-read+write case pass
   — `ctest -LE gpu`.
6. **`compile_algorithm.h`/`.cpp`**'s `detail::compile()` (§6, all 8 steps
   including the three-color-DFS witness) as a free function over plain
   `RawPass` data. Add `compile_algorithm_tests.cpp` to the source list: build
   + `ctest -LE gpu`, exercising its white-box cases directly — no builder
   involved yet.
7. **`RenderGraphBuilder::compile()`** wired to `detail::compile()`, translating
   `passes_`/`resources_` into `RawPass`/resource-count inputs and a successful
   result into a public `CompiledGraph` (§4's private constructor now has a
   caller). Add `dependency_derivation_tests.cpp`, `cycle_detection_tests.cpp`,
   `pass_retention_and_ordering_tests.cpp`, and `ownership_lifetime_tests.cpp`
   to the source list — **only now buildable**, since every one calls
   `compile()`: build + full `ctest -LE gpu` — first point every public-API
   test file exercises the full API end-to-end.
8. **Full test suite pass**: every file green via `ctest -C Debug -LE gpu`,
   then `-C Release -LE gpu`.
9. **Documentation**: `src/README.md`, `tests/README.md` only (CMake
   registrations completed in step 1, grown in steps 6–7).
10. **Final verification pass**: Debug + Release configure/build; both `ctest
    -LE gpu` runs; zero new compiler warnings; §10's grep checklist; `ctest -N
    -L gpu` count unchanged before step 1 and now (§9); §13's mapping
    re-confirmed against the actual diff; Definition of Done pass before PR.

**Sequencing:** steps 1–4 have no interdependency beyond step 1. Step 5 depends
on 1–2 (handles) and 4 (forward-declares `CompiledGraph`). Step 6 depends on 3
only — deliberately independent of step 5, so the algorithm's correctness is
verified against plain data before any public-API wiring. Step 7 depends on
4–6. Step 8 depends on 7. Steps 9–10 depend on everything. No step requires a
Vulkan SDK, a GPU, or a live window.

## 13. Acceptance Criteria Mapping

Every Spec 0005 Acceptance Criterion, mapped (exactly Spec 0005's 34 literal
criteria — no more, no fewer; the extra test-design detail §6/§9 add strengthens
coverage of the *same* rows).

| Spec 0005 Acceptance Criterion | Satisfied by (step) | Verified by |
|---|---|---|
| Public headers contain no `Vk*`/`#include <vulkan/...>`/Platform type/RHI resource type | Step 1 | §10 grep checklist |
| No Vulkan call, no RHI command-recording call | Steps 1–10 | §10 grep checklist |
| No GPU-required test exists | Step 1 (no `gpu`-labeled test) | `ctest -N -L gpu` count unchanged; code review of §11 CMake |
| No rendering output/image/`RenderTarget` | Steps 1–10 | §10 grep checklist |
| Empty graph compiles successfully | Step 6/7 (§6 step 5's `0 == 0`) | `pass_retention_and_ordering_tests.cpp` |
| Single pass compiles successfully | Step 7 | `pass_retention_and_ordering_tests.cpp` |
| Single-producer single/multi-reader order | Step 6/7 | `dependency_derivation_tests.cpp`, `compile_algorithm_tests.cpp` |
| Multiple readers produce no edge between them | Step 6 (§6 step 2 only derives producer→reader) | `dependency_derivation_tests.cpp`, `compile_algorithm_tests.cpp` |
| Independent producer/reader groups: deterministic, declaration-order-driven | Step 6 (§6 step 4) | `pass_retention_and_ordering_tests.cpp` |
| Multiple producers rejected unconditionally | Step 6 (§6 step 1) | `dependency_derivation_tests.cpp`, `compile_algorithm_tests.cpp` — incl. smallest-`declarationIndex`-wins and multiple-producer-beats-cycle |
| Same-pass read+write rejected as programmer error | Step 5 | `dependency_derivation_tests.cpp` |
| Two-pass cycle detected with deterministic witness | Step 6 (§6 step 6) | `cycle_detection_tests.cpp`, `compile_algorithm_tests.cpp` |
| Longer cycle detected | Step 6 | `cycle_detection_tests.cpp` (incl. downstream-pass and multiple-cycle cases) |
| Cycle witness unambiguous under duplicate labels | Step 6 (`declarationIndex`) | `cycle_detection_tests.cpp` |
| Isolated pass and producer-with-no-readers retained | Step 6/7 (§6 step 5) | `pass_retention_and_ordering_tests.cpp` |
| Declared-but-unused resource accepted, no dependency relation | Step 6 | `dependency_derivation_tests.cpp` |
| Every declared pass appears exactly once | Step 6/7 (§6 step 5 invariant) | All of §9 |
| Builder non-copyable/non-movable | Step 1/5 | `handle_ownership_tests.cpp` (`static_assert`) |
| Handle/`CompiledPassId` types mutually distinct, no runtime cross-type case | Step 2/4 | `handle_ownership_tests.cpp` (`static_assert`); §10 grep |
| Default/foreign-live-builder handle triggers assertion | Step 5 (§5's `owns()` + check-then-early-return) | `handle_ownership_tests.cpp` |
| Use-after-builder-destruction not claimed/tested | §5 design note | §9's "explicitly not tested" statement; code review |
| Successful `CompiledGraph` valid after builder destruction | Step 7 (§5 deep copy) | `ownership_lifetime_tests.cpp` |
| Further declarations don't change an already-produced `CompiledGraph` | Step 7 | `ownership_lifetime_tests.cpp` |
| Two compiles produce independent, equivalent values | Step 7 | `ownership_lifetime_tests.cpp` |
| Duplicate labels legal, no effect on identity/order | Step 5/6 | `ownership_lifetime_tests.cpp`, `cycle_detection_tests.cpp` |
| Repeated compile deterministic | Step 6 (§6 "Complexity") | `ownership_lifetime_tests.cpp` |
| Failed compile: no partial graph, builder still valid | Step 6/7 (§6 step 8, §5) | `ownership_lifetime_tests.cpp` |
| No public mutation API on `CompiledGraph` | Step 4 (no non-`const` public method) | header inspection |
| No caller-authored dependency edge / `dependsOn` / sort key | Steps 1–10 | §10 grep checklist |
| No mutex/atomic/job system; no declared thread-safety beyond the baseline | Steps 1–10 | §10 grep checklist |
| No global handle registry/allocator/mutable graph state | Steps 1–10 (§5 has none) | §10 grep checklist |
| No lifetime interval/imported-transient/physical property | Steps 1–10 | §10 grep checklist |
| No `src/renderer/`, Shader System, or RHI resource/command source created | Steps 1–10 | directory listing |
| Both ADRs `Accepted` before Spec `Approved` | already satisfied 2026-08-09 | recorded fact |

## Verification Checklist

- [ ] **Unit tests:** all six `tests/render_graph/` files (§9) pass via `ctest
      --test-dir <build> -C Debug -LE gpu --output-on-failure` and
      `-C Release -LE gpu`.
- [ ] **Headless integration / image regression / Vulkan Validation:** not
      applicable — no GPU work of any kind (Spec 0005 Testing & Verification
      Plan).
- [ ] **Other:** §10's grep checklist passes; full Debug + Release build with
      zero new compiler warnings; `ctest -N -L gpu` count unchanged
      before/after implementation; §13's mapping re-confirmed against the
      actual diff.

## Rollback Plan

Purely additive: one new module (`src/render_graph/`) and its tests
(`tests/render_graph/`), plus CMake/documentation touch-ups. Reverting the
implementing PR removes both directories and restores `CMakeLists.txt`,
`src/README.md`, and `tests/README.md` to their post-Spec-0003 state — a
standard revert, nothing outside `src/render_graph/`/`tests/render_graph/` is
behaviorally changed.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas:

- **"Image regression tests", "Vulkan Validation Layers run clean", and
  "headless verification" are not applicable** — Spec 0005's entire scope is
  GPU-independent; nothing is rendered and no Vulkan call exists.
- **This Plan does not add, remove, or modify the repository's existing
  `atlantis_vulkan_backend_gpu_tests` suite** — whether that suite runs as part
  of this PR's gate is governed by Plan 0003's Definition of Done.
- Add: §10's grep checklist passes; §13's mapping is re-confirmed against the
  actual diff; the full GPU-independent suite (§9) has actually been run via
  both `ctest -C Debug -LE gpu` and `ctest -C Release -LE gpu`, not merely
  written; `ctest -N -L gpu`'s count is confirmed unchanged.
