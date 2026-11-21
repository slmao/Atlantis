# Spec 0128 — Place One Static Mesh on a Viewport Surface

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-21
- **Related Plan:** [Plan 0128](../plans/0128-viewport-mesh-placement.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0177](../adr/0177-editor-viewport-placement-boundary.md) (Proposed)

## Summary

Drag one static mesh from Assets onto visible geometry in the hosted
Viewport of a hand-authored scene. Show its proposed pivot while hovering.
A valid release places one root renderable. One Undo removes it and one Redo
restores it. The first use case is placing a chair on the Bistro café terrace.

## Motivation and existing capabilities

[0115](0115-asset-drag-drop.md) supplies the window-local Assets token,
source and draft guards, and Inspector and Hierarchy drops.
[0087](0087-add-mesh-to-scene.md) supplies root creation, structural history
and material fallback at the origin. [0109](0109-viewport-navigation.md)
supplies the confirmed camera and the projection used for geometry picking.
The shared raycast returns the nearest renderable GUID and distance along
a world-space unit ray. [0111](0111-viewport-camera-light-guides.md) and
[ADR-0132](../adr/0132-viewport-overlay-surface.md) provide transient
primitives on the existing per-client overlay.

## Goals and non-goals

Place one catalog Mesh at the cursor in an editable, persistable scene and
preserve the existing click, Add to Scene and drag-and-drop alternatives.
Exclude automatic orientation, surface classification, snapping, ground-plane
fallback, mesh ghosts, multi-asset placement, parent-to-hit, Prefab, cross-window
or file drops, and imported-scene conversion.

## Requirements

### R1 — Source, destination and input

Reuse 0115's active token for the visible, selected Assets row in this Editor
and admit only a catalog Mesh. Require live authoring for a hand-authored
document, a ready hosted Viewport and a confirmed projection. Only the drawable
child's client pixels are eligible. Toolbar, chrome, occluded regions and
another window are outside the target. Leaving for an existing 0115 target
clears the Viewport preview and lets that target handle its own drop.

Apply 0115 draft, busy and source rules to all stages. Allow the original
Assets press's normal focus-loss edit to settle independently. While an
editable draft or commit is outstanding, show a short reason and refuse
placement. Preserve 0112 copy-only text and its existing leave rules.
Placement hover never takes focus, changes selection, or invokes ordinary
click picking, navigation or Gizmo hit tests. Use the Editor-owned native
input boundary in ADR-0177.

### R2 — Hit and preview

Convert the pointer to the same physical-pixel rectangle, confirmed camera
and NDC convention as geometry picking. Use the nearest mesh raycast alone.
Camera/light markers, Gizmos and overlay primitives are not placement surfaces.
Reject a miss, `NoGeometry`, an unavailable projection, an invalid hit or a
non-finite result. Compute the pivot as `origin + unitDirection * distance`
for a finite positive distance within the confirmed camera clip range
(check view depth against the lens, not radial distance).

Draw a small screen-constant cross and center point anchored at that world
position with existing overlay primitives. Use an on-top, non-pickable mark
for legibility at contact and a short Viewport status with asset name and
pending, valid or refused state. Advertise Copy only for a current candidate.
Compose the mark with the existing Editor overlay so selection, Gizmos and
guides retain their normal behavior and budget. If the mark cannot be
published within that budget, clear it and refuse placement. No preview
entities, asset loads, PNG requests or document changes are needed.

Use at most one queued/latest pointer sample and one hover ray in flight.
Sample movement at up to 15 Hz while over the Viewport, coalesce newer
samples and do no placement polling while idle. Retire a candidate on
pixel/extent or view change, leave, cancellation, or loss of eligibility.
A late response may update neither the mark nor a queued drop. Remove only
this preview on exit and preserve the latest ordinary overlay. Disconnect
uses the existing per-client cleanup.

### R3 — Release and expiry

Consume the 0115 token once only if its advertised candidate is current
at the release pixel. A release before the current mark is ready, after
Escape/capture loss, or on an invalid point sends no mutation, writes no
history and issues no Apply. Keep a queued release distinct from native
drag completion. Show a reason for an expired or refused placement.

