# Spec 0107 — Multi-Object Gizmo Transforms

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-15
- **Related Plan:** [Plan 0107](../plans/0107-multi-object-gizmo.md)
- **Approval:** Maintainer approved the revision at [PR #379, `24827821`](https://github.com/slmao/Atlantis/pull/379) on 2026-11-15.
- **Related ADR:** [ADR-0163](../adr/0163-guarded-transform-batches.md) (Accepted)

## Summary

Move, rotate, or uniformly scale a Hierarchy selection about one
Viewport Gizmo. A completed drag writes the affected local transforms
as one revision-guarded authoring transaction. One Undo restores the
original transforms, and ordinary Apply and Save project and persist the
result.

## Motivation and scope

[Spec 0106](0106-scene-multiselect-batch-edit.md) supplies a bounded
selection, Viewport highlights, and one-field batch edits. Its revision
guard admits one leaf field per batch. [Spec 0068](0068-authoring-editor.md)
supplies single-object handles, Runtime World previews and local history.
Orbiting a selection around a common pivot can change several local
Transform fields per node. The point of this feature is to commit those
fields and nodes together, including when another client is editing.

The first release covers the existing 2–64 live-node selection in one
editable hand-authored document. Translation and rotation use world XYZ
handles. Scale uses one positive uniform factor. Local group axes,
individual origins, custom pivots, nonuniform group scale, geometry
previews during group drags, and new selection or structural actions are
deferred. Single-selection tools and common-field Inspector editing
retain their existing behavior.

## Requirements

### R1. Targets, coordinates and handles

Keep the full selection highlighted and show one Gizmo. The transform
roots are selected nodes with no selected ancestor, even through
unselected intermediates. Order roots by the captured document node
order. The pivot is the arithmetic mean of their authoring world
origins, including roots with no mesh or marker. With one effective
root, use its origin and keep group behavior. Write only the effective
roots. Selected and unselected descendants inherit the root's change
through hierarchy, preserving their local properties. Their origins
carry no weight in the pivot.

Use the existing world-axis handle geometry and direction conventions
for Translate and Rotate. Use a distinct center handle for uniform
Scale, with horizontal movement using the existing scale sensitivity
of one factor unit per 80 Viewport pixels.
Show World as the available group space and label uniform Scale in the
current [0093 visual conventions](0093-editor-visual-layout.md).
Restore single-selection settings on returning to one node.
An unavailable gesture shows its reason and keeps the requested mode
visible. Snap using the existing increments of 0.5 world units, 15
degrees and 0.1 of a factor. Freeze mode, axis, snap, pivot and baselines
on grab. Follow the existing embedded Viewport coordinate path for both
hit tests and drag input.

### R2. Group transform and preview

Compute each sample from the frozen baseline. For each root with world
matrix `W`, world pivot `p` and parent world matrix `P` (identity at
scene root), apply the same world delta `D`. Translation uses
`D = T(delta)`. Rotation uses `D = T(p) R(axis, angle) T(-p)`.
Uniform scale uses `D = T(p) S(factor) T(-p)`, with the existing
positive factor floor of `1e-4`; a negative pointer-derived factor
clamps at the floor. Solve the local matrix as `inverse(P) D W`.

Translation preserves the original Euler and scale leaves exactly.
Uniform scale preserves Euler values and multiplies the original signed
local scale by the common factor. Use the solved local translation.
For rotation, validate the solved basis with the existing TRS
decomposition and apply its reflection and Euler conventions. Keep the
captured Euler and scale together if they reproduce the solved basis.
Otherwise use the decomposed Euler and scale, retaining the captured
scale when paired with the solved Euler it still reproduces the basis.
Omit exactly unchanged typed leaves. Check that each recomposed basis
column differs from the corresponding solved column by at most the
existing Gizmo tolerance (`1e-4`) times the length of the solved column,
with handedness preserved. The normalized shear and minimum axis
length checks in the existing decomposition also apply. Normalize
identity deltas before any decomposition, and end an identity or
zero-write drag without writing.

A missing or unstable baseline, nonfinite result, non-invertible
parent, or nonidentity result without a usable TRS refuses the entire
sample. A rotation under a nonuniform parent can introduce shear.
Report the reason and first affected root in captured order. An
invalid sample at release refuses the drag even if an earlier sample
was valid. Solve from the LeftUp coordinates even when earlier moves
were coalesced.

Reuse the shared per-client overlay for the handles and bounded
destination guides. Show the provisional root origins and their
orientation, using up to one point and three short segments per
effective root. Clear destination guides on an invalid sample and show
why. Use the same selected highlights on the current Runtime
projection. The viewport and status distinguish guides from applied
geometry. [ADR-0163](../adr/0163-guarded-transform-batches.md) defines
preview ownership. Use the latest sample on the existing worker path.
Before dispatch, a refused overlay update cancels the gesture. Replace
its contribution with the ordinary selection overlay, or clear that
contribution if the replacement is refused. Send updates and clears in
order on the same connection. A late sample from a canceled or released
gesture cannot restore its guides.

### R3. Gesture invalidation and shared submission

Arm only with a stable view of the current writable document and a
positive transform-transaction capability. The pointer must grab a
handle from the same layout and selection the Editor displayed. If
preparation finds stale placement inputs, refresh the layout and ask
for a new grab. A grab freezes the document lifetime, selection
generation, selected-node identities, and parent edges and node
identities along their full paths to the scene root. It also freezes
the three Transform leaves and component identities of the effective
roots and the ancestors used to solve their matrices. A selected
descendant's parent path is relevant even though its local values
inherit the edit without a write. Missing or ill-typed Transform
reads cannot provide a baseline, including in the display-to-grab
check. Invalidate a grab or queued release on changes to these
dependencies, including away-and-back edits and destroy/recreate with
the same GUID. An unrelated change settled before final preflight can
coexist. Missing events, incomplete reads, or a change across the
final revision fence refuse the attempt.

Escape while the Viewport or Editor owns focus, unexpected loss of
drag capture, deactivation, Viewport resize, release outside the
Viewport, selection or mode change, reopen and disconnect cancel an
unsent drag. Suppress camera navigation input during a grab. Use the
existing focus and keyboard routes and the Runtime child's capture
notification in [ADR-0163](../adr/0163-guarded-transform-batches.md#decision).
A valid in-bounds LeftUp ends the left-button grab and queues
settlement. A subsequent routine capture-loss notification does not
cancel that release. Escape, deactivation or a change of editing context
can still cancel it before send. Discard canceled guides and pending
samples. An old release cannot pick, restart or submit, including
after returning to the same selection. A group drag and its settlement
serialize with existing Inspector commits, property replay and other
Gizmo edits.

On release, read the frozen dependencies through the bounded shared
reads with event observation and a revision fence. Build the ordered
set of changed leaves in root order and position, Euler, scale order.
After capability discovery, recheck gesture, selection and lifetime
immediately at send, using the same serialization point as selection
changes. Submit once through [ADR-0163](../adr/0163-guarded-transform-batches.md).
The host checks the revision and admits or refuses the whole candidate
at its transaction boundary. After send, settle the original write set
even if the selection changes. A replacement document receives none of
the old gesture's UI callbacks or history. Missing capability leaves
selection, overlays and other supported editing available.

### R4. Settlement, Undo/Redo and Apply

Use [Spec 0106 R3](0106-scene-multiselect-batch-edit.md#r3-shared-settlement-and-undoredo)
for ticket-range and failure-drain settlement, correlation and
revision-fenced verification of ordered events and final values.
Record one grouped history entry only for a verified commit. Keep
each changed leaf's exact original and final value and the placement
context for the roots. Confirmed refusals add no history or Apply.
Publish confirmed settlement and history before fallible refreshes.

Undo/Redo use the same guarded transaction and a fresh fenced revision,
with the original root and Transform incarnations, ancestry identities
and parent edges, and exact expected Transform values on all effective
roots and their ancestors. This includes roots with no changed leaves
and unchanged leaves on changed roots. Replay recorded values without
recomputing a pivot. Eligibility uses the identities, edges and
values of the current side of the entry. An intervening local
Undo/Redo that has restored those values does not invalidate it.
A relevant mismatch refuses the whole replay and drops the conflicted
entry with a reason. A revision-only refusal retains it on its original
stack for explicit retry. A verified replay moves one entry. An unknown
or overtaken settlement follows 0106's rules for history and explicit
Apply, with the mode and affected roots in the notice. Inspect a fresh
authoritative snapshot before retrying an uncertain action. Preserve
settlement of any older owned Apply, including when the new transaction
result is unknown.

One verified commit or replay creates one ordinary tracked Apply
intent, subject to existing coalescing and recovery. Correlate the
receipt with the revision actually snapshotted. On a paused, failed or
unknown reload, distinguish committed document and Runtime projection.
A verified edit keeps its history even if its Apply receipt is lost.
While recovery is suspended, allow verified edits to the document
without automatic Apply. Explicit Apply snapshots the current revision
without repeating the transform or adding history. No guide survives
the drag, cancellation or settlement. The last successful projection
remains authoritative for displayed geometry until a reload succeeds.

### R5. Persistence

After the authored edit settles, use ordinary Save on the hand-authored
source. Reopen that same source
on the same active asset generation. The multi-selection, gesture and
local history clear, and the saved locals, hierarchy and asset
references recover by GUID. Explicit Apply of the reopened document
rebuilds the matching world placements.

## Architectural Impact

[ADR-0163](../adr/0163-guarded-transform-batches.md) supplements the
shared transaction contract and records the multi-object preview
boundary. It extends the existing Windows embedded input route with
the capture-loss notification. The Editor owns the gesture and its
history. Authoring owns admission. Connection and existing clients
carry the command. Reuse the Runtime overlay, scene reload and source
format.

## Testing and Verification

[Plan 0107](../plans/0107-multi-object-gizmo.md#verification-checklist)
maps the requirements to model, shared-client, failure and Windows
100% Viewport-to-reopen verification, plus the applicable existing gates.
