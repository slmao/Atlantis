# Plan: Atlantis RenderGraph Foundation (GPU-Independent Graph Core)

- **Spec:** [specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; approved at joint Spec 0005 + Plan 0005 Human Review on
  2026-08-09 (see the Approval transition note immediately below for the
  exact scope approved).

> **Approval transition complete, 2026-08-09:** Human Review of
> `specs/0005-render-graph-foundation.md` (already `Approved`, its own
> Human Review recorded 2026-08-09) together with this Plan is complete.
> This Plan is now `Approved / Ready for Implementation`. The reviewer's
> decision, recorded here rather than only in chat:
> 1. **All nineteen Plan-stage details in Section 7's disposition
>    table are accepted as written**, including this round's items 18
>    (error selection priority and determinism between a multiple-producer
>    conflict and a dependency cycle) and 19 (`CompiledGraph` keeps both
>    move construction and move assignment, with the full view-
>    invalidation contract Section 4 now states) — none of the nineteen
>    changes Spec 0005/ADR-0017/ADR-0018's own decisions, confirming
>    Section 7's own claim that this Plan surfaced **zero** Human Review
>    blockers.
> 2. **`CompileError`'s `std::variant<MultipleProducersError,
>    DependencyCycleError>` representation (Section 2), the deterministic
>    error-selection priority rule (Section 6), and `CompiledGraph`'s
>    finalized move/view contract (Section 4) are accepted as the
>    authoritative candidate shapes** — no further restructuring of these
>    three areas is expected before Implementation absent a new defect.
> 3. **Every Non-Goal (top-of-document Non-Goals section), Section 10's
>    Explicit Prohibitions, and Section 9's "explicitly not tested" list**
>    remain fully in force for Implementation: no caller-authored
>    dependency edge, no pass culling, no resource-level query on
>    `CompiledGraph`, no global handle registry/generation counter, and no
>    dynamic test attempting to exercise either documented lifetime/
>    identity precondition violation.
> 4. **Sections 1–13, the Verification Checklist, the Rollback Plan, and
>    the Acceptance Criteria Mapping (all 34 rows) are accepted as
>    written** as the basis for Implementation, Section 12's ten-step
>    Implementation Order in the stated sequence.
>
> This record documents the Human Review decision; it does not itself
> constitute Implementation, and **no Implementation has begun** under
> this Plan as of this approval record.

> **Scope banner — read before anything else (historical framing,
> superseded in status by the Approval transition note above; the scope
> statement itself remains accurate).** Every C++ type, function
> signature, algorithm, and file name below was a **candidate shape for
> Human Review** at drafting time, now accepted as the approved basis for
> Implementation per the Approval transition note above — Spec 0005 and
> [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)/[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
> fix *behavior*; this Plan proposes the concrete C++ that satisfies that
> behavior, nothing more. Drafting this Plan surfaced no place where the
> Spec or an ADR would need to change — Section 7's blocker list is empty,
> confirmed accepted by the Approval transition note above.

> **Revision history:**
> - **This revision** makes four further corrections, none of which
>   changes Spec 0005/ADR-0017/ADR-0018's decisions: (1) `CompileError`
>   is restructured from the previous revision's own
>   `{kind, std::optional<ResourceDiagnostic> resource, std::vector<PassDiagnostic> passes}`
>   shape into `std::variant<MultipleProducersError, DependencyCycleError>`,
>   so a `DependencyCycle` payload carrying a resource, or a
>   `MultipleProducers` payload missing one, is no longer representable at
>   all — a correction of this Plan's own prior judgment call, not a new
>   Spec/ADR requirement (Sections 2, 6–9, 13); (2) the error selection
>   priority between a multiple-producer conflict and a dependency cycle
>   is now fixed and tested: multiple-producer validation always runs to
>   completion/failure before cycle detection starts, a graph with both
>   problems always reports `MultipleProducersError`, and the smallest-
>   `declarationIndex` violating resource is reported when more than one
>   resource qualifies (Section 6's new "Error selection priority and
>   determinism" subsection, Sections 7, 9, 13); (3) `CompiledGraph`'s
>   `std::string_view label()` borrow-lifetime contract is completed for
>   both move directions — the move-assignment-destination case, the
>   no-cross-instance-effect guarantee, and the self-move-assignment
>   caveat are now stated alongside the already-documented move-FROM case,
>   and the decision to keep (not remove) move assignment is recorded with
>   its rationale (Sections 4, 7, 9); (4) the public contract was
>   re-reviewed end to end: error payloads independently own their data
>   and never borrow the Builder, duplicate labels remain disambiguated by
>   `declarationIndex`, `CompileError` has no representable invalid
>   combination, every public type's thread-safety/ownership note remains
>   complete, and every candidate header's C++ standard includes still
>   precede its project includes — no gap found beyond items (1)–(3)
>   above (Consistency Review, items 14–16).
> - **Previous revision** fixed six further gaps found on re-review, none
>   of which changes Spec 0005/ADR-0017/ADR-0018's decisions: (1)
>   `CompileError`'s payload is restructured — `CycleWitnessEntry` (which
>   named a cycle-specific concept but was reused for `MultipleProducers`
>   too, with no way to name the offending resource) is replaced by
>   `PassDiagnostic`/`ResourceDiagnostic` plus an explicit, optional
>   `resource` field, so a `MultipleProducers` error can actually identify
>   the resource in conflict (Sections 2, 6–9, 13); (2) every public type
>   now states its thread-safety and ownership contract explicitly, not
>   only some of them (Sections 2–4); (3) `CompiledGraph::label()`'s
>   returned `std::string_view`'s borrow lifetime — tied to the
>   `CompiledGraph` instance, invalidated by destruction or by that
>   instance being moved-from — is now stated explicitly, alongside the
>   already-stated "input labels are copied immediately" rule (Section
>   4); (4) `CompiledPassId`'s boundary (invalid sentinel, out-of-range
>   detectable, cross-`CompiledGraph` collision not detectable, no owner
>   pointer/generation/registry) is restated as a single, consolidated
>   reference alongside the contract audit, rather than left implicit
>   between Sections 4 and 5 (Section 4); (5) every candidate header's
>   `#include` order is corrected to match `AGENTS.md` (C++ standard
>   library, blank line, project headers) — `render_graph_builder.h` had
>   project headers listed first (Section 3); (6) this note itself and
>   the Consistency Review are updated to reflect all of the above without
>   dropping the previous revision's own record.
> - **Earlier revision** fixed six earlier gaps: (1) the `PassBuilder`
>   proxy type was removed — it made a default/foreign `PassHandle`
>   untestable through the public API (Sections 1, 3, 5, 7–9, 12–13); (2)
>   `CompiledPassId`'s contract was corrected — an explicit invalid
>   sentinel (not `0`), no owner pointer (since `CompiledGraph` is
>   movable, unlike the builder), and an explicit acknowledgment that
>   cross-`CompiledGraph` misuse with a coincidentally in-range index is a
>   graph-scoped identity precondition violation, not a guaranteed
>   assertion (Sections 4–5, 8); (3) the cycle-witness algorithm's prior
>   assumption — "every pass Kahn's algorithm fails to output is part of a
>   cycle" — was corrected (false: a pass can merely depend on a cycle
>   without being in it) and replaced with an explicit three-color DFS
>   over the remaining subgraph (Section 6); (4) every non-`void` method
>   that can assert was given a documented, non-misleading fallback return
>   value, and every check-then-early-return example was fixed to compute
>   its condition exactly once, not twice (Sections 4–5, 8); (5) the
>   `Step 11` reference and the incorrect repo-wide "`ctest -L gpu` returns
>   zero tests" claim were corrected against this Plan's actual 10-step
>   Implementation Order and the repository's existing, unrelated
>   `atlantis_vulkan_backend_gpu_tests` suite (Sections 11–13); (6) the
>   private-`detail::`-algorithm test's include-path plumbing and its
>   relationship to public-API test coverage were made explicit, with a
>   dedicated white-box test file distinct from four black-box, public-
>   API-only test files (Sections 1, 9, 11–12).

## Objective

Turn `specs/0005-render-graph-foundation.md` into an ordered, reviewable
set of concrete changes: a new, GPU-independent **Atlantis RenderGraph**
module (`RenderGraphBuilder`, strongly-typed pass/resource handles, a
single-producer dependency model, deterministic compilation into an
independently-owned `CompiledGraph`) per
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)
(construction/compilation layering and ownership) and
[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
(dependency derivation and deterministic ordering).

## Authoritative Sources

Treated as authoritative, not reinterpreted:
`specs/0005-render-graph-foundation.md` (`Approved`),
`adr/0017-render-graph-construction-compile-layering.md` and
`adr/0018-render-graph-dependency-derivation-and-ordering.md` (both
`Accepted`), `AGENTS.md`, `docs/process/testing-strategy.md`,
`docs/process/definition-of-done.md`,
`plans/0003-rhi-vulkan-windowed-foundation.md` (structural precedent for
how a Plan proposes candidate C++ shapes, a disposition table for
Spec-flagged open items, an Acceptance-Criteria-mapping table, and a
`detail::`-namespaced-pure-function testing pattern, without treating any
of that as new architecture; also read directly for how it documents the
repository's existing `atlantis_vulkan_backend_gpu_tests` suite, so this
Plan does not misdescribe it), `adr/0010-cmake-structure.md` (module/
target/alias/namespace convention), `src/core/include/atlantis/assert.h`
and `tests/core/assert_tests.cpp` (read directly, to ground Section 8's
assertion-testing design in the actual current `ATLANTIS_CHECK` behavior
rather than assuming one), `src/rhi/CMakeLists.txt` and
`tests/rhi/CMakeLists.txt` / `tests/core/CMakeLists.txt` (read directly,
for the exact current CMake target/test-registration pattern this Plan
follows), `src/README.md` and `tests/README.md` (read directly, for the
exact per-module documentation format this Plan's own doc updates match).

## Critical Architectural Boundaries (preserved, not re-decided here)

```
RenderGraphBuilder (mutable, non-copyable, non-movable, append-only)
  -- declares passes/resources/usages -->
  compile() [const, non-mutating]
  -- reads Builder state, never writes it -->
  Result<CompiledGraph, CompileError>
    Ok:  CompiledGraph -- independently owned, immutable, outlives Builder
    Err: CompileError  -- independently owned (no Builder handle inside),
                          outlives Builder, carries a deterministic witness
```

- RenderGraph's public headers: zero `Vk*` types, zero OS-specific types,
  zero RHI resource types. Only `Atlantis::Core` is a dependency
  (Spec 0005 Functional Requirements; `Approved`).
- No caller-authored pass-to-pass dependency edge of any kind — the only
  ordering mechanism is a producer-derived edge from resource usage
  (ADR-0018, `Accepted`).
- No pass culling — every successfully declared pass appears in the
  compiled order exactly once (ADR-0018, `Accepted`).
- The builder is the sole, exclusive, non-copyable, non-movable owner of
  its accumulated declarations; handles are builder-scoped value tokens
  (ADR-0017, `Accepted`).
- `compile()` never mutates, consumes, or invalidates the builder, on
  either success or failure; a successful `CompiledGraph` independently
  owns its own data (ADR-0017, `Accepted`).
- Single Phase 1 logical frame thread
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)); no declared
  concurrent-access guarantee for the builder or `CompiledGraph` beyond
  that baseline (Spec 0005 Proposed Design, "Threading").

## Non-Goals (explicitly confirmed, matching Spec 0005)

This Plan does **not** propose implementing, sketching, or illustratively
pre-declaring: any RHI resource/command type or call; any GPU/Vulkan
call of any kind; a `Renderer`, `RenderTarget`, or Shader System; a
caller-authored pass-to-pass dependency edge (`dependsOn`, before/after
relation, manual edge list, priority/order override, integer sort key);
automatic pass culling of any kind; resource lifetime intervals, an
imported/transient resource classification, or any resource physical
property (size/format/usage/memory); a resource-versioning model or
in-place read-modify-write; a global handle registry, generation counter,
handle recycling, or cross-builder/serialization-stable identity; any
declared thread-safety guarantee beyond Phase 1's single-logical-frame-
thread baseline; a job/task system, mutex, atomic, or lock-free
structure; Android/iOS/Linux support; or a second graphics backend of any
kind. See Section 10 for the verification-checkable version of this list.

---

## 1. Module and CMake Target Boundaries

One new module, following
[ADR-0010](../adr/0010-cmake-structure.md)'s established
`src/<module>/{include/atlantis/<module>/, src/}` → `atlantis_<module>` →
`Atlantis::<Module>` pattern exactly, matching `src/rhi/CMakeLists.txt`'s
current form:

| Module | Directory | CMake target | Alias | Namespace | Depends on (link) |
|---|---|---|---|---|---|
| Atlantis RenderGraph | `src/render_graph/` | `atlantis_render_graph` | `Atlantis::RenderGraph` | `atlantis::render_graph` | `Atlantis::Core` (PUBLIC) only |

```cmake
# src/render_graph/CMakeLists.txt (candidate — mirrors src/rhi/CMakeLists.txt)
add_library(atlantis_render_graph STATIC
  src/render_graph_builder.cpp
  src/compile_algorithm.cpp
  src/compiled_graph.cpp
)
add_library(Atlantis::RenderGraph ALIAS atlantis_render_graph)

target_include_directories(atlantis_render_graph
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_render_graph
  PUBLIC
    Atlantis::Core
  PRIVATE
    atlantis_compiler_warnings
)
```

