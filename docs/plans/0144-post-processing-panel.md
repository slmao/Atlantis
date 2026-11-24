# Plan 0144 — Post Processing Panel V1

- **Spec:** [Spec 0144](../specs/0144-post-processing-panel.md)
- **Status:** Draft
- **Author:** Codex
- **Design baseline:** `8c75b99b` on `main`
- **Joint Human Review:** Pending
- **ADR:** None proposed

## Objective

Give the Editor one place to tune the active authoring camera's existing
exposure, fog and Bloom fields. Prove isolated edits and Undo with the two
Bistro reference views, then verify persistence after an independent launch.

## Milestones and file scope

1. **S1 — Joint review.** Settle J1–J4 and record the exact approved revision.
   Implementation starts from current `main` after maintainer merge.
2. **S2 — Target and settlement.** Add the panel's camera/field read model
   with explicit identity, epoch and refusal states. Compose the shared
   active-camera query, token/occurrence checks, one-write revision guard,
   and existing history and Apply settlement.
3. **S3 — Inspector mode.** Add the compact header and grouped field controls
   in the existing pane. Integrate the Inspector draft and leave lifecycle.
   Keep the existing selection, viewport navigation and capture actions.
4. **S4 — Verification and PR.** Run the focused checks and Windows matrix
   on the final candidate. Attach original evidence and applicable gate
   results to the implementation PR.

| Files | Scope |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/Inspector.cs`, new `PostProcessingPanel.cs` in the same module | Reuse reflective field parsing and formatting. Model the snapshot, explicit target, draft and stale-result checks. |
| `src/editor_app/Atlantis.Editor/EditorSession.cs`, `MainWindow.cs`, new `MainWindow.PostProcessing.cs` in the same module | Read and guard on the Worker. Reuse property settlement and display the Inspector mode and existing state indicators. Integrate draft protection and 0112 leave handling. |
| `src/editor_app/tests/Atlantis.Editor.Tests/InspectorTests.cs`, `BistroReferenceViewTests.cs`, `Program.cs`, new `PostProcessingPanelTests.cs` in the same test project | Targeted model, relay and real-window checks on the existing test entry points. |
| Disposable Windows scenes, runner and evidence outside the implementation diff | The accepted 0143 daylight fixture, a separate two-camera race fixture, frame reports and configuration evidence. |

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Scope and UI** | Approve one mode in the existing Inspector, all eight existing leaves and an always-visible target label within the mode. The target is the authoring active camera. Approve read-only if the document lacks the guarded write capability. Confirm no new ADR. |
| **J2 — Draft and conflict rules** | Approve explicit per-field commits, read-only stale text, strict revision invalidation and a host-checked one-field transaction addressing the captured camera. Keep already-sent outcomes and Undo/Redo tied to the original camera. |
| **J3 — Verification** | Approve both 0131 views at Windows 100%, one family at a time, settled projection evidence and a combined non-default configuration reopened through Saved Scenes. Reuse the existing report and read back fog and Bloom alongside it. |
| **J4 — Authorization** | After approval and maintainer merge of this design, authorize S2–S4 together, including scoped fixes, applicable verification and a reviewable implementation PR. The maintainer retains final merge. |

## Verification checklist

- [ ] **R1–R2 — Scope and read guards.** Cover all eight leaf addresses and
  their parsing/domain checks, empty active camera, missing or malformed
  capability and unavailable fields. Verify name/GUID, unnamed fallback,
  selection independence and focus/leave behavior. Hold an A read or
  callback, switch to B from a second client, then release A. Verify B's
  current display and draft survive. Also cover A→B→A, a recreated node with
  the old GUID, Reopen, disconnect and an intervening field write.
- [ ] **R2–R4 — Send and settlement.** Use a separate two-camera hand-authored
  fixture and the existing shared `SetActiveCamera` command. Test switching
  after the draft is queued and after the preflight checks, holding the old
  submit until the host has accepted B. Verify A and B field values, revision,
  history and Apply are unaffected by the refused edit. In the inverse order,
  let the Worker verify A's write, then hold its panel callback until B appears.
  Verify the one history step for A and that B's fields, draft and newer
  feedback survive. If B arrives before A's readback can be fenced, verify the
  existing uncertain-settlement path and diagnostics for A. Check Undo/Redo
  against the captured camera, conflict refusal, lost receipt and explicit
  Apply recovery. Verify the existing Inspector and 0112 leave controls still
  behave as before. Run the affected Editor build and suites.
- [ ] **R5 — Windows fixture and controls.** Use native Windows at 100%
  (96 DPI) with the saved 0143 daylight inputs documented in [PR #451](https://github.com/slmao/Atlantis/pull/451).
  Verify the baseline scene hash and asset pin against that evidence. Use
  the existing Save As and trusted Saved Scenes workflow to obtain an editable
  working copy. Record fixture and candidate commits, scene and active-camera
  GUIDs, launch environment artifact hash, lights, GPU/driver and extent.
  Retain native screenshots of the panel showing the target and grouped
  controls. Keep unrelated authoring and viewport policy changes out of the
  image comparison.
- [ ] **R5 — One family at a time.** Start each family from the same settled
  daylight parameters. Use one explicit panel edit per comparison. The
  starting examples are exposure `0` → `+1 EV`, fog density `0.015` → `0.035`,
  and Bloom strength `0.3` → `0.65` with threshold `1`. Fix the other two
  families for each trial. Capture baseline, Apply success and one Undo in
  both Street Corner and Storefront Terrace at `960 × 540` physical pixels.
  Record the document token, Apply ID, generation, camera GUID and frame
  number. With ordinary 0131 capture at either view, bracket each frame with
  current document, property, Apply and Runtime status reads. Require the same
  confirmed projection throughout that capture. Pending, failed, superseded,
  unknown and previous-projection frames are ineligible. Missing effective EV
  makes the exposure comparison unverified.
- [ ] **R5 — Image proof.** Retain the raw PNGs and frame/readback reports.
  Verify matching view/projection matrices within `0.0001` per element and
  the 0131 landmarks within one pixel. Verify light reports and the two
  other parameter families stay fixed. Before tuning, record unsaturated
  lit surfaces for exposure, distant geometry for fog, and a region outside
  a bright source's core for Bloom. Show an image response above unchanged
  capture noise in a corresponding region for each family and compare both
  views. For Undo and cold comparisons, compare decoded RGB pixels within
  one code value per channel and record the maximum difference. Keep numeric
  frame and authoring evidence alongside the images.
- [ ] **R4–R5 — Persistence and gates.** Apply the combined non-default
  configuration through three separate panel edits and capture both views.
  Save and close, then independently Build/Open the saved source through
  [0090](../specs/0090-saved-scene-build-open.md). Read all eight fields and
  recover the two fixed captures in the new Runtime. Check the same saved
  scene and camera identities and unchanged 0143 source. Keep Vulkan
  validation enabled. Run `atlantis_runtime --gate --applies` on the final
  diff and run any selected gate under the existing protocol. Record image
  regression applicability. Committed renderer goldens are expected to stay
  unchanged because this slice edits scene values through the existing path.

## Rollback and definition of done

A revert removes the Editor mode. Saved camera settings remain ordinary
camera fields and can be edited through the existing Inspector. See the
[definition of done](../process/definition-of-done.md). Summarize S4 results
and unresolved gaps against R1–R5 in the implementation PR.
