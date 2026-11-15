# Plan: Reparent a Scene Subtree by Drag and Drop

- **Spec:** [Spec 0103](../specs/0103-hierarchy-reparent.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending.
- **Related ADRs:** None new. See the Spec's Architectural Impact.

## Objective

Deliver direct single-subtree reparenting in Hierarchy and route the
existing reparent buttons through the same guarded edit path.

## Milestones / Task Breakdown

1. **S1 — Joint review.** Approve the Spec, Plan and J1–J4 together.
   The maintainer merges the design before implementation starts.
2. **S2 — Implementation.** Make reparent planning read complete
   authoring transforms and validate the captured identities. Reuse the
   existing transform math and parent/property history representation.
   Run the settled edit through the worker, then connect the Hierarchy
   drag and button entry points to it.
3. **S3 — Verification.** Complete the applicable checklist on the
   candidate and document the results and evidence reuse.
4. **S4 — Review-ready PR.** Link joint approval, scope, applicable
   verification and any remaining human decisions. Final merge is manual.

## Files / Modules Touched (expected)

| Scope | Work |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/{HierarchyCommands,DocumentTree,AuthoringDocument,EditHistory,StructuralEdits}.cs` | Strict transform and hierarchy reads, reparent planning, and existing structural history and observation. |
| `src/editor_app/Atlantis.Editor/{EditorSession,MainWindow}.cs` | Worker admission, settlement and feedback; Hierarchy drag and existing button integration. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{HierarchyTests,HistoryTests,ApplyOutcomeTests,IntegrationTest,J7Acceptance,Program}.cs` | Focused model, worker, shared-client and Windows acceptance checks in the existing harnesses. |
| `README.md` and `docs/specs/README.md` | Document the implemented entry point and link the implementation PR at closeout. |

Runtime and native modules, shared SDKs/CLI/MCP, wire and source formats,
build rules and goldens are reference inputs.

## Sequencing & Dependencies

Start S2 from latest main after joint approval and manual merge. Build on
[0068](../specs/0068-authoring-editor.md) for the parent/transform edit and
history, [0083](../specs/0083-authoring-apply-outcomes.md) for recovery,
[0093](../specs/0093-editor-visual-layout.md) for feedback and
[0102](../specs/0102-duplicate-scene-subtree.md) for revision and settlement
guards. Prove planning and history before connecting the gesture. Use a
disposable hand-authored scene for the Windows run.

| Decision | Recommendation |
| --- | --- |
| J1 — Scope and interaction | Approve single-node row-to-row drop in a hand-authored scene. Preserve button access and route it through the same guarded submission. Leave sibling ordering for a separate design. |
| J2 — Transform and shared contract | Approve world-preserving `SetParent` and local TRS in one existing transaction, with strict reads and existing math tolerance. No new ADR. |
| J3 — Settlement and recovery | Approve the observed lifetime/revision checks and final non-atomic call boundary. Record verified commits in the existing history and use the existing correlated apply and explicit recovery rules. |
| J4 — Acceptance and authorization | Approve model/shared-client checks and Windows 100% save/reopen, with applicable existing gates. Authorize S2–S4 in one decision after design merge. |

Suggested joint authorization for the reviewed revision

> I approve J1–J4 and Spec/Plan 0103 together. After manual merge of the
> design, proceed from latest main through S2–S4, including implementation,
> ordinary scoped fixes, applicable verification, reuse of valid evidence
> and a review-ready PR. Preserve existing gates. Final merge remains a
> human decision.

## Verification Checklist

- [ ] **S1 design.** Verify number availability, links, scope and Markdown
  diff against the current main.
- [ ] **R1/R2 model and shared contract.** Use a nested renderable subtree
  and a transformed new parent to check root and descendant world matrices,
  unchanged global order and identities, and one revision advance. Include
  a representable reflected transform. Confirm no submission for an
  unreadable transform, missing node, current parent, cycle, singular
  destination or shear outside the existing tolerance. Use one ordinary
  CLI, SDK or MCP client to submit and drain the same command shape.
- [ ] **R1/R3 worker and gesture.** Check that cancel, drop outside the
  tree, a busy request and an active Gizmo cannot submit a reparent.
  Exercise the button fallback, source parent change, destination removal,
  same-source reopen, refused reopen and a late callback from an old worker.
  Verify a later selection cannot retarget the request or be overwritten
  by its completion. Cover a revision change during reads, a correlated
  refusal, and a lost settlement reply. Check that a refused or unknown
  edit issues no automatic apply or speculative history and never replays.
- [ ] **R3/R4 history and apply.** Verify a single Undo and Redo with
  recorded parents and local values, plus refusal after an external
  source conflict. Check a confirmed edit overtaken by a later revision
  and a lost apply reply. If the intended edit cannot be verified, confirm
  Redo is invalidated for the same document without constructing an unsafe
  Undo entry. Reuse existing apply controller evidence where inputs
  match, and verify the reparent path obeys pending and suspended behavior.
- [ ] **R1–R4 Windows closeout.** At 100% DPI, open a disposable hand-authored
  scene on a named generation. Pause a known successful projection. With
  a second client, commit a missing mesh reference outside the subtree
  without applying. Drag the nested renderable subtree onto a transformed
  parent and confirm the new parent, selection and pending receipt. Step
  and observe the correlated reload failure and retained projection and
  history. Restore the binding and explicitly Retry. Verify the same
  GUIDs and world placement on recovery. Undo and Redo once each, settling
  their applies, then Save. Reopen the saved source on the same generation
  and check topology, local TRS, asset bindings, stable GUIDs and cleared
  local history. Explicitly Apply the reopened revision and verify its
  projection. Record the Windows interaction and distinguish the saved
  source, reopened document and Runtime results.
- [ ] **Existing gates.** Build the affected Editor and run the required
  model, UI and shared authoring checks. Use Vulkan Validation Layers for
  the live Runtime repro. Assess headless/golden and Android applicability,
  and reuse equivalent evidence when inputs match. Evaluate the full
  candidate with `--gate --applies` and follow the existing protocol if
  selected. Preserve baselines and goldens. Leave #340 deferred.

## Rollback Plan

Revert the UI gesture and guarded reparent composition to restore the
previous button workflow. Saved scenes still contain ordinary parent
links and local transforms, readable by existing clients.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md).
The implementation PR owns evidence, deviations and merge conditions.
