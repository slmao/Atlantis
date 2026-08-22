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
- **Revision 2 (2026-08-22, targeted correction, two points):**
  (1) the prior revision's own "64 bits makes wraparound practically
  unreachable" argument was a probabilistic mitigation, not a formal
  closure of the risk — added an explicit, unconditional rule (permanent
  slot retirement at the generation counter's maximum value) that
  guarantees no historical handle can ever become valid again, regardless
  of cycle count, superseding the prior "considered and rejected" verdict
  on retirement — see Decision's own "Generation width and overflow
  behavior, formally closed" and the reworked Alternatives Considered.
  (2) Re-classified stale-handle detection more precisely against
  [AGENTS.md](../AGENTS.md)'s own Programmer-error/`Result` model: a
  non-owning handle legitimately issued by World, later invalidated by an
  ordinary (explicit or cascading) destruction, is a normal, observable
  runtime state by `EntityId`'s own defined contract — not a violated
  precondition — which is why it is a `Result`; an internal generation/
  slot-bookkeeping inconsistency (a bug in World's own implementation)
  remains a `Result`-would-be-wrong, assertion-only case. The prior
  revision's `assert.h` Release-mode-compile-away observation is retained
  only as supporting, secondary evidence, not the primary reason — see
  Decision's own reworked justification. No change to this ADR's
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
- **Generation width and overflow behavior, formally closed — two
  complementary mechanisms, not one.** A 64-bit generation counter alone
  is a **probabilistic** mitigation: it makes wraparound (a slot's
  generation counter cycling back to a value a still-held historical
  handle already carries) astronomically unlikely — `2^64 ≈ 1.8×10^19`
  destroy/reuse cycles on one slot, or roughly 57 years of continuous
  execution even at a sustained, unrealistic 10 billion cycles per second
  — but "astronomically unlikely" is not the same claim as "impossible by
  construction," and this ADR does not overstate it as such. **The rule
  that actually, formally closes the risk — unconditionally, not merely
  reduces its probability — is permanent slot retirement at the
  generation counter's maximum representable value:**
  `std::numeric_limits<std::uint64_t>::max()` is reserved as a tombstone
  value, never assigned to any live entity. When `destroyEntity()`
  increments a slot's generation and the result equals that tombstone
  value, the slot is marked permanently dead and its index is **not**
  returned to the free list — that index can never be allocated by
  `createEntity()` again, for the remaining lifetime of the `World`
  instance. Because every live entity at that index, for its entire
  history, held a generation strictly less than the tombstone value (the
  tombstone is reached only as the *result* of the increment that retires
  the slot, never assigned to anything that was ever live), **no
  `EntityId` that was ever legitimately issued for that index can, by
  construction, ever match a live entity again** — not "vanishingly
  unlikely to," *cannot*, for any generation width, including a much
  narrower one. This needs no new public API, no new `WorldError`
  enumerator, and no change to any validation code path: `isValid()`/
  every `EntityId`-accepting API already checks "index in range and
  generation matches the slot's current generation" — a retired slot
  simply never matches any handle again, exactly the same check already
  in place. The 64-bit width's own role is now precisely stated: it makes
  actually reaching the tombstone (and therefore losing that one index
  from the reusable pool) so rare it is not a practical concern for this
  Spec's own process lifetime, while the retirement rule is what makes
  the *safety property itself* — no historical handle ever revalidates —
  hold unconditionally rather than merely with very high probability. See
  Consequences for the one real, disclosed cost of the retirement rule
  (a permanently shrinking index-reuse pool, in the practically
  unreachable case it ever triggers), and Alternatives Considered for why
  retirement alone, at a narrower generation width, was not chosen
  instead of both mechanisms together.
- `createEntity()`'s own "never fails" contract (below) is unaffected by
  retirement: a retired index is simply excluded from the free list; if
  every existing index has ever been retired (requiring the practically
  unreachable case above at every one of them), `createEntity()` still
  succeeds by growing the underlying storage and allocating a new index,
  exactly as it already does whenever the free list is empty for any
  other reason.
- **Every public World API that accepts an `EntityId` validates it**
  (index in range **and** generation matches the slot's current
  generation) before acting, and returns
  `atlantis::Result<T, WorldError>` with `WorldError::InvalidEntity` on
  failure — never undefined behavior, never a silent no-op, never a
  crash.
  **Classification, aligned precisely with [AGENTS.md](../AGENTS.md)'s
  own Programmer-error/`Result` split — two categorically different
  failure sources, not one, given two different treatments:**
  - **A stale/expired `EntityId` — the case this ADR's `Result` covers —
    is a normal, observable runtime state `EntityId`'s own contract
    already defines, not a violated precondition.** `EntityId` is
    explicitly a **non-owning** handle (see "World never returns a
    reference or pointer..." below): World never promises, and the
    handle's own type never claims, that the entity it names stays alive
    for as long as the handle is held. Any code with a legitimate,
    successful call to `destroyEntity()` — on the target entity directly,
    or transitively via a cascading destroy of an ancestor (ADR-0050) —
    is *correct*, ordinary use of World's own public API; it is not a bug
    for that call to happen while some other, unrelated code still holds
    an `EntityId` for the entity that call just destroyed. Querying that
    now-stale handle afterward is therefore not "the caller violated a
    precondition it should have upheld" — it is "the caller asked a
    well-defined question (`isValid()`/any accessor) about state that
    legitimately changed since the handle was captured," directly
    analogous to `std::weak_ptr::lock()` returning `nullptr` after the
    object it named was legitimately destroyed elsewhere: a normal,
    expected, `Result`/optional-shaped outcome of correct code, not a
    programmer error requiring a fail-fast abort. This is why
    `WorldError::InvalidEntity` is the right shape for this case, per
    [AGENTS.md](../AGENTS.md)'s own rule that "recoverable runtime errors
    use explicit result/error types" — extended here to the new World
    module for the first time.
  - **An internal generation/slot bookkeeping inconsistency — a bug in
    World's own implementation, not a caller mistake — remains squarely
    an assertion/`ATLANTIS_CHECK` matter, never a `Result`.** If World's
    own internal invariant (a slot's alive flag and generation counter
    always move together, in the exact sequence `destroyEntity()`/
    `createEntity()` define) is ever violated by a defect in World's own
    code — e.g. a slot marked alive with a generation that does not match
    what the last `destroyEntity()`/`createEntity()` pair should have set
    — that is a violated invariant of World's own correctness, exactly
    the "violated precondition/invariant fails fast" case
    [AGENTS.md](../AGENTS.md) reserves for assertions. This is a
    genuinely different failure source than an external caller holding a
    stale handle: the caller did nothing wrong in either case, but only
    the second case indicates World itself is broken. World's own
    internal validation code (the "index in range and generation
    matches" check itself, and the update traversal's own defense-in-
    depth revisit guard — ADR-0050) is implemented once, correctly, and
    is not expected to fail from correct World code; if it ever
    disagreed with World's own bookkeeping in a way that indicates
    corruption rather than an ordinary stale handle, that would be a
    `ATLANTIS_CHECK`-worthy internal-invariant failure, not folded into
    `WorldError`.
  - **Supporting, secondary evidence** (not the primary reason a `Result`
    is correct here, which is the classification above): re-checked
    directly against `src/core/include/atlantis/assert.h` during this
    ADR's own evidence pass, `ATLANTIS_ASSERT` compiles to `((void)0)` —
    the condition unevaluated — whenever `NDEBUG` is defined, i.e. every
    Release build, so choosing it for the *external* stale-handle case
    above would additionally have silently disabled detection in exactly
    the configuration a real build ships — reinforcing, not founding, the
    classification-based conclusion. **Flagged explicitly for Human
    Review, not silently locked** — see the related Spec's Human Review
    Decision Table, item 3.
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
  for Human Review, not silently locked** — see the related Spec's Human
  Review Decision Table, item 4.
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
  (the related Spec's own Human Review Decision Table, item 14) never
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

- Stale-handle use (using an `EntityId` after its entity was legitimately
  destroyed) is reported clearly and recoverably — a
  `Result::Err(WorldError::InvalidEntity)` the caller must handle —
  instead of silently corrupting an unrelated, slot-reused entity's
  state, matching what `EntityId`'s own non-owning contract already
  promises rather than treating an expected outcome as a crash.
- The permanent-retirement rule makes "no historical handle ever becomes
  valid again" an unconditional, formally closed property of `EntityId`
  — not a probabilistic one resting on the generation counter's width
  alone — while adding no new public API surface, no new `WorldError`
  enumerator, and no change to any validation code path (see Decision).
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
  memory/copy cost, now justified as reducing how often the retirement
  rule below would ever practically engage, not as the mechanism that
  itself closes the risk (see Decision).
- The retirement rule permanently removes one index from the reusable
  pool the one time (per slot, if ever) its generation counter reaches
  the tombstone value — a real, disclosed cost in the case it triggers
  (a permanently, slightly smaller reusable-index pool), accepted because
  triggering it at all requires the same practically-unreachable cycle
  count the 64-bit width's own reasoning already establishes, and because
  `createEntity()` remains non-fallible regardless (it grows storage
  instead — see Decision).
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
- **Handle validation via `ATLANTIS_ASSERT`/`ATLANTIS_CHECK` for the
  external stale-handle case**, treating it the same as a programmer
  error. Rejected on classification grounds first (see Decision's own
  "Classification, aligned precisely with AGENTS.md" note): a stale
  `EntityId` produced by a legitimate, correct `destroyEntity()` call
  elsewhere is not a violated precondition the holder of the handle could
  have prevented — it is a normal consequence of `EntityId`'s own
  documented non-owning contract, the same category of outcome as
  `std::weak_ptr::lock()` returning `nullptr`. Treating it as an assertion
  would also have two further, concrete problems, kept here as
  reinforcing (not primary) evidence: `ATLANTIS_ASSERT` compiles to
  `((void)0)` whenever `NDEBUG` is defined, silently disabling detection
  in every Release build; `ATLANTIS_CHECK` would abort the entire Runtime
  process for what the classification above already establishes is
  ordinary, correct-code-triggered caller bookkeeping, not evidence
  World itself is broken — a severity mismatch even setting the
  classification argument aside.
- **Widen the generation field alone, without also adding permanent
  retirement at the maximum value.** This ADR's own first revision took
  this position. Reconsidered: a wider field only reduces the
  *probability* of wraparound, it does not make "no historical handle
  ever revalidates" true by construction — retirement is what actually
  provides that guarantee, at any generation width, so omitting it would
  leave the stated safety property resting on an unstated probabilistic
  assumption. Retained the 64-bit width anyway (not reverted to 32-bit)
  because it makes actually reaching the tombstone value practically
  unreachable, minimizing how often the one real cost of retirement (a
  permanently shrunk reusable-index pool for that slot) could ever occur
  — see Decision.
- **Retire a slot's index once its generation counter *nears* overflow**
  (a proactive, early cutoff before the true maximum), rather than
  retiring exactly at the maximum representable value. Rejected: retiring
  early discards usable generation values for no safety benefit — the
  maximum value itself is never assigned to a live entity by construction
  (it is reached only as the *result* of the increment that performs
  retirement), so waiting until exactly that value is reached loses
  nothing and keeps the rule simple (a single equality check after each
  increment, no separate "how close to the edge" threshold to choose or
  justify).
