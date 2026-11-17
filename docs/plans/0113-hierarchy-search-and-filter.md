# Plan 0113 — Hierarchy Search and Filter

- **Spec:** [Spec 0113](../specs/0113-hierarchy-search-and-filter.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Maintainer approved J1–J4 and Spec/Plan 0113 jointly, confirmed no new ADR and authorized S2–S4 on 2026-11-17 at [PR #391, `cecb5769`](https://github.com/slmao/Atlantis/pull/391#issuecomment-6950680552). The design was manually merged at `803fcf8f`.

## Objective and sequencing

Add the [0113](../specs/0113-hierarchy-search-and-filter.md) Hierarchy view
projection and protect edits that would otherwise target hidden selections.
Design baseline: `f3145d9e`, the manual merge of [PR #390](https://github.com/slmao/Atlantis/pull/390).
Start S2 from the latest `main` after joint approval and manual design merge.
Reuse the [0068](../specs/0068-authoring-editor.md) document worker, [0106](../specs/0106-scene-multiselect-batch-edit.md)
selection and batch edits, [0107](../specs/0107-multi-object-gizmo.md) group Gizmo,
and [0112](../specs/0112-unsaved-document-protection.md) leave protection.

## Milestones

1. **S1 — Joint review.** Resolve J1–J4, approve this Spec/Plan together,
   confirm the no-ADR decision and manually merge the design.
2. **S2 — Implement.** Extend the worker's existing Hierarchy snapshot with
   name and component metadata, keeping the complete rows and existing
   incarnation references. Build the UI-free projection over that snapshot.
   Wire the controls, selection synchronization, hidden status and scroll
   restore in Avalonia. Audit guarded command entry points and fence pending
   intents before send using the existing worker checks; keep sent operations
   on their existing settlement and history paths.
3. **S3 — Verify.** Run the mapped affected checks and the Windows 100%
   flow against the final candidate. Record applicable gates, precise
   evidence provenance and any reuse.
4. **S4 — Submit.** Open a review-ready implementation PR with approval,
   acceptance evidence, any deviations and manual merge conditions.

## Expected files and modules

| Scope | Work |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/{HierarchyFilter,SceneMarkers,DocumentTree}.cs` | New UI-free projection and intent rules; reuse canonical tree order, names and types. Keep row reference/incarnation usable by existing operations. |
| `src/editor_app/Atlantis.Editor/{EditorSession,MainWindow}.cs` | Publish metadata with the existing rows and lifecycle; manage the view, focus and status. Guard filtered UI commands and pre-send work without changing shared APIs or replay. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{HierarchyTests,HierarchyFilterTests,IntegrationTest,J7Acceptance,Program}.cs` | Extend existing tests and add focused projection/guard cases. Reuse the existing desktop test entry and disposable scenes. |

No native, SDK, CLI, MCP, shader, source format, archived baseline,
packaging or build-system change is expected. Assess any extra file under
the repository's existing scope rules.

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — Filter scope** | Approve name substring plus one exact schema component, with AND matching, ordered ancestor context and no shared endpoint (R1). |
| **J2 — Selection and reveal** | Keep hidden entries in the selection with a count. Locate reveals all when needed and brings the first hidden row into view. Restore the full expanded presentation and the pre-filter anchor on normal clear (R2, R4). |
| **J3 — Editing safety** | Block row-targeted structure and component changes whenever filtering. Block the entire batch Set/group Gizmo when any selected target is hidden, with current worker-side validation before send and unchanged settlement for already-sent edits. Confirm no new ADR (R3). |
| **J4 — Acceptance and authorization** | Approve the checks below, including the actual Windows 100% flow and existing applicable gates. Authorize S2–S4 together after the human design merge. |

Suggested one-time authorization for the maintainer:

> Approve J1–J4 and Spec/Plan 0113 jointly, including no new ADR. After the
> design is manually merged, implement S2–S4 from the latest main, including
> fixes within scope, applicable verification and a review-ready PR. Reuse
> valid evidence, retain the Windows 100% acceptance and all applicable
> gates, and leave final merge to a human.

## Verification checklist

- [ ] **R1/R4 — Matching and lifetime (model).** Exercise explicit names
  including unnamed, whitespace and case, exact component IDs, AND, an
  unused type, no matches, overlapping paths, and omission of unmatched
  descendants without reordering. Verify recomputation on rename, component
  add/remove, reparent, reorder, delete and reopen. Verify old snapshot and
  same-GUID replacement handling, and clear with a surviving or missing
  anchor and Locate override.
- [ ] **R2 — Selection routing (model/UI).** Preserve full membership and
  existing selection generation across filter changes and programmatic
  refresh. Test plain replace, Ctrl+click and Ctrl+Space toggle across hidden
  entries and at the limit, an ancestor context selection, a Viewport pick
  under a filter and a pick miss. Check Locate for all visible, mixed, all
  hidden and no selection, and verify no extra Viewport camera or overlay
  action from a filter change.
- [ ] **R3 — Edit guard and settlement (Editor integration).** Cover button,
  drag and keyboard routes, including duplicate/delete on a matched parent
  with invisible descendants and neighbouring visible rows separated by an
  invisible sibling. Check hidden-target batch Set/Enter and group Gizmo,
  then allow the existing batch path when all targets are visible. Exercise
  filter changes while an intent is queued, a canceled preview, and an
  external rename or component change observed by the worker before the UI
  updates. Prove no mutation/history/Apply for an unsent refusal and that a
  sent operation still settles exactly once. Keep existing Undo/Redo, Apply
  recovery and 0112 draft/leave tests relevant to the touched paths.
- [ ] **R1–R4 — Actual Windows, 100%.** Use a disposable hand-authored scene
  with nested named and unnamed nodes, different component types and a long
  label. In the standard and compact layouts exercise both filters and
  keyboard routing, context, no results, hidden count, Locate, and restore
  the full tree/scroll anchor on clear. Verify Inspector and Viewport
  selection feedback and that structure actions cannot use filtered rows.
  Try Set with a partly hidden selection, then Locate and Set on the full
  selection. Undo/Redo, Save and Reopen from Source; verify only the intended
  values persist and the filter resets upon confirmed reopen. Check Cancel
  on the existing protection prompt leaves the current filter and draft
  intact. Retain native captures and state or transaction evidence.
- [ ] **Repository gates.** Build the Editor and run affected model, package
  policy, smoke and live integration checks. Run Vulkan validation clean on
  the affected hosted Viewport selection/preview paths. Renderer output and
  capture contracts are unchanged; review any actual rendered change under
  the existing headless/golden rules and reuse matching unaffected evidence
  where justified. Run `atlantis_runtime --gate --applies` on the final full
  implementation diff and follow the original gate protocol if selected.
  Evaluate platform/build coverage from the final diff; no native or Android
  change is proposed. S1 is documentation, link and whitespace checks only.

## Rollback and definition of done

Revert the Editor projection, controls and guards together. Source scenes,
the shared client contract and existing document history format require
no migration. Follow the repository [definition of done](../process/definition-of-done.md).
