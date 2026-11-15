# ADR 0163 — Guarded Transform Batches

- **Status:** Proposed
- **Date:** 2026-11-15
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0107](../specs/0107-multi-object-gizmo.md)

## Context

One group gesture can write position and Euler rotation or scale for
each root. [ADR-0162](0162-revision-guarded-authoring-property-batch.md)
guards one shared leaf across distinct nodes. Using several of those
batches would expose intermediate placements and could partially
commit. Changing what the existing peer accepts would make its
capability ambiguous. The single-object preview in
[ADR-0135](0135-two-document-edit-and-apply.md) also writes temporary
matrices to the Runtime World. For a group gesture that would entail
restoring multiple subtrees against a projection that can be replaced
independently.

## Decision

1. **Use a distinct guard in the existing transaction.** Add the
   closed command `RequireTransformBatchRevision{expectedRevision}`.
   It is valid only as the first command of a transaction followed by
   1–192 `SetProperty` writes across 1–64 distinct node GUIDs. Each
   write must address one of the existing whole-vector Transform
   leaves `localPosition`, `localEulerAnglesRadians`, or `localScale`.
   Validate these addresses against the authoring schema. An address
   may occur only once. A node may supply any subset of those leaves.
   Reject a guard-only, misplaced, repeated, or mixed guard, other
   component or leaf, duplicate address, or out-of-bound transaction
   as `InvalidGuardedPropertyBatch`, correlated to the first guard's
   ticket. An absent document keeps `NoDocument`. Leave the existing
   `RequireDocumentRevision` admission shape unchanged.
2. **Admit in AuthoringSession.** After validating the shape, compare
   the expected document revision at the existing serialized admission
   point. Use `PropertyBatchStaleRevision` with the guard ticket on a
   mismatch. Apply through existing `SetProperty` type, access and
   validation rules, with normal property failure attribution and
   candidate cook. A refusal changes no values, revision or event.
   A success advances one revision and emits ordinary property events
   in supplied write order. The guard emits no event. The normal
   ticket range includes it and every write. Selection, parent pruning
   and world-to-local math remain client decisions. Apply remains a
   separate tracked request under [ADR-0144](0144-correlated-authoring-apply-outcomes.md).
3. **Discover on the current open document.** Add optional
   `supportsGuardedTransformTransactions` to the existing
   `authoring.document` response. Only literal true on a valid
   descriptor for the current session and open document enables
   submission. Omit the member when unadvertised. Null document,
   omission, false or malformed value leaves the new behavior
   unavailable and preserves otherwise readable data. Reopen and
   reconnect invalidate discovery. Earlier peers still read the
   descriptor and retain their existing command paths. A peer without
   the new command refuses a raw copy during full-transaction decode,
   before any property write. The original property capability and
   the transform capability are independent. Updated clients never
   replay these writes unguarded when discovery or settlement fails.
4. **Reuse shared clients.** Native `scene tx` accepts
   `require-transform-revision <revision>` before existing `set` lines.
   The C# SDK exposes the typed command and descriptor capability.
   MCP extends its existing exact authoring transaction schema and
   uses its existing failure drain. Editor uses the same transaction
   and settlement via the SDK. Discovery refusal and potentially sent
   but unverified submission remain distinguishable. Keep the
   `atlantis.remote/1` envelope, events and old command encodings.
5. **Preview through the current overlay.** Group gesture feedback
   uses existing per-client overlay primitives under
   [ADR-0132](0132-viewport-overlay-surface.md) and never writes
   provisional World matrices. Other connections can see guides
   through the usual overlay union and geometry stays on the applied
   projection. The originating Editor clears the guides on
   cancellation or release through overlay replacement. Disconnect
   uses the existing server cleanup. This supplements ADR-0135's
   single-object preview, which retains its current World path.

## Consequences

- A mixed Transform edit has one host revision and normal ticket
  settlement. The guard extends the closed authoring command set in
  [ADR-0130](0130-authoring-access-interface-and-module-boundary.md)
  alongside the guarded single-leaf transaction. The shared data
  path requires native and managed conformance and compatibility
  verification. It introduces no document format or Runtime work on
  idle frames.
- Guarded commits can lose to any writer racing admission. Explicit
  retry uses a fresh read. A group preview shows placement guides and
  full geometry appears after Apply. The separate preview avoids
  temporary subtree writes and their rollback races.

## Alternatives Considered

- Several single-leaf transactions would allow partial success. An
  unguarded transaction after client preflight leaves an admission race.
- Widening the old guard under its current capability would make a
  capable 0106 peer look compatible with a mixed-leaf batch. A new
  RPC or scene format would duplicate an existing boundary.
- Live World previews would have to update and recover all affected
  descendants across paused or superseded Runtime snapshots.
  Destination guides provide bounded feedback using the shared overlay.
