# ADR 0139: Const ECS Query and Logical Iteration State

- **Status:** Proposed
- **Date:** 2026-11-01
- **Deciders:** slmao (pending)
- **Acceptance:** Pending joint human review in [PR #314](https://github.com/slmao/Atlantis/pull/314).
- **Related Spec:** [Spec 0075](../specs/0075-ecs-const-query.md) (In Review)
- **Related ADR(s):** Extends [ADR-0101 D6](0101-runtime-ecs-core-storage-identity-and-placement.md); retains its storage/identity/single-thread rules and [ADR-0102](0102-authoring-runtime-world-separation-and-scene-bake.md). No accepted ADR is superseded or edited.

## Context

The existing ECS callback query can expose const components, but its receiver
must be non-const because traversal maintains a depth counter. Plan 0051 J1
deferred a const receiver while adopting mutable collector parameters. A const
World is a useful access capability even before those consumers change, and its
meaning must be explicit: a const owning pointer does not make its pointee const.

The depth counter protects borrowed component references from row-moving changes
through other aliases and CommandBuffer application. Omitting that counter on
the new path would weaken the access contract even when all yielded references
are const. Retaining single-threaded operation is an existing constraint.

## Decision

Propose an additive receiver-const query under Spec 0075 R1–R5. Const propagation
is enforced at the callback constraint and at a separate read-only type-erased
column view; the writable path remains the existing path. Do not cast a const
World into that path or introduce a snapshot, lock or new query owner.

Treat only the existing per-World query-depth field as mutable logical access
bookkeeping, with one guard state across both traversal kinds. Component/entity
data and storage ownership are not made mutable by this decision. This makes
bookkeeping valid for a genuinely const object while preserving alias-based
structural refusal and the existing nested-query contract.

Receiver constness restricts the capability furnished by that call; it supplies
no concurrent-reader guarantee, snapshot isolation or revocation of independently
held mutable aliases. Borrowed references keep the current callback-only lifetime.
Runtime adoption and changes to its collector signatures are separate scope.

This extends the access decision in ADR-0101 D6 without changing its existing
mutable-query meaning, storage model, identity rules or single-thread boundary.
The proposed public API and logical-constness decision require joint acceptance;
this Proposed document does not authorize implementation.

## Consequences

### Positive

- A genuinely const ECS World has a constrained read-only traversal capability.
- Iteration safety is shared across const/mutable aliases rather than bypassed.
- The old call sites, data formats and module boundaries retain their contracts.

### Negative / Trade-offs

- Even const queries mutate private bookkeeping and remain not thread-safe.
- Separate walks can drift; order/matching/protection equivalence needs tests.
- Type constness cannot prevent callers from retaining references or deliberately
  casting away qualifiers; lifetime/capability discipline remains required.

## Alternatives Considered

- Require every term to be spelled `const T`: safe, but adds syntax without an
  additional safety guarantee over the proposed receiver-based propagation.
- Delegate by const-cast: obscures the access boundary and const-object legality.
- Skip bookkeeping for read-only queries: aliases can still request row-moving changes.
- Copy a snapshot or add reader locks/atomics: changes cost, ownership or threading.
- Convert Runtime immediately or refactor both walkers generically: expands the
  reviewed first slice without being needed to provide the const capability.
