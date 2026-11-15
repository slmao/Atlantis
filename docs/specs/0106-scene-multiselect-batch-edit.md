# Spec 0106 — Scene Multiselection and Batch Property Editing

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-15
- **Related Plan:** [Plan 0106](../plans/0106-scene-multiselect-batch-edit.md)
- **Approval:** Pending joint review.
- **Related ADR:** [ADR-0162](../adr/0162-revision-guarded-authoring-property-batch.md) (Proposed).

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
the set intact and reports the limit. A Viewport hit selects one node
and a miss clears the set. A late pick answer must match the latest
pick and the selection generation, connection and document lifetime
of its request before changing the set.

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
Use the [0093 visual conventions](0093-editor-visual-layout.md).

### R2. Common fields and explicit assignment

Intersect component IDs across the entire set. Within those
components, use schema leaf addresses and show their typed values as
uniform, mixed, uniformly absent or unavailable. Use the same typed
equality as the existing Inspector. A mixed placeholder represents
no draft value. Never substitute zero, blank or a representative
node's value for a mixed or unavailable read.

For a hand-authored document on a capable Runtime, enable common
non-optional editable numeric scalars, whole vectors and enum leaves.
All other common leaves can be inspected read-only. A vector draft
assigns the full typed vector, with each element explicitly provided.
Use a per-field Set action or explicit Enter to confirm, and Escape to
discard. Changing focus alone does not submit. Permit one draft at a
time. Changes to the selection, subject identity, field availability or
document lifetime invalidate the draft. Refreshes for an unchanged
selection leave focused text intact. If a relevant external event
invalidates the draft's baseline, show a conflict and let the user
review the refreshed values before starting another draft.

Bind a draft to the displayed field, exact selection and its
generation, document lifetime, node and component incarnations, and
each selected node's original typed value. Populate the view and
preflight through existing bounded component/property reads and
revision fencing. Partial or unstable reads cannot authorize a write.
On preflight, observe pending lifecycle and property events, read the
current targets and values, and fence the reads with the same document
revision before and after. An observed change to the drafted field or
replacement of an addressed node or component invalidates the draft,
including same-GUID replacement. Unrelated edits settled before
preflight may coexist once the captured targets and values still
match. A revision change during the fence or before host admission
refuses the attempt and refreshes the view.

### R3. Shared settlement and Undo/Redo

Use the revision-guarded property transaction in
[ADR-0162](../adr/0162-revision-guarded-authoring-property-batch.md#decision).
The Editor assigns one typed value to changed targets in the
captured deterministic order. Compare all selected targets during
preflight, including those already equal to the requested value.
An all-equal assignment has no transaction, history or Apply.
Older Runtime peers can show the selection and common fields
read-only and continue existing single-node editing.

Settle the ticket and its complete failure drain before scheduling
Apply. A correlated refusal remains known if a later refresh fails.
Before recording local authorship, verify the matching property
events and resulting target values under a fence at the expected
next revision. A refusal
creates no history or Apply. A verified batch makes one local history
record with each changed address and its own before/after value.
Keep selection independent from the record.

Undo and Redo use the same guarded transaction. Use a fresh fenced
revision with the recorded node/component incarnations and exact
expected values for the entire recorded write set. An unrelated edit
can coexist when this check holds. A changed value, missing target,
or replaced instance refuses the whole replay and drops the
conflicted entry with a reason. A verified replay moves one record
between the history stacks. Publish replay settlement and known
history before any subsequent fallible refresh. A real reopen clears
local history.

A lost or invalid settlement is unknown. A settled commit overtaken
before verification is confirmed but unavailable for Undo. Report the
field and affected targets and direct the caller to refresh and inspect
before attempting another batch. Do not synthesize history or repeat
the writes. A confirmed new commit clears Redo even when its history
cannot be verified. Preserve settlement for any older owned Apply,
drop automatic follow-ups and require explicit Apply of the current
revision. A lost Apply receipt leaves the verified edit and history
intact and reports the projection outcome as unknown.

### R4. Apply, Save and reopen

Use the [0083 Apply controller](0083-authoring-apply-outcomes.md) after
verified batch, Undo and Redo, with one Apply intent per action and the
existing coalescing rules. With Runtime paused or reload failed, report
the distinction between the document and its last successful Runtime
projection. While recovery is suspended, further verified edit or
history actions can update the document. Explicit Retry/Apply projects
the latest document without repeating the property transaction.

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
