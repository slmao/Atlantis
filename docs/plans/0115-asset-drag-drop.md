# Plan 0115 — Asset Drag and Drop

- **Spec:** [Spec 0115](../specs/0115-asset-drag-drop.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending, including Spec 0115 and S2–S4 authorization

## Objective and baseline

Add the two Editor-local gestures in Spec 0115 without changing their shared
mutation contracts. Start from latest main after joint approval and manual
design merge; design baseline is `f620279e` ([PR #394](https://github.com/slmao/Atlantis/pull/394)).
The 0114 implementation is merged; its approval and evidence remain in that PR.

## Milestones and dependencies

1. **S1 — Joint review.** Resolve J1–J4 together. Confirm the no-ADR judgment,
   scope and evidence boundaries. Submit design only; do not implement before
   explicit joint approval and manual merge.
2. **S2 — Implement.** Model one local source intent and advertised target;
   wire visible Asset rows and the two targets with distinct drag formats
   from Hierarchy moves. Reuse schema recognition, reference target guards,
   assignment and AddMesh settlement/history. Share busy gates and preserve
   source/target checks at the actual send boundary. Apply current feedback
   and focus policy; do not add engine or protocol capabilities.
3. **S3 — Verify.** Complete mapped checks below against the final candidate,
   including Windows pointer gestures. Retain initial failures and explain
   fixes; reuse unaffected evidence only with matched inputs and provenance.
4. **S4 — Submit.** Push an implementation PR linking approval, deviations,
   evidence and manual merge conditions. Do not merge it.

## Expected files and modules

| Files | Purpose |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/AssetDragDrop.cs` (new) | UI-free captured source/target intent, eligibility and sticky cancellation/admission state. |
| `src/editor_app/Atlantis.Editor.Model/AssetReferencePicker.cs` | Reuse/extract the existing target guards for chooser and drop without fabricating list receipts or changing chooser semantics. |
| `src/editor_app/Atlantis.Editor/{MainWindow,EditorVisuals}.cs` | Source pointer adapter, local payload validation, reference-row targets, explicit root zone, feedback and focus; retain virtualized row state. |
| `src/editor_app/Atlantis.Editor/EditorSession.cs` | Worker asset/target refresh, final intent checks and composition with existing edit and AddMesh settlement; preserve transport/history/Apply separation. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{AssetDragDropTests,AssetDragDropUiTests}.cs` (new) | Focused model/worker races and hosted interaction/persistence checks. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{Program,InspectorTests,AssetReferencePickerTests,AssetBrowserTests}.cs` | Register new checks and preserve existing chooser/browse behavior. |

No build/package/CI, SDK, MCP, native engine or asset file is planned. Use
existing test project globbing and pinned Avalonia drag facilities. Record
any necessary nonsemantic file-list adjustment in the implementation PR
under the existing approval rule; design changes return to joint review.

## Joint review recommendations

| Decision | Recommendation |
| --- | --- |
| **J1 — Interaction** | Approve one visible Assets row in the same window; one single-selection Inspector reference row or an explicit Hierarchy root-add zone. No node-row/Viewport targets or material-dialog texture slots (R1–R4). |
| **J2 — Identity, drafts and compatibility** | Approve immutable source/catalog and target-incarnation guards, checks after queued lookups, unresolved-draft refusal, shared target metadata and existing generic fallback. Retain the observed, non-atomic reference-edit boundary; no new ADR (R2–R3). |
| **J3 — Mutation and recovery** | Reuse the ordinary assignment and root-add transactions, histories and duplicate gates. Separate commit from Apply, preserve unknown receipts and require explicit recovery; retain default unnamed origin placement and Runtime material fallback (R3–R5). |
| **J4 — Acceptance and authorization** | Approve the mapped verification below, including actual Windows 100% pointer drags and Save/reopen. After human approval and manual design merge, authorize S2–S4 as one execution (including ordinary scoped fixes). |

Suggested one-time authorization:

> Approve J1–J4 and Spec/Plan 0115 jointly, including no new ADR and the
> existing non-atomic assignment boundary. After the design PR is manually
> merged, execute S2–S4 from latest main: implement, repair ordinary errors
> within scope, verify and submit a review-ready PR. Reuse valid evidence,
> retain Windows 100% acceptance and applicable repository gates, and leave
> final merge to a human.

## Verification checklist

- [ ] **R1–R2, model/worker.** Source row identity under virtualization,
  selected/hidden/removed states, sort/group retention, reload epoch and
  disconnect/reconnect. Reject another window, arbitrary GUID/file payloads,
  expired/replayed tokens and headers. Delay asset/target queries, change
  source eligibility or destination selection while queued, then verify no
  transaction/history/Apply. Same-GUID node replacement and component
  remove/re-add must invalidate even before UI catches up. Distinguish
  changes before admission from view changes after a sent request.
- [ ] **R2–R3, reference checks.** Typed mesh/material fields, optional
  absent value, generic metadata omission, wrong type, malformed schema,
  unreadable field, multi-selection and same-value no-op. Exercise ordinary
  blur commit separately from unresolved/invalid/stale draft refusal,
  cancellation and no duplicate LostFocus write. Compare shared document
  values before/after; one changed reference gives one transaction/history
  entry. Undo/Redo restore exact GUID and absent presence; outside changes
  conflict without overwriting them. Existing chooser/manual edit paths
  and focus restoration still pass after target-guard reuse.
- [ ] **R4, root creation.** Correct type and hand-authored provenance,
  default transform/name/material, append order and fresh node GUID; root
  zone only. Reject scene/imported-document, non-mesh and node-row/Viewport
  drops. Test active Hierarchy filter/hidden selection without implicit
  parenting or filter clearing. Check Add button/drop duplicate admission,
  delayed Apply feedback ownership and no second node/history/apply on a
  duplicate. Undo removes the node; Redo restores its original identity.
- [ ] **R5, failure and recovery.** Host refusal, missing/broken artifact,
  Apply pending/failed/superseded, before-send disconnect, lost transaction
  receipt and verified commit with lost Apply reply. Assert no automatic
  replay; verified history publishes before refresh; explicit Apply repairs
  projection without another edit. Late callbacks after same-path reopen or
  reconnect cannot change focus/selection/feedback in the replacement view.
- [ ] **R1–R5, actual Windows 100%.** Use native pointer drags in standard
  and compact windows. Retain screenshots of valid reference/root feedback
  and rejected target feedback, plus state/request evidence. Exercise
  scrolling a virtualized source, Escape, release outside, wrong type,
  focus/draft handling and unchanged Hierarchy node drag routes. Synthetic
  routed events supplement, but do not replace, native gesture evidence.
- [ ] **R3–R5, complete persistence.** Use an already published imported
  mesh and an existing compatible material in the active catalog. Drop the
  mesh at root, verify selection/defaults, Undo/Redo once, then drop material
  onto that node's reference field and verify one Undo/Redo. Edit Transform,
  explicitly Save and reopen the same hand-authored scene on the same active
  generation. Verify exact node/mesh/material GUIDs, values and visual
  projection; reopen clears local history. New import/publish and 0090
  build/restart evidence may be reused when their inputs are unchanged;
  these are not new workflows in this feature.
- [ ] **Repository gates.** Locked Editor build, affected model/worker,
  package policy, smoke, browse/reference and preview live checks. Clean
  Vulkan validation on affected Runtime/preview paths. Reuse unchanged
  renderer goldens with source/asset/build provenance. Evaluate full final
  diff with `atlantis_runtime --gate --applies`; follow the existing protocol
  if selected, otherwise explain N/A. Assess platform/build coverage from
  the final diff; managed-only UI adds no Android execution requirement.
  S1 requires document/link/whitespace checks, no engine rebuild.

## Rollback and definition of done

Revert gesture adapters and local guard code together, retaining existing
chooser and Add to Scene operations. No scene/asset migration or cleanup
is needed. Follow [Definition of Done](../process/definition-of-done.md);
no additional mandatory gate beyond the mapped checks is introduced.
