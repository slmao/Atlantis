# Spec: Runtime World Transaction

- **Status:** In Review
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-07
- **Related Plan(s):** none yet. Drafting a Plan is authorized only after this
  Spec's Approval.
- **Approval:** pending. The maintainer fixed this Spec's scope and
  boundaries before drafting (2026-10-07, chat). They are recorded under
  Goals / Non-Goals and are not open questions:
  - v1 is atomic commit / rollback of a group of commands only;
  - no new verbs;
  - Spec 0052's contracts are unchanged;
  - Undo/Redo and the rest are named only.
- **Related ADR(s):**
  [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) (`Proposed`,
  drafted alongside this Spec). It records the atomicity mechanism and why the
  undo-log and snapshot alternatives were not chosen. It extends
  [ADR-0103](../adr/0103-runtime-world-operation-boundary.md) D9, which named
  atomicity as this Spec's, and changes no ADR-0103 decision.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

A **transaction** is a group of Spec 0052 commands that applies **all or
nothing**:

- **Commit:** every command takes effect, in order, and each emits its usual
  event.
- **Abort:** if any command would be refused, none takes effect, no event is
  emitted, and the failure names the first refused command.

The transaction is a grouping over Spec 0052's operation boundary. It is
built from the same five commands, with no new verb. Spec 0052's contracts
are untouched:

- single-command `submit()`;
- one event per successful command, none for a failure;
- the `applyPending()` application point;
- the validation and crash-guard set.

The recommended mechanism is **projected pre-validation**. The whole group
is validated against a projection of the world as each command would leave
it, and executed only if every command passes. An aborted transaction never
touched the world, so nothing needs undoing.

## Motivation / Problem Statement

### Why atomicity was deferred to here

- **Plan 0050 J3.** The ECS `CommandBuffer::apply()` continues past a failed
  command. All-or-nothing was set aside because it "needs a rollback that v1
  does not otherwise need".
- **Spec 0052 ruling Q9 (B-a).** The Runtime World boundary applies commands
  "ordered, per-command apply-or-fail", stating that "atomicity and undo are
  Transaction's (0053)". ADR-0103 D9 records the same.

### What a client cannot do today

A client cannot express an edit that is only meaningful as a whole. The
simplest example is adding a Point light, which takes several commands:

1. `CreateEntity(g)`;
2. `AddComponent(Light)`;
3. `SetProperty(kind = Point)`;
4. `AddComponent(WorldMatrix)`;
5. `SetProperty(WorldMatrix …)`.

Under ruling Q9, if step 4 is refused (the 65th Point light), steps 1–3
have already happened: a half-built entity is left in the world. The client
must clean up with more commands, which can themselves fail. An Editor's
"apply this change", or an Agent's multi-step edit, needs the group to
succeed or leave no trace.

### Why "no trace" is cheap here

Spec 0052's implementation (merged in PR #216) already holds two
invariants:

- **Per-command validation before any ECS call.** Every check runs before
  the command's first ECS call, so a refused command has no effect
  (`src/world/src/access/runtime_world_access.cpp`, `Impl::apply` overloads,
  `:110-:230`).
- **Validation reads only a small amount of world state:**
  - whether a GUID names a live entity (the GUID index plus `isValid`);
  - which components an entity has;
  - the counts of Directional and Point lights among Light + WorldMatrix
    holders (`countLights` / `lightFits`, `:95-:108`);
  - whether an entity is the active camera (`:82`).

  Field resolution and value checks (`resolveField`, `checkValue`) are
  stateless.

That state can be projected command by command without touching the ECS
(Q1).

## Goals

These are maintainer-fixed and are not to be relaxed in review:

- **v1 is atomic commit and rollback of a group of commands, nothing
  more.** Undo/Redo is named, not built. Maintainer's words: "不需要第一版就完整实现 Undo/Redo"
  (the first version need not implement Undo/Redo completely).
- **A transaction is grouping above Spec 0052's boundary.** It adds no
  verb, which guards against the surface creep Spec 0052's Risks named. The
  five commands are grouped as they are.