No dependency on `Atlantis::RHI`, `Atlantis::Platform`,
`Atlantis::VulkanBackend`, or the Vulkan SDK — confirmed by this
`target_link_libraries` call having no such entry, matching Spec 0005's
own Module Boundary section ("this round has no RHI resource/command
dependency to consume").

### File-level layout (expected — not created by this Plan)

```
src/render_graph/
  CMakeLists.txt
  include/atlantis/render_graph/
    handles.h              -- PassHandle, ResourceHandle (public, opaque)
    render_graph_builder.h -- RenderGraphBuilder (declareResource,
                               declarePass, reads, writes, compile) --
                               no proxy/builder-scoped helper type; see
                               Section 3 for why
    compiled_graph.h        -- CompiledGraph, CompiledPassId, CompiledDependencyEdge
    compile_error.h         -- PassDiagnostic, ResourceDiagnostic,
                               MultipleProducersError, DependencyCycleError,
                               CompileError (variant of the two)
  src/
    render_graph_builder.cpp -- RenderGraphBuilder method bodies;
                                 provenance checks (Section 5); compile()
                                 delegates to detail::compile() (below)
    compile_algorithm.h / .cpp
                              -- private detail::compile(): the entire
                                 algorithm in Section 6 (including the
                                 three-color-DFS cycle-witness extraction),
                                 operating on plain, Builder-internal
                                 record types — directly unit-testable
                                 without constructing a full
                                 RenderGraphBuilder (mirrors plans/0003's
                                 detail::decideRecreateAction() precedent)
    compiled_graph.cpp        -- CompiledGraph method bodies, including
                                 the assertion-fallback return values
                                 (Section 5)

tests/render_graph/
  CMakeLists.txt           -- see Section 11 for the PRIVATE include-path
                               addition compile_algorithm_tests.cpp needs
  handle_ownership_tests.cpp       -- non-copyable/non-movable static
                                       checks; distinct handle/id types;
                                       default/foreign PassHandle/
                                       ResourceHandle assertion policy,
                                       exercised directly through
                                       RenderGraphBuilder::reads()/
                                       writes() (Section 8) -- public
                                       RenderGraphBuilder API only
  compile_algorithm_tests.cpp      -- white-box tests of
                                       detail::compile() directly against
                                       hand-built RawPass fixtures (no
                                       RenderGraphBuilder constructed) --
                                       the only file with the PRIVATE
                                       include-path addition (Section 11);
                                       this is not the algorithm's only
                                       consumer -- RenderGraphBuilder::
                                       compile() (production code, Section
                                       6/12 step 7) is the other
  dependency_derivation_tests.cpp  -- producer→reader edges, RAR no-edge,
                                       multiple-producer, same-pass
                                       read+write, producer-less/unused
                                       resources -- public
                                       RenderGraphBuilder/CompiledGraph
                                       API only
  cycle_detection_tests.cpp        -- two-pass and longer derived cycles
                                       (including a downstream-but-not-
                                       in-cycle pass with a smaller
                                       declarationIndex than the cycle
                                       itself), deterministic witness
                                       selection when more than one cycle
                                       exists, witness under duplicate
                                       labels, repeated-compile witness
                                       equivalence -- public
                                       RenderGraphBuilder/CompiledGraph
                                       API only
  pass_retention_and_ordering_tests.cpp
                                    -- empty/single-pass graphs, isolated
                                       pass retention, producer-with-no-
                                       readers retention, exactly-once
                                       invariant, declaration-order
                                       tie-break -- public API only
  ownership_lifetime_tests.cpp     -- builder-destroyed-before/after-
                                       CompiledGraph, repeated compile
                                       independence, failed-compile
                                       builder validity, no public
                                       mutation on CompiledGraph,
                                       CompiledPassId default/out-of-range
                                       assertion-fallback behavior --
                                       public API only
```

### Files to Modify (expected)

```
CMakeLists.txt   -- add_subdirectory for src/render_graph, tests/render_graph
src/README.md    -- add src/render_graph/ entry (mirrors the rhi/ entry's format)
tests/README.md  -- add tests/render_graph/ entry, documenting that it is
                     entirely GPU-independent (no `gpu` CTest label) and
                     runs under the existing `-LE gpu` command already
                     documented there
README.md        -- only if the top-level project-status paragraph needs
                     a one-line update once RenderGraph exists; no new
                     external dependency to document (Section 1's link
                     list has no Vulkan SDK entry)
```

**Both `CMakeLists.txt` `add_subdirectory` additions (`src/render_graph`
and, inside the existing `ATLANTIS_BUILD_TESTS` block, `tests/render_graph`)
land in Implementation Order Step 1 (Section 12), not the Documentation
step** — the module and its test target must both be registered before
any of Steps 2–8 can build or run anything.

No file under `src/core`, `src/platform`, `src/rhi`, `src/vulkan_backend`,
any `tests/` directory other than the new `tests/render_graph/`, or any
`examples/` directory is modified — this Plan is purely additive. No
`src/renderer/`, RHI resource/command source, or Shader System directory
is created.

---

## 2. Value Types and Handles — Candidate Shapes

**`handles.h`:**
```cpp
#pragma once

#include <cstddef>

namespace atlantis::render_graph {

class RenderGraphBuilder;  // fwd decl; only it may construct/interpret handles

// A graph-local, builder-scoped reference to a declared pass. Distinct,
// strongly-typed from ResourceHandle -- the two never implicitly convert
// to one another; using one where the other is expected is a compile
// error (Spec 0005 Error Model, compile-time-type-error tier).
//
// Ownership: a plain, trivially-copyable value token. It owns nothing --
// copying does not transfer or share ownership of anything, it only
// produces another reference to the same builder-scoped identity
// (ADR-0017). Default-constructed to an always-invalid state (index_ ==
// the sentinel below, never a valid position). Only valid for use with
// the RenderGraphBuilder instance that vended it, for that builder's
// lifetime -- using it after that builder is destroyed is a lifetime
// precondition violation, not a guaranteed-detectable error (Spec 0005
// Error Model; see Section 5 for why).
//
// Thread-safety: the value itself has no internal synchronization and
// needs none -- it may be freely copied and passed between threads as
// ordinary data. What is *not* thread-safe is using it: every call that
// takes a PassHandle (RenderGraphBuilder::reads()/writes(), etc.) must
// happen on the single Phase 1 logical frame thread that owns the
// RenderGraphBuilder it names (ADR-0004) -- the same restriction
// RenderGraphBuilder itself states (Section 3), not an independent one.
class PassHandle {
 public:
  PassHandle() noexcept = default;
  [[nodiscard]] bool operator==(const PassHandle&) const noexcept = default;

 private:
  friend class RenderGraphBuilder;
  PassHandle(const void* owner, std::size_t index) noexcept : owner_(owner), index_(index) {}

  const void* owner_ = nullptr;
  std::size_t index_ = static_cast<std::size_t>(-1);  // invalid sentinel, never a real position
};

// A graph-local, builder-scoped reference to a declared logical resource.
// Same ownership/thread-safety contract as PassHandle above, a distinct
// type from it.
class ResourceHandle {
 public:
  ResourceHandle() noexcept = default;
  [[nodiscard]] bool operator==(const ResourceHandle&) const noexcept = default;

 private:
  friend class RenderGraphBuilder;
  ResourceHandle(const void* owner, std::size_t index) noexcept : owner_(owner), index_(index) {}

  const void* owner_ = nullptr;
  std::size_t index_ = static_cast<std::size_t>(-1);
};

}  // namespace atlantis::render_graph
```

`PassHandle` and `ResourceHandle` are separate classes with no shared
base, no conversion operator between them, and no free function accepting
one where the other is declared — the "three mutually distinct types"
requirement (Spec 0005 Error Model; the third type, `CompiledPassId`, is
Section 4) is satisfied by the type system alone, verifiable by
inspection/compilation, not by a runtime check.

**`compile_error.h`:**
```cpp
#pragma once

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace atlantis::render_graph {

// A single pass's owned diagnostic identity, used inside CompileError.
//
// Ownership: fully independent -- `declarationIndex` is the zero-based
// position among all passes declared on the builder at the time
// compile() was called (a plain integer, not a PassHandle -- Section 5
// explains why a CompileError never carries a PassHandle), and `label`
// is an owned copy of that pass's diagnostic label (never borrowed).
// Both remain meaningful even after the originating builder is
// destroyed. `label` may be empty, and may duplicate another entry's
// label -- `declarationIndex`, not `label`, is what disambiguates
// entries (Spec 0005 Error Model).
//
// Thread-safety: an ordinary owned value type with no internal
// synchronization; safe to copy, move, or read from any single thread at
// a time, like any other plain data. Not applicable to unsynchronized
// concurrent mutation, which nothing in this Plan performs on it.
struct PassDiagnostic {
  std::size_t declarationIndex = 0;
  std::string label;
};

// A single logical resource's owned diagnostic identity, used inside
// MultipleProducersError (below). Same ownership/thread-safety contract
// as PassDiagnostic, but `declarationIndex` here is a position in the
// builder's *resource* declarations, never to be confused with a pass's
// position even though the two structs have identical shape -- they
// index different declaration spaces (`passes_` vs. `resources_`,
// Section 3).
struct ResourceDiagnostic {
  std::size_t declarationIndex = 0;
  std::string label;
};

// The exact, and only, shape a compile() failure takes when more than
// one pass declared a write usage against the same logical resource
// (Section 6, step 1). `resource` is that resource's own diagnostic
// identity -- always present; there is no state in which this type
// exists without one. `producers` lists every distinct producing pass,
// ordered by declarationIndex -- always at least two entries, since the
// only code path that constructs this type (Section 6) has already
// confirmed at least two distinct producers before doing so.
//
// Ownership: independently owned -- never references the builder that
// produced it (ADR-0017's independent-ownership requirement, applied to
// error payloads as well as the success payload).
//
// Thread-safety: an ordinary owned value type, safe to move/copy/read
// from a single thread at a time; produced once by a single compile()
// call and typically only read thereafter.
struct MultipleProducersError {
  ResourceDiagnostic resource;
  std::vector<PassDiagnostic> producers;
};

// The exact, and only, shape a compile() failure takes when a dependency
// cycle is found among producer-derived edges, with no resource at
// fault (Section 6, step 6). `passes` is the deterministic cycle
// witness: exactly the passes forming one concrete cycle, in canonical
// traversal order starting from its own smallest-declarationIndex
// member -- never a downstream pass that merely depends on the cycle
// without being part of it, and never every pass involved in every
// cycle if more than one exists. There is deliberately no `resource`
// field anywhere on this type -- not an always-empty one -- because no
// single resource ever identifies a cycle.
//
// Ownership/thread-safety: same as MultipleProducersError above.
struct DependencyCycleError {
  std::vector<PassDiagnostic> passes;
};

// The only two ways compile() can fail (Section 6): a closed sum type,
// not a `{kind, optional-payload}` struct -- see the rationale below for
// why this shape was chosen instead.
using CompileError = std::variant<MultipleProducersError, DependencyCycleError>;

}  // namespace atlantis::render_graph
```

**Why `CompileError` carries owned `declarationIndex`/`label` pairs
(`PassDiagnostic`/`ResourceDiagnostic`), not a `PassHandle`/
`ResourceHandle`:** a `PassHandle`/`ResourceHandle` is only valid for use
with its originating builder — putting one inside a `CompileError` that a
caller may inspect after discarding the builder (a normal, expected
pattern once `compile()` fails) would silently reintroduce a builder-
lifetime dependency this Plan's `CompiledGraph` design goes out of its way
to avoid on the success path (Section 4). A plain `std::size_t`
`declarationIndex` has no such dependency, and doubles as the
disambiguator duplicate labels need (Spec 0005 Acceptance Criteria: "a
cycle whose participating passes carry duplicate diagnostic labels still
produces a deterministic, unambiguously identifiable witness").

**Why `CompileError` is a `std::variant<MultipleProducersError,
DependencyCycleError>`, not a `{kind, std::optional<ResourceDiagnostic>,
std::vector<PassDiagnostic>}` struct (this Plan's own prior shape,
reversed this revision):** the earlier shape let a caller construct — or
a bug silently produce — a `CompileError` whose `kind` and payload
disagreed: a `DependencyCycle` with `resource` populated, or a
`MultipleProducers` with `resource` empty or fewer than two `passes`
entries. The earlier design could only rule these out by convention and
by this Plan's own prose, not by the type system. `MultipleProducersError`
and `DependencyCycleError` are two distinct, closed types, each shaped
exactly like the one case it represents: `MultipleProducersError` always
has a `resource` (not optional — a resource is in conflict whenever this
type exists at all) and always has `producers` (never fewer than two);
`DependencyCycleError` has no `resource` field at all. `std::variant`
makes every one of those invariants a compile-time property of which
alternative is active, not a runtime convention a consumer has to trust —
this is a genuine correction of this Plan's own earlier judgment call
(recorded, not silently dropped, in Section 7 item 10's rationale), not a
reversal forced by any change to Spec 0005 or ADR-0017/ADR-0018, neither
of which fixes this shape.

Consumers branch with `std::holds_alternative`/`std::get_if`/`std::visit`,
e.g.:
```cpp
atlantis::Result<CompiledGraph, CompileError> result = builder.compile();
if (result.isErr()) {
  const CompileError& error = result.error();
  if (const auto* multi = std::get_if<MultipleProducersError>(&error)) {
    // multi->resource, multi->producers
  } else {
    const auto& cycle = std::get<DependencyCycleError>(error);
    // cycle.passes
  }
}
```
This is no more verbose than the `kind`-switch it replaces, and it is not
possible to even name `.resource` on a `DependencyCycleError` — a compile
error, not a runtime `nullopt` check a caller could forget to make.

---

## 3. `RenderGraphBuilder` — Candidate Shape

**`render_graph_builder.h`:**
```cpp
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <atlantis/render_graph/compile_error.h>
#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/render_graph/handles.h>
#include <atlantis/result.h>

namespace atlantis::render_graph {

// Accumulates a graph-local description of passes, logical resources, and
// their read/write usage.
//
// Ownership: non-copyable, non-movable (see the deleted special members
// below), the sole, exclusive owner of its accumulated declarations
// (ADR-0017). Purely additive -- there is no operation to remove,
// replace, or edit an already-accumulated declaration (ADR-0017); a
// caller that needs a different graph description constructs a new
// builder. Every PassHandle/ResourceHandle it vends is scoped to this
// specific instance (Section 5) and remains valid for this instance's
// entire lifetime.
//
// Thread-safety: **not thread-safe.** Every method -- every declaration
// call and compile() itself -- must be called on the single Phase 1
// logical frame thread that owns this instance (ADR-0004); no method may
// be called concurrently with any other method on the same instance from
// a different thread.
class RenderGraphBuilder {
 public:
  RenderGraphBuilder() = default;
  ~RenderGraphBuilder() = default;

  RenderGraphBuilder(const RenderGraphBuilder&) = delete;
  RenderGraphBuilder& operator=(const RenderGraphBuilder&) = delete;
  RenderGraphBuilder(RenderGraphBuilder&&) = delete;
  RenderGraphBuilder& operator=(RenderGraphBuilder&&) = delete;

  // Creates a graph-local logical resource. `label` is an optional,
  // non-unique diagnostic label (copied into owned storage immediately;
  // never borrowed) -- see Section 5. A resource that is never given a
  // write usage by any pass is valid and may still be read (Spec 0005
  // Functional Requirements, "Logical resources").
  [[nodiscard]] ResourceHandle declareResource(std::string_view label = {});

  // Declares a pass. `label` is an optional, non-unique diagnostic label,
  // under the same rules as declareResource()'s.
  [[nodiscard]] PassHandle declarePass(std::string_view label = {});

  // Declares a read usage of `resource` by `pass`. Declaring the same
  // (pass, resource) read usage more than once is legal and idempotent --
  // see Section 7 item 3. `pass`/`resource` must have been vended by this
  // builder instance and still be within range; a default/invalid or
  // foreign-live-builder handle is a programmer error (assertion) -- see
  // Section 5/8.
  void reads(PassHandle pass, ResourceHandle resource);

  // Declares a write usage of `resource` by `pass`. Declaring the same
  // (pass, resource) write usage more than once (by this same pass) is
  // legal and idempotent -- it does not count as a second, distinct
  // producer (Section 7 item 4). Declaring a *read* and a *write* of the
  // same resource on the same pass is a programmer error (assertion) --
  // see Section 8; this method (and reads(), symmetrically) performs that
  // check, since each has both this pass's accumulated usage state and
  // the new usage available at the point of the call.
  void writes(PassHandle pass, ResourceHandle resource);

  // Reads this builder's accumulated state and produces a result; never
  // mutates, consumes, or invalidates this builder, on either success or
  // failure (ADR-0017) -- enforced by the compiler via `const`, not only
  // documented. Repeatable: calling this again on an unmodified builder
  // yields an equivalent result every time (ADR-0018's determinism
  // guarantee). See Section 6 for the algorithm.
  [[nodiscard]] atlantis::Result<CompiledGraph, CompileError> compile() const;

 private:
  enum class UsageKind { Read, Write };
  struct ResourceUsage {
    std::size_t resourceIndex;
    UsageKind kind;
  };
  struct PassRecord {
    std::string label;
    std::vector<ResourceUsage> usages;
  };
  struct ResourceRecord {
    std::string label;
  };

  // Provenance/validity checks -- see Section 5 for the UB-safe
  // check-then-early-return pattern every caller of these follows.
  [[nodiscard]] bool owns(PassHandle handle) const noexcept;
  [[nodiscard]] bool owns(ResourceHandle handle) const noexcept;

  void declareUsage(PassHandle pass, ResourceHandle resource, UsageKind kind);

  std::vector<PassRecord> passes_;
  std::vector<ResourceRecord> resources_;
};

}  // namespace atlantis::render_graph
```

**Why there is no `PassBuilder`-shaped proxy type here (a prior revision
of this Plan had one):** the prior revision returned a `PassBuilder`
(bound to one `PassHandle`) from `declarePass()`, and scoped `reads()`/
`writes()` to that proxy. On review, that design made a whole class of
required Acceptance Criteria tests **impossible to express through the
public API**: a "default-constructed `PassHandle`" or "a `PassHandle`
from a different, live builder" test case needs a `PassHandle` value to
pass to something — but the only way to obtain a `PassBuilder` at all was
`declarePass()`, which always returns one bound to a freshly, validly
allocated pass. There was no public way to construct or obtain a
`PassBuilder` wrapping an invalid or foreign `PassHandle` to exercise the
assertion path Section 8 requires being testable. The direct signatures
`reads(PassHandle, ResourceHandle)`/`writes(PassHandle, ResourceHandle)`
above close that gap: `PassHandle` is a real, public, copyable, default-
constructible value — exactly what `handle_ownership_tests.cpp` (Section
9) needs to construct a default one, or to carry a valid handle from one
`RenderGraphBuilder` over to a call on a different one. This also removes
a borrowed-reference type (`PassBuilder`, which had its own "must not
outlive the builder" lifetime rule layered on top of `PassHandle`'s own)
without losing any capability — the builder is still exactly as
append-only, and no explicit-dependency or edit/remove API is introduced
by this change.

A pass's `declarationIndex` (used throughout Section 6 and in
`PassDiagnostic`) is simply its position in `passes_` — identical to
`PassHandle::index_` for that pass. No separate field is introduced for
it; Section 6 refers to "declarationIndex" and "the handle's index" as
the same value. A resource's `declarationIndex` (used in
`ResourceDiagnostic`) is, symmetrically, its position in `resources_`,
identical to `ResourceHandle::index_`.

---

## 4. `CompiledGraph` — Candidate Shape

**`compiled_graph.h`:**
```cpp
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::render_graph {

class RenderGraphBuilder;  // fwd decl; only compile() constructs a CompiledGraph

// A compiled-local identity for a pass, distinct from PassHandle and
// distinct from CompileError's PassDiagnostic::declarationIndex.
// Interpreting one never requires the originating RenderGraphBuilder to
// still be alive (ADR-0017) -- it is meaningful only with respect to,
// and only while, the specific CompiledGraph that vended it is alive and
// has not been the source of a move (see that class's own contract
// below). Not usable as a RenderGraphBuilder declaration handle (a
// different type -- compile-time error, not a runtime check, if
// misused).
//
// Ownership/provenance contract -- consolidated here as the single
// authoritative statement of CompiledPassId's boundary, deliberately
// narrower than PassHandle/ResourceHandle's, because the difference
// matters:
//   - Default-constructed to an always-invalid sentinel (never a real
//     compiled-order position -- see index_ below).
//   - A default-constructed or otherwise out-of-range CompiledPassId,
//     passed to CompiledGraph::passOrder()/label()/dependency(), IS
//     guaranteed-detectable (a programmer-error assertion -- Section
//     5/8).
//   - A CompiledPassId that was vended by a *different* CompiledGraph,
//     whose index() happens to be in range for the CompiledGraph it is
//     actually passed to, is NOT claimed to be detectable -- this is a
//     graph-scoped identity precondition violation (Spec 0005 Error
//     Model's lifetime-precondition-violation tier, extended to this
//     case -- Section 8), the caller's obligation to avoid, not a case
//     this Plan tests dynamically (Section 9).
//   - Dependency endpoints (CompiledDependencyEdge::from/to, below) are
//     always meant to be interpreted against the *same* CompiledGraph
//     that produced the edge -- a CompiledDependencyEdge obtained from
//     one CompiledGraph is never mixed with a CompiledPassId from
//     another.
//   - CompiledPassId carries NO owner pointer/tag of any kind, unlike
//     PassHandle/ResourceHandle, and no generation counter or global
//     registry either. This is a deliberate difference, not an
//     oversight: PassHandle/ResourceHandle's owner-pointer trick is only
//     safe because RenderGraphBuilder is non-movable (Section 5) --
//     CompiledGraph is, per Spec 0005, at minimum movable (Section 7 item
//     11), so its address is NOT stable, and tagging a CompiledPassId
//     with "the CompiledGraph's address at vend time" would break the
//     moment that CompiledGraph is moved, exactly the address-instability
//     hazard ADR-0017's Context identifies for the builder case.
//     Manufacturing a stable identity anyway (a generation counter, a
//     heap-allocated shared control block, a global registry) would be
//     exactly the machinery Spec 0005 forbids for this purpose.
//
// Thread-safety: an ordinary, trivially-copyable value type with no
// internal synchronization and none needed -- safe to copy and pass
// between threads as data. Using it (passing it to a CompiledGraph
// method) is subject to that CompiledGraph's own no-concurrent-access
// contract (below), not an independent restriction.
class CompiledPassId {
 public:
  CompiledPassId() noexcept = default;
  [[nodiscard]] bool operator==(const CompiledPassId&) const noexcept = default;

  // The pass's zero-based position in the compiled execution order that
  // vended it -- this compiled-local identity's own concrete
  // representation, a Plan-stage decision (Spec 0005 leaves the
  // representation open). Returns the invalid sentinel for a default-
  // constructed CompiledPassId.
  [[nodiscard]] std::size_t index() const noexcept { return index_; }

 private:
  friend class CompiledGraph;
  explicit CompiledPassId(std::size_t index) noexcept : index_(index) {}
  std::size_t index_ = static_cast<std::size_t>(-1);  // invalid sentinel, never a real position
};

// A single producer -> reader dependency relation in a compiled graph.
// Both endpoints are meant to be interpreted against the same
// CompiledGraph that produced this edge (see CompiledPassId above).
// Default-constructed to two invalid-sentinel CompiledPassId endpoints --
// see CompiledGraph::dependency()'s assertion fallback (Section 5).
//
// Ownership: a plain, trivially-copyable aggregate of two CompiledPassId
// values -- owns nothing beyond them, no borrowed pointers.
//
// Thread-safety: same as CompiledPassId above -- safe to copy as data;
// using it against a CompiledGraph is subject to that CompiledGraph's
// own contract.
struct CompiledDependencyEdge {
  CompiledPassId from;  // the producer
  CompiledPassId to;    // the reader
};

// The immutable, independently-owned result of a successful compile()
// (ADR-0017). Owns its own compiled-local pass identity, order, labels,
// and dependency relations -- never borrows the originating
// RenderGraphBuilder's storage, and has no shared/reference-counted state
// of any kind (no two CompiledGraph instances ever share data). The
// originating builder may be destroyed, or may keep accumulating further
// declarations, without affecting any CompiledGraph already produced
// (ADR-0017; Section 5).
//
// Ownership and move semantics: move-only -- both move construction and
// move assignment are kept (see "Why move assignment is kept" below for
// why this Plan did not narrow to construction-only). Full view-
// invalidation contract, stated once here as the authoritative version
// (label()'s own comment cross-references this rather than repeating
// it):
//   - Moving FROM a CompiledGraph (as the source of either move
//     construction or move assignment) transfers its owned data to the
//     destination; the moved-from source is left in a valid but
//     unspecified state and must not be used for anything other than
//     destruction or re-assignment. Every std::string_view previously
//     obtained from label() on that source instance is invalidated by
//     the move, exactly as if the source had been destroyed.
//   - Moving INTO a CompiledGraph via move ASSIGNMENT (the destination
//     of `a = std::move(b)`) first discards `a`'s own prior owned data --
//     every std::string_view previously obtained from `a.label(...)`,
//     for content `a` held *before* the assignment, is invalidated at
//     that point, for the same reason as any other replaced owned
//     string: the storage it pointed into no longer holds that content.
//     Views obtained from `b` before the assignment remain invalid per
//     the "moving FROM" rule above -- a caller must re-call `label()` on
//     `a` after the assignment to obtain valid views into `a`'s new
//     (formerly `b`'s) content.
//   - Move construction of a new CompiledGraph from an existing one has
//     no effect on any *other*, independent CompiledGraph instance --
//     views into an unrelated CompiledGraph `c` remain valid regardless
//     of what happens to `a`/`b`.
//   - Self-move-assignment (`a = std::move(a)`) is not given any
//     stronger guarantee than the defaulted move-assignment operator
//     provides; a caller must not rely on it leaving `a` unchanged or in
//     any particular state.
//
// Thread-safety: **not declared thread-safe for concurrent access,
// including concurrent reads** (Spec 0005 Proposed Design, "Threading").
// Immutability here is a mutation guarantee (no public method mutates a
// CompiledGraph after construction), not a concurrency guarantee -- this
// Plan does not reason about, and does not claim, that it is safe to
// call any method (even a `const` one) on the same CompiledGraph
// instance from more than one thread without external synchronization.
// All access is assumed to happen on the single Phase 1 logical frame
// thread (ADR-0004).
class CompiledGraph {
 public:
  CompiledGraph(CompiledGraph&&) noexcept = default;
  CompiledGraph& operator=(CompiledGraph&&) noexcept = default;
  CompiledGraph(const CompiledGraph&) = delete;
  CompiledGraph& operator=(const CompiledGraph&) = delete;
  ~CompiledGraph() = default;

  // Every successfully declared pass appears exactly once, at some
  // position in [0, passCount()) -- no dead-pass/unreferenced-pass
  // culling of any kind (ADR-0018).
  [[nodiscard]] std::size_t passCount() const noexcept;

  // The pass at compiled-order position `position` (< passCount()).
  // Position 0 executes first. Passing position >= passCount() is a
  // programmer error (assertion); the fallback return on a non-
  // terminating handler is a default-constructed (invalid-sentinel)
  // CompiledPassId -- never a value that could be mistaken for position
  // 0 or any other real position, precisely because the sentinel is not
  // 0 (see CompiledPassId above). See Section 5/8.
  [[nodiscard]] CompiledPassId passOrder(std::size_t position) const;

  // `pass`'s diagnostic label, as a view into this CompiledGraph's own
  // owned copy of it (never borrowed from the builder or any caller-
  // supplied string at the time compile() ran -- that copy already
  // happened once, into this CompiledGraph's storage) -- may
  // legitimately be empty.
  //
  // Borrow-lifetime contract (the caller owns nothing here): the
  // returned std::string_view is valid only as long as (a) this
  // CompiledGraph instance is still alive, (b) it has not since been
  // moved FROM (as either move-construction or move-assignment source),
  // and (c) it has not since been the DESTINATION of a move-assignment
  // that replaced its content -- see this class's own header comment for
  // the full move/view contract, including the destination-of-
  // move-assignment and self-move-assignment cases. Destroying this
  // CompiledGraph has the same effect. A caller must not cache a
  // label() view across any of these boundaries; if the label is needed
  // to outlive this CompiledGraph, the caller must copy it into an owned
  // std::string of its own.
  //
  // Passing a CompiledPassId not in range for this CompiledGraph is a
  // programmer error (assertion); the fallback return on a non-
  // terminating handler is an empty std::string_view. Note this fallback
  // is not distinguishable from a legitimately empty label by its return
  // value alone -- acceptable because label() is diagnostic-only, and the
  // fallback path is only reachable with a non-terminating handler
  // installed (i.e. under test), where the test already knows an
  // assertion fired via the handler's own recording, not by inspecting
  // label()'s return. See Section 5/8.
  [[nodiscard]] std::string_view label(CompiledPassId pass) const;

  // The number of producer -> reader dependency relations in this
  // compiled graph -- a read-only representation sufficient to check
  // derived edges (Spec 0005 "Compiled graph output").
  [[nodiscard]] std::size_t dependencyCount() const noexcept;

  // The dependency edge at index `i` (< dependencyCount()). Passing an
  // out-of-range index is a programmer error (assertion); the fallback
  // return on a non-terminating handler is a CompiledDependencyEdge whose
  // `from`/`to` are both the invalid-sentinel CompiledPassId -- never
  // mistakable for a real edge, since no real edge ever has an
  // invalid-sentinel endpoint. See Section 5/8.
  [[nodiscard]] CompiledDependencyEdge dependency(std::size_t i) const;

 private:
  friend class RenderGraphBuilder;
  struct PassRecord {
    std::string label;
  };
  CompiledGraph(std::vector<PassRecord> passesInOrder, std::vector<CompiledDependencyEdge> edges);

  std::vector<PassRecord> passesInOrder_;  // index IS the compiled position
                                            // AND the CompiledPassId value
  std::vector<CompiledDependencyEdge> edges_;
};

}  // namespace atlantis::render_graph
```

**Why `CompiledGraph` exposes no resource identity or resource-level
query at all:** Spec 0005's "Compiled graph output" section fixes the
minimum content (deterministic pass order; producer-derived dependency
relations; whatever pass/resource identity is needed to interpret those)
— it does not require resources to be independently queryable. Every
Acceptance Criterion this Plan maps in Section 13 is checkable from pass
order and pass-to-pass dependency edges alone; adding a `CompiledResourceId`
concept with no required consumer would be exactly the kind of
convenience API Spec 0005 does not authorize. If a future execution-
focused spec needs compiled resource identity, it is that spec's addition
to make, against a real consumer.

**Why `CompiledPassId::index()` is a compiled-order position, not an
opaque unrelated integer:** every pass appears in the compiled order
exactly once (ADR-0018's retention invariant), so "the pass's position in
that order" and "the pass's identity" are the same information — using
one field for both avoids a redundant second identity space with its own
mapping table.

**Why move assignment is kept (not narrowed to move-construction-only):**
`Result<CompiledGraph, CompileError>`'s own return-by-value plumbing only
ever needs move *construction* — a `Result` is built once, from either
the success or the error branch, and never has an existing `CompiledGraph`
assigned into it afterward. That fact alone does not justify removing
move assignment, though: ordinary caller code that already holds a
`CompiledGraph` (e.g. re-running `compile()` each time a graph
description changes, and replacing a previously-held `CompiledGraph`
with the new one) is a normal, expected usage pattern, and forcing that
caller into destroy-then-placement-construct or an `std::optional`
wrapper just to avoid move assignment would be exactly the kind of
speculative restriction this Plan avoids elsewhere. Keeping move
assignment costs nothing beyond the view-invalidation documentation
above (already necessary for move construction) plus one additional
bullet for the destination case — it is not free-standing complexity
added without a consumer. `static_assert(std::is_move_constructible_v<CompiledGraph>)`
and `static_assert(std::is_move_assignable_v<CompiledGraph>)` are both
part of the test matrix (Section 9, `ownership_lifetime_tests.cpp`) to
lock in that both are actually available, not just declared.

---

## 5. Ownership, Provenance, and Lifetime — Implementation Strategy

This section is the concrete mechanism behind
[ADR-0017](../adr/0017-render-graph-construction-compile-layering.md)'s
ownership/handle/`CompiledGraph`-independence decision — nothing here
changes that decision, it implements it.

### Handle provenance: the builder's own address, safely, because it cannot move

`PassHandle`/`ResourceHandle` store `owner_` as `static_cast<const
void*>(this)` at the moment the builder vends them (i.e. the builder's
own address). This is exactly the "address-based provenance" strategy
ADR-0017's Context flags as a hazard *if the builder can be relocated* —
and exactly the strategy ADR-0017's Decision makes safe by making the
builder non-copyable and non-movable: an object's address never changes
across its own lifetime once it is neither copied nor moved, so comparing
`handle.owner_ == this` inside a `RenderGraphBuilder` method is a stable,
correct provenance check for as long as that specific builder instance is
alive. This directly resolves the two guaranteed-detectable cases
(Spec 0005 Error Model):

- **Default/invalid handle:** a default-constructed handle has
  `owner_ == nullptr`, which can never equal a real, constructed
  builder's `this` (never null) — `owns(handle)` returns `false`
  unconditionally for it.
- **Handle from a different, currently-live builder:** that builder's
  `this` is a different, live address — `owns(handle)` returns `false`.

**Why this does not need a global registry, a generation counter, or
handle recycling** (Spec 0005 Non-Goals): there is no removal/reuse
mechanism anywhere in this Plan's API for a handle to need recycling
from, and no cross-builder comparison ever needs a name/ID beyond "is
this address the one I am"; a bare address comparison is sufficient and
introduces no additional state.

**Why a handle used after its originating builder is destroyed is *not*
claimed to be detectable** (Spec 0005 Error Model, lifetime-precondition-
violation tier): if builder `A` is destroyed and a later builder `B`
happens to be allocated at the same address (routine with stack reuse or
a heap allocator reusing freed memory), a stale handle from `A` would
compare equal to `B`'s `this` and be misidentified as one of `B`'s own.
This Plan does **not** attempt to prevent or detect that case — doing so
would require exactly the generation-counter/registry machinery Spec 0005
explicitly excludes. This is a deliberate, spec-conformant limitation,
not an oversight: it is why Spec 0005 classifies use-after-builder-
destruction as an *undetected* lifetime precondition violation rather
than a third guaranteed-detectable case, and why Section 9's test matrix
does not include a dynamic test for it (Spec 0005 Acceptance Criteria
says as much explicitly).

```cpp
bool RenderGraphBuilder::owns(PassHandle handle) const noexcept {
  return handle.owner_ == this && handle.index_ < passes_.size();
}
bool RenderGraphBuilder::owns(ResourceHandle handle) const noexcept {
  return handle.owner_ == this && handle.index_ < resources_.size();
}
```

### `CompiledPassId`: a graph-scoped identity, not a builder-scoped one

`CompiledPassId` cannot reuse the same owner-pointer trick, because the
object it would need to tag with — `CompiledGraph` — is movable (Section
4, Section 7 item 11). A `CompiledGraph` that has been moved-from/moved-to
no longer lives at the address it was originally constructed at, so any
`CompiledPassId` tagged with that original address would silently stop
matching the very `CompiledGraph` it was vended by. Rather than solve
this with a stable-identity mechanism Spec 0005 does not authorize (a
generation counter, a heap-allocated shared control block, a global
registry), this Plan accepts the narrower contract stated in Section 4's
header comment: only a default-constructed or out-of-range
`CompiledPassId` is guaranteed-detectable; a `CompiledPassId` from a
*different* `CompiledGraph` whose index happens to be in range for the
one it is passed to is an undetected, graph-scoped identity precondition
violation — the same *category* of caller obligation as a builder handle
used after its builder is destroyed (Spec 0005's lifetime-precondition-
violation tier), for the same underlying reason (no durable identity to
check against without inventing state Spec 0005 forbids). This is judged
an acceptable, honestly-documented limitation rather than a defect to
engineer around, consistent with how Section 8 classifies it.

### The check-then-early-return pattern (why assertion tests never reach real UB)

`ATLANTIS_CHECK`'s macro expansion (`src/core/include/atlantis/assert.h`,
read directly for this Plan) calls the currently-installed
`AssertFailureHandler` and then **falls through — it does not return,
throw, or terminate control flow itself**; only the *default* handler
happens to call `std::abort()`. `tests/core/assert_tests.cpp` already
relies on exactly this to unit-test `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`
without crashing the test binary, by installing a recording (non-
terminating) replacement handler via `atlantis::assertions::setFailureHandler()`.

This means every `RenderGraphBuilder`/`CompiledGraph` method that checks
a handle's/id's validity **must not assume the check aborts** — if it
did, and a test installs a non-terminating handler to verify the
assertion fires, execution would fall through into whatever the method
does next (e.g. indexing a vector with an out-of-range or foreign-
builder-scaled index), which is real, unrelated undefined behavior the
assertion mechanism was never meant to cause. Every such method therefore
follows one fixed pattern: **compute the validity condition exactly
once, into a local `const bool`; pass that same local to `ATLANTIS_CHECK`;
then branch on it explicitly and return (a safe, documented, non-
misleading fallback for a non-`void` method — never a value that could be
mistaken for a legitimate result, per Section 4's per-method callouts) —
never re-evaluate the condition a second time.**

`void` methods (`reads()`/`writes()`):
```cpp
void RenderGraphBuilder::reads(PassHandle pass, ResourceHandle resource) {
  const bool validPass = owns(pass);
  const bool validResource = owns(resource);
  ATLANTIS_CHECK(validPass);
  ATLANTIS_CHECK(validResource);
  if (!validPass || !validResource) return;
  declareUsage(pass, resource, UsageKind::Read);
}
// writes() is symmetric, calling declareUsage(pass, resource, UsageKind::Write);
// declareUsage() itself performs the same-pass-read+write check described
// in Section 3/8 against `pass`'s already-recorded usages.
```

Non-`void` methods on `CompiledGraph` (Section 4):
```cpp
CompiledPassId CompiledGraph::passOrder(std::size_t position) const {
  const bool valid = position < passesInOrder_.size();
  ATLANTIS_CHECK(valid);
  if (!valid) return CompiledPassId{};  // invalid sentinel, never a real position
  return CompiledPassId(position);
}

std::string_view CompiledGraph::label(CompiledPassId pass) const {
  const bool valid = pass.index() < passesInOrder_.size();
  ATLANTIS_CHECK(valid);
  if (!valid) return {};  // see Section 4's callout on why this is an
                          // acceptable, if not perfectly distinguishable,
                          // fallback for a diagnostic-only accessor
  return passesInOrder_[pass.index()].label;
}

CompiledDependencyEdge CompiledGraph::dependency(std::size_t i) const {
  const bool valid = i < edges_.size();
  ATLANTIS_CHECK(valid);
  if (!valid) return CompiledDependencyEdge{};  // both endpoints invalid-sentinel
  return edges_[i];
}
```

In the default (production) configuration this pattern costs one
`bool` local and one branch and is unreachable in practice
(`std::abort()` never returns); in a unit test with a recording handler
installed, it is what makes the test observe "the assertion fired" and
return normally, with no out-of-bounds access, no dangling dereference,
and no other real UB anywhere in the call, and with a fallback value that
is never disguised as a valid one. **This is the fallback's only purpose:
keeping the failure-handler-replaced test path memory-safe. It does not
turn a programmer error into recoverable, caller-facing behavior** — a
production build never observes it (the default handler aborts first),
and this Plan does not document or test the fallback values as anything
other than "what a test sees when it deliberately replaces the failure
handler to verify the assertion fired." **Every provenance/bounds-
checking method in this module follows this exact pattern** — stated
once here rather than repeated at each call site in Sections 3–4.

### `CompiledGraph` independent ownership

`compile()`'s success path deep-copies each pass's `label` (an owned
`std::string`, never a `std::string_view` into the builder's storage)
into `CompiledGraph::PassRecord`, and builds `edges_` from plain
`CompiledPassId` values (plain `std::size_t` positions, not pointers back
into the builder) — nothing inside a `CompiledGraph` references the
`RenderGraphBuilder` it was compiled from, directly satisfying:

- The builder may be destroyed immediately after a successful `compile()`
  call; the resulting `CompiledGraph` is unaffected.
- The builder may keep accumulating declarations after producing a
  `CompiledGraph`; that `CompiledGraph` is unaffected.
- Two `compile()` calls on the same unmodified builder each produce their
  own, independent `CompiledGraph` value (two separate deep copies);
  destroying one never touches the other.

### Failed-compile builder validity

`compile()` never writes to `passes_`/`resources_` on any path — every
step in Section 6 reads that state into local, algorithm-scoped
variables. A failed `compile()` call therefore leaves the builder exactly
as it was; the same builder may be inspected (there is no public API to
inspect it beyond re-declaring or re-compiling — Spec 0005 does not
require one) and compiled again, unmodified, to observe the same
`CompileError` deterministically (Section 6).

---

## 6. Dependency Derivation and Compilation Algorithm

`compile() const` delegates to a private, free function operating on
plain data — directly unit-testable without constructing a full
`RenderGraphBuilder` (mirrors `plans/0003`'s
`detail::decideRecreateAction()` precedent):

```cpp
namespace atlantis::render_graph::detail {

enum class UsageKind { Read, Write };
struct RawUsage { std::size_t resourceIndex; UsageKind kind; };
struct RawPass { std::string label; std::vector<RawUsage> usages; };

[[nodiscard]] atlantis::Result<CompiledGraphData, CompileError>
compile(const std::vector<RawPass>& passes, std::size_t resourceCount);

}  // namespace atlantis::render_graph::detail
```

(`CompiledGraphData` is a plain, non-owning-of-a-builder intermediate —
the two parallel vectors `CompiledGraph::compile()` uses to construct the
public `CompiledGraph` value; not itself a public type.)

### Step-by-step

1. **Verify at most one producer per resource.** Iterate resource index
   `r` in **ascending order** (== ascending `declarationIndex`, since a
   resource's index is its declarationIndex — Section 3 item 7). For each
   `r`, scan every pass's usages in ascending `declarationIndex` (pass
   position) order; collect the *distinct* pass indices that declare a
   `Write` usage against `r` (a `std::vector<std::size_t>`, pushed in
   ascending order and only if not already present — a linear
   `O(passes)` scan per resource, not a set, since duplicate writes by
   the same pass must collapse to one entry, not increment a count). If
   more than one distinct pass is found for this `r`, **stop the entire
   check immediately** — do not continue scanning later resources — and
   return
   `Err(MultipleProducersError{.resource = ResourceDiagnostic{r's
   declarationIndex, r's label}, .producers = producers})`, where
   `producers` lists those distinct producer passes (`declarationIndex` +
   owned `label` copy each), in ascending `declarationIndex` order. If no
   resource has more than one distinct producer, proceed to step 2 having
   scanned every resource. **This check runs to completion (or fails)
   before any edge is derived or any ordering is attempted** — a
   multiple-producer graph never reaches cycle detection; see "Error
   selection priority and determinism" below for why this ordering is
   itself a fixed, tested contract, not an implementation accident.
2. **Derive producer → reader edges.** With every resource now known to
   have zero or one producer, scan every pass's `Read` usages; for each
   read of resource `r` whose producer (from step 1) is pass `p` (and
   `p` is not the reading pass itself — structurally impossible anyway,
   since same-pass read+write is rejected at declaration time, but the
   check costs nothing and documents the invariant), record the ordered
   pair `(p, reader)`.
3. **De-duplicate edges.** Insert every pair from step 2 into a
   `std::set<std::pair<std::size_t, std::size_t>>` — an ordered
   (red-black-tree) container, compared by value, **not**
   `std::unordered_set`/`std::unordered_map` — so the resulting, deduped
   edge sequence is already a fully deterministic, ascending-by-`(from,
   to)` order with no dependency on hash-bucket layout, insertion order,
   or pointer values. Copy it into a `std::vector<std::pair<std::size_t,
   std::size_t>>` (call it `edges`) for the remaining steps.
4. **Deterministic topological order (Kahn's algorithm, declaration-order
   tie-break).** Compute `inDegree[p]` for every pass from `edges`.
   Repeat exactly `passes.size()` times: linearly scan passes in
   ascending index order (ascending index **is** ascending
   `declarationIndex`) for the first not-yet-output pass with
   `inDegree == 0`; if none exists, stop the loop early (step 6 below
   handles this). Output it (append to `compiledOrder`), mark it output,
   and decrement `inDegree` for every pass it has an edge to (iterating
   `edges` in its already-`(from, to)`-sorted order, so each pass's
   out-edges are visited in ascending target-index order too — no
   separate adjacency-list sort is needed). **This scan-for-smallest-
   ready-index approach is deliberately `O(passes²)` instead of a
   priority-queue-based `O(passes·log passes)`** — at Phase 1 scale
   (a frame's pass count), the constant-factor simplicity of "always scan
   ascending from zero" is judged to outweigh the asymptotic cost, and it
   sidesteps a `std::priority_queue`'s own comparator/tie-break subtleties
   entirely, in favor of a one-line "ascending index, first hit wins" rule
   that is obviously deterministic by inspection.
5. **Success.** If `compiledOrder.size() == passes.size()`, every
   declared pass was output — build the returned `CompiledGraphData` from
   `compiledOrder` (this *is* the compiled position order) and `edges`
   (translated to `CompiledPassId` pairs via `compiledOrder`'s
   positions), copying each pass's `label` into owned storage. Return
   `Ok(...)`.
6. **Cycle witness (only reached if step 4 stopped early) — three-color
   DFS over the remaining subgraph, not "every not-output pass is in a
   cycle."** An earlier draft of this algorithm assumed the passes step 4
   fails to output are exactly the passes forming cycles. **That
   assumption is false**: a pass that is not itself part of any cycle but
   merely *depends* (directly or transitively) on a pass that is part of
   one also never reaches `inDegree == 0`, and so is also left un-output
   by step 4 — the "remaining" set can be strictly larger than the actual
   cycle. Extracting a correct, deterministic witness therefore requires
   an explicit search over the remaining subgraph, not treating that
   whole set as the witness:
   - Every remaining (not-output) pass starts `Unvisited`. Iterate the
     remaining passes in ascending `declarationIndex` order; for each
     still-`Unvisited` one, start a DFS from it (a pass already marked
     `Finished` by an earlier root's DFS is skipped, never re-explored).
   - DFS pushes the current pass onto an explicit path stack and marks it
     `Visiting`, then visits its outgoing edges — restricted to edges
     whose target is also in the remaining subgraph — in ascending
     target-`declarationIndex` order (the same order `edges` is already
     sorted in from step 3):
     - An edge to an `Unvisited` pass: recurse into it.
     - An edge to a `Visiting` pass (a **back edge**): a cycle is found.
       The cycle is exactly the path-stack suffix from that `Visiting`
       pass's position to the current top, closed by this edge back to
       it — **not** the entire remaining subgraph, and **not** any pass
       below that suffix on the stack (those are upstream of the cycle,
       not part of it).
     - An edge to a `Finished` pass: skip it — already fully explored, it
       cannot lead to a new cycle from here.
   - When a pass's outgoing edges are all visited with no back edge
     found, mark it `Finished` and pop it from the path stack.
   - The **first** back edge encountered, in this fixed root-selection
     and edge-visitation order, determines the reported cycle. This is
     deterministic — not because any one cycle is "the" cycle when more
     than one exists, but because the traversal order that finds one is
     itself entirely fixed by `declarationIndex` (root order, and each
     pass's own edge order), never by container iteration order, pointer
     values, or thread-scheduling.
   - **Canonical rotation.** Once the cycle's pass sequence is known (in
     the order the DFS stack held them, e.g. `[C, A, B]` closing back to
     `C`), rotate it to begin at its own smallest-`declarationIndex`
     member — e.g. `[A, B, C]` if `A` has the smallest index among
     `{A, B, C}` — while preserving the edge-following direction (never
     reversed). This makes the reported witness independent of which
     pass the DFS happened to be visiting when it found the back edge,
     which would otherwise vary with irrelevant details of traversal
     order rather than the cycle's own identity. An implicit correctness
     property of this construction: every adjacent pair in the final
     witness (and the wrap-around pair from the last entry back to the
     first) corresponds to a real derived edge from `edges`, by
     construction of the DFS path.
   - Implemented with an **explicit stack, not recursion** — Phase 1
     frame-scale graphs make either choice safe from a call-stack-depth
     standpoint, but an explicit stack keeps every piece of traversal
     state (the path, the visited-color map) inspectable as ordinary data
     rather than implicit call frames, matching this algorithm's general
     preference for auditable, explicit state (Section 6's "Complexity"
     subsection).
   - Return `Err(DependencyCycleError{.passes = witness})` — there is no
     `.resource` field to set at all (unlike `MultipleProducersError`),
     since no single resource identifies a cycle — `witness` built from
     the rotated sequence (each entry: `declarationIndex` + owned `label`
     copy).
7. **The builder is never mutated.** `compile()` is `const`
   (compiler-enforced); every step above reads `passes_`/`resources_`
   (via the caller-supplied `RawPass` vector) into local algorithm state
   and writes only to that local state.
8. **No partial `CompiledGraph` is ever constructed.** The `CompiledGraph`
   constructor is only ever invoked from step 5's single success path;
   every other path (step 1's early return, step 6's cycle case) returns
   `Err(...)` before that constructor is reachable.

### Error selection priority and determinism

A single graph description can, in principle, exhibit both a
multiple-producer conflict and a dependency cycle at once (among
unrelated resources/passes). This algorithm fixes, deterministically,
which error is reported when that happens — a choice Spec 0005 leaves to
the Plan, and one this Plan does not leave to whichever check an
implementation happens to run first:

- **Multiple-producer validation (step 1) always runs, and always fails
  first, before cycle detection (steps 2–6) ever starts.** Step 1 is a
  hard gate: if it finds a violation, `compile()` returns
  `MultipleProducersError` immediately, and steps 2–6 (edge derivation,
  topological sort, DFS-based cycle extraction) never execute at all —
  not "run but discarded," literally not reached. A graph containing both
  problems is therefore **always** reported as a `MultipleProducersError`,
  never a `DependencyCycleError`, regardless of which passes/resources
  are involved in each.
- **When more than one resource independently has multiple producers,
  the one with the smallest `declarationIndex` is reported.** Step 1
  iterates resource indices in ascending order and stops at the *first*
  one it finds with more than one distinct producer — it does not scan
  every resource and then pick the "best" violation afterward. This is a
  direct consequence of the iteration order stated in step 1, not a
  separate tie-break rule bolted on afterward.
- **The `producers` list inside a reported `MultipleProducersError` is
  always sorted ascending by `declarationIndex`** — stated already in
  step 1, restated here because it is part of the same determinism
  contract: which resource is reported, and in what order its producers
  are listed, are both fixed, index-only choices.

None of this depends on `std::unordered_map`/`std::unordered_set`
iteration, pointer/address values, or thread-scheduling — exactly the
same determinism discipline the "Complexity" subsection below states for
the rest of this algorithm. A consequence worth stating plainly: **repeated
`compile()` calls on the same unmodified, multi-problem graph description
report the identical `MultipleProducersError` (same resource, same
producer list) every time** — this is not a new guarantee invented for
this case, it is ADR-0018's existing determinism requirement applied to
the priority rule above.

### Complexity and why no unordered/hash/pointer-order container is used

Steps 1–2 are `O(passes · usages-per-pass)`; step 3 is
`O(edges · log edges)` (a `std::set` insert, tree-ordered, not hashed);
step 4 is `O(passes² + edges)`; step 6 (only on failure) is
`O(remaining passes + remaining edges)` — a standard DFS over the
remaining subgraph, run at most once per `compile()` call (only reached
after step 4 has already failed). Every ordering decision this algorithm
makes — which pass to output next, which edge to follow, which resource's
witness to report, which DFS root to start from, which back edge
determines the reported cycle — is driven by `declarationIndex` (a plain,
stable, ascending integer assigned once at declaration time) or by
`std::set`'s value-ordering, **never** by `std::unordered_map`/
`std::unordered_set` iteration, a raw pointer/address comparison used for
*ordering* (as opposed to provenance *identity*, Section 5, which
legitimately does compare addresses but never orders by them), or any
thread-scheduling/wall-clock effect. This is what makes ADR-0018's
determinism guarantee (repeated `compile()` on the same input yields an
identical result, including an identical `CompileError` witness) a
structural property of the implementation, not an incidental one.

### What this algorithm deliberately does not do

No explicit dependency edge is accepted or consulted anywhere in this
algorithm — `edges` in step 2 is derived from resource usage alone. No
pass is ever omitted from `compiledOrder` on success — step 5's condition
is exactly "every declared pass was output." A reported cycle witness
never includes a pass that is merely downstream of a cycle without being
part of it (step 6's DFS-based extraction, not "the whole remaining
set"). No resource lifetime, physical binding, or RHI/Vulkan call is
computed, allocated, or invoked anywhere in this algorithm.

---

## 7. Plan-Stage Details Closed by This Round

Per Spec 0005's own framing ("concrete C++ type/method names and exact
signatures are left to the Plan"), this Plan closes the following
implementation-shape decisions. **None of them changes the ownership,
dependency-derivation, threading, or error-classification model Spec
0005/ADR-0017/ADR-0018 already fix** — each disposition below states why,
so a reviewer can check that claim rather than take it on faith. Per
Section titled "Scope banner" above, if any of these had required
changing that model, it would be listed as a **Human Review blocker**
instead; **none does**, so the blocker list at the end of this section is
empty.

| # | Plan-stage detail | Disposition | Rationale |
|---|---|---|---|
| 1 | Diagnostic label type for passes/resources | `std::string_view` parameter (default `{}`), copied into an owned `std::string` immediately at declaration time (Section 3) | Matches Spec 0005's "the builder owns (copies) whatever label data it needs; it never borrows a caller-supplied temporary string." No new type introduced. |
| 2 | Resource label required or optional | Optional, defaulting to an empty label, under the exact same rule as pass labels | Spec 0005 states resource labels are optional "under the same rules" as pass labels; forcing a required label on every resource adds caller friction with no correctness value, since labels never participate in identity/ordering. |
| 3 | Duplicate **read** declaration (same pass, same resource, called twice) | Legal, idempotent no-op with respect to compiled output — the duplicate usage record is harmless because edge derivation (Section 6, step 2–3) naturally de-duplicates the resulting producer→reader pair | Not named as an error anywhere in Spec 0005's Error Model; treating it as a silent no-op avoids inventing a new error category the Spec never asked for. |
| 4 | Duplicate **write** declaration (same pass, same resource, called twice) | Legal — collapses to that one pass being counted as a single distinct producer (Section 6, step 1's "distinct pass indices" collection); does not trigger `MultipleProducers` | Spec 0005's multiple-producer rule is about more than one *pass* producing a resource, not about a single pass's own usage-declaration count; a pass writing its own declared resource twice is still exactly one producer. |
| 5 | Whether a declared-but-unused resource appears in `CompiledGraph`'s query surface | It does not — `CompiledGraph` exposes no resource-level query at all (Section 4) | Spec 0005 leaves this "an implementation-shape detail left to the Plan"; not exposing resources at all is the simplest choice that still satisfies every Acceptance Criterion (Section 13), and avoids inventing a resource-identity concept with no required consumer. |
| 6 | How a producer-less resource is represented | No separate representation — a `ResourceRecord` with zero recorded `Write` usages across all passes *is* a producer-less resource; nothing marks it specially at declaration time (Section 3) | Matches Spec 0005's "whether it ends up with a producer or not is an emergent property of how it is later used, not a property fixed at creation time" — there is no "producer-less" flag to set. |
| 7 | Pass/resource identity and declaration-order representation | A pass's/resource's position (`std::size_t` index) in `passes_`/`resources_` *is* both its `PassHandle`/`ResourceHandle` index and its declaration order (Section 3) | One field serves both purposes because they are the same information by construction (purely additive, no removal); introducing a second field would be redundant state to keep in sync. |
| 8 | Builder provenance implementation | The builder's own `this` pointer, safe because the builder is non-copyable/non-movable (Section 5) | Directly implements ADR-0017's Decision and Alternatives Considered — see Section 5 for the full reasoning, including why a global registry/generation counter is not needed. |
| 9 | Deterministic cycle-witness selection algorithm | Three-color DFS over exactly the remaining (not-output) subgraph, smallest-`declarationIndex`-first root and edge order, canonical rotation to the cycle's own smallest-`declarationIndex` member (Section 6, step 6) | A concrete, auditable, index-only rule with no unordered/hash/pointer dependency, and — unlike an earlier draft — one that does not conflate "left over by Kahn's algorithm" with "part of a cycle." See Section 6's "Complexity" subsection. |
| 10 | Error payload representation | `using CompileError = std::variant<MultipleProducersError, DependencyCycleError>` — `MultipleProducersError{resource, producers}` (`resource` a plain `ResourceDiagnostic`, always populated; `producers` a `std::vector<PassDiagnostic>`, always ≥2 entries), `DependencyCycleError{passes}` (no resource field at all) (Section 2) | Corrects this Plan's own prior `CompileErrorKind` + `{kind, optional<ResourceDiagnostic>, vector<PassDiagnostic>}` shape, which allowed representing internally-inconsistent combinations (e.g. `DependencyCycle` with `resource` populated); the variant makes every illegal combination a compile error instead of a runtime possibility. Not a Spec/ADR change — see Section 2's rationale. |
| 11 | `CompiledGraph` copy/move strategy | Move-only (`= default` move, `= delete` copy) | See Section 4's callout — no current consumer needs to duplicate a `CompiledGraph`; move-only avoids an unnecessary deep-copy path while still satisfying "at minimum movable" (Spec 0005). Left open in Spec 0005's own Risks & Open Questions as a Plan-level choice, not a Human Review item — see that section's explicit note. |
| 12 | Empty graph return shape | `Ok(CompiledGraph)` with `passCount() == 0` and `dependencyCount() == 0` — no special-cased error or sentinel | Spec 0005 Error Model: "An empty graph … compiling successfully to an empty compiled result … is not a defect." Falls out of the algorithm (Section 6) with no special-casing: step 4's loop runs zero times, step 5's condition `0 == 0` holds. |
| 13 | Assertion testing without triggering real UB | The check-then-early-return pattern, condition computed exactly once (Section 5) | Grounded directly in `src/core/include/atlantis/assert.h`'s actual, non-`[[noreturn]]` `ATLANTIS_CHECK` behavior and `tests/core/assert_tests.cpp`'s existing replacement-handler pattern — not a new testing mechanism. |
| 14 | Pass/resource usage declaration API shape | Direct `RenderGraphBuilder::reads(PassHandle, ResourceHandle)`/`writes(PassHandle, ResourceHandle)` methods — no intermediate proxy/builder-scoped helper type (Section 3) | An earlier revision's `PassBuilder` proxy made a default/foreign `PassHandle` untestable through the public API (Section 3's full rationale); the direct signatures close that gap with less API surface, not more. |
| 15 | `CompiledPassId` invalid-sentinel value | `static_cast<std::size_t>(-1)`, matching `PassHandle`/`ResourceHandle`'s existing sentinel convention (Section 4) — never `0`, since `0` is a legitimate compiled-order position | Using `0` as the "invalid" value would make a fallback return indistinguishable from a real result at position 0; the sentinel must be a value no real position ever takes. |
| 16 | `CompiledPassId` cross-`CompiledGraph` misuse detectability | Not claimed to be detectable when the index happens to be in range for the `CompiledGraph` it is (mis)used against — an accepted, documented limitation, not a defect (Section 5) | `CompiledGraph` is movable (item 11), so it has no stable address to tag identity with the way the non-movable builder does (Section 5); manufacturing one would require exactly the generation-counter/registry machinery Spec 0005 forbids. |
| 17 | Assertion-fallback return values for `CompiledGraph`'s non-`void` accessors | `passOrder()` → invalid-sentinel `CompiledPassId`; `label()` → empty `std::string_view`; `dependency()` → a `CompiledDependencyEdge` with both endpoints invalid-sentinel (Section 4/5) | Each fallback is either provably distinguishable from every legitimate result (the two `CompiledPassId`-based ones, given item 15's sentinel choice) or explicitly documented as not perfectly distinguishable but harmless for a diagnostic-only accessor (`label()`) — never silently presented as equivalent to a real result. |
| 18 | Error selection priority when a graph exhibits both a multiple-producer conflict and a dependency cycle at once | Multiple-producer validation (step 1) always runs to completion/failure before cycle detection ever starts; such a graph is always reported as `MultipleProducersError`, never `DependencyCycleError` (Section 6, "Error selection priority and determinism") | Spec 0005 does not fix which error wins when both are present; leaving it undefined would make `compile()`'s result depend on implementation-internal ordering rather than a stated, testable contract — this Plan closes that gap with a single fixed rule (Section 9 tests it). |
| 19 | `CompiledGraph` copy/move: keep or drop move assignment | Both move construction and move assignment are kept (`= default` for each); the full view-invalidation contract is documented for both the move-FROM and move-assignment-destination cases, plus the cross-instance and self-move-assignment cases (Section 4) | `Result<CompiledGraph, CompileError>`'s own plumbing only needs move construction, but ordinary caller code re-running `compile()` and replacing a previously-held `CompiledGraph` is a normal usage pattern this Plan does not want to force into an `std::optional` wrapper just to avoid move assignment — see Section 4's "Why move assignment is kept." |

**Human Review blockers surfaced by drafting this Plan: none.** Every
item above is a Plan-stage implementation-shape decision with no public
ownership/dependency/threading/error-classification weight beyond what
Spec 0005 and ADR-0017/ADR-0018 already fix, per the bar
`AGENTS.md`'s Golden Rule sets for requiring a human/ADR decision instead
of a Plan-level judgment call.

---

## 8. Error Model Implementation

Directly implementing Spec 0005's four-tier Error Model, no reopening:

- **Compile-time type error:** `PassHandle`/`ResourceHandle`/
  `CompiledPassId` are distinct C++ types with no conversion between any
  pair (Sections 2, 4) — a misuse fails to compile; there is nothing for
  this Plan's runtime code to check.
- **Guaranteed-detectable runtime programmer error (`ATLANTIS_CHECK`):**
  every `RenderGraphBuilder`/`CompiledGraph` method that accepts a handle
  or an index calls `owns(...)`/an equivalent bounds check via the
  check-then-early-return pattern (Section 5) before using it. This
  covers: default/invalid `PassHandle`/`ResourceHandle`, a handle from a
  different currently-live builder, a pass's own read-then-write or
  write-then-read of the same resource (checked against that pass's own
  accumulated `usages`, inside `declareUsage()`, called from both
  `reads()` and `writes()`), and `CompiledGraph::passOrder()`/`label()`/
  `dependency()` receiving an out-of-range position/id/index — each with
  the specific, non-misleading fallback return value Section 4/5 states.
- **Lifetime/identity precondition violation (not guaranteed-detectable,
  not tested):** two cases, both accepted as documented limitations
  rather than defects, for the same underlying reason (no stable address
  to tag identity with, without inventing state Spec 0005 forbids):
  - Using a `PassHandle`/`ResourceHandle` after its originating builder is
    destroyed — see Section 5's explanation of exactly why this Plan does
    not attempt detection.
  - Using a `CompiledPassId` against a `CompiledGraph` other than the one
    that vended it, when its index happens to be in range for that other
    `CompiledGraph` — see Section 5's "`CompiledPassId`: a graph-scoped
    identity, not a builder-scoped one" for why `CompiledGraph`'s
    movability rules out the builder's own address-based mechanism.

  Section 9 states explicitly that no dynamic test exercises either case.
- **Recoverable compile error (`atlantis::Result`):** `CompileError`, the
  `std::variant<MultipleProducersError, DependencyCycleError>` (Section
  2), returned from `compile()`/`detail::compile()` (Section 6) — never
  an exception, matching `atlantis::Result`'s existing use in Core/RHI. A
  caller inspects which alternative is active via `std::holds_alternative`/
  `std::get_if` (Section 2's usage example); there is no `kind` field to
  switch on separately from the payload, and no representable combination
  where the active alternative's own invariants (a `MultipleProducersError`
  with a resource and ≥2 producers; a `DependencyCycleError` with no
  resource field at all) do not hold — the type system enforces this,
  not a runtime check.

No new assertion macro, no new `Result`-like type, and no third error
mechanism is introduced anywhere — `ATLANTIS_CHECK` (ADR-0009) and
`atlantis::Result` (Spec 0001) are reused exactly as they already exist.

---

## 9. Testing Strategy

Every test below is GPU-independent, runs with no Vulkan device and no
window, and belongs to the single `atlantis_render_graph_tests` Catch2 v3
executable — no CTest `gpu` label is introduced or needed anywhere in
this module's own test registration (Spec 0005 has no GPU-touching scope
at all). Run via the already-documented GPU-independent command:

```
ctest --test-dir <build> -C Debug -LE gpu --output-on-failure
ctest --test-dir <build> -C Release -LE gpu --output-on-failure
```

**Two test layers, deliberately kept separate:** `compile_algorithm_tests.cpp`
exercises `detail::compile()` directly (white-box, plain `RawPass` data,
no `RenderGraphBuilder` involved) — but it is not the algorithm's only
consumer: `RenderGraphBuilder::compile()` (production code, Section 6/12
step 7) calls the same function. Every other file below exercises the
full public `RenderGraphBuilder`/`CompiledGraph` API exclusively
(black-box), so the same core behaviors are verified both in isolation
and through the real, shipped API surface.

| Test file | Covers |
|---|---|
| `handle_ownership_tests.cpp` | `RenderGraphBuilder` is not copy-constructible/not move-constructible (`std::is_copy_constructible_v`/`std::is_move_constructible_v` `static_assert`s — compile-time, not a `TEST_CASE`); `PassHandle`/`ResourceHandle`/`CompiledPassId` are pairwise distinct types with no implicit conversion (compile-time, via `static_assert(!std::is_convertible_v<…>)` or simply the absence of a conversion path — no `TEST_CASE` needed for a property the compiler already enforces); a default-constructed `PassHandle`/`ResourceHandle` passed to `reads()`/`writes()` triggers the assertion policy (replacement `AssertFailureHandler` installed, per Section 5); a handle vended by one `RenderGraphBuilder` used on a second, concurrently-alive `RenderGraphBuilder` likewise triggers the assertion policy — all exercised directly through the public API, since `PassHandle`/`ResourceHandle` are real, constructible values (Section 3's rationale for removing the `PassBuilder` proxy) |
| `compile_algorithm_tests.cpp` | The same core algorithm behaviors as the two rows below, but exercised directly against hand-built `detail::RawPass` fixtures: producer/reader edge derivation, RAR no-edge, multiple-producer rejection, cycle detection and witness extraction (including the downstream-pass and multiple-cycle cases below) — white-box coverage that is fast to write additional edge cases against without needing a full `RenderGraphBuilder` |
| `dependency_derivation_tests.cpp` | One producer, one reader (edge + order correct); one producer, multiple readers (fan-out, no reader↔reader edge); multiple independent producer/reader groups (declaration-order-driven relative order); multiple readers of the same resource produce no edge between them; more than one producer for the same resource is rejected as `MultipleProducersError` (checked via `std::get_if<MultipleProducersError>`) with `.resource` correctly identifying the offending resource's own `declarationIndex`/label and `.producers` listing exactly its distinct producers, sorted ascending by `declarationIndex`, unconditionally, including the same-pass-double-write case (Section 7 item 4) correctly *not* triggering it; a pass declaring both a read and a write of the same resource triggers the assertion policy; a producer-less resource is legal and readable; a declared-but-unused resource (no producer, no reader) is legal and produces no dependency relation; **two independent resources each with more than one producer report a `MultipleProducersError` naming only the resource with the smaller `declarationIndex`**, deterministically across repeated compiles — through the public `RenderGraphBuilder` API |
| `cycle_detection_tests.cpp` | A two-pass, two-resource cycle formed entirely from producer-derived edges (pass A writes X/reads Y, pass B writes Y/reads X) is reported as `DependencyCycleError` (checked via `std::get_if<DependencyCycleError>`; note the type itself has no `.resource` field to assert `== std::nullopt` on) with `.passes` identifying both passes and no others; a longer (three-or-more-pass) producer-derived cycle is likewise detected; **a graph containing a cycle plus a separate downstream pass that only reads a cycle member's output (not part of the cycle itself, and declared with a *smaller* `declarationIndex` than any cycle member) reports a witness containing only the actual cycle's passes, never the downstream one**; a graph with more than one independent cycle reports the same one, deterministically, across repeated compiles; repeated compilation of the same cyclic graph reports an equivalent witness every time; a cycle whose participating passes carry duplicate diagnostic labels still produces a deterministic, unambiguously identifiable witness (checked via `declarationIndex`, not label text); every adjacent pair in a reported witness (including the wrap-around pair) corresponds to an actual derived edge; **a graph containing both a dependency cycle and an unrelated multiple-producer conflict reports `MultipleProducersError`, never `DependencyCycleError`** (Section 6, "Error selection priority and determinism") — through the public `RenderGraphBuilder`/`CompiledGraph` API |
| `pass_retention_and_ordering_tests.cpp` | An empty graph compiles to `passCount() == 0`; a single pass compiles successfully; an isolated pass (no usage relationship to anything) is retained and participates in the declaration-order tie-break; a producer pass whose resource is never read is retained (no dead-pass culling); every successfully declared pass appears in the compiled order exactly once, across every graph shape exercised by this file and the two files above; declaration order determines compiled order only among otherwise-unordered passes (a case that would fail if declaration order instead influenced edge direction or producer legality) — through the public API |
| `ownership_lifetime_tests.cpp` | Repeated compilation of an unmodified graph description yields an identical result (same order, same dependency relations) every time; a failed `compile()` never returns a partial `CompiledGraph` and never invalidates the builder — the same builder compiles again, unmodified, to observe the same failure deterministically; after a successful `compile()`, destroying the builder leaves the resulting `CompiledGraph` fully and correctly queryable; after a successful `compile()`, continuing to add declarations to the (still-alive) builder does not change the already-produced `CompiledGraph`; two `compile()` calls on the same unmodified builder produce independent, equivalent `CompiledGraph` values, and destroying one does not affect the other; duplicate diagnostic labels on two different passes/resources are legal and do not affect identity, dependency relations, or order; `CompiledGraph` has no public mutation API (verifiable by inspection of Section 4's header — no non-`const` public method exists); `passOrder()`/`label()`/`dependency()` called with a default-constructed or out-of-range `CompiledPassId`/index trigger the assertion policy and return the documented, non-misleading fallback (Section 5) |

**Explicitly not tested, per Spec 0005's own Acceptance Criteria and
Section 5/8's design:**
- Using a `PassHandle`/`ResourceHandle` after the `RenderGraphBuilder`
  that vended it has been destroyed.
- Using a `CompiledPassId` against a `CompiledGraph` other than the one
  that vended it, in the case where its index happens to be in range for
  that other graph.

Both are lifetime/identity precondition violations this Plan does not
claim to detect (Section 5/8); a dynamic test exercising either would be
exercising undefined behavior, which this Plan does not write or sanction.

**Compile-time property verification:** non-copyable/non-movable and
handle/id-type-distinctness are verified via `static_assert` at the top
of `handle_ownership_tests.cpp` (or an equivalent header), which fails
the *build* if violated — a stronger guarantee than a runtime `TEST_CASE`
could offer for a property that is supposed to hold unconditionally at
compile time, and consistent with Spec 0005's own framing of these as
"compile-time properties."

**On CTest's `gpu` label, precisely:** this module's `atlantis_render_graph_tests`
executable registers no test with the CTest `gpu` label (Section 11's
CMake snippet has no `PROPERTIES LABELS "gpu"` call anywhere), so every
case in the table above is included in the existing GPU-independent
`ctest -LE gpu` command shown at the top of this section. **This Plan
does not claim, and it would be false to claim, that `ctest -L gpu`
returns zero tests repository-wide** — `atlantis_vulkan_backend_gpu_tests`
(from Spec/Plan 0003, already merged) is real, existing, unrelated
GPU-required test infrastructure this Plan does not touch, add to, or
remove from. The verification claim this Plan actually makes is narrower
and checkable two ways: (a) inspect `tests/render_graph/CMakeLists.txt`
(Section 11) and confirm no `catch_discover_tests(...)` call there passes
`PROPERTIES LABELS "gpu"`; or (b) compare `ctest --test-dir <build> -N -L
gpu` (list-only, GPU-required tests) before and after this Plan's
implementation — the count must be identical, since this Plan registers
none. Running the pre-existing `atlantis_vulkan_backend_gpu_tests` suite
is not required to verify anything in this Plan's own scope (it exercises
no RenderGraph code at all); whether it continues to run as part of this
repository's overall Definition of Done is governed by
`docs/process/testing-strategy.md` and Plan 0003's own Verification
Checklist, unchanged by this Plan.

No GPU test, no Vulkan SDK dependency, and no rendered output exists
anywhere in `atlantis_render_graph_tests` or its CMake registration.

---

## 10. Explicit Prohibitions (verification-checkable)

None of the following may appear anywhere in `src/render_graph/` or
`tests/render_graph/` introduced by this Plan — each is grep- or
inspection-checkable at review time:

| Prohibited | Check |
|---|---|
| Any `Vk*` type or `#include <vulkan/...>` | grep across `src/render_graph/include` and `src/render_graph/src` |
| Any Atlantis Platform type (`NativeWindowHandle`, `PlatformEvent`, …) | grep for `atlantis/platform` includes |
| Any RHI type (`Device`, `Presentation`, `Extent2D`, …) | grep for `atlantis/rhi` includes |
| A caller-authored dependency edge (`dependsOn`, `before`, `after`, priority/order override, integer sort key) | grep for those identifiers (case-insensitive) across new public/private headers |
| Any pass-culling logic (dead-pass/unreferenced-pass/output-root removal) | code review of `compile_algorithm.cpp` confirms `compiledOrder.size()` on success always equals the input pass count |
| Any lifetime interval, imported/transient classification, or resource physical property field | grep for `lifetime`, `imported`, `transient`, `format`, `usage.*flag`, `memory` (case-insensitive) across `handles.h`/`compiled_graph.h`/`render_graph_builder.h` |
| A generation counter, handle-recycling field, or global handle registry | grep for `generation`, `recycle`, and any `static`/global mutable container across `src/render_graph/` |
| A mutex, atomic, or lock-free structure | grep for `std::mutex`, `std::atomic`, `std::lock_guard` across `src/render_graph/` |
| A `gpu`-labeled test registration | grep for `LABELS` in `tests/render_graph/CMakeLists.txt` — no match expected |
| `ATLANTIS_CHECK`/`ATLANTIS_ASSERT` misuse that skips the early-return pattern, or re-evaluates its condition instead of reusing a single local `bool` | code review of every provenance/bounds check against Section 5's pattern |

---

## 11. Build Integration

```cmake
# root CMakeLists.txt (candidate diff) -- lands in Implementation Order Step 1
add_subdirectory(src/rhi)
add_subdirectory(src/vulkan_backend)
add_subdirectory(src/render_graph)      # new

...

add_subdirectory(tests/rhi)
add_subdirectory(tests/vulkan_backend)
add_subdirectory(tests/render_graph)    # new (inside the existing ATLANTIS_BUILD_TESTS block)
```

```cmake
# tests/render_graph/CMakeLists.txt (candidate)
add_executable(atlantis_render_graph_tests
  handle_ownership_tests.cpp
  compile_algorithm_tests.cpp
  dependency_derivation_tests.cpp
  cycle_detection_tests.cpp
  pass_retention_and_ordering_tests.cpp
  ownership_lifetime_tests.cpp
)

# PRIVATE to this test target only -- needed solely so
# compile_algorithm_tests.cpp can #include the private
# src/render_graph/src/compile_algorithm.h header (Section 9's white-box
# layer). Never added to atlantis_render_graph's own PUBLIC include
# interface (Section 1) -- library consumers never see this path.
target_include_directories(atlantis_render_graph_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/render_graph/src
)

target_link_libraries(atlantis_render_graph_tests
  PRIVATE
    Atlantis::RenderGraph
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# No PROPERTIES LABELS "gpu" anywhere in this file -- see Section 9's
# "On CTest's gpu label, precisely" note.
catch_discover_tests(atlantis_render_graph_tests DISCOVERY_MODE PRE_TEST)
```

No `find_package` addition — RenderGraph introduces no new external
dependency (Section 1).

---

## 12. Implementation Order

Each step ends with a build-and-test action. None of these steps has
been executed by this Plan document itself; they describe the
Implementation phase a completed joint Spec + Plan Human Review would
authorize, not steps already taken. This Plan has exactly **10** steps —
no step beyond the last one below exists.

1. **Module/CMake skeleton** (Sections 1, 11): `src/render_graph/CMakeLists.txt`;
   root `CMakeLists.txt`'s two `add_subdirectory` additions
   (`src/render_graph`, and `tests/render_graph` inside the
   `ATLANTIS_BUILD_TESTS` block) — **both land here, not deferred to the
   Documentation step**; `tests/render_graph/CMakeLists.txt` created with
   its `add_executable(atlantis_render_graph_tests ...)` call initially
   listing only `handle_ownership_tests.cpp` (the only file this step's
   API surface can satisfy — later steps append their own file to this
   same call as each is written) and its `PRIVATE` include-path addition
   for `src/render_graph/src` (Section 11) added now, even though
   `compile_algorithm_tests.cpp` itself does not exist until step 6;
   empty `include/atlantis/render_graph/` headers with forward
   declarations only. `atlantis_render_graph` target configures and
   builds (Debug + Release), linking only `Atlantis::Core`.
2. **`handles.h`** (Section 2) + `handle_ownership_tests.cpp`'s
   compile-time `static_assert`s: build + `ctest -LE gpu` run (trivial
   pass — no runtime behavior yet beyond default construction/equality).
3. **`compile_error.h`** (Section 2): build only (pure data types).
4. **`compiled_graph.h`/`.cpp`** (Section 4), including the
   `passOrder()`/`label()`/`dependency()` assertion-fallback bodies
   (Section 5), without yet being constructible from real compiled data
   (private constructor, no caller yet): build only.
5. **`render_graph_builder.h`**'s declaration surface (Section 3) —
   `declareResource()`, `declarePass()`, `reads()`, `writes()`, the
   `owns()` checks and the check-then-early-return pattern (Section 5),
   but `compile()` not yet implemented (declared, not defined, so nothing
   links yet): build; `handle_ownership_tests.cpp`'s runtime assertion-
   policy cases (default/foreign `PassHandle`/`ResourceHandle` passed
   directly to `reads()`/`writes()`) and `dependency_derivation_tests.cpp`'s
   same-pass-read+write case now pass — `ctest -LE gpu` run.
6. **`compile_algorithm.h`/`.cpp`**'s `detail::compile()` (Section 6, all
   8 steps, including the corrected three-color-DFS cycle-witness
   extraction) as a free function over plain `RawPass` data. Add
   `compile_algorithm_tests.cpp` to `tests/render_graph/CMakeLists.txt`'s
   source list (Section 11's `PRIVATE` include path, added in step 1, now
   has a file to serve): build + `ctest -LE gpu` run, exercising
   `compile_algorithm_tests.cpp`'s white-box cases directly against
   `detail::compile()` — no `RenderGraphBuilder` involved yet.
7. **`RenderGraphBuilder::compile()`** wired to `detail::compile()`,
   translating `passes_`/`resources_` into `RawPass`/resource-count
   inputs and a successful `detail::compile()` result into a public
   `CompiledGraph` (Section 4's private constructor now has a caller).
   Add `dependency_derivation_tests.cpp`, `cycle_detection_tests.cpp`,
   `pass_retention_and_ordering_tests.cpp`, and
   `ownership_lifetime_tests.cpp` to `tests/render_graph/CMakeLists.txt`'s
   source list — **only now buildable**, since every one of them calls
   `compile()`: build + full `ctest -LE gpu` run — this is the first
   point every public-API test file in Section 9 can exercise the full
   API end-to-end.
8. **Full test suite pass**: every file in Section 9 green via
   `ctest --test-dir <build> -C Debug -LE gpu --output-on-failure`, then
   `-C Release -LE gpu`.
9. **Documentation**: `src/README.md` and `tests/README.md` only — the
   CMake registrations themselves were already completed in step 1 (and
   grown incrementally in steps 6–7), not performed here.
10. **Final verification pass**: Debug configure/build, Release
    configure/build, `ctest --test-dir <build> -C Debug -LE gpu
    --output-on-failure`, `ctest --test-dir <build> -C Release -LE gpu
    --output-on-failure`, zero new compiler warnings, Section 10's grep
    checklist, `ctest --test-dir <build> -N -L gpu` run before starting
    step 1 and again now, confirming an identical count (Section 9 — this
    Plan adds no `gpu`-labeled test), Section 13's Acceptance Criteria
    mapping re-confirmed against the actual diff, Definition of Done pass
    before PR. Whether the repository's pre-existing
    `atlantis_vulkan_backend_gpu_tests` suite is also run as part of this
    PR's overall verification is governed by Plan 0003's own Verification
    Checklist and `docs/process/testing-strategy.md`, not decided or
    changed by this Plan.

### Sequencing & Dependencies

- Steps 1–4 have no interdependency beyond step 1 and can proceed in any
  relative order among themselves.
- Step 5 depends on steps 1–2 (handles) and step 4 (forward-declares
  `CompiledGraph`'s type for `compile()`'s signature).
- Step 6 depends on step 3 (`CompileError`) only — it is deliberately
  independent of step 5, so the algorithm's own correctness can be
  verified against plain data before any public-API wiring exists.
- Step 7 depends on steps 4–6.
- Step 8 depends on step 7.
- Steps 9–10 are final and depend on everything above.

No step in this sequence requires a Vulkan SDK, a GPU, or a live window
to build or to run its tests — every `ctest` invocation in this section
is `-LE gpu`, and this module registers no `gpu`-labeled test to exclude
or include in the first place.

---

## 13. Acceptance Criteria Mapping

Every Spec 0005 Acceptance Criterion, mapped. This table maps exactly
Spec 0005's 34 literal criteria — no fewer, no more; the additional
test-design detail Sections 6–9 add beyond this literal set (the
downstream-pass and multiple-cycle cycle-detection cases; `CompiledPassId`'s
own assertion-fallback behavior) strengthens coverage of the *same* rows
below, rather than introducing new rows for criteria Spec 0005 does not
itself enumerate.

| Spec 0005 Acceptance Criterion | Satisfied by (step) | Verified by |
|---|---|---|
| Public headers contain no `Vk*`/`#include <vulkan/...>`/Platform type/RHI resource type | Step 1 (Section 1's link-library list has no such dependency) | Section 10's grep checklist |
| No Vulkan call, no RHI command-recording call | Steps 1–10 (never introduced) | Section 10's grep checklist |
| No GPU-required test exists | Step 1's `tests/render_graph/CMakeLists.txt` registers no `gpu`-labeled test (Section 11) | `ctest --test-dir <build> -N -L gpu` count unchanged before/after this Plan's implementation (Section 9's "On CTest's `gpu` label, precisely"); code review of Section 11's CMake snippet confirms no `PROPERTIES LABELS "gpu"` call |
| No rendering output/image/`RenderTarget` | Steps 1–10 (never introduced) | Section 10's grep checklist |
| Empty graph compiles successfully | Step 6/7 (Section 6 step 5's `0 == 0` case) | `pass_retention_and_ordering_tests.cpp` |
| Single pass compiles successfully | Step 7 | `pass_retention_and_ordering_tests.cpp` |
| Single-producer single/multi-reader order | Step 6/7 | `dependency_derivation_tests.cpp`, `compile_algorithm_tests.cpp` |
| Multiple readers produce no edge between them | Step 6 (Section 6 step 2 only derives producer→reader) | `dependency_derivation_tests.cpp`, `compile_algorithm_tests.cpp` |
| Independent producer/reader groups: deterministic, declaration-order-driven order | Step 6 (Section 6 step 4) | `pass_retention_and_ordering_tests.cpp` |
| Multiple producers rejected unconditionally | Step 6 (Section 6 step 1) | `dependency_derivation_tests.cpp`, `compile_algorithm_tests.cpp` — including the smallest-`declarationIndex`-wins tie-break when two resources both qualify, and the multiple-producer-beats-cycle priority case (Section 6, "Error selection priority and determinism") |
| Same-pass read+write rejected as programmer error | Step 5 (`RenderGraphBuilder::reads()`/`writes()`, Section 3/8) | `dependency_derivation_tests.cpp` |
| Two-pass cycle detected with deterministic witness | Step 6 (Section 6 step 6) | `cycle_detection_tests.cpp`, `compile_algorithm_tests.cpp` |
| Longer cycle detected | Step 6 | `cycle_detection_tests.cpp`, `compile_algorithm_tests.cpp` (including the downstream-pass, multiple-cycle, and multiple-producer-coexists-with-cycle cases) |
| Cycle witness unambiguous under duplicate labels | Step 6 (`declarationIndex`, Section 2/6) | `cycle_detection_tests.cpp` |
| Isolated pass and producer-with-no-readers retained | Step 6/7 (Section 6 step 5) | `pass_retention_and_ordering_tests.cpp` |
| Declared-but-unused resource accepted, no dependency relation | Step 6 (never referenced in step 2) | `dependency_derivation_tests.cpp` |
| Every declared pass appears exactly once | Step 6/7 (Section 6 step 5's invariant) | All of Section 9 |
| Builder non-copyable/non-movable | Step 1/5 (Section 3's deleted special members) | `handle_ownership_tests.cpp` (`static_assert`) |
| Handle/`CompiledPassId` types mutually distinct, no runtime cross-type case | Step 2/4 (Sections 2, 4 — separate classes, no conversion) | `handle_ownership_tests.cpp` (`static_assert`); Section 10's grep checklist |
| Default/foreign-live-builder handle triggers assertion | Step 5 (Section 5's `owns()` + check-then-early-return) | `handle_ownership_tests.cpp` |
| Use-after-builder-destruction not claimed/tested | Section 5's explicit design note | Section 9's "Explicitly not tested" statement; code review confirms no such test exists |
| Successful `CompiledGraph` valid after builder destruction | Step 7 (Section 5's independent-ownership deep copy) | `ownership_lifetime_tests.cpp` |
| Further declarations don't change an already-produced `CompiledGraph` | Step 7 | `ownership_lifetime_tests.cpp` |
| Two compiles produce independent, equivalent values | Step 7 | `ownership_lifetime_tests.cpp` |
| Duplicate labels legal, no effect on identity/order | Step 5/6 (Section 7 items 1–2) | `ownership_lifetime_tests.cpp`, `cycle_detection_tests.cpp` |
| Repeated compile deterministic | Step 6 (Section 6's "Complexity" subsection) | `ownership_lifetime_tests.cpp` |
| Failed compile: no partial graph, builder still valid | Step 6/7 (Section 6 step 8, Section 5) | `ownership_lifetime_tests.cpp` |
| No public mutation API on `CompiledGraph` | Step 4 (Section 4's header — no non-`const` public method) | Header inspection |
| No caller-authored dependency edge / `dependsOn` / sort key | Steps 1–10 (never introduced) | Section 10's grep checklist |
| No mutex/atomic/job system; no declared thread-safety beyond Phase 1 baseline | Steps 1–10 (never introduced) | Section 10's grep checklist |
| No global handle registry/allocator/mutable graph state | Steps 1–10 (Section 5's design has none) | Section 10's grep checklist |
| No lifetime interval/imported-transient/physical property | Steps 1–10 (never introduced) | Section 10's grep checklist |
| No `src/renderer/`, Shader System, or RHI resource/command source created | Steps 1–10 (never touched) | Directory listing |
| Both ADRs `Accepted` before Spec `Approved` | **Already satisfied** — recorded 2026-08-09 | No action needed; recorded fact |

Every Spec 0005 Acceptance Criterion is mapped; none paraphrased away.

---

## Verification Checklist

- [ ] **Unit tests:** all six `tests/render_graph/` files (Section 9)
      pass via `ctest --test-dir <build> -C Debug -LE gpu
      --output-on-failure` and `-C Release -LE gpu`.
- [ ] **Headless integration tests:** not applicable — this Plan's scope
      performs no GPU work of any kind (Spec 0005 Testing & Verification
      Plan).
- [ ] **Image regression tests:** not applicable — nothing is rendered.
- [ ] **Vulkan Validation Layers clean:** not applicable — no Vulkan call
      exists anywhere in this Plan's scope.
- [ ] **Other:** Section 10's grep checklist passes; full Debug + Release
      build with zero new compiler warnings; `ctest -N -L gpu` count
      unchanged before/after implementation (Section 9/12); Section 13's
      Acceptance Criteria mapping re-confirmed against the actual diff,
      not just this Plan's intent.

## Rollback Plan

Purely additive: one new module (`src/render_graph/`) and its tests
(`tests/render_graph/`), plus CMake/documentation touch-ups. Reverting
the implementing PR removes both directories and restores
`CMakeLists.txt`, `src/README.md`, and `tests/README.md` to their
post-Spec-0003 state — no migration or cleanup beyond a standard revert,
since nothing outside `src/render_graph/`/`tests/render_graph/` is
behaviorally changed.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this plan:

- **"Image regression tests" and "Vulkan Validation Layers run clean" are
  not applicable** — Spec 0005's entire scope is GPU-independent; nothing
  is rendered and no Vulkan call exists.
- **"Headless verification performed for any rendering-adjacent change"
  is not applicable** — this change is not rendering-adjacent; it
  produces no GPU work of any kind.
- **This Plan does not add, remove, or modify the repository's existing
  `atlantis_vulkan_backend_gpu_tests` suite** — whether that suite is run
  as part of this PR's own CI/verification gate is governed by Plan
  0003's Definition of Done, unaffected by this Plan.
- Add: Section 10's grep checklist passes; Section 13's mapping is
  re-confirmed against the actual diff; the full GPU-independent test
  suite (Section 9) has actually been run via both `ctest -C Debug -LE
  gpu` and `ctest -C Release -LE gpu` on the implementer's machine, not
  merely written; `ctest -N -L gpu`'s count is confirmed unchanged.

---

## Consistency Review

1. **Consistency with Spec 0005:** every Functional Requirement has a
   corresponding implementation section above (Sections 2–6); every
   Non-Goal is restated in Section 10's checklist; every Acceptance
   Criterion is mapped in Section 13 (exactly 34, matching Spec 0005's
   own count); the four-tier Error Model is implemented verbatim in
   Section 8, extended only in its explanatory text (not its tiers) to
   name the `CompiledPassId` cross-graph case alongside the pre-existing
   builder-handle lifetime case. `CompileError`'s two alternatives —
   `MultipleProducersError{resource, producers}` and
   `DependencyCycleError{passes}` (this revision) — satisfy the Spec's
   "identify participating passes" requirement for both error kinds, and
   additionally name the offending resource for `MultipleProducersError`
   — a strengthening, not a narrowing, of what the Spec itself requires.
2. **Consistency with ADR-0017:** the builder is non-copyable, non-
   movable, purely additive, and never mutated by `compile()` (Sections
   3, 5); `CompiledGraph` independently owns its data and is move-only
   (Section 4, 7 item 11); handle provenance uses the builder's own
   stable address, exactly the mechanism ADR-0017's Decision names as
   safe once the builder cannot move (Section 5) — and `CompiledPassId`
   deliberately does *not* reuse that mechanism, because `CompiledGraph`,
   unlike the builder, is movable (Section 5).
3. **Consistency with ADR-0018:** the single-producer model, producer→
   reader-only derived edges, unconditional multiple-producer error,
   cross-resource cycle detection (now correctly scoped to the actual
   cycle via three-color DFS, not the whole Kahn-remaining set), the
   declaration-order tie-break, and the all-passes-retained invariant are
   all implemented exactly as decided (Section 6) — no caller-authored
   edge, no pass culling, anywhere in this Plan.
4. **Consistency with ADR-0004:** no thread, lock, mutex, atomic, or
   job/task system introduced anywhere (Sections 3–6, 10); every
   `RenderGraphBuilder`/`CompiledGraph` method this Plan's own code
   executes is single-thread-only.
5. **Consistency with ADR-0009:** every assertion uses `ATLANTIS_CHECK`
   exactly as it already exists, with the check-then-early-return pattern
   (Section 5) — condition computed exactly once — grounded directly in
   `assert.h`'s actual behavior; no modification to `ATLANTIS_CHECK`,
   `reportFailure()`, or `setFailureHandler()` anywhere.
6. **No new public API beyond what Spec 0005 authorizes:** `handles.h`,
   `render_graph_builder.h`, `compiled_graph.h`, `compile_error.h`
   declare exactly the types Sections 2–4 list — no proxy/builder-scoped
   helper type (Section 3's `PassBuilder` removal), no resource-level
   query on `CompiledGraph`, no `dependsOn`-shaped API, introduced
   anywhere (Section 7's disposition table explicitly reasons through the
   closest candidates for scope creep and rejects each).
7. **No re-litigation of any Accepted ADR's conclusion:** every decision
   in Sections 2–8 either quotes or directly implements ADR-0017/ADR-0018's
   already-fixed text (or an already-Accepted ADR referenced from Spec
   0005, e.g. ADR-0004/ADR-0009); Section 7's seventeen dispositions are
   all Plan-level implementation judgment calls with no public-API/
   ownership/threading/dependency/error-classification weight, explicitly
   reasoned through rather than asserted — and the section states plainly
   that zero Human Review blockers were surfaced.
8. **No candidate shape overclaimed as final before it actually was:**
   every heading in Sections 2–4 says "Candidate Shape(s)", accurately
   describing this Plan's status at drafting time; the Approval
   transition note at the top of this document now records exactly when
   and how those candidate shapes became the approved basis for
   Implementation, rather than silently relabeling them without a
   recorded decision.
9. **No verification claim overstates what is covered:** Section 9
   explicitly and separately states which two cases are *not* dynamically
   tested (use-after-builder-destruction; cross-`CompiledGraph`
   `CompiledPassId` misuse) and why, rather than implying full coverage;
   the previous revision's incorrect repo-wide "`ctest -L gpu` returns
   zero tests" claim is corrected to the precise, checkable claim this
   Plan actually makes (Section 9's "On CTest's `gpu` label, precisely");
   every `Step 11` reference (this Plan has 10 steps) is removed; Section
   13 traces every Acceptance Criterion to a concrete test file or
   code-review check, never to "steps 1–10" alone without naming what
   specifically verifies it.
10. **Every public type states its ownership and thread-safety contract
    explicitly (this revision):** `PassHandle`/`ResourceHandle`,
    `CompiledPassId`, `CompiledDependencyEdge`, `PassDiagnostic`/
    `ResourceDiagnostic`, `CompileError`, `RenderGraphBuilder`, and
    `CompiledGraph` (Sections 2–4) each now state, in their own header
    comment: whether they own data or are borrowed/plain value tokens;
    whether copying/moving transfers or shares ownership; and their
    thread-safety contract (uniformly: no declared concurrent-access
    safety beyond Phase 1's single-logical-frame-thread baseline, and a
    value token's copyability across threads is explicitly not the same
    claim as its safe *use* being cross-thread). No plain, read-only
    struct is misdescribed as a new cross-thread guarantee — every one is
    stated as an ordinary owned value type with no synchronization, safe
    to copy/move/read from one thread at a time, nothing stronger.
11. **`CompiledGraph::label()`'s `std::string_view` borrow lifetime is
    stated explicitly, including both move directions (this revision):**
    valid only while the `CompiledGraph` instance it was obtained from
    remains alive, has not been the source of a move (construction or
    assignment), and has not been the destination of a move-assignment
    that replaced its content; move construction of one `CompiledGraph`
    has no effect on any other, independent instance; self-move-
    assignment is not relied upon; invalidated by that instance's
    destruction, exactly like any other view
    into an owned container this codebase's conventions already assume
    (Section 4); a caller needing the label to outlive the `CompiledGraph`
    must copy it into an owned `std::string`. This is listed as an
    explicit test/code-review item in Section 9's `ownership_lifetime_tests.cpp`
    row and Section 10's checklist intent, not left implicit.
12. **`CompiledPassId`'s boundary is a single, consolidated statement
    (this revision):** default-invalid sentinel, out-of-range
    guaranteed-detectable, cross-`CompiledGraph` coincidental-index
    collision explicitly *not* guaranteed-detectable (a documented
    precondition violation, not dynamically tested), no owner pointer, no
    generation counter, no registry — stated once, in full, in Section 4's
    class comment, rather than split across Sections 4 and 5 as in the
    previous revision. Dependency endpoints
    (`CompiledDependencyEdge::from`/`to`) are documented as meant to be
    interpreted against the one `CompiledGraph` that produced them.
13. **Every candidate header's `#include` order now matches `AGENTS.md`**
    (C++ standard library, blank line, project headers) — `render_graph_builder.h`
    previously listed project headers first; every other candidate header
    in this Plan was checked and already conformed (Sections 2–4),
    including the just-rewritten `compile_error.h` (`<cstddef>`,
    `<string>`, `<variant>`, `<vector>`, blank line, no project headers
    needed).
14. **`CompileError`'s illegal states are unrepresentable, not merely
    undocumented (this revision):** `CompileError` is
    `std::variant<MultipleProducersError, DependencyCycleError>`
    (Section 2) — there is no way to construct a value with
    `DependencyCycleError`'s shape carrying a resource field, or a
    `MultipleProducersError` missing one, because neither type has such a
    field to misuse in the first place; a consumer switches on the active
    alternative (`std::holds_alternative`/`std::get_if`, Section 8), never
    a separately-tracked `kind` enumerator that could disagree with the
    payload it accompanies.
15. **Error selection priority is fixed and deterministic, not
    implementation-order-dependent (this revision):** Section 6's "Error
    selection priority and determinism" subsection states, and Section 9
    tests, that multiple-producer validation always completes or fails
    before cycle detection starts, that a graph with both problems always
    reports `MultipleProducersError`, and that the smallest-`declarationIndex`
    violating resource is reported when more than one qualifies — Section
    7 item 18 records this as a Plan-stage disposition, not a Human
    Review item, since Spec 0005/ADR-0018 leave the shape of this rule
    open while already fixing that everything must be deterministic.
16. **`CompiledGraph`'s move/view contract is fully finalized, covering
    both move directions (this revision):** Section 4's class comment now
    states the destination-of-move-assignment view-invalidation case, the
    no-cross-instance-effect guarantee for move construction, and the
    self-move-assignment caveat, alongside the pre-existing move-FROM
    case; Section 7 item 19 records the decision to keep both move
    construction and move assignment (rather than narrowing to
    construction-only) with its rationale; `static_assert`s for both
    `std::is_move_constructible_v<CompiledGraph>` and
    `std::is_move_assignable_v<CompiledGraph>` are part of Section 9's
    test matrix.

All sixteen checks passed at this Plan's own pre-review draft, and were
confirmed at the joint Spec 0005 + Plan 0005 Human Review recorded in the
Approval transition note at the top of this document (2026-08-09). This
Plan is now `Approved / Ready for Implementation`; no Implementation has
begun under this Plan as of this approval record.
