# Plan: Add a Mesh from Assets to the Current Scene

- **Spec:** [Spec 0087](../specs/0087-add-mesh-to-scene.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Owner approved J1–J4 and Spec/Plan 0087 together at revision `aef1a45e` and authorized S2–S4 in chat on 2026-11-09. [PR #337](https://github.com/slmao/Atlantis/pull/337) was manually merged as `dbb6f57e`. The authorization covers scoped fixes, applicable verification, valid evidence reuse and a review-ready implementation PR, with existing gates and manual final merge.

## Objective

Deliver one Assets action that adds a selected cooked Mesh to the current
hand-authored scene using the existing shared transaction, tracked apply and
Editor structural history.

## Milestones / Task Breakdown

1. **S1 — Design.** Review this Plan and Spec together. Record J1–J4 and
   explicit implementation authorization in the review PR.
2. **S2 — Compose and settle one edit.** Add the UI-free transaction plan,
   reuse the structural recorder and settle submit/drain before using the
   existing automatic apply policy. Keep the document guard and immutable
   mesh target local to this action.
3. **S3 — Assets action and feedback.** Wire the action and eligibility to
   the existing panel and Hierarchy selection. Exercise refusal, pending
   apply, recovery, and late completion in model or UI fixtures as mapped below.
4. **S4 — Verify and submit.** Run the mapped checks, record candidate and
   evidence provenance, and submit a review-ready implementation PR.

## Files / Modules Touched (expected)

- Editor Model: add the operation to
  [HierarchyCommands.cs](../../src/editor_app/Atlantis.Editor.Model/HierarchyCommands.cs).
  Reuse the existing EditPlan and structural record shape. Adapt the local
  ApplyController in [DocumentTree.cs](../../src/editor_app/Atlantis.Editor.Model/DocumentTree.cs)
  to handle an already settled local edit. Use
  [AuthoringDocument.cs](../../src/editor_app/Atlantis.Editor.Model/AuthoringDocument.cs)
  if needed to preserve event observation errors for this action. Keep the
  existing pipelined edit path for other actions.
- Editor App: [EditorSession.cs](../../src/editor_app/Atlantis.Editor/EditorSession.cs)
  for worker-side eligibility, settlement and document observation, and
  [MainWindow.cs](../../src/editor_app/Atlantis.Editor/MainWindow.cs) for the
  action and guarded feedback.
- Editor tests: [InspectorTests.cs](../../src/editor_app/tests/Atlantis.Editor.Tests/InspectorTests.cs)
  for plan/history checks and the existing Windows imported-mesh fixture.
  [Program.cs](../../src/editor_app/tests/Atlantis.Editor.Tests/Program.cs) only
  if needed to invoke a focused model check.
- Documentation: [README](../../README.md) for use and
  [Spec registry](../specs/README.md) and [blueprint](../project-blueprint.md)
  for delivery navigation.

Runtime, shared protocol/schema, SDKs, CLI, MCP, cooker, build/CI inputs and
committed goldens are outside the expected implementation diff. Record any
necessary deviation under the repository's existing workflow.

## Sequencing & Dependencies

Start S2 from current main after explicit joint approval and manual merge
of the design. Reuse the existing node, history and apply paths (0068/0083),
shared catalog (0081), typed asset metadata (0084) and managed import (0086).
S2–S4 can proceed continuously under one human authorization.

| Decision | Recommendation |
| --- | --- |
| J1 — Scope and defaults | Approve one selected Mesh in a hand-authored scene, root placement with identity Transform, existing unnamed label and absent material. |
| J2 — Composition and races | Approve CreateNode with Renderable plus SetProperty in one transaction, explicit EntityGuid, and request/worker/document-open guards. Keep existing non-atomic cross-client semantics. No new ADR or shared API. |
| J3 — Feedback and recovery | Settle submit/drain before automatic apply. Select and record history after confirmed commit. Distinguish unknown commit from known commit with unknown apply. Keep ordinary undo/redo/save and explicit recovery. |
| J4 — Acceptance and delivery | Approve the focused model/UI and import-to-reopen flow below with existing gate rules. Authorize S2–S4 through a review-ready PR after manual design merge. |

Suggested joint authorization for the current reviewed revision:

> I approve J1–J4 and Spec/Plan 0087 together. After manual merge of the
> design PR, proceed from current main through S2–S4, including scoped
> implementation fixes, applicable verification, reuse of valid evidence,
> and submission of a review-ready implementation PR. Preserve existing
> gates and leave final review and merge to a human.

## Verification Checklist

- [ ] **S1 design.** Check numbering, document links, statuses, scope and
  whitespace. This design-only PR needs document checks.
- [ ] **R1/R2 model and UI.** Check Mesh vs. non-Mesh, deselection/filtering,
  a pending display callback, absent or imported document, active gesture,
  duplicate activation and an observed reopen or worker replacement. Cover
  a same-source successful reopen, a refused reopen, and failed event
  observation before submission. Verify
  current GUID/type and schema failure cause refusal, captured GUID is used
  if selection changes, and exactly one command batch creates one unnamed
  root with identity Transform, correct mesh and absent material.
  Verify a transaction refusal keeps node count, selection and history intact
  and issues no apply from this action. Verify external changes to the
  created node block destructive undo, and a conflicting GUID blocks redo.
- [ ] **R3 settlement and recovery.** Test late feedback after reopen or
  replacement, loss before transaction settlement, and loss of the apply
  reply after known settlement. Check that a node changed before history
  capture does not receive a destructive undo record. Check that a reopened
  document cannot receive the old request's selection, history or apply when
  the reopen was observed before that action. Check pending apply coalescing and a suspended
  edit waiting for explicit Retry. On a disposable generation, start Runtime
  and verify discovery, then withhold an as-yet-unloaded mesh artifact before
  the add's apply and restore it after the correlated reload failure. Verify
  the old Runtime projection, committed node GUID and one history entry, then
  explicitly Retry Apply and verify the same node renders. No recovery submits
  a second creation.
- [ ] **R2–R4 live acceptance.** In a disposable hand-authored scene with an
  origin-visible camera, use a valid self-contained 0086 glTF whose mesh has a
  distinguishable render. Import and publish using the existing path. Verify
  the active old catalog omits it and explicitly restart with the new
  generation. Select the mesh in Assets and add it. Check node count +1,
  root identity Transform, exact mesh GUID, absent material, Hierarchy
  selection and Inspector subject. With Runtime paused, verify the committed
  node is selected while apply is pending and the old projection remains.
  Step or Resume, verify the receipt reaches Applied and capture the added
  draw using the existing fallback. Undo removes the node and draw, redo
  restores the same node GUID, mesh, transform and draw. Save explicitly and
  verify both GUIDs in source and the generation files unchanged. Restart on
  the same generation, reopen the saved source, and confirm the same node and
  rendered mesh with fresh session history.
- [ ] **Applicable gates.** Run affected Editor builds and model/UI checks,
  live Vulkan validation and the established full-diff performance
  applicability selector. Run or justify reuse of applicable headless/golden
  checks, with candidate/configuration, unchanged inputs and evidence cited
  in the implementation PR. The disposable scene capture is functional
  evidence. Do not update goldens or baselines as part of this feature.

## Rollback Plan

Revert the Editor action and its model composition. Existing scene source,
asset registrations and published generations use their existing formats.
An object already added can be removed with ordinary structural editing.

## Definition of Done

See [Definition of Done](../process/definition-of-done.md). Implementation
starts only after joint authorization and manual design merge. The
implementation PR records scope, results and any reuse. Merge is manual.
