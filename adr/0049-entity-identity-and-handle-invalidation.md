# ADR 0049: Entity Identity and Handle Invalidation

- **Status:** Proposed
- **Date:** 2026-08-22
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md)

## Context

[ADR-0033](0033-runtime-authority-and-client-boundary.md) (`Accepted`)
already states a binding constraint on whatever entity representation a
future World/ECS Spec chooses: "No public, cross-module API may return or
accept a raw pointer/reference to a Runtime-owned object whose lifetime
Runtime itself controls (e.g. an internal entity record, a component
array)," and names "index+generation or otherwise" as the open
representation question it explicitly defers to this Spec. `World` (per
[ADR-0048](0048-world-scene-module-boundary-and-ownership.md)) owns every
entity and component outright — external code, including Runtime itself,
never holds a `Transform&`/`Camera&`/`Renderable&` reference into World's
own storage. What external code holds instead, across calls, is an
identifier — and that identifier must behave safely when the entity it
names has since been destroyed, since this Spec's own single-threaded,
per-frame model (Runtime calls into World repeatedly across many frames)
makes "a handle captured last frame, entity destroyed since" an ordinary,
expected occurrence, not a rare edge case.

Two broad shapes exist:

1. **A raw dense index** (`std::uint32_t` or similar) into World's
   internal storage. Simple, but a destroyed and reused slot is
   indistinguishable from the original entity — a stale index silently
   aliases whatever unrelated entity now occupies that slot, corrupting
   state without any error signal.
2. **An index plus a per-slot generation counter**, incremented every
   time a slot is destroyed and its index reused. A handle is valid only
   if both its index is in range and its generation matches the slot's
   current generation — a stale handle is detectably invalid, not
   silently aliased.

## Decision

**`atlantis::world::EntityId` is an index+generation handle:**

```cpp
struct EntityId {
  std::uint32_t index;
  std::uint32_t generation;
};
```

with value equality, a fixed invalid sentinel (`kInvalidEntityId`, exact
sentinel values — e.g. `index == generation ==
std::numeric_limits<std::uint32_t>::max()` — a Plan-stage detail), and no
other public data or behavior. `EntityId` is a plain, copyable value type;
it owns nothing and its lifetime is entirely decoupled from the entity it
names.

- **World is a slot map.** Internally, World holds a growable array of
  slots, each carrying: an alive flag, a generation counter, and that
  entity's component data (Transform always present; `Camera`/
  `Renderable` each optional — see the related Spec's Requirements).
  Destroying an entity marks its slot dead, increments its generation,
  and returns the index to a free list for reuse by a future
  `createEntity()` call. This is the same pattern
  [ADR-0033](0033-runtime-authority-and-client-boundary.md) itself named
  as its own illustrative example ("index+generation") — this ADR is the
  first concrete Spec to actually adopt it.
- **Every public World API that accepts an `EntityId` validates it**
  (index in range **and** generation matches the slot's current
  generation) before acting, and returns
  `atlantis::Result<T, WorldError>` with `WorldError::InvalidEntity` on
  failure — never undefined behavior, never a silent no-op, never a
  crash. This is a **recoverable** runtime condition per
  [AGENTS.md](../AGENTS.md)'s own error-handling rule ("Recoverable
  runtime errors use explicit result/error types, not exceptions"),
  extended here to the new World module: holding a handle to a since-
  destroyed entity is an expected consequence of this Spec's own
  single-threaded-but-temporally-decoupled model, not a programmer
  precondition violation.
- **World never returns a reference or pointer into its own internal
  storage from any public accessor.** Every getter (`getLocalTransform()`,
  `getWorldMatrix()`, ...) returns a **by-value** copy
  (`Result<Transform, WorldError>`, `Result<std::array<float, 16>,
  WorldError>`, ...), and every setter takes its argument **by value**.
  This is stricter than strictly required to satisfy ADR-0033's own
  "no raw pointer across a public API" rule (which is about cross-module
  Client access, not necessarily same-process, same-module internal
  accessors) — adopted anyway because World's own internal slot array can
  reallocate on `createEntity()` growth, which would otherwise leave any
  previously-returned reference dangling. By-value access removes that
  hazard entirely rather than documenting around it.
