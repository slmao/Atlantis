# ADR 0160: Mutable Order for Adjacent Scene Siblings

- **Status:** Accepted
- **Date:** 2026-11-15
- **Deciders:** Atlantis maintainer
- **Acceptance:** Maintainer accepted [PR #373, revision 6d6abb11](https://github.com/slmao/Atlantis/pull/373) with the Spec and Plan.
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
   GUIDs, their resulting zero-based global positions and the committed
   document revision. A node filter can match either GUID. A component
   filter excludes it. Clients consume the delta in commit order and
   preserve row identities. If a delta cannot be reconciled, suspend
   order actions until a revision-fenced `listNodes` refresh. Account
   for queued lifecycle events and retire positional deltas covered by
   that snapshot before applying newer events. Invalidate captures
   whose node identity cannot be established. A refresh does not open
   the source or advance the document lifetime.
3. **Keep the Remote /1 wire additive.** New event delivery requires its
   name in `kinds`. Legacy filters still receive events they already
   decode. Clients that use reorder establish support on the same
   connection with the well-formed unfiltered subscription including
   the new kind. Inspect the correlated raw response before the ordinary
   Remote result handler. The legacy `BadRequest` with
   `malformed or missing filter` permits exactly one retry using the
   original filter. With a valid retry result, keep the connection
   usable for legacy calls and mark reorder unavailable. Other errors
   follow existing connection handling. A write-only client releases
   a probe-only subscription after the check and submits a swap only
   on a capable connection. Passive order-aware clients use the
   successful order-aware subscription for observation.

   Old clients can take fresh `listNodes` snapshots and use
   order-independent GUID-addressed calls. Legacy cached positional
   writes and structural replay are outside mixed-version support
   once upgraded clients reorder. End the old editing session before
   enabling that concurrent workflow. The policy uses ordinary client
   compatibility and no host lease. Existing source and cooked versions
   remain readable.
4. **Fence order-dependent history.** Local Undo/Redo follows the linear
   stack and exchanges the recorded pair in the appropriate direction.
   A settled local swap or replay keeps preceding local position records
   usable when the linear history returns to their state. Classify an
   event as local only after the matching ticket and failure drain
   confirm this Editor's swap at the expected next revision and the
   pair and resulting slots agree. Defer history effects while that
   settlement is outstanding. Treat external or uncorrelated order
   changes, including changes found by snapshot refresh, as invalidating
   earlier undo/redo entries containing a `NodeLife` record. Pending
   history cannot later restore a discarded positional entry. Keep
   other entries subject to their guards. Order entries retain node
   identities and check the pair and orientation on replay. Preserve
   reopen cleanup and settled-commit Redo invalidation.
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
