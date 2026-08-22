# ADR 0049: Entity Identity and Handle Invalidation

- **Status:** Proposed
- **Date:** 2026-08-22
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md)
- **Revision (2026-08-22, pre-Human-Review evidence pass):** widened
  `generation` from `std::uint32_t` to `std::uint64_t` and added an
  explicit slot-reuse/enumeration-order contract and a blanket mutation-
  atomicity guarantee — see Decision and Consequences below. Re-grounded
  the Result-vs-assertion choice in `src/core/include/atlantis/assert.h`'s
  actual, verified Release-build semantics rather than resting the
  argument on [ADR-0033](0033-runtime-authority-and-client-boundary.md)
  alone (which governs cross-module Client access this Spec does not yet
  exercise — see the narrower framing below). No change to this ADR's
  `Proposed` status; still pending Human Review.

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

**`atlantis::world::EntityId` is an index+generation handle, with a
64-bit generation field:**

```cpp
struct EntityId {
  std::uint32_t index;
  std::uint64_t generation;
};
```

with value equality, a fixed invalid sentinel (`kInvalidEntityId`, exact
sentinel values — e.g. `index == std::numeric_limits<std::uint32_t>::max()`
— a Plan-stage detail), and no other public data or behavior. `EntityId`
is a plain, copyable value type (16 bytes, once padding for the
`uint64_t`'s own alignment is counted — see Consequences); it owns
nothing and its lifetime is entirely decoupled from the entity it names.

- **World is a slot map.** Internally, World holds a growable array of
  slots, each carrying: an alive flag, a `uint64_t` generation counter,
  and that entity's component data (Transform always present; `Camera`/
  `Renderable` each optional — see the related Spec's Requirements).
  Destroying an entity marks its slot dead, increments its generation,
  and returns the index to a free list for reuse by a future
  `createEntity()` call. This is the same pattern
  [ADR-0033](0033-runtime-authority-and-client-boundary.md) itself named
  as its own illustrative example ("index+generation") — this ADR is the
  first concrete Spec to actually adopt it.
- **Generation width, and why 64 bits closes the wraparound risk rather
  than merely disclosing it.** A 32-bit generation counter wraps after
  `2^32 ≈ 4.3` billion destroy/reuse cycles on the same slot index, at
  which point a sufficiently old, still-held stale handle could alias a
  live entity again — a real, if narrow, safety gap for a handle type
  this Spec requires to fail detectably (see below). A 64-bit counter
  instead wraps only after `2^64 ≈ 1.8×10^19` cycles on the same slot:
  even a sustained, unrealistic churn rate of 10 billion
  `destroy`+`createEntity()` cycles per second on one single slot index
  would take roughly 57 years of continuous execution to wrap — far
  beyond any plausible single-process Runtime lifetime (hours to days,
  per this codebase's own existing verification record). This closes the
  risk for this Spec's own practical scope rather than leaving it as an
  accepted, unmitigated gap; a permanently-retire-the-slot scheme (never
  reusing an index once its generation nears overflow) was considered and
  rejected as needless extra bookkeeping for a risk 64 bits already makes
  practically unreachable — see Alternatives Considered.
- **Every public World API that accepts an `EntityId` validates it**
  (index in range **and** generation matches the slot's current
  generation) before acting, and returns
  `atlantis::Result<T, WorldError>` with `WorldError::InvalidEntity` on
  failure — never undefined behavior, never a silent no-op, never a
  crash. This is a **recoverable** runtime condition per
  [AGENTS.md](../AGENTS.md)'s own error-handling rule ("Recoverable
  runtime errors use explicit result/error types, not exceptions"),
  extended here to the new World module. **The concrete, verified reason
  this is a `Result` and not an assertion** (re-checked directly against
  `src/core/include/atlantis/assert.h` during this ADR's pre-Human-Review
  evidence pass, not merely asserted): `ATLANTIS_ASSERT` compiles to
  `((void)0)` — the condition is not even evaluated — whenever `NDEBUG`
  is defined, i.e. in every Release build; using it for stale-handle
  detection would silently remove the entire safety net in exactly the
  configuration where an undetected stale-handle alias would do real
  damage. `ATLANTIS_CHECK` is evaluated in both configurations, but its
  own, single failure behavior is `reportFailure()` → `ATLANTIS_LOG_FATAL`
  → `std::abort()` — aborting the entire Runtime process for what is, in
  a single-threaded, single-owner model, often an ordinary caller
  bookkeeping condition (e.g. one part of a frame's own composition code
  destroying an entity another, unrelated part of the same frame still
  holds a handle to) rather than memory corruption or a violated
  invariant an assertion is meant to catch. Note this is a narrower,
  more concrete justification than resting on
  [ADR-0033](0033-runtime-authority-and-client-boundary.md)'s own
  Client-access principle alone would be: this Spec has no second Client
  yet (see the related Spec's own Non-Goals), so ADR-0033's
  cross-process-race framing does not, by itself, apply within a single,
  single-threaded Runtime process — the `assert.h`-grounded argument
  above is what actually carries this decision. **Flagged explicitly for
  Human Review, not silently locked** — see the related Spec's Decisions
  Requiring Human Review.
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
  hazard entirely rather than documenting around it. **Flagged explicitly
  for Human Review, not silently locked** — see the related Spec's
  Decisions Requiring Human Review.
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
- **Deterministic slot reuse: the free list is a LIFO stack.** The most
  recently destroyed slot's index is the first index a following
  `createEntity()` call reuses (an ordinary `std::vector`-backed
  push/pop, not a `std::set`/hash-based free set whose own iteration or
  extraction order would be an unspecified implementation detail). This
  makes index assignment (and therefore every downstream ordering
  question — see "Deterministic enumeration order" below) a pure,
  reproducible function of the exact sequence of `createEntity()`/
  `destroyEntity()` calls a caller makes, never dependent on container
  internals a caller cannot observe or reason about.
- **Deterministic enumeration order.** Any World API that enumerates more
  than one entity (the related Spec's own `renderableEntities()`, and any
  future equivalent) iterates **in ascending slot-index order** — a
  total, well-defined order for any given World state, not "whatever the
  underlying container's own iteration happens to produce." Combined with
  the LIFO free-list rule above, this makes multi-entity enumeration a
  fully deterministic, reproducible function of the exact sequence of
  World mutations — required so a multi-entity image-regression golden
  (the related Spec's own Decisions Requiring Human Review, item 9) never
  depends on unspecified container order for its own reproducibility.
- **Every mutating World operation is atomic: full success, or
  `Result::Err` with zero mutation.** `destroyEntity()`, `setParent()`,
  `setLocalTransform()`, `setCamera()`/`removeCamera()`/
  `setActiveCamera()`, and `setRenderable()`/`removeRenderable()` each
  validate every precondition (handle validity, and — for `setParent()`
  — the cycle check; see
  [ADR-0050](0050-transform-hierarchy-composition-and-update-model.md))
  **before** performing any state change, and change nothing at all on
  the `Result::Err` path. A caller that receives an error can always
  assume World is exactly as it was immediately before that call — never
  a torn, partially-applied state to reason about or roll back manually.

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
- `EntityId` is a small, trivially-copyable, comparable value (16 bytes
  with the widened 64-bit generation, double the 8 bytes a plain 32-bit
  generation would cost) — still cheap to store in `std::vector`, pass by
  value, and use as a map key (e.g. Runtime's own AssetId→Mesh resource
  table, or a future test's own bookkeeping), with no ownership or
  lifetime complexity of its own.
- The LIFO free-list and ascending-slot-index enumeration rules make
  every multi-entity ordering question (which entity a traversal visits
  first, second, ...) a specified, reproducible property of World's own
  mutation history — not an accident of `std::unordered_map`/`std::set`
  iteration a future refactor of World's own internals could silently
  change out from under a consumer (in particular, the related Spec's own
  multi-entity image-regression golden).

### Negative / Trade-offs

- Every World mutation/query pays a small, constant-time validation cost
  (index-range check, generation compare) — not a performance concern at
  this Spec's own scale (a handful of entities), but a real, disclosed
  per-call cost this ADR accepts rather than an unchecked raw-index
  fast path.
- The widened, 64-bit generation field doubles `EntityId`'s own size
  relative to a 32-bit generation (16 bytes vs. 8) — a small, disclosed
  memory/copy cost accepted in exchange for closing the wraparound risk
  for any plausible process lifetime (see Decision) rather than merely
  disclosing it as accepted risk.
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
- **Handle validation via `ATLANTIS_ASSERT`, trusting callers in
  Release.** Rejected, with a concrete, verified reason rather than a
  general appeal to AGENTS.md's own Programmer-error/Result split alone:
  `src/core/include/atlantis/assert.h` compiles `ATLANTIS_ASSERT` to
  `((void)0)` — the condition unevaluated — whenever `NDEBUG` is defined,
  i.e. in every Release build. Choosing it here would silently remove
  stale-handle detection in exactly the build configuration a real
  product ships, turning a detectable, recoverable condition into the
  same silent-aliasing hazard a raw index (no generation at all) already
  has, but only in Release — a worse, environment-dependent failure mode
  than either "always detect" (this ADR's own `Result` choice) or
  "never detect" (the raw-index alternative above) would be on their own.
- **Handle validation via `ATLANTIS_CHECK` (always evaluated, both
  configurations), aborting the process on a stale handle instead of
  returning a `Result`.** Considered as the one alternative that does not
  share `ATLANTIS_ASSERT`'s Release-mode blind spot — `ATLANTIS_CHECK` is
  evaluated in both Debug and Release. Rejected anyway: its own failure
  behavior (`reportFailure()` → `ATLANTIS_LOG_FATAL` → `std::abort()`)
  terminates the entire Runtime process for a condition that, in this
  Spec's own single-threaded, single-owner model, is often ordinary
  caller bookkeeping (one part of a frame's composition code destroying
  an entity another, unrelated part of the same frame still holds a
  handle to) rather than a violated memory-safety invariant — the kind of
  condition AGENTS.md reserves assertions for. A `Result` lets the caller
  choose its own response (skip an entity and log, fail just that frame,
  or genuinely treat it as fatal and abort itself) rather than World
  unilaterally deciding "this is always fatal" on every caller's behalf.
- **Permanently retire a slot's index once its generation counter nears
  overflow**, instead of widening the generation field. Considered as an
  alternative way to close the same wraparound gap without growing
  `EntityId`'s own size. Rejected in favor of widening to a 64-bit
  generation: retiring slots requires extra bookkeeping (an additional
  near-overflow check on every `destroyEntity()` call, and permanently
  shrinking the usable index space over a long enough run) to guard
  against a risk a 64-bit counter already makes practically unreachable
  (see Decision's own `2^64`/57-years-at-10-billion-cycles-per-second
  reasoning) — the simpler fix for a risk this remote.