- **`createEntity()` never fails** (it always has a free slot to return —
  either from the free list or by growing the underlying storage) and
  returns a plain `EntityId`, not a `Result` — matching this repository's
  own existing convention (e.g. `Buffer`/`Texture` construction is
  fallible because it depends on an external GPU allocator; `EntityId`
  creation depends on nothing external and cannot fail for a reason a
  caller could meaningfully act on).
- **`EntityId` equality is index-and-generation equality**, not
  index-only — two handles with the same index but different generations
  (one stale, one current) compare unequal, matching the same validity
  semantics every other World API already enforces.

## Consequences

### Positive

- Stale-handle bugs (using an `EntityId` after its entity was destroyed)
  fail loudly and recoverably — a `Result::Err(WorldError::InvalidEntity)`
  the caller must handle — instead of silently corrupting an unrelated,
  slot-reused entity's state.
- By-value get/set removes an entire class of dangling-reference bugs
  (slot-array reallocation) without requiring callers to reason about
  World's own internal storage growth.
- Directly satisfies [ADR-0033](0033-runtime-authority-and-client-boundary.md)'s
  own binding constraint, and does so with the exact representation that
  ADR itself named as an illustrative candidate — no new precedent
  invented, an existing one exercised for the first time.
- `EntityId` is a small (8-byte), trivially-copyable, comparable value —
  cheap to store in `std::vector`, pass by value, and use as a map key
  (e.g. Runtime's own AssetId→Mesh resource table, or a future test's own
  bookkeeping), with no ownership or lifetime complexity of its own.

### Negative / Trade-offs

- Every World mutation/query pays a small, constant-time validation cost
  (index-range check, generation compare) — not a performance concern at
  this Spec's own scale (a handful of entities), but a real, disclosed
  per-call cost this ADR accepts rather than an unchecked raw-index
  fast path.
- A 32-bit generation counter can theoretically wrap after ~4 billion
  create/destroy cycles on the same slot, at which point a very old,
  long-held stale handle could alias a live entity again. Not mitigated
  by this ADR — at this Spec's own scale and single-session lifetime,
  this is not a real risk; a future Spec revisiting long-running-process
  or save/load scenarios may need to reconsider.
- By-value component access means a caller reading several fields off the
  same entity repeatedly (e.g. `getLocalTransform()` inside a hot loop)
  pays a copy each call rather than a reference — acceptable at this
  Spec's own scale (a handful of entities, once per frame), not evaluated
  against a larger future entity count.

## Alternatives Considered

- **A raw dense index with no generation.** Rejected: makes stale-handle
  use-after-destroy silently alias a reused slot instead of failing
  detectably — exactly the invariant-violation-without-signal AGENTS.md's
  error-handling rules and ADR-0033's own access-safety intent both argue
  against.
- **A stable GUID (e.g. 128-bit UUID) per entity, with no index/slot
  reuse at all.** Rejected for this round: real, permanent uniqueness (no
  wraparound risk) at the cost of a larger handle, a lookup indirection
  (GUID → slot, typically a hash map, versus O(1) direct indexing), and
  no natural free-list reuse story. `specs/README.md`'s own Candidate
  Backlog already names "Serialization and Stable Identity" as its own,
  later, dedicated Spec (depending on this one) — exactly where a
  cross-session-durable identity scheme belongs; inventing one here
  ahead of a real save/load or cross-process consumer would be the
  premature abstraction AGENTS.md's Golden Rule warns against.
- **Return raw pointers/references from World's own internal accessors**,
  relying on documentation ("do not hold across a `createEntity()` call")
  instead of a by-value contract. Rejected: directly conflicts with
  [ADR-0033](0033-runtime-authority-and-client-boundary.md)'s own explicit
  rule, and this repository's own established convention (RHI never
  exposes an internal `Vk*` handle across its public boundary) already
  argues for the stricter, safer default.
- **Handle validation only on debug builds (`ATLANTIS_ASSERT`), trusting
  callers in Release.** Rejected: a stale `EntityId` is not a programmer
  precondition violation in the same sense AGENTS.md reserves assertions
  for (e.g. a null pointer a caller controls) — it is an ordinary runtime
  condition arising from this Spec's own decoupled create/destroy/query
  timing, which AGENTS.md's own error-handling rule requires to be a
  `Result`, not an assertion, in both configurations.
