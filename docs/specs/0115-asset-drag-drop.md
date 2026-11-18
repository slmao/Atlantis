# Spec 0115 — Asset Drag and Drop

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-18
- **Related Plan:** [Plan 0115](../plans/0115-asset-drag-drop.md)
- **Approval:** J1–J4 jointly approved on revision `c46a4adb`; see [PR #395](https://github.com/slmao/Atlantis/pull/395). No new ADR.
- **Related ADRs:** None new — see Architectural Impact

## Summary

Offer two shortcuts from Assets: drop one asset on a single-selection
Inspector reference field to assign it, or drop one mesh on an explicit
Hierarchy root-add zone to create an object. Both compose existing shared
asset and authoring operations, history and Apply recovery. They do not
change asset registration, scene placement or client contracts.

## Motivation and existing capabilities

Baseline is latest main `f620279e`, the manual merge of
[PR #394](https://github.com/slmao/Atlantis/pull/394).
[0114](0114-assets-search-and-browse.md) provides a virtualized searchable
catalog, GUID selection, hidden-selection protection and isolated details
and PNG results. [0082](0082-generic-asset-reference-picker.md) and
[0084](0084-asset-reference-target-types.md) provide schema-recognized
reference fields, captured-target guards, type metadata and assignment.
[0087](0087-add-mesh-to-scene.md) already creates a root renderable in one
transaction with structural history. The current Hierarchy uses Avalonia's
existing drag facilities for node moves; asset gestures need separate
payload identity and target feedback so those operations cannot collide.

## Goals and non-goals

Deliver one asset gesture and two explicit destination kinds. Retain the
chooser, manual reference entry and Add to Scene button as alternatives.
Exclude multi-asset dragging, batch-reference assignment, material-editor
texture slots, cross-window/process transfer, clipboard export, file import
drops, Viewport hit placement, node-row parenting, custom materials on
creation, new rendering, asset activation and new public APIs.

## Requirements

### R1 — Source and gesture lifetime

Only a ready, visible, selected AssetSummary row in this window's Assets
list can start one left-pointer drag. A nonselected row first follows normal
single-asset selection; the gesture uses that row, never a later selection.
Use the existing pointer movement threshold; normal click, double click,
scrolling and text selection/copy retain their behavior. Headers, details,
preview images and hidden remembered selections are not sources. Never
attach drag state to a recycled list container alone.

Capture an immutable Editor-local intent: a fresh nonce, originating window
and worker/connection identity, accepted catalog/list request identity,
asset GUID/type and source row identity by GUID. A payload token resolves
only to that window's currently active intent; MIME text or a GUID alone is
not authority. Reject tokens from another window/process, files, unknown
formats, old drags and replayed completed tokens. Use Copy feedback, never
Move; assets themselves are unchanged.

Until send, sticky-invalidate the intent if its source becomes deselected,
hidden or missing, Reload begins, the connection/worker changes, or the
window/document closes or reopens. Restoration of the same GUID does not
revive it. Sorting/grouping with the same eligible GUID and accepted catalog
and row virtualization do not themselves invalidate it. Escape, release
outside a target, unexpected capture loss before native drag handoff, and
native cancellation clear hover and cancel an undropped gesture. Normal
native capture transfer is not cancellation. A valid Drop consumes the
payload once and queues one intent; successful native drag completion
clears hover but does not cancel that queued intent. An Escape consumed by
the drag handler before send cancels; do not repurpose Escape after the
native gesture or let it interrupt settlement (R5). Hover, cancellation
and local refusal do not send an edit, write history or request Apply.
An independently completed focus-loss edit is not rolled back (R3).

### R2 — Targets, feedback and final checks

Advertise only a live, enabled Inspector reference row (R3) or the explicit
Hierarchy root-add zone (R4). Use the existing 0093 neutral dark theme,
blue focus border and concise text: asset name, target field or “Add mesh
at scene root”. Include a Copy cursor for provisional eligibility; unavailable
targets have a no-drop cursor and a short reason. Do not use color alone.
Hover is provisional, not a promise of successful commit. Do not synchronously
query Runtime on every pointer move or fetch details/PNG to enable a drop.

Bind hover and Drop to the worker-published identity of the displayed
snapshot, instead of deriving target identity from the current control or
resolving it afresh on the worker. If the displayed row cannot be verified,
refuse it and allow a fresh gesture after refresh. In queued worker work,
query shared assets.get for the captured GUID/type, consume authoring events
and refresh the target. After all lookups and transaction preparation,
recheck the source intent, document lifetime, draft/edit/busy state and
applicable destination guards at the existing serialized send boundary.
For Inspector this includes the captured selection intent, address, node
and component incarnations, Before value/presence and observed property
change stamp. Observed destroy/recreate of the same node GUID, component
remove/re-add, a field change even if restored, selection-away-and-back or
same-path reopen expires the target. For root add, guard the document and
root-add eligibility; Hierarchy selection is not part of that intent.
Neither UI recycling nor a late callback can redirect the edit. A refusal
reports its reason in the current view; it changes no document, history
or Runtime projection through the drop.

Current catalog membership/type is the evidence of asset identity; a
published inactive generation is unavailable. A missing/broken cooked
artifact can still fail during existing transaction/apply validation.
Do not inspect new generations or substitute another GUID. Same-session
assets.get uses the existing immutable Runtime catalog; rechecking it does
not mean detecting filesystem edits or refreshing the snapshot.

### R3 — Inspector assignment and drafts

Support exactly fields accepted by InspectorModel.CanChooseAsset in the
single-selection scene Inspector: writable, readable primitive AssetGuid
with AssetReference metadata, including an optional field whose value is
absent. No hard-coded mesh/material field allow-list. Shared targetAssetType
is authoritative when present and must match the catalog type. An omitted
annotation retains the chooser's generic behavior and warning, including
with an older host that omits metadata. Require a recognized catalog type
matching the captured row even in this case. Malformed schema is an error,
not an unconstrained field. Type agreement does not prove complete asset
combination compatibility. Multi-selection, disabled or unreadable fields
and an open chooser/material modal are ineligible.

Entering either target does not focus it, select a scene node, edit text
or discard input. The source pointer press may already have caused an
ordinary LostFocus commit; settle that edit independently and reflect its
verified document value before advertising a drop. Pending Runtime Apply
alone is not a pending field edit; retain the existing Apply policy. If an
unsubmitted or invalid draft remains or its commit is still pending, refuse
with “Finish or cancel the field edit first”. A new draft before send also
expires the drop. An already-invalidated 0112 copy-only record is not an
editable draft: preserve it for inspection and copy and keep the existing
leave confirmation. A drop must not send that stale value, erase the text,
or cause a second LostFocus write.

Capture the displayed scene address, target type, Before value, document
open lifetime, node/component incarnation and selection intent for the
advertised row. Reuse the chooser's target observation/confirmation rules
without opening the chooser; do not forge a chooser list receipt. Candidate
eligibility comes from the captured Assets snapshot plus worker assets.get.
Use the same Inspector edit plan and ordinary shared SetProperty transaction
as reference selection. An unchanged value is a no-op with no extra history
or Apply. A changed value is one transaction and one ordinary local history
record. External-change checks remain the existing observed-value/event
checks: they do not provide atomic compare-and-swap with another client or
guarantee detection of an unobserved change-and-restore. Host validation is
still authoritative. This design adds no revision precondition to the wire.

After settlement, display the ordinary edit/Apply feedback; restore focus
to the same field's reference launcher only if that field and document are
still current. Never steal focus from a newer modal, gesture or target.
Cancellation introduces no additional focus transition or text restoration.
Undo/Redo use the existing external-conflict guards and value-presence
semantics, including restoring an absent optional reference.

### R4 — Explicit root addition

Provide one always identifiable “Drop mesh to add at scene root” zone in
the Hierarchy pane, outside node rows and its virtualized scroll content.
It coexists with existing controls at standard and compact sizes. It is
available only under the same document/session/gesture/busy rules as Add to
Scene: an open hand-authored scene and a catalog mesh. Other asset types,
node-row centers/edges, empty list whitespace and Viewport are not asset
add targets. Existing node drag reparent/reorder remains distinct.

Root addition is independent of selected nodes, including hidden selected
nodes. It stays eligible with Hierarchy search active, as 0113 already
permits explicit root creation. It never chooses a parent from filtered
rows or silently clears the filter. On confirmed creation keep the Assets
selection, select the created node with existing Add to Scene behavior and,
if hidden by the current Hierarchy filter, use its existing hidden-selection
notice/Locate selection rather than implying the node was not created.

Use the existing AddMesh plan and history with the explicit captured mesh
and document lifetime. Share the Add to Scene button's admission gate and
feedback ownership; neither entry may bypass an in-flight add or overwrite
its result. The drag's pre-send guards do not change button eligibility. Create one
fresh EntityGuid at root in the existing append document position, unnamed,
with origin position, zero rotation, unit scale and absent optional material
(Runtime fallback). One atomic create-plus-mesh transaction and one structural
history record; Undo removes the node and Redo restores the same GUID/data.
No camera placement, world ray query or dependency-based material inference.

### R5 — Settlement, recovery and persistence

A queued Drop can still be refused before send (R1–R3). Once the guarded
send begins, later view changes may suppress gesture feedback, selection or
focus, but cannot abandon settlement in the owning document. For both targets,
use the existing submit/ticket/failure settlement without pipelining Apply.
Associate drained failures with the submitted ticket range and verify the
committed edit in its owning document. A transaction refusal creates no
edit/history/Apply. Publish verified history before the post-edit UI refresh
and before requesting Apply. If history cannot be verified, report that
separately; do not fabricate it or automatically Apply an unverified edit.
Keep Apply correlated independently and retain verified history if Runtime
reload fails. Keep the last successful Runtime projection and use the
existing suspended controller and explicit Apply/Retry policy; recovery
reapplies the current document, never replays the drop.

Distinguish a proved unsent request from a possibly submitted transaction
with a missing or unusable receipt. In the latter case show result unknown
and the scene address or attempted created GUID; direct the user to inspect
on reconnect before retrying. If commit was verified and only Apply's receipt is lost, retain
that commit/history and report only the projection result as unknown. No
automatic retry. Do not replace known history with guessed rollback. Old
callbacks cannot select/focus a target in another document or overwrite a
newer operation's feedback. Preserve the original transport error handling.

Saving remains explicit through the existing hand-authored scene contract.
After Save/reopen on the same active generation retain exact asset/node
GUIDs, hierarchy, transform and optional material presence; reopen clears
local history under the current policy. A reopened authoring document is
not proof that Runtime reloaded it; use the existing explicit Apply when
needed to verify the reopened projection. Other clients can perform these
same assignments/creations through existing shared queries and transactions;
there is no drag-specific engine command or serialized scene field.

### Non-functional

Keep UI-free intent/guard logic in Editor.Model and engine access on the
existing worker via C# SDK. Keep pins, packages and Runtime/frame code
unchanged. Bound state to one active gesture and one admitted operation;
no catalog rescans, repeated remote hover work, GPU resource or PNG work.
Retain list virtualization and 0114 scroll/selection rules.

## Proposed design

Compose a small model-owned asset drag intent with MainWindow's existing
Avalonia pointer/drag adapters, two target adapters and existing reference
and mesh operation paths. Resolve only the current window's opaque token.
Keep eligibility, transient hover and committed operation state distinct.
This also allows delayed-worker and recycled-row tests without native input.

## Architectural Impact

**None. No new ADR recommended.** This is Editor-local composition within
[ADR-0125](../adr/0125-out-of-process-editor-platform-and-uniform-client-interface.md),
[ADR-0135](../adr/0135-two-document-edit-and-apply.md),
[ADR-0136](../adr/0136-structural-edit-records.md),
[ADR-0143](../adr/0143-shared-cooked-asset-catalog-queries.md) and
[ADR-0144](../adr/0144-correlated-authoring-apply-outcomes.md).
The shared schema, asset queries, authoring transaction and Apply contracts
already express both mutations for Editor, CLI, SDKs and MCP. No module,
public API, dependency, thread, file format, RenderGraph or GPU change is
proposed. A need for atomic cross-client conditional assignment would be a
separate reviewed contract, not an implementation choice here.

## Alternatives considered

- Accept plain GUID/file payloads: loses source-session authority and expands
  scope into cross-window/process or import semantics.
- Drop on any Hierarchy row/Viewport: mixes parenting or spatial placement
  with root creation; keep one explicitly labeled destination instead.
- Discard drafts like pointer-launching the chooser: a drop is a different
  gesture and should not silently erase unrelated text; require resolution.
- Add a drag-specific shared API: existing operations already suffice.

## Testing and verification plan

[Plan 0115](../plans/0115-asset-drag-drop.md) maps R1–R5 to deterministic
model/worker checks, real Windows 100% gestures and persistence. Include
reference assignment and root creation, cancellation/refusal, stale inputs,
commit versus Apply unknown results and Undo/Redo. No new engine golden is
expected; reuse only input-matching evidence and evaluate the full final
implementation diff against repository gates.

## Risks and open questions

Joint review must decide source/destination scope, draft and target guards,
existing non-atomic assignment boundary and acceptance (Plan J1–J4). Native
Windows drag cancellation/routing and virtualization need actual pointer
validation; synthetic events alone do not prove the interaction. Root origin
placement can overlap geometry; the created GUID and Locate selection give
unambiguous feedback without expanding placement scope.

## Future work

The non-goals require separate decisions. This proposal stops at design;
implementation requires joint approval and manual design merge.
