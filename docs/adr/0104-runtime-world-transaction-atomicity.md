# ADR 0104: Runtime World Transaction — Atomicity by Projected Pre-validation

- **Status:** Proposed
- **Date:** 2026-10-07
- **Deciders:** slmao
- **Acceptance:** pending. To be `Accepted` before or during the review of
  [Spec 0053](../specs/0053-runtime-world-transaction.md), with D2–D6 as ruled
  there (Spec Q1–Q7).
- **Related Spec:** [Spec 0053: Runtime World Transaction](../specs/0053-runtime-world-transaction.md)
- **Related ADR(s):**
  - Extends [ADR-0103](0103-runtime-world-operation-boundary.md) D9
    ("atomicity is Transaction's"). No ADR-0103 decision changes.
  - Rejects, with reasons, the undo-log alternative over
    [ADR-0049](0049-entity-identity-and-handle-invalidation.md)'s generation
    rules, which [ADR-0101](0101-runtime-ecs-core-storage-identity-and-placement.md)
    D4 adopts for the ECS.
  - Keeps [ADR-0097](0097-guid-keyed-asset-and-entity-identity.md) D5/D6 and
    ADR-0004.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **Where atomicity was deferred.**
  - Plan 0050 J3: the ECS `CommandBuffer` continues past failures, and
    all-or-nothing was set aside for want of a rollback.
  - Spec 0052 ruling Q9 / ADR-0103 D9: the Runtime World boundary applies
    commands one by one, apply-or-fail, and names atomicity as Spec 0053's.
- **The maintainer fixed v1's scope** (2026-10-07):
  - atomic commit / rollback of a group of the existing five commands;
  - no new verbs;
  - Spec 0052's contracts unchanged: single-command semantics, one event per
    successful command and none for a failure, the `applyPending()` timing,
    the guard set;
  - Undo/Redo, nested transactions, cross-process batching, entity listing
    and an address text form named only.
- **Two properties of Spec 0052's implementation** (merged, PR #216):
  - every check of a command precedes its first ECS call, so a refused
    command has no effect;
  - validation reads only:
    - GUID → live entity;
    - an entity's component set;
    - the Directional and Point counts among Light + WorldMatrix holders;
    - the active camera's identity.

## Decision

1. **A transaction is an all-or-nothing group of Spec 0052 commands.**
   - It is submitted whole, and applied at `applyPending()` at its position
     in submission order.
   - **Commit:** every command applies in order, with effects and events
     identical to applying them one by one.
   - **Abort:** nothing applies, no event is emitted, and one failure names
     the first refused command.
   - No verb, event or validation rule is added. Spec 0052's single-command
     path and contracts are unchanged.
2. **The atomicity mechanism is projected pre-validation** (Spec 0053 Q1).
   - The whole transaction is validated against a projection of the state
     Spec 0052's validation reads, as each command would leave it. It is
     executed only if every command passes.
   - Execution then cannot be refused; a refusal there is a programming
     error (`ATLANTIS_CHECK`).
   - **Rollback is "never started":** an aborted transaction made no ECS
     call, so no `EntityId` was created or destroyed, and every handle (the
     bake's `EntityGuidMap`, `activeCamera`, the GUID index) is untouched.
   - **Not chosen:**
     - **Undo log.** The inverse of `DestroyEntity` cannot restore the
       entity's identity under ADR-0049: generations only advance, and a
       retired slot never returns. Every outside handle (`EntityGuidMap`,
       hence `EntityRef` resolution; `activeCamera`) would dangle, unless
       generations could be rewound, which breaks ADR-0049's no-aliasing
       guarantee. An aborted transaction would also churn ECS storage before
       reverting. An inverse log is the natural basis of Undo/Redo and
       belongs to that future Spec.
     - **ECS snapshot / copy.** `ecs::World` is non-copyable, so this needs
       a clone API. It costs O(world) per transaction. A clone has a new
       per-instance identity token, so every outside `EntityId` would be
       foreign to the swapped-in world.
3. **One source of validation rules** (Spec 0053 Q7). Spec 0052's
   per-command checks become one routine over a state view, implemented
   twice: real (the index plus the ECS) and projected. Projection and
   execution run the same rule code; only state bookkeeping differs, and a
   sequential-equivalence test pins it.
4. **Entry point and failure reporting** (Spec 0053 Q2, Q5). Recommended:
   - `submit(Command)` is kept;
   - `submitTransaction(...)` on `RuntimeWorldAccess` returns a
     `TransactionTicket` covering consecutive command tickets;
   - an abort reports one `CommandFailure` for the first refused command,
     whose position is derivable from its ticket.

   There is no new failure type, error kind or queue.
5. **Events** (Spec 0053 Q3): a committed transaction emits its commands'
   events in order; an aborted one emits none. There is no transaction-level
   event.
6. **Special cases** (Spec 0053 Q4). Recommended:
   - every rule is evaluated per command, as in Spec 0052;
   - commands after a projected destroy see the GUID as unknown;
   - destroy then re-create of the same GUID is allowed;
   - an active-camera refusal anywhere aborts the transaction;
   - there are no end-state or deferred checks;
   - an empty transaction is a no-op.
7. **Placement and consumers** (Spec 0053 Q5, Q6).
   - The transaction lives in `atlantis::world::access`.
   - Runtime needs no change: `runFrame()` already calls `applyPending()`
     first.
   - v1 is proven by World tests and, recommended, by the smoke test's
     creation group run as one transaction.

## Consequences

### Positive

- Clients can make multi-command edits that succeed whole or leave no trace,
  the shape an Editor's "apply" and an Agent's multi-step edit need.
- No ECS change, no identity disturbance, and cost proportional to the
  transaction.
- Spec 0052's single-command behaviour, events and guards are untouched.

### Negative / Trade-offs

- The validation code is restructured into a state-view routine (D3), a
  change to Spec 0052's internal implementation, not its contract.
- Projected state must track every input any future validation rule reads.
  A rule reading new world state must extend the projection too.
- No undo after commit. Undo/Redo is future work.
- No size limit. A very large transaction is fully validated before any
  effect.

## Alternatives Considered

- **Undo log, and ECS snapshot / copy:** see D2.
- **Begin/Commit/Abort verbs in the command stream.** These are new verbs,
  and they let a transaction span calls.
- **Making every `submit()` batch atomic.** This changes Spec 0052 ruling
  Q9's contract.
- **End-of-transaction ("deferred") guard evaluation.** This is new
  validation semantics. The active camera cannot be restored within a
  transaction anyway (D6).
