# Spec 0106 — Scene Multiselection and Batch Property Editing

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-15
- **Related Plan:** [Plan 0106](../plans/0106-scene-multiselect-batch-edit.md)
- **Approval:** Jointly approved J1–J4 and Spec/Plan in [PR #377 at `3f66ff58`](https://github.com/slmao/Atlantis/pull/377) on 2026-11-15.
- **Related ADR:** [ADR-0162](../adr/0162-revision-guarded-authoring-property-batch.md) (Accepted).

## Summary

Select several scene nodes in the Hierarchy, see the selection in the
Viewport, and assign one common Inspector field to the selection in one
authoring transaction. One Undo restores each changed node's prior
value. The usual Apply and Save flows project and persist the result.

## Motivation

[Spec 0068](0068-authoring-editor.md) gives the Hierarchy, Inspector and
Viewport one selected node. [Spec 0105](0105-hierarchy-sibling-drag-reorder.md)
adds persistent sibling drag with the same single-node scope. The
Inspector resolves a node's fields through the shared schema. The
Viewport overlay already accepts several GUIDs, and the authoring
transaction already accepts several typed property writes. Clients
can read the current revision, but ordinary property transactions have
no host-side revision precondition. An external writer can change a
value or replace a node between the Editor's last read and submission.

## Goals and scope

- One selection set within one open authoring document, with a bounded
  size of 64 distinct live nodes, within the existing overlay budget.
- One field assignment per action in the multi-selection Inspector. Use
  the existing field value types, transaction validation, local edit
  history, tracked Apply and scene source format.
- Share guarded property writes through AuthoringConnection, native
  CLI, C# SDK and MCP. Keep selection and Inspector presentation local
  to the Editor.

Multi-object Gizmos, viewport additive picking, range selection,
batch structural actions, batch asset-reference picking, optional
field assignment or clearing in the multi-selection Inspector,
cross-document selection, and editing multiple fields in one action
are deferred. Existing single-node workflows remain available when one
node is selected.

## Requirements

### R1. Selection and Viewport feedback

Plain Hierarchy click replaces the set. Ctrl+click toggles one row
without selecting its descendants. Support keyboard focus navigation
and Ctrl+Space toggle within the Hierarchy. A toggle beyond 64 leaves
the set intact and reports the limit. A Viewport hit selects one
live node and a miss clears the set. Each new Viewport pick, including
a synchronous marker pick, supersedes older picks. An explicit
Hierarchy selection action supersedes pending picks even if the set
is unchanged. A reply must match the request, selection intent,
connection and document lifetime. An observed removal or replacement
of the hit node invalidates the pick.

Keep selection through ordinary Apply and Hierarchy refresh for
unchanged node incarnations. Destroy removes the selected incarnation
even when its GUID is reused. Reopen clears a multi-selection and its
draft. Disconnect or session replacement clears the outgoing set and
pending pick. Existing single-selection reopen behavior remains.

Use the existing overlay's selection GUIDs for meshes and a selected
color for existing light/camera markers. Mesh outlines resolve against
the current Runtime projection. A node with neither a projected mesh
nor an existing marker is represented in the Hierarchy and selected
count. Show the selection count, hide the Gizmo during multiselection
and reject single-target actions while several nodes are selected,
including structural commands, row drag and asset-reference editing.
Cancel uncommitted single-target previews or gestures on entering
multiselection. A submitted edit settles against its original target.
Use the [0093 visual conventions](0093-editor-visual-layout.md).

### R2. Common fields and explicit assignment

Intersect component IDs across the entire set. Within those
components, use schema leaf addresses and the existing Inspector's
typed equality. Show a uniform value if all reads agree on a present
value, uniformly absent if all reads explicitly report absence, and
mixed for differing successful reads, including absent/present.
An incomplete component list leaves the intersection unavailable.
A failed value read makes its row unavailable. Display a mixed
placeholder without populating an edit value.

For a hand-authored document on a capable Runtime, enable common
non-optional editable numeric scalars, whole vectors and enum leaves.
All other common leaves can be inspected read-only. A vector draft
assigns the full typed vector, with each element explicitly provided.
Use a per-field Set action or explicit Enter to confirm, and Escape to
discard. Changing focus alone does not submit. Keep a draft when
focus moves within the same Inspector context. Starting another
field draft discards the previous one. Changes to the selection,
subject identity, field availability or document lifetime invalidate
the draft. A relevant external change shows a conflict and requires
a fresh displayed snapshot and new input before another submission.

Accept an Inspector snapshot only for the current connection,
document lifetime, selection generation and latest refresh of that
selection. Selecting A, then B, then A requires a fresh snapshot.
Bind a draft to the snapshot actually displayed, with the field,
node and component incarnations, and every selected node's original
typed value. An unrelated refresh can update other fields without
overwriting or rebasing that draft. Populate the view and preflight
through existing bounded component/property reads and revision
fencing. Partial or unstable reads cannot authorize a write.
On preflight, observe pending lifecycle and property events, read the
current targets and values, and fence the reads with the same document
revision before and after. An observed change to the drafted field or
replacement of an addressed node or component invalidates the draft,
including same-GUID replacement. Observe those changes from the
displayed snapshot, including a value changed away and back before
Set. Unrelated edits settled before preflight may coexist once the
captured targets and values still match. A revision change during the
fence or before host admission refuses the attempt and refreshes the
view. Recheck the selection generation at submission. Once sent,
settle against the original targets even if the selection changes.
Permit only one guarded edit or replay to settle per Editor worker.

### R3. Shared settlement and Undo/Redo

Use the revision-guarded property transaction in
[ADR-0162](../adr/0162-revision-guarded-authoring-property-batch.md#decision).
The Editor assigns one typed value to changed targets in the
captured deterministic order. Compare all selected targets during
preflight, including those already equal to the requested value.
An all-equal assignment has no transaction, history or Apply.
Older Runtime peers can show the selection and common fields
read-only and continue existing single-node editing.

Settle a nonzero N+1 ticket range and the complete failure drain
for N writes. Use the same submission's ticket range to classify
refusals. A correlated refusal remains known if a later refresh
fails, with no new history or Apply. Before recording local
authorship, verify the matching ordered property events and
resulting values at the guarded revision plus one. Fence these reads
and the event drain against that revision on both sides. A verified
batch makes one local history record with each changed address and
its own before/after value. Publish it even if the selection changed
after submission.

Undo and Redo use the same guarded transaction. Use a fresh fenced
revision with the recorded node/component incarnations and exact
expected values for the entire recorded write set. An unrelated edit
can coexist when this check holds. A changed value, missing target,
or replaced instance refuses the whole replay and drops the
conflicted entry with a reason. A known stale-revision refusal on
its own keeps the entry on its original stack. Refresh and recheck
the targets before an explicit retry. A verified replay moves one
record between the history stacks. Publish replay settlement and
known history before any subsequent fallible refresh. A real reopen
clears local history. An old document's settlement cannot update the
new document's history or Inspector.

A lost or invalid settlement is unknown. A settled commit overtaken
before verification is confirmed but unavailable for Undo. Report the
field and affected targets and require a fresh authoritative read
and inspection before another batch. An unknown or unverified
replay drops the attempted entry without moving it to the opposite
stack. An unknown or unverified new forward batch clears Redo
because it may have overtaken that branch. A verified edit keeps its
history if only the Apply receipt is lost. Report the projection
outcome as unknown in that case. Preserve settlement of any older
owned Apply, clear automatic follow-ups and require explicit Apply
of the current revision after an uncertain or unverifiable edit.

### R4. Apply, Save and reopen

Use the [0083 Apply controller](0083-authoring-apply-outcomes.md) after
verified batch, Undo and Redo, with one Apply intent per action and the
existing coalescing rules. The guarded edit and Apply are separate
requests. Correlate the exact accepted Apply ID and revision. If
another writer advances the document between edit verification and
Apply acceptance, the receipt describes the revision actually
snapshotted. Show that revision and retain the verified history.
A later accepted snapshot provides no proof that the earlier
revision was projected separately.

With Runtime paused or reload failed, show the current document and
last successful Runtime projection as separate states. While
recovery is suspended, further verified edits and replays can
update the document without issuing Apply. Explicit Retry/Apply
projects the latest document without repeating the property
transaction.

Save the accepted values through ordinary Save. Reopen the same
hand-authored source on the same active asset generation, read the
saved values by GUID, and explicitly Apply the reopened document.
Selection, drafts and local history are Editor session state. The
scene source and asset formats are unchanged.

## Architectural Impact

[ADR-0162](../adr/0162-revision-guarded-authoring-property-batch.md)
extends the shared authoring command and document descriptor. The
Editor keeps its selection, per-target baselines and undo records
locally. The existing overlay, transaction, Apply and persistence
paths retain their ownership. There is no per-frame Runtime addition
or new dependency. Editor acceptance runs on Windows at 100%.

## Testing and Verification

[Plan 0106](../plans/0106-scene-multiselect-batch-edit.md#verification-checklist)
maps these requirements to model and shared-client checks, conflict and
receipt faults, and a real Windows authoring, projection and persistence
flow. Existing applicable build, Vulkan, golden and performance gates apply.