Bind the candidate to the source/catalog/worker, document-open lifetime,
observed document revision, confirmed camera/extent and Runtime
`SceneGeneration`, hit GUID and exact ray/distance. Require the hit GUID
to resolve in the current authoring tree and capture its observed node
incarnation. The hit is on the displayed Runtime projection even if the
document contains unapplied edits. While projection settlement is pending
or unknown, suspend placement. A camera change,
client-area change or observed document/Runtime projection change retires
that candidate. A later preview needs a fresh observation. Source loss
and document reopen are sticky invalidations of the intent. Before sending,
repeat `assets.get`, consume authoring events, verify the same hand-authored
document and its observed revision, and recast the **release ray**. Require
the same first hit GUID and finite distance with no intervening observed
generation change. Use the very same ray values for that comparison.
Refuse a changed distance, an observed hit-node replacement or a changed
current view; never silently move the placement to the new hit. Check the
preconditions again at the serialized Editor send boundary after preparing
the transaction.

The existing shared reads and root-create transaction are separate
operations. This guards observed changes and rechecks geometry, while
another client can still change the scene after the last observation.
The host remains authoritative about transaction validity. A concurrent
change-and-restore the Editor cannot observe, or a competing client camera
override outside the existing camera ownership/confirmation, is outside
this guarantee. No guarded-create variant of the shared protocol is proposed.

### R4 — Creation, settlement and persistence

Create one fresh, unnamed root node at the document's append position with
the captured mesh GUID. Local position equals the world hit because the
node has no parent. Identity rotation, unit scale and the existing absent
material override match Add to Scene. Mesh local origin is its pivot.
There is no bounds-bottom offset. A mesh with feet away from its origin
retains that offset. The Bistro chair fixture has its pivot at its feet.

Use one existing authoring transaction for CreateNode plus the mesh and
position SetProperty commands. Share Add to Scene admission and feedback
with both existing root-add entrances. Verify the returned edit, including
parent, position and mesh, against the same document before recording one
structural history entry. Select the new node on verified creation if the
UI intent is still current, using the existing hidden-selection feedback.
One Undo removes the node and one Redo restores the same GUID and placement
under the existing history conflict guards.

Publish verified history before refresh and independently settle the
correlated Apply through the existing controller. Known refusal performs no
edit/history/Apply. With unknown transaction or unverifiable history, show the
intended node/mesh and ask the user to inspect before retrying. Do not
automatically replay or Apply it. If Apply fails or is unknown, preserve the
verified edit and history, retain the established projection/recovery
semantics and offer explicit Apply after the user resolves the issue. Later
UI changes after send may suppress view feedback but cannot abandon
settlement. Save and reopen through the existing scene path. The same
GUID, root placement and mesh binding persist and no overlay data is saved.

## Proposed design and architectural impact

Editor.Model owns transient intent and geometry math. MainWindow admits
drag input and publishes view/asset identity. EditorWorker owns the bounded
queries, overlay updates and transaction settlement via the C# SDK.
[ADR-0177](../adr/0177-editor-viewport-placement-boundary.md) records the input
and projection boundary around the native child. Existing shared
`assets.get`, `control.raycast`, `control.setViewportOverlay` and authoring
operations serve all clients. No new wire method, module dependency, package,
asset format, Runtime resource or frame path is proposed.

## Alternatives considered

Keep origin-only Add to Scene as the explicit alternative. A fixed ground
plane can place into empty space or at the wrong elevation. An AABB offset or
ghost needs more mesh data and changes the meaning of the asset pivot.
Server-side guarded placement would require a separate cross-client
contract.

## Verification and open questions

[Plan 0128](../plans/0128-viewport-mesh-placement.md) maps native Windows 100%
input, Bistro placement, stale releases, history, Save/reopen and the
applicable gates to R1–R4. Joint review resolves J1–J4, including the
native input route and observed concurrency boundary. Prepare the Bistro
fixture through the existing content and Saved Scenes paths, without
expanding imported-scene editing.
