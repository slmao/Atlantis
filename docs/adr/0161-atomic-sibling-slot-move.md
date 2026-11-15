# ADR 0161: Atomically Move a Node Within Its Sibling Slots

- **Status:** Proposed
- **Date:** 2026-11-15
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending.
- **Related Spec:** [Spec 0105](../specs/0105-hierarchy-sibling-drag-reorder.md).

## Context

[ADR-0160](0160-mutable-adjacent-sibling-order.md) adds one two-slot
exchange to the canonical node array. It requires a single-command
transaction and its `NodeOrderChanged` event describes two participants.
Moving past several siblings requires one settlement and a coherent
update for clients. Other parents' nodes can occupy global slots
between the affected siblings.

## Decision

1. **Rotate the occupied sibling slots.** Add
   `MoveSiblingRelative{node,anchor,parent,placement,expectedRevision}`
   to the shared authoring transaction. Placement is `Before` or
   `After`. Check the revision, hand-authored provenance, distinct
   live nodes, expected parent and single-command shape before commit.
   An insertion into the existing gap is a no-op refusal. Resolve the
   insertion relative to the anchor after removing the source from the
   sibling sequence. Assign the new sequence to the same global slots
   and preserve every other sibling's relative order. For `A, X, B, Y, C`
   with `A/B/C` siblings, moving `A` after `C` produces `B, X, C, Y, A`.
   `X` and `Y` keep their slots. Validate and commit the usual
   transaction candidate once. Use separate typed results for unknown
   nodes, stale revision and unsupported provenance, and a closed
   invalid-move refusal for invalid pairs, placements, no-op gaps and
   mixed batches.
2. **Publish one fixed-size delta.** Add
   `NodeMovedInSiblingOrder{node,parent,fromPosition,position,revision}`.
   The positions are the source's old and final zero-based global
   slots. Validate the source at the old slot and the same-parent
   endpoints, then rotate that parent's slots between the endpoints.
   A node filter matches the moved node. A component filter excludes
   the event. Positional observers use the unfiltered stream. On a
   delta mismatch suspend order actions. Fence the `listNodes`
   snapshot with the same revision before and after observation,
   account for queued lifecycle events once and retire covered order
   deltas before applying newer events. Preserve established
   incarnations and invalidate captures whose identities cannot be
   established. Refresh retains the document lifetime.
3. **Keep the wire additive.** Deliver the event only when requested.
   Establish a separate move capability alongside adjacent exchange
   support through the canonical unfiltered full subscription.
   On the exact legacy `BadRequest` / `malformed or missing filter`
   reply, retry with the 0104 kinds including `NodeOrderChanged`.
   If that tier receives the same recognized refusal, retry once with
   the pre-0104 kinds. A valid subscription enables only the successful
   tier's operations. Inspect the correlated envelope before the
   ordinary error handler. Other errors follow existing handling.
   Malformed or zero receipts enable no capability. Preserve the
   defaults used by existing callers. Write-only clients release a
   probe-only subscription. Passive observers retain the successful tier.
   Clients send a move only on a capable peer. Old decoders still
   accept legacy subscriptions and fresh snapshots. As in ADR-0160,
   end older cached positional editing sessions before a newer client
   starts reordering with an event those sessions do not request.
4. **Settle and replay as one edit.** Use 0104's revision and row
   guard on the intended interval. Establish local authorship through
   a valid one-command ticket, its failure drain, a matching
   next-revision event and the resulting slots. Verified local moves
   or replays preserve earlier positional history. External or
   uncorrelated order changes invalidate earlier `NodeLife` records,
   including pending transitions, under ADR-0160. Replay uses the
   captured original gap for the inverse and validates the affected
   interval under the current revision. Keep the older Apply receipt
   observable if a move cannot be verified. Projection of the current
   document then requires explicit recovery under Spec 0105.
5. **Reuse the saved order.** Existing lossless source, cooker and
   bake consume the resulting global sequence. Parent, camera and
   other references continue to resolve by GUID under
   [ADR-0102](0102-authoring-runtime-world-separation-and-scene-bake.md).

## Consequences

- One move changes one document revision and has one settlement in
  each direction. Existing clients decode the values they request.
- Order observers need the new event or a fresh snapshot to see a move.
  Negotiation takes one additional subscription attempt on a 0104
  Runtime and two on a pre-0104 Runtime.
- Undo storage grows with the affected interval within the existing
  history depth. Baked indices can change within the same formats.

## Alternatives Considered

- Consecutive existing swaps would commit one revision at a time.
  Batching those swaps would also change 0104's sole-command and
  event contracts.
- Removing and inserting in the full array would move slots occupied
  by unrelated parents.
- Sending all affected GUIDs in the event would increase payload size
  with move distance. Endpoints and the existing snapshot recovery
  let an observer reach the same order.
