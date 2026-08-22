# ADR 0049: Entity Identity and Handle Invalidation

- **Status:** Accepted. **A new "Proposed Amendment" section below
  (2026-08-22, cross-`World`-instance `EntityId` use) is `Proposed`, not
  `Accepted` — it does not change this ADR's own status or any decision
  above it; see that section and
  [plans/0014-world-scene-foundation.md](../plans/0014-world-scene-foundation.md)'s
  own Deviations for why Plan 0014's Human Review Approval is blocked on
  it.**
- **Date:** 2026-08-22
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-22 as part of Spec 0014's Human Review Approval
- **Related Spec:** [specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md)
- **Acceptance Record (2026-08-22):** Accepted by Human Review as Human
  Review Decision Table items 2 (`EntityId` shape, generation width, and
  overflow behavior), 3 (stale-handle `Result` classification), and 4
  (by-value accessor access) of
  [specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md)'s
  own Human Review Approval (2026-08-22), which additionally accepted this
  ADR's own deterministic slot-reuse/enumeration-order contract explicitly
  (not a separately numbered table row, but confirmed load-bearing for
  item 14's own image-regression reproducibility) — see that Spec's own
  approval note for the full record. This record does not change this
  ADR's own Decision, Consequences, or Alternatives Considered below.
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

## Proposed Amendment (2026-08-22, Plan 0014 Independent Review Round 2) — Cross-`World`-instance `EntityId` use

**Status: Proposed — not yet Accepted. This section does not change any
`Accepted` Decision, Consequence, or Alternative above; it adds a new,
previously-undecided precondition. [plans/0014-world-scene-foundation.md](../plans/0014-world-scene-foundation.md)'s
own Human Review Approval is blocked on this section reaching Accepted,
per that Plan's own Independent Review Round 2.**

**Human Review direction (2026-08-22):** of the two options originally
proposed below, Human Review **rejected Option A** (documented,
unenforceable UB) and **directed Option B**, with a specific mechanism:
a stable, per-`World` identity **token** — not a global, process-wide
incrementing instance counter. Option A's own text is retained
unmodified immediately below for the historical record of what was
considered and rejected, per this repository's own "history stays
intact" convention for ADRs; Option B is expanded below it into the full
concrete design Human Review directed. This section remains `Proposed`
— not `Accepted` — until a subsequent, explicit Human Review Approval
note is recorded.

### Context

Plan 0014's second Independent Review round asked a question this ADR's
existing Decision never states an answer to: is an `EntityId` obtained
from one `World` instance valid — and if not, *reliably rejected* — when
passed to a **different**, independently constructed `World` instance?

Every guarantee this ADR's own `Accepted` Decision makes (stale-handle
detection, permanent retirement closing wraparound) is proven relative to
**one** `World` instance's own `slots_` mutation history. Nothing in the
Decision, and nothing in [Spec 0014](../specs/0014-world-scene-foundation.md)
or [ADR-0048](0048-world-scene-module-boundary-and-ownership.md), states
this as a precondition on `EntityId` itself. The only existing text
adjacent to this question — Spec 0014's and ADR-0048's identical
"Runtime owns the one real `World` instance" sentence — describes
Runtime's own composition choice in this round's scope (which module is
the sole Client observing/mutating `World`), not a stated precondition on
`EntityId`'s own public contract. Treating that sentence as sufficient
justification for `EntityId`'s own correctness would be exactly the
"current caller happens to only do X" reasoning that must not stand in
for a public module's own safety contract — flagged explicitly during
Plan Review, not assumed.

The gap is not a rare edge case comparable to generation wraparound. Two
freshly constructed `World` instances each hand out `{index=0,
generation=0}` for their own first `createEntity()` call — a handle from
one **validates against the other's own first entity by construction**,
on the single most common possible sequence (first entity in each), not
a statistically remote coincidence.

### Decision (proposed)

**Option A — NOT ADOPTED. Rejected by Human Review, 2026-08-22. Retained
below, unmodified, only for the record of what was considered — skip to
Option B for the adopted direction.**

