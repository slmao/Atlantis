# ADR 0160: Mutable Order for Adjacent Scene Siblings

- **Status:** Proposed
- **Date:** 2026-11-15
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending.
- **Related Spec:** [Spec 0104](../specs/0104-adjacent-sibling-order.md).

## Context

[ADR-0130](0130-authoring-access-interface-and-module-boundary.md)
defines one validated authoring transaction and a closed command set.
[ADR-0136](0136-structural-edit-records.md) records positions for structural
replay and assumes a node's position remains fixed after creation.
Scene serialization already persists the document sequence, and
[ADR-0102 D3 and D5](0102-authoring-runtime-world-separation-and-scene-bake.md)
use it for baked entity allocation and extraction. Existing Remote
authoring event decoders reject unknown kinds.

## Decision

1. **Use the canonical node array.** Add `SwapAdjacentSiblings` to the
   existing authoring command set. It carries the source GUID,
   adjacent sibling GUID, expected parent (null for roots), direction
   (`Up` or `Down`) and expected document revision. It is the sole
   command in its transaction. The Authoring host validates the hand-authored
   provenance, expected revision, both GUIDs, same parent and directional
   adjacency in the sibling projection. It exchanges the nodes at their
   existing global positions in the candidate document and uses the
   existing whole-transaction validation and commit path. An accepted
   swap advances the revision once. A refusal changes no scene state and
   emits no event. Reuse `NoDocument` and `UnknownNode`, with closed
   swap-specific refusals for a stale revision, unsupported provenance,
   or invalid pair, direction or transaction shape.
2. **Publish one order event on commit.** `NodeOrderChanged` carries both
   GUIDs, their resulting global positions and the committed document
   revision. A node filter can match either GUID. A component filter
   excludes it. Clients use this delta in commit order or refresh with
   `listNodes` when they cannot reconcile it. Editor updates observed
   positions without recreating rows' node identities. The event leaves
   document lifetime and existing ownership intact.
3. **Keep the Remote /1 wire additive.** Keep existing subscribe filters
   and message shapes. New event delivery requires the new name in
   `kinds`; existing lists receive only events they already decode.
   New clients can establish support with their well-formed
   unfiltered, order-aware subscription. When that well-formed request
   receives the legacy `BadRequest` with `malformed or missing filter`,
   use the existing filter and require its success. Keep reorder
   unavailable for that connection. Other errors follow existing
   connection handling. Old clients may
   query the current order with `listNodes` and use GUID-addressed calls
   that do not depend on cached order. Cached positional writes and
   structural history in an old Editor are outside mixed-version editing
   support while reorder is in use. Close the old editing session before
   reordering alongside an upgraded writer. This adds no host lease or
   new editor channel. Existing source and cooked versions remain readable.
4. **Fence order-dependent history.** Local Undo/Redo follows the linear
   stack and exchanges the recorded pair in the appropriate direction.
   A settled local swap or replay keeps preceding local position records
   usable when the linear history returns to their state. On an external
   swap event, or when an order event cannot be correlated with this
   Editor's settled swap at that revision, discard earlier undo/redo
   entries containing a `NodeLife` record, since one replay direction
   recreates a node at its recorded position. Pending history cannot
   later restore a discarded positional entry. Keep other entries
   subject to their existing guards. Order entries check the pair and
   orientation on replay. Preserve reopen cleanup and settled-commit
   Redo invalidation.
5. **Bake the persisted sequence.** Keep GUID-based references and the
   same scene formats. When saved, transient source indices are
   derived from the new positions and parent and camera references
   resolve by GUID. Both the Apply cook and normal file cook feed the
   same bake, preserving the ADR-0102 order rule. Changes in existing
   ordering-dependent outcomes for a reordered scene are allowed.

This supersedes the node-order immutability and positional replay
assumptions in ADR-0136 only for this operation, and adds one command
and event to the ADR-0130 surface.

## Consequences

- The swap is shared, atomic and persistable without extra scene
  metadata. Existing non-reordered scenes keep their bake order.
- One order-sensitive edit can change transient baked indices and
  results whose existing tie-breaker depends on them.
- External reorders can discard some otherwise usable positional
  history. Legacy editors retain existing wire decoding and require a
  fresh order-aware client for concurrent positional editing.

## Alternatives Considered

- An independent presentation-order field keeps bake order fixed but
  adds persisted metadata and two ordering contracts.
- Destroy/recreate emits identity lifecycle events, disturbs stale-row
  guards and makes history depend on recreating component state.
- Moving a subtree as a contiguous global block changes the order of
  unrelated nodes when their document slots are interleaved. The slot
  exchange supports the sibling operation with two changed positions.
