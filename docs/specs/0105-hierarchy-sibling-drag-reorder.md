# Spec: Drag a Hierarchy Node to a Sibling Position

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-15
- **Related Plan:** [Plan 0105](../plans/0105-hierarchy-sibling-drag-reorder.md)
- **Approval:** Pending joint review.
- **Related ADR:** [ADR-0161](../adr/0161-atomic-sibling-slot-move.md) (Proposed).

## Summary

Drag one selected Hierarchy node before or after any visible sibling in
the current hand-authored scene. The authoring document owns the order.
One accepted drop is one transaction and one undoable edit. The existing
source, Apply and cold-start paths consume that order.

## Motivation / Problem Statement

[0103](0103-hierarchy-reparent.md) handles dropping onto a node to
change parent. [0104](0104-adjacent-sibling-order.md) makes **Move up**
and **Move down** persistent, one sibling at a time. A longer move
currently needs several edits with separate revision and reload outcomes.

## Goals

- Support first, last and intermediate sibling positions, including roots.
- Use one shared authoring transaction across Editor, CLI, SDK and MCP.
- Support guarded Undo/Redo, tracked Apply recovery, Save and cold start.

## Non-Goals

Multi-selection, cross-scene or cross-parent positional insertion,
blank-space drop targets, new auto-scroll behavior, imported-scene reorder
and separate display or render order. Cross-parent changes use 0103.

## Requirements

### R1. Drop targets and feedback

Use the existing single-selection Hierarchy drag. Use the top and bottom
quarters of the target row for **before** and **after**. These zones
accept a distinct live sibling, including another root, in a hand-authored
document on a connection that supports the move. The middle half uses
0103 reparent validation and feedback. An invalid edge target leaves
the gesture uncommitted.

Show an insertion marker distinct from the center highlight. The **after**
marker represents the gap after the target's subtree, including when its
children are visible. At a viewport edge where that gap is clipped, keep
the relative placement explicit in the feedback. Clear the marker on
leave, cancellation, invalidation and settlement. Show no valid insertion
marker for a no-op gap. Preserve ordinary Inspector focus and draft
behavior. Disable a new move during settlement or an active Gizmo gesture.
Keep selection by GUID after a verified move if no newer selection has
taken precedence. The 0104 buttons keep their existing capability and
guards.

### R2. Shared mutation and observation

Reorder the sibling sequence within its occupied global slots according
to [ADR-0161](../adr/0161-atomic-sibling-slot-move.md#decision). GUIDs,
parents, node properties, local and world transforms, asset references
and active camera retain their values. Descendants keep their document
slots and follow their parent in the Hierarchy.

The shared operation takes the source, anchor, expected parent
(`null` for roots), `Before` or `After` and expected document revision.
It is the only command in its transaction. A valid move advances the
revision once and emits one opt-in event. Self and cross-parent targets,
missing nodes, no-op gaps, imported documents, stale revisions and mixed
batches do not mutate or emit a move event.

Bind the source incarnation and document lifetime at drag start.
At drop, capture the anchor from the displayed row and the displayed
same-parent interval between the participants. On the worker, sample
the revision, drain authoring events and verify the captured interval
and row incarnations. Drain again and require the same revision and
lifetime before submitting. A successful same-source reopen invalidates
the gesture. The host checks the revision and move atomically on admission.
A refused move writes no history and requests no Apply.

Use the usual ticket and failure drain, then verify the next revision
and affected slots before recording history or scheduling Apply. A lost
or invalid settlement reports the outcome as unknown, identifies source
and anchor, and directs the user to refresh and inspect before retrying.
Preserve the CLI transport error category on disconnect. The opt-in
event, recovery snapshot and old-peer rules follow ADR-0161.

### R3. History and Apply

Keep one Editor-local entry per verified drag reorder. Record the node
identities and incarnations in the inclusive sibling interval between
source and anchor, its before/after order and the forward and inverse
anchors. Undo/Redo uses the same transaction and settlement path with a
fresh revision. Require the recorded interval in the expected order and
parent, with the same incarnations. An insert, replacement, reparent or
other conflict inside that interval drops the entry and submits nothing.
An unrelated change outside the interval can coexist if the guard holds.

Use 0104's rules for existing position-bearing history. Publish replay
settlement and confirmed history before refresh calls that may fail.
For a confirmed move with an unverifiable result, invalidate Redo and
report Undo unavailable for the edit. Unknown or unverifiable moves
preserve settlement of any older owned Apply receipt and require an
explicit Apply for the current revision. Clear automatic follow-ups to
the older receipt. A successful reopen clears local history.

Use the [0083](0083-authoring-apply-outcomes.md) controller after a
verified local edit. A failed reload keeps the document, history and
last successful projection. Explicit Retry/Apply reprojects the current
document without submitting another move.

### R4. Persistent and cold order

Save the reordered global sequence through the existing source format.
Reopen it on the original Runtime's active asset generation, then
explicitly Apply. Use the same saved source and registration fragment
with independent [0090](0090-saved-scene-build-open.md) Build and Open,
using matching dependency identities and content in both routes.

Runtime bake and extraction follow the saved sequence. Preserve world
matrices and the active camera by GUID. Transient indices and existing
order-dependent outcomes can reflect the new sequence.

### Non-functional

Use existing dependencies, formats and client transports. Mutation and
reconciliation use the existing authoring transaction and observed-event
work, with no new per-frame Runtime work. Keep the event fixed-size
across move distances. UI acceptance is Windows at 100% scaling.

## Architectural Impact

[ADR-0161](../adr/0161-atomic-sibling-slot-move.md) supplements ADR-0160
with one command and one explicitly subscribed event on the shared
authoring boundary. It preserves the adjacency-only exchange and the
canonical scene source and bake order.

## Testing & Verification Plan

Use the [Plan 0105 checklist](../plans/0105-hierarchy-sibling-drag-reorder.md#verification-checklist)
for exact slot order, compatibility, conflict and Apply handling.
Acceptance includes Windows 100% drag and Save/reopen, then a same-source
0090 cold start with an order-sensitive Runtime check before any Apply.