Originally recommended by this Plan Review round: document
cross-`World`-instance `EntityId` use as an unenforceable precondition
violation — undefined behavior, not covered by `WorldError::InvalidEntity`
— with no change to `EntityId`'s 16-byte shape.

An `EntityId` is valid only for the exact `World` instance whose
`createEntity()` call returned it. Passing it to any `isValid()`/
`EntityId`-accepting call on a *different* `World` instance is a
violated caller precondition, not a condition `World`'s `Result`-based
API is required to detect: no instance-identifying information exists
anywhere in `EntityId` or `World` for such a check to compare against, so
unlike the same-instance stale-handle case (a legitimate, expected
outcome of correct code, correctly given a `Result` per this ADR's own
existing Decision), a cross-instance handle is squarely the "violated
precondition/invariant" category [AGENTS.md](../AGENTS.md) reserves for
assertions — except here no general, reliable assertion is possible
either, so the contract is stated as documentation only, the same
category as this codebase's other non-enforceable borrowed-reference/
lifetime preconditions (e.g. a reference outliving the object that owns
it). `World`'s own `Result`-based safety net remains exactly as strong as
the existing Decision already states for same-instance use; this
amendment narrows what it was ever claimed to cover, it does not weaken
it.

This keeps `EntityId` at 16 bytes (unchanged from the `Accepted`
Decision), requires no new `WorldError` enumerator, and requires no
change to `isValid()`'s existing check. The cost is a genuinely
undetectable misuse mode for any *future* consumer that legitimately
constructs more than one simultaneous `World` instance — none exists in
this Spec's own scope (Runtime constructs exactly one, for its entire
process lifetime; Spec 0014's Non-Goals exclude a second Client/process)
— explicitly flagged here for whichever future Spec first introduces one
(e.g. a Serialization/Stable-Identity Spec, or a Tool/Editor protocol
maintaining a staging `World`), not silently deferred without a record.

**Option B — ADOPTED DIRECTION (Human Review, 2026-08-22): a stable,
heap-allocated, address-stable `World` identity token, not a global
instance counter.**

Cross-`World`-instance `EntityId` misuse becomes **reliably** detected
(`Err(WorldError::WrongWorld)`), not merely documented as UB, without
introducing a process-wide global (no counter, no registry, no random
identifier, no new third-party dependency) and without giving up
`World`'s own move-constructibility or `EntityId`'s own value-type
simplicity.

**Mechanism.** Each `World` instance, at construction, allocates exactly
one small, opaque, empty marker object on the heap and holds it by
`std::unique_ptr`:

```cpp
// world.cpp -- private; never declared in any public header
struct WorldIdentity {};  // no data, no behavior; only its own heap
                           // address matters, as a per-instance token
                           // no other live World can ever share
```

```cpp
// world.h
namespace atlantis::world {
class WorldIdentity;  // opaque forward declaration only -- EntityId
                       // holds a pointer to it, never a complete object,
                       // never dereferences it

class World {
 public:
  World();
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept;
  World& operator=(World&&) = delete;
  // ... rest of public API ...
 private:
  std::unique_ptr<WorldIdentity> identity_;
  std::vector<Slot> slots_;
  std::vector<std::uint32_t> freeList_;
  std::optional<EntityId> activeCamera_;
};
}  // namespace atlantis::world
```

`World`'s default constructor and destructor are declared in `world.h`
but **defined in `world.cpp`** (`= default` bodies suffice) — not
inlined in the class body — because `std::unique_ptr<WorldIdentity>`'s
own construction (`std::make_unique<WorldIdentity>()`) and destruction
both require `WorldIdentity`'s complete definition, which is deliberately
private to `world.cpp` and never exposed in any public header. This is
the standard, well-established C++ idiom for an opaque-pointer member (a
"pimpl"-adjacent pattern), not a new mechanism this codebase invents.

**Why this satisfies "heap-allocated, address-stable, opaque" together,
and why that combination is what makes move-then-still-valid work.** A
`unique_ptr`'s own move operation transfers ownership of the *same*
underlying heap block — the `WorldIdentity` object's own address never
changes across a `World` move, only which `unique_ptr` (and therefore
which `World` C++ object) owns it. An inline, non-indirected token (e.g.
a plain member field with no heap allocation) would **not** have this
property — moving the containing `World` object would move the field's
own storage along with it, changing its address. The heap indirection is
what makes the token's own identity survive a `World`'s own object-
identity change across a move, exactly the property "old `EntityId`
stays valid after a move" requires.

