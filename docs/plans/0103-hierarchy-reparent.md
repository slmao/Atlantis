# Plan: Reparent a Scene Subtree by Drag and Drop

- **Spec:** [Spec 0103](../specs/0103-hierarchy-reparent.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Atlantis maintainer, 2026-11-15, [PR #371](https://github.com/slmao/Atlantis/pull/371) at `d05358ff`. Approved Spec/Plan 0103 and J1–J4 and authorized S2–S4 after human merge at `3de5d65f`.
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
| J2 — Transform and shared contract | Approve one existing transaction with `SetParent` and local TRS, strict reads, existing TRS decomposition and a recomposed world matrix check. No new ADR. |
| J3 — Settlement and recovery | Approve scope by worker, lifetime and revision and the final non-atomic call boundary. Require a verified single revision advance for history and automatic apply. Retain existing explicit recovery. |
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
  unreadable or non-finite transform, missing node, current parent,
  cycle, singular destination, non-representable shear or failed world
  matrix comparison. Use one ordinary CLI, SDK or MCP client to submit
  and drain the same command shape.
- [ ] **R1/R3 worker and gesture.** Check hover, Escape, a drop outside
  a row or from another window, an already settling move and an active
  Gizmo for no submission. Exercise the shared button path, including
  Move to root and existing imported-document button access. Cover
  source parent change, captured destination destruction and GUID reuse,
  same-source reopen, refused reopen, and a late callback from an old worker. Verify a later selection cannot retarget the request
  or be overwritten by its completion. Cover a revision change during
  reads, unrelated failure tickets, a correlated refusal, and a malformed
  ticket or lost settlement reply. Check that a refused or unknown edit
  issues no automatic apply or speculative history and never replays.
- [ ] **R3/R4 history and apply.** Verify a single Undo and Redo with
  recorded parents and local values, plus refusal after an external
  source conflict or deletion of the replay destination. Interleave an
  external source write in the final read-to-submit gap. If the reparent
  commits and the expected after-values are visible at a higher revision,
  confirm the known commit, Redo invalidation, unavailable Undo and no
  automatic apply. Distinguish a lost apply reply from unknown transaction
  settlement. Reuse equivalent controller evidence and verify that the
  reparent path obeys pending and suspended behavior.
- [ ] **R1–R4 Windows closeout.** At 100% DPI, open a disposable hand-authored
  scene on a named generation. Check a cancelled drag clears the target
  indicator and changes no document revision. Pause a known successful
  projection. From a second client, commit a missing mesh reference
  outside the subtree without applying. Drag the nested renderable subtree
  onto a transformed parent and confirm the new parent, selection and
  pending receipt. Step
  and verify `ScenePreparationFailed` for that receipt, retained
  projection and available Undo. Restore the binding and explicitly
  Retry, then Step or Resume. Verify the same GUIDs and world placement
  with no new reparent transaction. Undo and Redo once each and settle
  their applies before Save. Reopen the saved source on the same
  generation and check topology, local TRS, asset bindings, stable GUIDs
  and cleared local history. Explicitly Apply the reopened revision and
  verify its projection. Record the Windows interaction and distinguish
  the saved source, reopened document and Runtime results.
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
