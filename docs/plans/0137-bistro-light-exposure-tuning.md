# Plan 0137 — Bistro Storefront Lighting and Exposure Tuning

- **Spec:** [Spec 0137](../specs/0137-bistro-light-exposure-tuning.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending
- **ADR:** None. Existing [ADR-0180](../adr/0180-shared-frame-report-exposure.md) applies.
- **Design baseline:** `40ce4ad3`, maintainer merge of [PR #436](https://github.com/slmao/Atlantis/pull/436)

## Objective

Complete a repeatable point-light and active-camera exposure tuning workflow
using existing Inspector edits and fixed-view Runtime captures.

## Milestones / Task Breakdown

1. **S1 — Joint review.** Decide J1–J4 on the exact Spec/Plan revision.
   Begin implementation from current `main` after maintainer merge.
2. **S2 — Capture admission and active-camera location.** Reuse the current
   Worker, shared authoring and status queries, existing camera intent and
   drawable guards. Check document token/lifetime, settled revision and
   projection, active-camera identity and the fixed matrices. Revalidate
   after Apply without issuing an Apply or changing the authored camera.
   Resolve the camera locator against the current Hierarchy incarnation and
   current selection intent before using the existing selection path.
3. **S3 — Editor interaction.** Add paired capture controls and a read-only
   comparison display in the existing Bistro workflow. Label projection
   metadata and blocked states, retain the independent ORCA reference and
   keep the ordinary 0131 capture workflow. Dispose replaced images.
   Keep edits on the existing single and batch Inspector paths.
4. **S4 — Validation and PR.** Verify the final candidate on actual Windows
   100%, retain PNGs, frame reports and a native screenshot of the paired
   view, and submit a review-ready implementation PR with gates and results.

## Files / Modules Touched (expected)

| Area | Scope |
| --- | --- |
| `src/editor_app/Atlantis.Editor/BistroReferenceWindow.cs` | Terrace actions, paired image lifetime, status and layout in the existing comparison window. |
| `src/editor_app/Atlantis.Editor/EditorSession.cs`, `MainWindow.cs` | Worker admission and revalidation, active camera lookup, selection and existing settlement event wiring. |
| `src/editor_app/tests/Atlantis.Editor.Tests/BistroReferenceViewTests.cs`, `Program.cs` | Extend the existing Bistro tests and runner if needed for focused cases and the actual Windows run. |
| Existing disposable Bistro fixture for the Windows run | Keep the 0131 world coordinates. Use two existing suitable terrace point lights or prepare two identified lights in the disposable copy. Record fixture identity and settings. |

## Sequencing & Dependencies

S2 establishes verified context before S3 enables the paired UI.
Reuse [0131](../specs/0131-bistro-reference-views.md) for the preset, capture and
source attribution,
[0106](../specs/0106-scene-multiselect-batch-edit.md) for batch transactions,
[0083](../specs/0083-authoring-apply-outcomes.md) for settled projection
and [0136](../specs/0136-viewport-visibility-picking.md) for reference-view
eligibility under a visibility policy.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Editing and locating** | Use absolute common-field Set on a group of point-light nodes, then a separate single-field edit on the active scene camera. Add a read-only camera locator with existing draft and selection protection. |
| **J2 — Capture and scope** | Use two temporary Atlantis slots at the existing 960 × 540 Storefront Terrace preset. Keep the preset through verified same-document Apply and reuse the existing frame report. ORCA remains a separate credited visual reference. Editor-only work needs no new ADR. |
| **J3 — Settlement and acceptance** | Require projection identity for the exact current revision, unchanged authoring/capture context during acquisition, and the existing matrix admission. Preserve the prior captured evidence on failed or unknown Apply. Verify parameter isolation, composed image changes and two separate Undo restorations on a quiescent fixture. |
| **J4 — One implementation authorization** | After joint approval and maintainer merge, authorize S2–S4 once, including in-scope fixes, affected tests, Windows 100% verification, applicable gates and submission of a review-ready PR. Final merge remains with the maintainer. |

## Verification Checklist

- [ ] **R1, R4 — Inspector and location.** Verify a Set across two mixed
  float values is one transaction and one history entry. Undo restores both
  values. The locator picks the active camera while a different camera is
  selected, and refuses a missing/hidden camera, a replaced row or stale
  response, and a protected draft without changing selection or writing.
  In a failed Apply scenario, verify the edit stays in authoring with one
  history entry and explicit Apply changes only the projection.
- [ ] **R2–R4 — Pair lifecycle.** Verify confirmed same-document Apply and
  Undo re-enable capture at the original extent and matrices. Reject stale
  revisions, expiry, superseded or unknown result, absent projection proof,
  paused Runtime, and a frame with mismatched matrices or generation.
  Exercise Apply while a capture is in flight, an expired locator reply,
  navigation/policy/extent changes, reopen/disconnect and late PNGs. Check
  pair replacement and disposal, clear eligibility on invalidated context,
  preserve historical labels, and keep the ordinary 0131 capture available
  under its own existing rules.
- [ ] **Windows 100% — Actual hosted Storefront Terrace.**
  1. Record commit, Runtime/driver, asset pin, disposable scene, active
     camera GUID and chosen light GUIDs. Use the original 0131 Terrace
     coordinates and three landmarks. Select two visible effective point
     lights with distinguishable positions and mixed initial intensity.
     Fix a nonsaturated patch in their illuminated area before the edit.
     Record initial typed values and report tuples. Explicitly Apply the
     opened document if no matching projection receipt exists. Capture Before
     with overlays off and take a repeated baseline frame for comparison.
  2. In the batch Inspector Set one legal intensity greater than both
     old values. Record committed revision and history independently from
     settled Apply. Capture After at the same view, confirm both effective
     light intensities equal Set, actual EV stays unchanged, and other light
     values are unchanged when matched by their stable report attributes.
     Undo once, wait for confirmed projection, capture restoration and
     verify both exact old values plus the return of the image effect.
  3. Start another pair from the restored lighting state. Locate the active
     camera and increase its exposure by 1 EV within the schema's
     `[-16, +16]` range. Confirm only the authored exposure changed and the
     actual captured EV increased by 1, with the entire reported lighting
     unchanged. Capture After. Undo once and confirm exact restoration of
     the authored and reported EV and the image effect.
  4. For all trial and restored PNGs verify 960 × 540, view/projection
     matrices within 0.0001 per element and the three 0131 landmarks within
     one output pixel of Before. Use mean decoded PNG luminance on the
     predeclared patch, brightening beyond the variation between repeated
     baseline frames on each positive edit, and returning within that
     repeatability variation after Undo. Choose an exposure-responsive
     nonsaturated patch as needed and record coordinates before each trial.
     Retain numerical values and source PNGs. No visual match to ORCA or
     expected linear relationship to displayed brightness is required.
  5. Record authored camera identity and pose, scene revision, history,
     and Apply result around comparison-only actions. Verify that locating
     and capturing leave those values and the source file unchanged. Show
     both Atlantis panes and the labelled ORCA source separately.
- [ ] **Applicable gates.** Run affected Editor model/UI tests and the
  real hosted/offscreen capture path with clean Vulkan validation. Evaluate
  image regression, golden review, Android, and the repository's
  `--gate --applies` selector on the final diff. Record applicability and
  reuse unchanged evidence only with matching inputs. Run the original
  performance protocol if selected and retain the official verdict. No
  golden or baseline change is proposed.

## Rollback Plan

Revert the Editor workflow and its tests. Existing Inspector edits use the
unchanged document and history contracts, and the 0131 comparison remains.

## Definition of Done

Use the [repository definition of done](../process/definition-of-done.md).
The implementation PR links the joint approval and records both paired
trials, both restoration captures, metadata and applicable gate results.