**`EntityId` gains a third field:**

```cpp
// entity_id.h
namespace atlantis::world {
class WorldIdentity;  // forward declaration only

struct EntityId {
  std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
  std::uint64_t generation = 0;
  const WorldIdentity* worldIdentity = nullptr;

  friend bool operator==(const EntityId&, const EntityId&) = default;
};
inline constexpr EntityId kInvalidEntityId{};  // worldIdentity == nullptr
}  // namespace atlantis::world
```

The defaulted `operator==` now compares all three fields automatically —
satisfying "equality must include identity" with no separately written
comparison logic. `worldIdentity` is a plain observer pointer: `EntityId`
never dereferences it, never allocates or frees anything through it, and
owns nothing — unchanged in *kind* from `index`/`generation`, which were
already non-owning values. A pointer to a type with **zero fields**,
used **exclusively** for equality comparison and never dereferenced by
any caller, exposes no Runtime-owned state across the public boundary —
this is a materially different case from
[ADR-0033](0033-runtime-authority-and-client-boundary.md)'s own "no raw
pointer/reference to a Runtime-owned object" rule, which targets pointers
that grant access to live, mutable data; `WorldIdentity` has no data to
grant access to.

**Validation ordering — identity before slot/generation, with an
explicit carve-out for the sentinel:**

```cpp
Result<void, WorldError> World::validate(EntityId id) const {
  if (id.worldIdentity != nullptr && id.worldIdentity != identity_.get())
    return Result<void, WorldError>::Err(WorldError::WrongWorld);
  if (id.index >= slots_.size() || !slots_[id.index].alive
      || slots_[id.index].generation != id.generation)
    return Result<void, WorldError>::Err(WorldError::InvalidEntity);
  return Result<void, WorldError>::Ok({});
}
```

The `id.worldIdentity != nullptr` guard is deliberate: `kInvalidEntityId`
(and any other default-constructed `EntityId`) carries `worldIdentity ==
nullptr`, which is never "a foreign instance's identity," only "no
claimed identity at all" — without this guard, the existing sentinel
would incorrectly report `WrongWorld` instead of its own existing,
unchanged `InvalidEntity` classification. A real handle from a
**different, live** `World` (non-null, non-matching pointer) correctly
reaches `WrongWorld`; every other case reaches the existing, unmodified
index/generation check exactly as before. `isValid(EntityId) const ->
bool` applies the same two-part check, collapsed to a boolean — its own
signature and existing callers are unaffected by which specific reason a
handle fails.

**`WorldError` gains a fourth enumerator:**

```cpp
enum class WorldError {
  InvalidEntity,
  WouldCreateCycle,
  NoCameraComponent,
  WrongWorld,  // EntityId belongs to a different, currently live World instance
};
```