- **Spec 0052's contracts are unchanged:**
  - single-command `submit()` semantics;
  - events: one per successful command, none for a failure;
  - the `applyPending()` timing;
  - the guard set: type correctness, finite floats, and the three crash
    guards.

  None is widened by transactions.
- **Named only — not designed or scaffolded:**
  - Undo/Redo;
  - nested transactions;
  - cross-process batching (0054);
  - an entity-listing query (left over from Spec 0052);
  - an address text form (0054).

Goals of this Spec within those boundaries:

- **All or nothing.** A committed transaction's effect equals applying its
  commands one by one with none refused. An aborted transaction has no
  effect, no event, and one reported failure.
- **Determinism.** Whether a transaction commits depends only on the world
  state when it applies and on its commands.
- **Cost proportional to the transaction**, not to the world.

## Non-Goals

These are maintainer-fixed:

- Undo/Redo, nested transactions, cross-process batching, an entity-listing
  query, an address text form.
- New commands, new events, new validation rules, or a changed application
  point.

Also out of scope:

- **Concurrency control or isolation levels.** Single-threaded (ADR-0004).
  Commands from one client apply in submission order.
- **A transaction spanning frames, or kept open across `applyPending()`
  calls.** A transaction is submitted whole.
- **Partial commit or savepoints.**
- **Any change** to the ECS core, Runtime's frame, assets, shaders or goldens
  (Q6).

## Requirements

### Functional

- **R1 — Grouping without new verbs.** A transaction is an ordered list of
  Spec 0052 `Command`s, submitted through one transaction entry point (Q2).
  It applies at the owner's `applyPending()`, at its position in the
  submission order relative to single commands.
- **R2 — Atomic commit** (Q1). If every command in the list would be
  accepted by Spec 0052's rules, applied in order against the world as the
  earlier commands leave it, all are applied. The resulting world equals
  applying them one by one.
- **R3 — Atomic abort** (Q1). Otherwise none is applied: the world, the GUID
  index and every queue except the failure queue are exactly as before the
  transaction.
- **R4 — Failure reporting** (Q2). An abort reports the **first** refused
  command, with its `AccessError` and its position in the transaction.
- **R5 — Events** (Q3).
  - **On commit:** one event per command, in order, identical to Spec
    0052's events for those commands.
  - **On abort:** none.
- **R6 — Unchanged single-command path** (Q2). `submit(Command)` keeps Spec
  0052's per-command semantics (ruling Q9), events and failures exactly.
- **R7 — Same rules** (Q7). The validation a transaction is held to is Spec
  0052's validation, the same rules from the same source, applied to
  projected state. No rule is added, removed or relaxed.
- **R8 — Special cases** (Q4). The cases ruled in Q4 hold:
  - a destroyed entity within a transaction;
  - destroy then re-create of the same GUID;
  - the active-camera guard mid-transaction;
  - an empty transaction.

### Non-functional

- **Performance:** a transaction of *n* commands costs O(*n* log *n*) plus
  one pass over the lights (the guard counts, as today). No copy of the
  world or of any component storage.
- **Memory:** projected state is O(entities the transaction touches).
- **Threading:** single-threaded (ADR-0004), on the frame thread, as in
  Spec 0052.
- **Portability:** Windows and Android, same code.
- **Dependencies:** none added.

## Proposed Design

```
submitTransaction([c1 … cn]) ──► pending (one entry, in submission order)
applyPending():
   for each pending entry:
     single command ─► Spec 0052 path (unchanged)
     transaction    ─► 1. project:  validate c1…cn against projected state
                                    (rules = Spec 0052's, Q7); stop at first refusal
                       2a. refused ─► record one failure (first refused, its position); nothing else
                       2b. all ok  ─► execute c1…cn for real, in order; each now cannot fail;
                                     append their n events
```

**Projected state.** The state Spec 0052's validation reads, as the
commands so far would leave it:

- **GUID → entity:** existing (with its real `EntityId`), projected-created,
  or projected-destroyed;
