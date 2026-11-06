# Plan: Generic Asset Reference Picker

- **Spec:** [Spec 0082](../specs/0082-generic-asset-reference-picker.md) (Approved)
- **Status:** Approved
- **Author:** Codex, at slmao's direction
- **Joint Human Review:** slmao, 2026-11-07; [joint ruling](https://github.com/slmao/Atlantis/pull/327#issuecomment-6689535730) approves Spec/Plan 0082, J1–J4 at `00b6674`, and the no-new-ADR assessment. #327 manually merged as `6b8b568`; S2–S4 continuously authorized from latest main through implementation, applicable verification and a review-ready PR under 0080. Existing gates, evidence provenance and human final merge retained; the proposal wording below is historical and does not override this ruling.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Objective

Deliver Spec 0082's generic reference chooser using existing shared discovery and
ordinary document editing, with no new engine contract. This round drafts and
submits the design only, based on main `882c691e` after #326's human merge.

## Milestones / Task Breakdown

1. **S1 — Joint design (this PR).** Inspect the shared schema, AssetBrowser,
   Inspector/worker/history and #326 closure. Submit this pair; update Spec 0081's
   merged navigation and add 0082's proposal entry. Check documents and stop.
2. **S2 — Headless chooser and guarded edit.** Only after joint approval and human
   design merge, add the target/candidate lifecycle model, eligibility and
   stale-target/no-op checks. Compose existing Inspector edit plans and retain
   worker-local document-open lifetime; no wire generation. Add focused models.
3. **S3 — Product UI.** Add the owner-modal chooser on the existing worker and
   dispatcher, using independent AssetBrowser state. Implement launch/focus,
   reload/error and one-shot confirmation behavior; retain the Assets tab and
   existing Inspector text/clear behavior. Add actual UI regressions.
4. **S4 — Verify and submit implementation.** Complete the mapped applicable
   checks, audit evidence reuse and actual whole-PR performance applicability,
   update narrow usage/as-built navigation and submit a review-ready implementation
   PR. Preserve failures and human final merge; no additional milestone gates
   unless explicitly imposed by the owner.

## Files / Modules Touched (expected)

**S1, exactly four documentation files:** this Plan; the new Spec;
`docs/specs/README.md`; the existing editor direction paragraph in
`docs/project-blueprint.md`. No new ADR or approved-document semantic rewrite.

**Future implementation scope, new files marked new:**

| Responsibility | Expected files |
|---|---|
| UI-free chooser state | New `src/editor_app/Atlantis.Editor.Model/AssetReferencePicker.cs`; `Inspector.cs` for schema-based eligibility and guarded composition of an existing edit plan. Reuse `AssetBrowser.cs` as-is unless a required local lifecycle correction is demonstrated. |
| Worker and UI | `src/editor_app/Atlantis.Editor/EditorSession.cs` for capture/recheck/confirmation over its existing queue; `MainWindow.cs` for eligible-field launch and focus handling; new `AssetReferencePickerWindow.cs` for the owner-modal UI and callback guards. No general Inspector or browser framework refactor. |
| Focused tests | New `src/editor_app/tests/Atlantis.Editor.Tests/AssetReferencePickerTests.cs`; existing `Program.cs` registration, `InspectorTests.cs` actual Windows UI checks, `IntegrationTest.cs` reference-edit integration and `HistoryTests.cs` only for relevant replay/conflict regression. Existing fake document and runtime helpers suffice. |
| Usage/as-built/closure | Narrow picker usage in `README.md`, editor paragraph in `docs/architecture/module_boundaries.md`, `docs/specs/README.md` and `docs/project-blueprint.md`. This Spec/Plan approval metadata only once authorized; no accepted ADR rewrite. |

No native, Core/schema, Connection, Remote, CLI/SDK/MCP production, Runtime,
Renderer, asset, generated/vector or package/build files are expected to change.
The SDK-style C# projects include source files without new registrations/targets.
Necessary nonsemantic file-list changes follow AGENTS/0080's PR approval rule;
contracts, architecture, acceptance changes require design review before dependent
implementation. Do not broaden this plan to fix unrelated Inspector behavior.

## Sequencing & Dependencies

Spec 0081 / ADR-0143 and the existing authoring editor are merged prerequisites;
there is no dependency on an unmerged feature. S1 ends at the joint design PR.
Review must explicitly approve both documents and authorize implementation; a
human must merge the design PR before S2 begins. No new ADR is needed for the
proposed composition. A new engine/metadata/concurrency requirement reopens design.

### Joint review — recommendations for human ruling

| Ruling | Recommended conclusion |
|---|---|
| **J1 — Minimum interaction** | Approve R1–R3 and Proposed Design: existing Inspector editability plus schema-marked AssetGuid fields; single modal chooser; visible-candidate explicit Confirm; fixed-snapshot local browsing. Pointer/F4 launch discards only the target draft before focus moves; a prior Tab focus-loss commit remains ordinary editing. No target-type inference/metadata or compatibility promise. |
| **J2 — Target and edit semantics** | Approve R4–R5: worker capture/event drain, sticky target invalidation and fresh guarded planning in the same Edit turn; separate browser cancellation from Submitted/Settled confirmation. Failed field read is not optional absence; transport failure invalidates even a cached list. Preserve no-op, one-shot submission and ordinary apply/history, accepting the stated non-atomic cross-client boundary. |
| **J3 — Architecture** | Approve R6 and the no-new-ADR assessment: UI composition under current ADRs, no shared operation, dependency or schema change; CLI/SDK/MCP retain the same discovery/edit capabilities. |
| **J4 — Delivery and verification** | Approve S2–S4's narrow files and checklist, valid-evidence reuse and retained applicable gates. Human final merge remains required. This proposal grants no current implementation authorization. |

Suggested later authorization, **pending human adoption**: jointly approve
Spec/Plan 0082 with J1–J4 as recommended, including the no-new-ADR assessment;
after confirming manual merge of this design PR, authorize continuous S2–S4
execution from then-latest main through implementation, applicable verification
and a review-ready implementation PR under 0080. Permit ordinary in-scope
code/test/disposable-fixture repairs and focused reruns without further stage
approval, preserving failures and exact reuse provenance. Report necessary
nonsemantic file-list adjustments in that PR for one explicit human approval;
no new Spec/Plan for those alone. Retain every applicable gate and any explicit
budgets/holds; do not reset measurements, weaken acceptance or merge automatically.
For changed contracts, architecture, dependencies or acceptance, stop dependent
work for a specific reviewed revision while independent authorized work continues.
Stop at the review-ready implementation PR for human final review/merge. This
authorization text is a recommendation only; the current design-only instruction
remains binding until the human explicitly adopts it.

## Verification Checklist

**S1 only:**

- [ ] Confirm immutable main/#326 merge and available numbering in PR; status
  In Review, joint approval pending; exactly four planned documentation files.
- [ ] `git diff --check`; local Markdown file links resolve; templates, R1–R6
  mapping, current 0081 merged navigation and no-new-ADR justification checked.
  Engine build, UI/GPU tests and performance measurements N/A for documents.

**Future final candidate (focused checks during work; reuse only unchanged inputs):**

- [ ] **R1/R2 models:** schema-marked AssetGuid eligibility with arbitrary
  component/field names, negative primitive/flag/read-only cases; optional absent
  and clear behavior. Include actual authoring-style Serializable + AssetReference
  with Editable unset; failed read (`null`) must differ from PropertyValue.Absent.
  Check current GUID listed/missing, explicit filters (hidden candidate cannot
  Confirm and no first-row auto-selection), dependency
  selection, Cancel/no-op/Confirm-once. Picker state does not alter Assets tab or
  Hierarchy/history. A pending list or failed/partial reply cannot enable Confirm.
- [ ] **R3 lifecycle models/UI:** empty/no-match/unavailable/UnknownAsset detail/
  disconnect distinct; detail error remains non-validating; cancel→reopen→old
  callback, selection→pending detail→Reload→old callback after new result, old
  session and superseded list replies rejected. Check same Runtime snapshot,
  candidate preservation and replacement detail request, not a disk reload.
  Distinguish an in-band detail refusal from transport/session failure after a
  successful cached list; the latter disables Confirm and invalidates the request.
- [ ] **R4 guard:** selection switches (including away/back), document reopen
  with identical GUID/value, target/component removal, eligibility or value/presence
  changes, disconnect and queued Confirm after invalidation refuse with no edit,
  apply or history entry. Include observed target change→restore and component
  remove→re-add with identical values before a single event-drain completes;
  invalidation remains sticky. Drain available events at capture and in the final
  Edit turn, refresh the actual target and check before constructing that plan;
  no guard-then-enqueue-path gap. Unrelated Applied generation alone is not reopen.
  Do not assert atomic CAS or detection of an unobserved remote change-and-restore.
- [ ] **R2/R5 actual Windows UI:** use actual MainWindow, chooser, worker and real
  Runtime with Avalonia focus/routed button/key events (declare this boundary).
  Pointer launch with focus in a drafted field must exercise pre-focus input→
  LostFocus→Click, not just RaiseEvent(Click); F4 must use the same early suppression.
  Neither submits the replaced draft. Also test Tab away's ordinary commit then
  keyboard activation from Choose, launch failure, and normal text editing after
  Cancel so suppression cannot leak. Capture current worker values and reject old
  deferred Inspector rows. Check modal ownership, Escape/close/Cancel,
  explicit Confirm→window close→worker submission/settlement, duplicate input,
  blocked reopen while Submitted, owner notice once, same-value no-op, optional absent assignment,
  existing text/clear controls and visible stale-target/refusal/apply messages.
  No screenshot, physical-keyboard or latency claim without evidence.
- [ ] **R5 shared-client integration:** default and available Bistro real Runtime
  in managed Debug/Release. Pick a known compatible material GUID from the same
  cooked snapshot, commit via actual chooser, read the identical property with a
  second SDK/CLI client, observe apply, one history step and Undo/Redo. External
  overwrite refuses replay; cancelled/stale requests do not edit. Verify pause
  holds apply until Step/Resume; transaction refusal creates no success record and
  an apply failure is not misreported as a reverted document. Use existing small
  fixtures for distinct failure stages; no production test seam or asset edits.
- [ ] **R5 persistence:** hand-authored disposable fixture save→reopen plus
  save→rebuild→restart preserves the chosen reference and matching catalog
  dependency; imported save remains refused. Restore fixture inputs and compare
  the baseline frame/capture after restoration; preserve tracked assets/goldens.
  Reuse #326's unchanged backend/persistence portions with exact provenance, but
  rerun changed chooser/worker behavior and its actual UI-to-reference integration.
- [ ] **R6 builds/boundaries:** affected Editor Model/App/tests Debug/Release,
  model/vector/history and editor/package boundary checks, existing pinned locked
  restore. Native/CLI/SDK/MCP/Android builds and protocol tests may reuse #326 only
  if relevant source/tests/dependencies/build inputs/assets/environment remain
  unchanged; report revision/configuration and evidence, not a fresh full matrix.
- [ ] **R6 GPU/gates:** live Runtime checks with clean Vulkan Validation on the
  reference environment; reuse unchanged renderer/golden cases with provenance.
  Evaluate performance applicability over the complete implementation PR using
  the unchanged selector. UI-only planned paths are expected unselected: record
  actual result/reason, no timing pass. If selected, honor the original native
  Release profiling-ON/Tracy-OFF gate protocol and explicit budgets; do not inherit
  #326's measured verdict for changed native inputs or reset budgets/retry-to-pass.
  No new latency benchmark, threshold, baseline/policy/golden change.
- [ ] **Closure:** full diff within scope, approvals/deviations/results/failures/
  evidence reuse and limits in implementation PR; usage/as-built/navigation current.
  All required gates settled before ready; otherwise Draft with concrete blocker.
  Human review and merge, never agent merge.

## Rollback Plan

Before implementation, revise/reject this draft without runtime effect. After a
future implementation merge, revert the picker UI/model/worker additions and
their tests/docs in a reviewed PR; existing GUID text editing and Assets browser
remain the fallback. No asset migration, wire downgrade or persisted picker data.

## Definition of Done

See [Definition of Done](../process/definition-of-done.md). S1 delivers reviewable
documents only; no implementation result is implied. Future completion requires
the mapped applicable checks with explicit valid reuse, a review-ready PR and
human final merge. No additional gates beyond the approved checklist and repository
rules; no implementation is authorized by merely opening this proposal.