**Lifetime remains a distinct concern `WrongWorld` does not, and cannot,
cover.** `WrongWorld` only helps when **both** `World` instances are
still alive — comparing `id.worldIdentity` against a live
`identity_.get()` is a well-defined pointer-value comparison. Using an
`EntityId` after **its own** `World` instance has been destroyed (its
`WorldIdentity` freed along with it) is not something any identity
scheme can detect: no mechanism can validate a call made against an
object that no longer exists. This remains exactly the borrowed-handle
lifetime precondition this codebase already establishes elsewhere
(AGENTS.md's own ownership/lifetime rules) — `EntityId` must not outlive
the `World` that issued it; violating this is undefined behavior, not a
`Result`, unchanged in kind from before this amendment.

**`World`'s own copy/move semantics, now load-bearing:** move-
constructible (the identity token and all state — `slots_`/`freeList_`/
`activeCamera_` — move together via ordinary `std::vector`/`unique_ptr`
move semantics, so a handle valid before the move remains valid after
it, against the moved-to instance, since `identity_.get()` returns the
same address before and after); **not** copyable (a copy constructor
would have to choose between sharing the source's own `identity_` token
— defeating the point, since two live `World`s would then validate the
same handles — or minting a fresh one, which would make every `EntityId`
copied over from the source silently `WrongWorld` against the copy; no
choice is right, so copying is deleted rather than answered with an
easy-to-get-wrong semantic); **not** move-assignable (would silently replace one live
`World`'s own identity and state with another's, freeing the original
token while handles issued against it may still be held — the same class
of hazard this amendment exists to close, reintroduced via assignment
instead of construction).

**Cost, honestly stated:** `EntityId` grows by one pointer-sized field
relative to the prior, `Accepted` 16-byte shape — the exact resulting
size is target- and alignment-dependent, an Implementation-time detail,
not a number this ADR fixes (see Spec 0014's own amendment). `World`
gains one small heap allocation per instance (a single
`sizeof(WorldIdentity) == 1`-byte block, in practice a single minimal
allocator block) and every validating call gains one additional pointer
comparison before its existing checks — both real, disclosed,
negligible-at-this-Spec's-scale costs, not evaluated against a future,
larger-scale entity count.

### Alternatives Considered (this amendment)

- **Option A — document as unenforceable UB, `EntityId` carries no
  identity at all.** Rejected by Human Review, 2026-08-22 — see Option A
  above for the full reasoning this amendment's own Decision already
  records; not repeated here.
- **A global, monotonically incrementing per-process `World`-instance
  counter** (e.g. a `static std::atomic<std::uint32_t>` or similar,
  assigning each `World` the next integer at construction). Rejected:
  Human Review explicitly directed against this shape. A global counter
  is process-wide mutable shared state outside any `World` instance's own
  ownership — exactly the kind of "process-wide singleton or global
  mutable database" this Spec's own module boundary
  ([ADR-0048](0048-world-scene-module-boundary-and-ownership.md)) already
  argues against for Asset resolution, now for the identical reason here;
  it also reintroduces a (much slower-growing, but nonzero) wraparound
  question of its own, and requires synchronization if `World` construction
  is ever not confined to one thread, neither of which the chosen
  per-instance heap token needs to consider at all.
- **`std::shared_ptr`/`std::weak_ptr`-based identity** (`World` holds a
  `std::shared_ptr<Something>` as its own token; `EntityId` holds the
  corresponding `std::weak_ptr`, `lock()`-ed and compared on validation).
  Rejected: pulls in reference-counting overhead and, more importantly,
  shared ownership semantics neither `World` nor `EntityId` needs or
  should have — `EntityId` remains a plain, trivially-copyable value type
  under the adopted design (a raw observer pointer, `memcpy`-safe, no
  atomic refcount touched on every copy); a `weak_ptr` member would make
  `EntityId` non-trivial to copy and add per-copy atomic-refcount cost to
  a value type this ADR's own existing Decision already establishes
  should stay cheap to store in `std::vector` and pass by value at high
  frequency (per-entity, per-frame). The adopted `unique_ptr`-owned,
  raw-pointer-observed design gets the same "stable address, owned by
  exactly one `World`" property without any of this cost.
- **Forbid constructing more than one `World` instance per process**
  (e.g. a runtime-checked singleton guard in `World`'s own constructor).
  Rejected: this does not answer the question this amendment exists to
  answer, it only makes the question unreachable *for this Spec's own
  process*, by fiat — Plan 0014's own Round 2 review explicitly rejected
  "only one instance exists today" as a sufficient justification for a
  public module's own correctness; a runtime-enforced singleton restatement
  of the same fact would not change that. It would also foreclose,
  without any stated reason tied to a real requirement, legitimate future
  uses this Spec's own Non-Goals never actually excluded (an in-process
  second, staging `World` for tooling or diffing) — a new, un-costed
  restriction speculatively added to solve an identity problem the chosen
  design solves directly instead.

### Disposition

Pending Human Review's own explicit Approval note on this amendment's
final wording (direction already given, 2026-08-22 — see above). Until
this section is marked `Accepted`,
[Plan 0014](../plans/0014-world-scene-foundation.md) does not begin
Implementation.