- **Per projected entity:** its component set, and its `Light.kind` if it
  has a `Light`;
- **The two light counts** over Light + WorldMatrix holders;
- **The active camera's entity** (fixed by the bake).

Projected state is seeded lazily from the real world as a command first
touches an entity. The light counts are taken once at the start (one query,
as `countLights` does today).

**Execution after a successful projection** runs Spec 0052's per-command
application. Because projection and execution apply the same rules (Q7) to
the same starting state and order, execution cannot be refused. A refusal
there is a programming error (`ATLANTIS_CHECK`), never a client-reachable
partial commit.

## Architectural Impact

Yes. Recorded in
[ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md), drafted
alongside:

- the transaction as all-or-nothing grouping over ADR-0103's five commands;
- the projected pre-validation mechanism, and why an undo log and an ECS
  snapshot were not chosen;
- a single source of validation rules;
- the entry point, failure and event semantics.

It extends ADR-0103 D9 ("atomicity is 0053's"). No ADR-0103 decision
changes, and Spec 0052's contracts are unchanged (Goals).

Expected code impact, for the Plan:

- `atlantis::world::access` (`runtime_world_access.h/.cpp`): the
  transaction entry and its projection. The validation code becomes one
  routine over "real" and "projected" state (Q7).
- New World tests.
- **No change** to:
  - the ECS core;
  - Runtime: `RuntimeApplication` already calls `applyPending()` first in
    `runFrame()`, and that call drains transactions too (Q6);
  - formats, shaders, goldens.
- **Possibly** `runtime_smoke_gpu_tests.cpp` (Q6).

## Alternatives Considered

See Q1 for the full comparison of the three atomicity mechanisms. Also
rejected:

- **New transaction verbs** (`Begin`/`Commit`/`Abort` commands interleaved
  in the command stream). Forbidden as surface creep, and they make a
  transaction span calls (Non-Goals).
- **Reinterpreting `submit()` batches as atomic.** That would change Spec
  0052 ruling Q9's contract.

## Testing & Verification Plan

- **Commit (R2, R5):**
  - transactions of each command kind, and mixed, including the Point-light
    group in the Motivation;
  - the world after commit equals the same commands applied one by one;
  - the events equal, one per command, in order.
- **Abort (R3, R4):** for each refusal kind that Spec 0052 can produce,
  placed first, middle and last in a transaction:
  - the world is byte-unchanged (every component of every entity, the GUID
    index, entity validity);
  - no event;
  - exactly one failure, naming the first refused command and its position.
- **Guards inside transactions:**
  - the 65th Point light and the second Directional light arising
    mid-transaction;
  - an earlier command in the same transaction that frees room (destroys a
    light) and lets a later one fit, or the reverse;
  - the active-camera guard mid-transaction.
- **Special cases (R8, Q4):**
  - commands after a projected destroy;
  - destroy then re-create of the same GUID;
  - an empty transaction.
- **Unchanged single path (R6):** every Spec 0052 test passes unchanged.
- **Ordering (R1):** single commands and transactions interleaved apply in
  submission order.
- **Same rules (R7, Q7):** for generated command sequences, the transaction
  verdict (commit, or the first refused index) equals the sequential Spec
  0052 verdict on an identical baked world. The "first refusal" of
  sequential application must coincide with projection's.
- **Runtime (Q6):**
  - full Debug + Release suites with every golden byte-identical;
  - Validation Layers clean;
  - Android `assembleDebug`;
  - per Q6, the smoke test's creation group as one transaction under fatal
    VVL.

## Risks & Open Questions

Risks:

- **Projection drift.** If projected and real validation ever diverged, a
  committed transaction could hit a refusal mid-execution. Q7's single
  source of rules and the sequential-equivalence test guard against it, and
  execution CHECKs it.
- **Long transactions.** A very large transaction is validated in full
  before any effect: O(*n*) work and memory, by design. There is no size
  limit in v1.
- **No undo.** A committed transaction cannot be reverted except by further
  commands; Undo/Redo is named only.

