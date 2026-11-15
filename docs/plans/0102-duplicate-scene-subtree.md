# Plan: Duplicate a Scene Node and Its Subtree

- **Spec:** [Spec 0102](../specs/0102-duplicate-scene-subtree.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Atlantis maintainer, 2026-11-15, [PR #369](https://github.com/slmao/Atlantis/pull/369) at `996edd11`. Approved J1–J4 and authorized S2–S4 after human merge at `8c882a93`.
- **Related ADRs:** None new. See the Spec's Architectural Impact.

## Objective

Deliver one bounded Hierarchy duplicate action using existing authoring
commands, preserving the distinction between document commit, Runtime
projection and persistence.

## Milestones / Task Breakdown

1. **S1 — Joint review.** Approve the Spec, Plan and J1–J4 together.
   The maintainer merges the design before implementation starts.
2. **S2 — Implementation.** Plan the copied states in the UI-free
   Editor Model from `DocumentTree`, schema reads and structural replay.
   Read every expected serializable value before building the plan.
   Treat an unknown or failed component or leaf read as a refusal.
   `PropertyValue.Absent` is a successfully read optional value.
   Retain float/vector payloads for the copy and redo, including signed
   zero. Check exact payloads for the copy's post-commit verification
   while retaining the existing Undo/Redo conflict comparisons.
   Reuse the settlement-first worker path from
   [0087](../specs/0087-add-mesh-to-scene.md) where it fits and
   the existing [0083](../specs/0083-authoring-apply-outcomes.md)
   controller. Connect the action and guarded feedback.
3. **S3 — Verification.** Complete the applicable checklist on the
   candidate. Record the commands, revisions, results and reused evidence.
4. **S4 — Review-ready PR.** Link joint approval, implementation scope,
   acceptance and any outstanding human decisions. Final merge is manual.

## Files / Modules Touched (expected)

| Scope | Work |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/{HierarchyCommands,StructuralEdits,DocumentTree,AuthoringDocument,EditHistory}.cs` | Duplicate plan, complete snapshot checks, existing replay and observation, and redo invalidation for a confirmed edit with no safe record. |
| `src/editor_app/Atlantis.Editor/{EditorSession,MainWindow}.cs` | Activate, settle and guard feedback, history, selection and apply. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{HierarchyTests,HistoryTests,ApplyOutcomeTests,IntegrationTest,J7Acceptance,Program}.cs` | Focused model, worker and Windows acceptance coverage in the existing harnesses. Temporary fixture input can stay in the harness. |
| `README.md`, `docs/specs/README.md` | Entry point, one-line shared composition reference and navigation. |

Runtime, Asset System, shared command and wire types, both SDKs, CLI,
MCP, build rules and committed goldens are reference inputs. Any
necessary scope adjustment follows the existing approval rule.

## Sequencing & Dependencies

Start S2 from latest main after joint approval and manual merge.
Prove in-model copy and history before UI integration. Use the same
fixture and shared contract for the Windows end-to-end proof.

| Decision | Recommendation |
| --- | --- |
| J1 — Scope and placement | Approve one hand-authored subtree of up to 64 nodes. Append at the same parent with local TRS, deterministic root naming and unchanged descendant names and active camera. |
| J2 — Identity and shared contract | Approve fresh explicit GUIDs, parent remapping and one existing transaction. Use observed lifetime/revision guards and accept the final cross-client read-to-submit gap. No new ADR or shared API. |
| J3 — Settlement and recovery | Approve submit/drain before automatic apply, one verified structural record, guarded selection, and explicit recovery from failed or unknown outcomes. |
| J4 — Acceptance and authorization | Approve the focused model/client checks, Windows 100% save/reopen and applicable existing gates. Authorize S2–S4 in one decision after design merge. |

Suggested joint authorization for the reviewed revision:

> I approve J1–J4 and Spec/Plan 0102 together. After manual merge of
> the design, proceed from latest main through S2–S4, including
> implementation, ordinary scoped fixes, applicable verification,
> reuse of valid evidence and a review-ready PR. Preserve existing
> gates. Final merge remains a human decision.

## Verification Checklist

- [ ] **S1 design.** Check number availability, document links, statuses,
  planned scope and whitespace.
- [ ] **R1/R2 model.** Cover leaf, root and nested sources, including
  document order with a child before its parent, existing sibling
  label collisions, unnamed sources, and a multibyte name at the byte
  limit. Compare complete components and optional leaves, nondefault
  TRS, a signed-zero leaf, internal parents, shared asset GUIDs and
  retained active camera.
  Check an unreadable component or leaf and the 64/65-node boundary,
  including early refusal without field reads or a submission for 65.
  Measure the full UTF-8 request with the existing codec for the largest
  supported 64-node shape with long escaped names and complete component
  values. Check both initial duplication and redo against the default
  1 MiB limit and exercise one valid 64-node batch through Remote.
- [ ] **R3 shared contract and races.** Prove the command batch can be
  submitted by an ordinary client outside the Editor, using `scene tx`
  or MCP's exact transaction and failure drain. Verify one revision
  advance, and no partial commit on a constraint refusal such as
  duplicating a scene's directional light. Cover imported/no-document
  eligibility, gestures, a later selection, busy suppression, revision
  changes during reads, same-source reopen, refused reopen, and late
  callbacks from a replaced worker. Check the event drain before final
  revision observation. Verify unrelated failure tickets are ignored
  and a mismatched, malformed or lost settlement remains unknown.
  Distinguish known commit with unknown apply from unknown settlement.
  Verify neither replays the creation. A copied node changed before
  history observation must not receive an unsafe Undo record. Check
  that a confirmed edit clears Redo in this case, while a refusal
  preserves prior history. Verify no apply is queued by the duplicate
  action when an unrelated external revision overtakes settlement.
- [ ] **R4 history and recovery.** Verify one Undo and one Redo with
  identical duplicate GUIDs and values. Exercise external edits to
  the copy and additional children before Undo, plus parent removal
  or GUID conflict before Redo. Verify that an externally activated
  copied camera makes removal fail atomically under the existing
  active-camera rule. Verify the existing coalescing and suspended-apply
  rules with a confirmed duplication.
- [ ] **R1–R4 Windows closeout.** At 100% DPI open a disposable
  hand-authored scene on a known generation. Duplicate one selected
  nested group containing a renderable subtree and nondefault
  transforms. With Runtime paused on a known successful projection,
  use a second shared client to give a renderable outside the copied
  subtree a non-nil missing mesh GUID in the document without applying
  it. Confirm the copy commits and
  is selected in Hierarchy/Inspector while the receipt is pending.
  Step the Runtime and verify `ScenePreparationFailed` for that
  receipt, along with the retained Runtime projection and copy history.
  Restore the original mesh GUID through an ordinary authoring
  transaction and explicitly Retry/Apply, then Step or Resume.
  Confirm that the same copy becomes visible with no further
  creation. Move the copy through
  an ordinary edit to distinguish the overlapping draws.
  Settle that edit separately before checking duplication Undo/Redo
  (undo the move first). Save the redone subtree explicitly and reopen
  the saved source through the existing authoring Open with the same
  active generation. Confirm stable scene and duplicate GUIDs, topology,
  transforms, asset bindings, active camera and fresh local history.
  Request Apply explicitly and verify the reopened document revision
  reaches Applied and the Runtime holds the same copied subtree. Record
  the Windows result and distinguish saved source, reopened document and
  Runtime projection in the evidence.
- [ ] **Existing gates.** Build the affected Editor and run model/UI and
  applicable shared authoring checks. Run the real Runtime repro
  with Vulkan Validation Layers. Use impacted headless/golden
  checks or applicable evidence reuse. Evaluate the full candidate
  diff with `--gate --applies` and follow the original performance
  protocol if selected. Keep baseline and golden policy, record
  Android applicability, and leave #340 deferred.

## Rollback Plan

Revert the Hierarchy action and its Editor composition. Already saved
copies are ordinary nodes and remain readable by existing clients.
Ordinary structural editing can remove one if desired.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md).
The implementation PR owns the evidence, any deviations and the merge
conditions.
