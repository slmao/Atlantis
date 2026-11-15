# Spec: Persist an Adjacent Sibling Swap in Hierarchy

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-15
- **Related Plan:** [Plan 0104](../plans/0104-adjacent-sibling-order.md)
- **Approval:** Pending joint review.
- **Related ADR:** [ADR-0160](../adr/0160-mutable-adjacent-sibling-order.md) (Proposed).

## Summary

Move a selected node one position up or down among its siblings in a
hand-authored scene. The shared authoring document owns the order. Save
persists it, and Apply and cold startup bake it through the current scene
path.

## Motivation / Problem Statement

[0065](0065-authoring-data-layer.md) defines the order at node creation.
[0102](0102-duplicate-scene-subtree.md) appends a copied sibling and
[0103](0103-hierarchy-reparent.md) changes its parent while preserving its
document position. Hierarchy displays each parent's children in document
order. A user needs to change that order without reconstructing existing
nodes. Scene source already stores the order used by Runtime bake.

## Goals

- Swap one selected node with its adjacent sibling in a chosen direction.
- Keep the operation shared across Editor, CLI, SDK and MCP.
- Support one-step Undo/Redo, explicit apply recovery and persistent reopen.

## Non-Goals

Free-position drag reorder, move across parents, multiple selected nodes,
block moves, reorder in imported documents and independent display or
render order metadata. Existing drag-on-node reparent remains the entry
point for parent changes.

## Requirements

### R1. Scope and slot semantics

Show **Move up** and **Move down** in Hierarchy Actions for a live
selection in a supported hand-authored document. Each action chooses
the adjacent row in the selected node's sibling list. Root nodes are
siblings under the null parent. Disable a direction at the end of a list,
and suppress another move during settlement or an active Gizmo gesture.
Keep selection by GUID after a verified commit if a newer selection has
not taken precedence. Follow the ordinary Inspector focus and draft
rules.

Swap the two occupied positions in the document's global node array.
For global order `A, X, B` with adjacent siblings `A` and `B` and an
unrelated node `X`, the result is `B, X, A`. Every other node stays in
its slot, including descendants of either participant. Hierarchy
continues to display each node as a subtree in its new sibling position.
Keep scene and node GUIDs, names, parent links, component values, local
transforms, asset references and active camera unchanged.

### R2. Shared settlement and compatible observation

Submit the single swap through the existing authoring transaction and
failure drain defined in [ADR-0160](../adr/0160-mutable-adjacent-sibling-order.md#decision).
Capture the intended pair and direction from the displayed Hierarchy.
Guard worker, document lifetime and both displayed node identities
using 0103's row-incarnation rule. Refresh the observed revision and
require that the captured pair still qualifies before submit. The host
checks the expected revision, parent and ordered adjacency atomically.
A newer view can enable an explicit retry. The request never substitutes
a different neighbor. Reject a stale or unsupported request without a
history entry or automatic apply.

After a valid ticket and matching failure drain, distinguish a refused
request from a confirmed commit. On lost or invalid settlement, report
the result as unknown, identify the pair and direct the user to refresh
and inspect before explicitly retrying. There is no automatic replay.
New clients use the dedicated order event and the existing full
`listNodes` snapshot. The event updates order within the same document
lifetime and retains node identities. Old-peer behavior and mixed
editing limits follow ADR-0160.

### R3. Undo/Redo and concurrent edits

Record one Editor-local undo entry for a verified exchange. Require the
same document lifetime, a single revision advance and the verified slot
result before recording history and scheduling automatic apply. For a confirmed commit with an unverifiable
result, invalidate Redo, report Undo unavailable for that edit, and
leave further apply explicit. A successful reopen clears local history.

Store the participants, parent and before/after sibling orientation.
Each Undo or Redo verifies the recorded pair's current identities and
expected adjacency, then submits the inverse or forward swap at the
freshly observed revision. An external deletion, reparent, pair-order
change or intervening sibling refuses the history step. Unrelated
changes already observed can coexist when the recorded pair still
satisfies those checks. Settle before applying either direction.
Existing positional history across this operation follows ADR-0160.

### R4. Apply, Save and restart

Use the existing [0083](0083-authoring-apply-outcomes.md) controller.
Distinguish a committed order from the correlated Runtime reload result.
Pending and suspended applies retain their existing behavior. A reload
failure preserves the reordered document, recorded history and last
successful Runtime projection. Explicit Retry/Apply reprojects the
current document without submitting another swap.

Save uses the existing scene source and preserves the reordered global
node sequence. Reopen that source on the same active asset generation,
then Apply explicitly. The independent [0090](0090-saved-scene-build-open.md)
Build and Open path cooks the same saved source using its own configured
asset inputs. Use matching dependency identities and content in the
two acceptance routes. Both bakes use the persisted node order.

Runtime allocation and extraction remain ordered by the canonical node
sequence. World matrices and active-camera identity remain equal by
GUID, while transient entity indices can change. Results that already
break ties by node order, such as equal-distance picking, follow the
new order. Preserve existing results for scenes whose order has not
changed.

## Architectural Impact

[ADR-0160](../adr/0160-mutable-adjacent-sibling-order.md) extends the closed
authoring command and event sets and narrows the previous immutability
of node order. The existing scene source, cooked format, document
ownership and Runtime bake rule carry the new order without a second
ordering field.

## Testing & Verification Plan

[Plan 0104](../plans/0104-adjacent-sibling-order.md#verification-checklist)
covers global slots, shared compatibility, Undo/Redo conflict and
recovery, Windows 100% Save/Open and independent cold startup of the
same source, and a Runtime order-sensitive fixture.