Open questions (to be ruled at review):

- **Q1 — The atomicity mechanism** (the central question).
  - **(a) Projected pre-validation.**
    - **How it works.** Validate the whole transaction against projected
      state, then execute only if all pass. Rollback is "never started".
    - **What it builds on.** Spec 0052's invariant that every check precedes
      any ECS call, and that a refused command has no effect. Its validation
      reads only the small state listed in the Motivation.
    - **Cost.** O(touched entities) shadow state, and no ECS change.
    - **Risk.** Projection must equal real validation (Q7).
    - **Identities.** None are disturbed: an aborted transaction never
      called the ECS, so no `EntityId` was created or destroyed.
  - **(b) Undo log.** Apply each command for real while recording its
    inverse, and replay the inverses on refusal.
    - **The inverse of `DestroyEntity` is the problem.** Under
      [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s
      rules (which ADR-0101 D4 adopts for `ecs::EntityId`), destroying an
      entity increments its slot's generation and frees its index. Undo can
      only `createEntity()` a new one: a different index, or the same index
      at a higher generation. A generation never goes back, and a retired
      slot never returns.
    - **Every handle to the original then dangles:**
      - the bake's immutable `EntityGuidMap` snapshot, through which
        `resolveEntityRef()` resolves persisted references (ADR-0097 D6,
        Spec 0052 ruling Q1), so an "undone" authored entity would resolve
        as `DeadEntity` forever;
      - `BakedScene::activeCamera`;
      - Runtime's cached `activeCameraEntity_`.
    - **The other way out** is resurrecting the old `EntityId`, by
      restoring the generation. That would break ADR-0049's guarantee that a
      stale handle never aliases a new entity, and needs a new ECS API.
    - **It also replays all component values,** and an aborted transaction
      still churns archetypes and chunks (row moves) before reverting them.
  - **(c) ECS snapshot / copy.**
    - Copy the world, apply to the copy, and swap on success.
    - `ecs::World` is non-copyable (`world.h:69`), so this needs a clone
      API in the ECS core.
    - The cost is O(world) per transaction, even for one command.
    - A clone carries a new per-instance identity token (ADR-0049
      Amendment / ADR-0101 D4). Every `EntityId` held outside the world (the
      `EntityGuidMap`, `activeCamera`, the boundary's index) would be
      foreign to the swapped-in world, unless identity tokens become
      transferable, a further ADR-0049 change.
  - **Recommendation: (a).** It is the only option that needs no ECS change,
    leaves every identity untouched on abort, and costs in proportion to the
    transaction. Its one risk, drift, is closed by Q7. (b) collides with
    ADR-0049 at `DestroyEntity` and is the natural basis for Undo/Redo, so
    it belongs to that future Spec, where a durable inverse log is the
    feature. (c) is the heaviest option and needs identity changes.
- **Q2 — Relation to the single-command path, and failure reporting.**
  - **Entry point:**
    - (E1) `submit(Command)` is kept exactly. A new
      `submitTransaction(std::vector<Command>)` returns a `TransactionTicket`
      (`{ first CommandTicket, count }`); the transaction's commands take
      consecutive command tickets.
    - (E2) Internally, every `submit()` becomes a one-command transaction.
      This is observably identical, since a single command is already
      all-or-nothing, but it changes the implementation of the Spec 0052
      path.
  - **Failure reporting:**
    - (F1) An abort appends one `CommandFailure` for the first refused
      command: its own ticket and error. Its position is `ticket −
      first`. No new failure type or error kind.
    - (F2) A new `TransactionFailure { TransactionTicket, index, error }`
      on a separate drain.
  - **Recommendation: E1 + F1.**
    - Spec 0052's path stays byte-for-byte the code it is today (R6).
    - Clients keep one failure queue. A failure whose ticket falls in a
      transaction's range means that transaction aborted. Its other
      commands report nothing, because they were not refused.
    - E2 may still be how the Plan shares code, provided R6 holds.
- **Q3 — Event semantics.**
  - **Committed transaction:** its commands' events, one per command, in
    order, identical to Spec 0052's.
  - **Aborted transaction:** none.
  - No transaction-level event: Spec 0052's event set is fixed.
  - **Recommendation:** as stated; this is offered for confirmation. Its
    alternative, "commit/abort marker events", would add event kinds, which
    is forbidden by the Goals.
- **Q4 — Special cases within a transaction:**
  - **(4a) Commands after a projected `DestroyEntity(g)`.** They see `g` as
    unknown, exactly as sequential application would. A later command on
    `g` is refused (`UnknownEntity`), which aborts the transaction.
  - **(4b) Destroy then `CreateEntity(g)` with the same GUID.**
    - Options:
      - (i) allowed, as in Spec 0052, where a destroyed GUID may be reused;
        `g` then names the new entity for the rest of the transaction;
      - (ii) refused within one transaction.
    - **Recommendation: (i).** It is what the same commands do one by one
      (R2, R7). Refusing it would make a transaction's rules differ from
      Spec 0052's.
  - **(4c) The active-camera guard mid-transaction.** A command refused by
    the guard aborts the whole transaction, wherever it appears.
    - Nothing later in the same transaction could restore the active
      camera: `BakedScene::activeCamera` is fixed by the bake, and no
      command re-designates it. So there is no "temporarily violated"
      state to allow.
    - **Recommendation:** the guard is evaluated per command, as in Spec
      0052. No end-of-transaction validation and no deferred guards. Guards
      that would compare the transaction's end state, rather than each
      step, would be new validation (Goals).
  - **(4d) An empty transaction.**
    - Options:
      - (i) accepted as a no-op: no event, no failure, and it consumes no
        command ticket;
      - (ii) refused.
    - **Recommendation: (i).** It is the natural identity, and refusing it
      would need a new error kind.
- **Q5 — Module and ADR.**
  - The transaction stays in `atlantis::world::access`, as a grouping
    extension of ADR-0103's boundary (`RuntimeWorldAccess`).
  - [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) records
    the mechanism and the three-way trade-off.
  - Alternative: a separate `TransactionBuilder` type. It would hold no
    state the boundary does not already have.
  - **Recommendation:** a method pair on `RuntimeWorldAccess` (Q2's E1), and
    ADR-0104.
- **Q6 — The v1 consumer, and Runtime.**
  - **Verified:** `RuntimeApplication` already calls
    `worldAccess_->applyPending()` as the first statement of `runFrame()`
    (Spec 0052 J6). Transactions enter the same pending queue, so
    **Runtime needs no code change**.
  - **Consumer options:**
    - (C1) World tests only;
    - (C2) also move `runtime_smoke_gpu_tests`' Point-light creation group
      (nine commands) into one transaction. This proves the atomic path on
      Runtime's real frame under fatal VVL, the Spec 0052 H-a precedent.
  - **Recommendation: C2.** It is a test-only change, and it is the case
    the Motivation names.
- **Q7 — One source of validation rules** (surfaced while drafting; it
  closes Q1's risk).
  - (V1) Spec 0052's per-command validation is factored into one routine
    over a state view. Two implementations exist:
    - real: the GUID index plus the ECS;
    - projected: the shadow state.

    Projection and execution use the same code.
  - (V2) The projection reimplements the checks, with execution's CHECK as
    the backstop.
  - **Recommendation: V1.** Drift becomes a compile-time impossibility for
    the rules themselves; only state bookkeeping can differ, and the
    sequential-equivalence test covers that. V2 duplicates every rule and
    every future rule.

## Out of Scope / Future Work

- **Named only (maintainer list):**
  - Undo/Redo, the natural home of an inverse log (Q1 (b));
  - nested transactions;
  - cross-process batching and the transport (0054);
  - an entity-listing query;
  - an address text form (0054).
- **Roadmap context.** The maintainer's roadmap (0054 RuntimeConnection and
  later) is context only; this Spec makes no commitment to it.
- **Further work:**
  - a transaction size limit;
  - transactions spanning frames;
  - savepoints.
