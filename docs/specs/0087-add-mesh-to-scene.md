# Spec: Add a Mesh from Assets to the Current Scene

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-09
- **Related Plan:** [Plan 0087](../plans/0087-add-mesh-to-scene.md)
- **Approval:** Pending joint human review
- **Related ADRs:** None new. See Architectural Impact.

## Summary

An explicit **Add to Scene** action in the Editor App's Assets panel creates
one root-level renderable from a selected mesh in the active Runtime catalog.
The action uses one existing authoring transaction and the Editor's existing
apply and history paths. The user can select and edit the new object, undo
and redo its creation, and save it to the current hand-authored scene.

## Motivation / Problem Statement

[Spec 0081](0081-asset-browser-shared-interface.md) exposes cooked asset
identities and [Spec 0086](0086-new-asset-import.md) makes a newly imported mesh
discoverable after publication and explicit Runtime restart. The Hierarchy can
already create a node, and the Inspector can edit asset references, but the
Assets panel has no one-step path from a selected mesh to a new scene object.
The existing Create Child operation shows that the Editor can submit creation
through ordinary authoring commands, record its inverse and select the node.

## Goals

- Add one catalog mesh to the currently open, persistable authoring document.
- Give creation, projection and persistence distinct feedback through existing
  editor behavior.
- Compose existing shared operations and keep document/Runtime ownership intact.

## Non-Goals

Bulk add, drag-and-drop, viewport ray placement, parenting under the selection,
automatic framing, source-derived names, material inference, instantiating an
imported scene or prefab, editing imported scene documents through this action,
and activating a newly published generation from the Assets panel.

## Requirements

### Functional

- **R1 — Eligibility and target.** Offer one explicit action for a selected
  Mesh row in a ready Assets catalog and an open hand-authored scene. Unknown,
  missing, filtered-out, deselected and non-Mesh rows are ineligible. A
  connected imported scene can still use its existing editing workflow but
  cannot persist this new action. Show why the action is unavailable when a
  mesh is selected and the scene is ineligible. Catalog eligibility is based
  on the selected row and a worker-side shared assets.get check of its GUID and
  Mesh type, independent of pending detail-display callbacks. A published but
  inactive generation remains outside the current catalog. Block the action
  during an active Gizmo gesture and suppress duplicate submissions while one
  add is settling. Normal Inspector focus-loss commits keep their own existing
  transaction and history semantics.
- **R2 — One structural transaction.** Capture the mesh GUID, current worker,
  and observed document lifetime at activation. On the worker, observe the
  current document and schema before submitting. Refuse an observed reopen,
  replacement or lost session before submission, even if the scene path or
  GUID is unchanged. Never substitute a later Assets selection for the captured
  mesh. Use a fresh explicit EntityGuid and the existing CreateNode command
  with Renderable in the initial component list, then SetProperty of
  Renderable.meshAsset in the same transaction. Resolve the component and
  property from the authoring schema. Use the existing append position from
  the current observed node order, a root parent, default unnamed node, and
  Transform defaults (origin, zero rotation, unit scale). Leave the optional
  material absent so the existing Runtime fallback supplies the material.
  Asset type establishes eligibility. Existing scene validation and reload
  still decide whether the mesh can be used. An external write between the
  checks and submission remains subject to the existing transaction rules,
  including insertion-position validation. No cross-client compare-and-swap
  is implied.
- **R3 — Settlement, selection and recovery.** A refused validation or
  transaction creates no node or history entry, leaves selection intact and
  shows the reason. The ordinary pipelined edit path can still request an
  apply of the existing document after a refused transaction. An apply result
  never confirms creation. After confirmed document commit, record one structural
  history entry and select the new GUID through the existing Hierarchy/
  Inspector selection path. Keep the Assets tab and its mesh selection in place.
  Selection refers to the document and may precede a Runtime swap. An old
  worker or old document's completion must never select a node or replace the
  current request state. If transport is lost before commit settlement, show
  an unknown result and the attempted EntityGuid when available. Avoid automatic
  resubmission and inspect the document on reconnect before an explicit retry.
  Use the existing
  ApplyController and its correlated outcomes. Paused or pending apply leaves
  the node in the document. Reload failure keeps the committed node, its
  history and the last applied Runtime projection. Use the existing repair,
  undo and explicit Retry Apply workflow, including its conflict and suspension
  rules. Recovery must never submit creation again.
- **R4 — History and persistence.** Undo removes the whole added node through
  the existing structural recorder and its external-change guards. Redo
  reconstructs the same EntityGuid, transform and mesh, with the material
  still absent. Existing selection cleanup on node removal applies. Redo
  preserves ordinary history selection behavior. Save remains explicit and
  uses the ordinary hand-authored source; reopening after Save resolves the
  same GUIDs on the same active generation. A document reopen clears local
  history under the existing policy.

### Non-functional

Use the existing session worker, immutable request values and UI-free Editor
Model. Keep the existing Windows hosting, package/toolchain pins and platform
coverage. Follow the established verification and evidence reuse rules.

## Proposed Design

Put the action next to the existing Assets selection actions. An editor-local
plan composes the shared discovery result, schema addresses and existing
authoring commands. Include Renderable in CreateNode's component list so the
structural recorder captures it in the created node snapshot together with
Transform. The worker submits through the ordinary edit settlement path and
reports the document result separately from the existing apply status. Use
the current selection affordances and ordinary UI feedback, with local request
and document-lifetime guards for this action.

## Architectural Impact

**None.** The existing Editor App/Model and worker boundaries cover the new
composition. The shared asset query, authoring command set, SDKs, wire,
dependencies, formats, Runtime ownership and apply/reload semantics stay within
[ADR-0125](../adr/0125-out-of-process-editor-platform-and-uniform-client-interface.md),
[ADR-0135](../adr/0135-two-document-edit-and-apply.md),
[ADR-0136](../adr/0136-structural-edit-records.md),
[ADR-0143](../adr/0143-shared-cooked-asset-catalog-queries.md) and
[ADR-0144](../adr/0144-correlated-authoring-apply-outcomes.md).
CLI, native/C# SDKs and MCP already have the shared queries, generic
transactions and apply operations to compose the same document mutation.
History and selection remain editor-local.

## Alternatives Considered

- Create a blank node and edit it in subsequent transactions. This introduces
  intermediate state and breaks the single history step.
- Place relative to camera, cursor or current parent. Each introduces placement
  choices and failure cases beyond a deterministic default.
- Select or synthesize a material during creation. The existing optional
  binding and fallback already support first placement. Material selection is
  a subsequent ordinary Inspector edit.

## Testing & Verification Plan

[Plan 0087](../plans/0087-add-mesh-to-scene.md) maps R1–R4 to model, real
Windows Editor and Runtime checks. The main fixture begins at managed import,
explicitly activates the published generation, adds the mesh to a disposable
hand-authored scene, verifies its projection and history, then saves and
reopens the scene. Negative cases distinguish transaction refusal, failed
Runtime reload and unknown document settlement.

## Risks & Open Questions

Origin placement can overlap geometry or be outside the current view. The
Hierarchy selection identifies the new object and exposes its Transform for
editing. Joint review must decide the scope, placement, settlement and
verification questions in the Plan before implementation.

## Out of Scope / Future Work

Keep the Non-Goals separate. Live asset activation and richer placement can
be considered independently of the transaction defined here.
