# Spec: Read-only Query on a Const ECS World

- **Status:** In Review
- **Author:** Codex, at slmao's explicit direction
- **Created:** 2026-11-01
- **Related Plan(s):** [Plan 0075](../plans/0075-ecs-const-query.md) (In Review)
- **Approval:** Pending joint human review; reviewing PR to be linked on submission.
- **Related ADR(s):** [ADR-0139](../adr/0139-ecs-const-query-and-logical-iteration-state.md) (Proposed), extending ADR-0101 access semantics.
- **Origin:** Candidate 9 and [Plan 0051 J1](../plans/0051-authoring-scene-runtime-world-bake.md).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Add one read-only `query<Ts...>(fn) const` overload to `world::ecs::World`, with
const component references, a read-only traversal view and the existing per-World
iteration protection. Keep the mutable query and all Runtime consumers unchanged.
This round submits a proposal only, independently based on `origin/main`, without
implementation, builds, GPU invocation or performance investigation.

## Motivation / Problem Statement

At reviewed main `b319fba`, `query<const T>` still requires a mutable World:
`world.h` has only a non-const receiver, and the private walk updates `queryDepth_`.
Runtime's read-only light/renderable collectors therefore still take mutable
`BakedScene&`. Plan 0051 J1 deliberately deferred this ECS API refinement. Its
candidate remains valid; immutable code evidence belongs in the reviewing PR.

The existing type-erased `detail::ChunkView` carries writable byte pointers, and
its callback helper derives reference mutability from `Ts`. A const receiver needs
an explicit access contract and legal bookkeeping for an actually const object.

## Goals

- Make an actually const ECS World queryable without exposing writable components.
- Preserve matching, order, borrowed-reference lifetime and iteration protection.
- Keep the first slice within World and its focused tests; leave consumer adoption separate.

## Non-Goals

- No Runtime collector signatures/call sites, `runFrame`, capture, Renderer or SDK/wire changes.
- No query language extension, caching, scheduler, locks/atomics, concurrency, snapshot or new dependency.
- No storage layout, component identity/descriptor, scene format, error enum or policy/baseline change.
- No implementation authority from this submission, or dependence on PR #307.

## Requirements

### Functional

- **R1 — Receiver-const API.** Add `query<Ts...>(fn) const` on `ecs::World`.
  On that overload, both an unqualified component term `T` and `const T` select
  the same component and supply a `const T&` argument. Supply `EntityId` by value.
  `query<>` remains a valid all-entity query supplying only the id. Accept the
  same component-term domain as the existing query; add no volatile/reference-term
  support, optional/excluded terms or duplicate-term policy. Return `void` and
  discard any callback return value as today.
- **R2 — Compile-time const boundary and compatibility.** Constrain the new
  overload on both valid component terms and a valid lvalue direct-call expression
  `fn(id, const-component-references...)`, matching the current callback category
  with R1's arguments. Do not broaden it to member-pointer/`std::invoke`-only
  callables through a constraint that the actual call expression cannot honor.
  A callback requiring `T&` must be rejected for a const
  receiver, including in a well-formed dependent `requires` probe; do not defer
  the only check to an unconstrained function body. A generic read-only callback
  observes const-qualified component types. Preserve existing non-const query
  overload selection and its per-term `T&`/`const T&` behavior; do not silently
  convert a mutable query into a read-only traversal. Preserve synchronous,
  callback-scoped invocation, without copying/storing the callable or extending
  reference lifetime.
- **R3 — Read-only traversal.** Use a distinct `detail::ConstChunkView` with
  `const std::byte*` component columns and a const entity view, plus a const
  private walker and const reference projection. Keep the writable view/walker
  for the existing mutable path. No const-cast or routing the const overload
  through a non-const World method; no writable component pointers/references
  may cross the const traversal view. No component copies/snapshots. Matching
  and archetype-creation/chunk/row order remain identical to the existing walk.
- **R4 — Logical iteration state.** Make only the existing `queryDepth_`
  bookkeeping member mutable, shared by const and mutable traversal of the same
  World. It is not component or entity state. The existing RAII depth mechanism
  must balance on normal exit and stack unwinding, including nested queries and
  empty matches. Structural changes through a separately captured non-const
  alias to that World stay refused until every active query exits: the existing
  Debug assertion plus `StructuralChangeDuringQuery` for destroy/add/remove,
  and `kInvalidEntityId` for create, with nothing changed. `CommandBuffer::apply`
  stays refused as a whole, including a set-only buffer, and preserves its queued
  commands for later application. Recording deferred commands remains allowed.
  No CommandBuffer production change or new error channel is required.
