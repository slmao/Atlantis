# Spec 0108 — Duplicate Selected Scene Subtrees

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-16
- **Related Plan:** [Plan 0108](../plans/0108-batch-duplicate-subtrees.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0164](../adr/0164-guarded-duplicate-transactions.md) (Proposed)

## Summary

Duplicate a Hierarchy selection of subtrees as one authoring edit. Copy
the effective subtrees with fresh node identities, keep their authored
placement, and make the whole batch a single Undo/Redo step. Apply and
Save use the current scene workflows.

## Motivation and scope

[0102](0102-duplicate-scene-subtree.md) copies one subtree. [0106](0106-scene-multiselect-batch-edit.md)
and [0107](0107-multi-object-gizmo.md) let users edit several selected
nodes together, while Hierarchy duplication still acts on one selection.
A user duplicating several populated subtrees needs the copies to appear
together and be removable together. The existing authoring transaction
can commit structural writes as a unit. A revision precondition is
needed to protect the append boundary, source snapshot and destructive
Undo across the final read-to-submit interval.

The first release covers duplication in one open, editable hand-authored
scene. Batch deletion of original nodes, batch reparenting and reordering,
partial subtree copy, clipboard or cross-scene copy, and prefabs are
deferred. The existing single-selection duplicate remains available.

## Requirements

### R1. Selection, eligibility and batch limit

Offer **Duplicate subtrees** in Hierarchy Actions for the existing
2–64 node selection on a capable Runtime. Follow
[0093 visual conventions](0093-editor-visual-layout.md). Explain
unavailable, busy and refused states. Serialize with other Editor
authoring edits, history replay and active gestures. Copy committed
authoring values. Existing draft and focus rules still apply.

The effective roots are selected nodes with no selected ancestor,
including an ancestor separated by unselected intermediates. Include
each effective root and every descendant once. A multi-selection that
reduces to one effective root follows the same batch path. Enumerate
the union from the authoring tree and stop at the 65th distinct node,
before component leaf reads or GUID allocation. Accept at most
64 copied nodes across all roots. Scene limits and the existing
Remote request-line limit still apply. Refuse an oversized batch as
a unit without splitting the transaction.

### R2. Identities, names and append order

Read the complete serializable Transform, Camera, Renderable and
Light state using the existing duplicate snapshot rules. Refuse
unreadable or unsupported leaves, including any unhandled future
node reference. Allocate one fresh explicit, nonzero GUID per copied
node, distinct from every other copy and every node in the observed
document. Complete the source-to-copy map before constructing commands.
Retain asset GUIDs, absent optional values, local TRS and descendant
names. Keep the scene identity, source path, active camera and
original nodes. A duplicated camera starts as a regular camera.

Put each copied root under the corresponding original parent, or
at scene root. Remap every parent link within each copied subtree.
Append one contiguous block to the authoritative global node order
using the relative order of all included source nodes. Keep this
order when source subtrees are interleaved or a child precedes its
parent in the source. Existing siblings retain their slots and
relative order.

Choose root names in source document order using 0102's `Copy`
suffix, ordinal sibling-name comparison and UTF-8 limit. Include
names already assigned under the same parent during the batch.

### R3. Guarded submission and settlement

Capture the selection generation, displayed row incarnations,
connection and open document lifetime when the action is invoked.
Before planning or replay, bracket event observation, authoritative
order and parent reads, and the required authored state with matching
`applyStatus` document revisions. Reconcile order against the observed
tree with complete lifecycle events. Refuse an incomplete observation,
unreconciled order, changed revision or replaced captured row. After
capability discovery, recheck the context at the actual send boundary,
serialized with changes to selection and lifetime. Keep discovery and
receipt waits outside that boundary. The host guard covers later
document changes, including a reopen in the same session. Once sent,
settle the captured request. A later selection cannot retarget it and
a replacement document receives none of its callbacks or history.

Use one [ADR-0164](../adr/0164-guarded-duplicate-transactions.md)
transaction with the observed revision. Require a valid nonzero
first ticket and an exact, non-wrapping ticket range for every
command including the guard. Settle that range with a complete failure
drain. A known refusal makes no authored writes, adds no history entry,
changes no selection and issues no Apply. Preserve that classification
if a later refresh fails. On success, verify the expected ordered
authored events and full copy block with observations fenced on both
sides at the guarded revision plus one. Apply events are settled
separately. Consume authored events for model and identity checks
before publishing one Editor-local history entry.
If the selection intent is still current, select just the verified
copied roots. Both results precede fallible view refresh.

A lost or malformed outcome after possible send is unknown. Report the
attempted root GUIDs and inspect fresh authoritative state and confirm
the previous request has ended before explicitly retrying. A known
commit that cannot be verified, including when overtaken, is confirmed
with unavailable Undo. Both cases clear Redo and leave earlier Undo
entries subject to their usual guards. Suspend automatic Apply for
that document and wait for explicit Apply after inspection. Never
reissue the duplicate automatically.

### R4. Undo/Redo and Apply recovery

Undo and Redo each use one guarded transaction with a fresh fenced
revision and R3 settlement. Keep the batch as a guarded history
entry in the existing stack. It always uses guarded replay, including
after reconnect or discovery failure. Undo removes the entire copy
block. Require the recorded GUIDs, current expected incarnations,
full authored state, child membership, parent identities and order
in the exact suffix. Verify reverse-slot destruction events, absence
of all copy GUIDs and the restored boundary after settlement. Refuse
if a copy has been changed, externally replaced even with equal
values, acquired an unrecorded child, or become the active camera.
Never change the active camera to make Undo succeed.

Redo recreates the recorded copies in the same slots with the same
GUIDs, properties, names and parent links. Require all copy GUIDs
absent and their surviving original parents present in the expected
incarnations. Require the recorded prefix count and the identity of
the node immediately before the recorded block, or the empty
boundary for a zero count. Undo checks the same prefix boundary
before removal. A shifted block or append boundary conflicts.
An edit elsewhere can coexist if the expected state still
matches at a fresh fenced revision. Evaluate this entry's own boundary
on replay. The existing broad sibling-order invalidation still
applies to legacy positional entries.

Redo uses the recorded snapshot and verifies the recreated block.
Refresh copy incarnations on verified Redo. A verified local Undo
followed by verified Redo of a batch can also update dependent batch
entries' recorded predecessor or original-parent incarnations for
the same recreated GUIDs in the same document. Update only references
to the exact incarnation removed by that local Undo. This keeps
stacked replay viable when batch B follows batch A at the tail or
uses its node as a parent. External events and unverified replay
never authorize that update.

A relevant conflict refuses the entire replay and drops that entry
with a reason. A host stale-revision refusal alone, or a pre-send
failure that cannot establish a conflict, retains the entry for
explicit retry after a fresh check. A verified replay moves one entry
between stacks and uses the existing selection cleanup behavior.
An indeterminate or unverifiable replay drops the attempted entry
and requires explicit Apply after inspection. Keep verified history
and publish its settlement before any fallible view refresh.

For a verified edit or replay, use the existing tracked
[0083 Apply controller](0083-authoring-apply-outcomes.md).
Create one ordinary Apply intent subject to existing coalescing and
suspension. Capture the actual accepted Apply ID and revision, which
may include a later external edit. Preserve settlement of older owned
Apply receipts even after an uncertain transaction. Those receipts
can settle while automatic follow-ups remain suspended. A failed,
superseded or unknown Apply keeps settled document edits and history
distinct from the last confirmed Runtime projection. While suspended,
use explicit Apply of the current document after inspection or repair.
Apply creates no extra copy or history entry.

### R5. Save and reopen

Use ordinary Save after the copy or replay settles. Reopen the same
hand-authored source with the same active asset generation. Verify
the original and copied nodes by GUID, including order, parent links,
component values, retained asset references and active camera.
Reopen clears batch selection and local history. Explicit Apply
projects the reopened document through the existing reload flow.

## Architectural Impact

[ADR-0164](../adr/0164-guarded-duplicate-transactions.md) adds a
bounded structural revision guard to the shared authoring command
set. The Editor owns selection and local replay context. Existing
transaction, session, scene source, asset, and Runtime projection
boundaries remain in use.

## Testing and Verification

[Plan 0108](../plans/0108-batch-duplicate-subtrees.md#verification-checklist)
maps these requirements to model and shared-client checks, contention
and receipt faults, and Windows 100% Save/reopen acceptance.