- **R5 — Nesting, aliasing and threading.** Preserve nested read/read,
  mutable/read and read/mutable queries when the caller legitimately owns the
  needed non-const alias; protection remains per World. `get`/`has` and existing
  non-structural `set` through such an alias remain allowed under 0050 J8. A const
  query is not a snapshot or a write lock over independently held capabilities;
  live values can change through that existing alias, but not through its const
  callback arguments. A genuinely const World needs no cast or mutable alias.
  Different Worlds remain independent. All access, including const queries,
  remains single-threaded/not thread-safe; const adds no concurrent-reader promise.
- **R6 — Scope and versioning.** Change no Runtime consumer or its existing
  query call, no schema-reflected type/descriptor, component id, format or public
  result type. Add no schema/fingerprint/version bump for World methods or the
  private traversal view. Accepted Specs/Plans/ADRs are not rewritten; any newly
  necessary file/contract change is a reported deviation before expansion.
- **R7 — Verification obligations.** Future implementation builds and verifies
  Debug/Release full suites and Android `assembleDebug` under the existing
  testing strategy/Definition of Done and library verification requirements. The
  planned `src/world/**` changes select the unchanged performance applicability
  list: the expected whole implementation-PR result is **`applies=true`**, even
  with no Runtime adoption. Evaluate the real full diff and fulfill the existing
  formal-gate obligation; record the raw verdict and provenance. Never treat
  fail, inconclusive, incomparable or an environment failure as pass, or assume
  an N/A/waiver from unused new API or unchanged consumers. N/A is permitted only
  under existing rules with its actual reason. Preserve failures and unresolved
  acceptance blockers for human disposition; keep the implementation draft when
  acceptance remains blocked. No policy/baseline/required-check adjustment or
  performance investigation is authorized by this proposal round.

### Non-functional

No new module/dependency, allocation ownership or synchronization mechanism.
The new walk has the existing traversal complexity and O(number of requested
types) temporary column bookkeeping; no per-entity state, new performance target
or timing assertion. The mutable path's algorithm stays intact. C++20, Windows
and Android boundaries remain unchanged; this is portable World code, not OS code.

## Proposed Design

Use R1–R5's additive overload and typed read-only traversal boundary. Share the
existing depth guard's state, with no public bookkeeping API. The const overload
is usable from both a const view and an actually const populated World produced
by move construction. Keep callable/reference handling consistent with the
existing callback API and keep old consumers on their existing overload.

## Architectural Impact

**Public access semantics and logical constness:** draft [ADR-0139](../adr/0139-ecs-const-query-and-logical-iteration-state.md)
alongside this pair. It extends [ADR-0101 D6](../adr/0101-runtime-ecs-core-storage-identity-and-placement.md)
with receiver constness and the narrowly mutable iteration state; existing
storage/identity/ownership and single-thread rules are retained. It does not
supersede an existing decision or grant Runtime adoption, so ADR-0102 and Plan
0051 J1's current collector signatures remain untouched. ADR-0139 must be Accepted
before or at joint implementation approval. All new choices remain pending review.

## Alternatives Considered

- Require explicitly const terms on a const World: safe but needlessly rejects
  `constWorld.query<T>(readOnlyCallback)`; J1 reviews the recommended const propagation.
- Const-cast into the writable path: hides access intent and can make writes to
  an actually const object's non-mutable bookkeeping invalid; rejected.
- Copy components into a snapshot: changes the callback/live-value contract and cost.
- Replace or privatize the old writable traversal, or generalize both walks now:
  broadens the patch and the old-path regression surface.
- Add Runtime adoption, locking or a read-only view wrapper: separate reviewed scope.

## Testing & Verification Plan

Plan 0075 maps R1–R7 to compile-time const-safety/compatibility checks, runtime
equivalence and shared-depth protection, existing World/CommandBuffer regressions,
unchanged Runtime extraction checks, and the existing correctness/performance gates.
GPU tests that run must use clean Vulkan Validation; rendered bytes/goldens are
not expected to change, and no golden refresh is authorized. Current review is
static source/test reading plus documentation checks only; no executed reproducer.

## Risks & Open Questions

J1–J4 in the Plan cover API spelling/constraints, logical state/aliasing, scope/ADR
and verification. Pointee constness is not inherited automatically from a const
`unique_ptr` owner, making the typed read-only view important. Overload constraints
must not make current mutable calls ambiguous. Separate walks can drift in order;
the tests must compare their complete results across archetypes/chunks. Const
queries remain subject to the existing single-thread and borrowed-reference rules.
Source independence from #307 supplies no evidence that a future gate environment
qualifies; formal verification may remain blocked and must be reported honestly.

## Out of Scope / Future Work

Runtime collector adoption, other consumers, broader ECS query facilities and
parallel access require their own authorization. This round stops at joint-review
submission; implementation and any measurement need later explicit authorization.
