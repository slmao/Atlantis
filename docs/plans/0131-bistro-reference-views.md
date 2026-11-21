# Plan 0131 — Bistro Reference Views and Image Comparison

- **Spec:** [Spec 0131](../specs/0131-bistro-reference-views.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Maintainer approved Spec/Plan 0131 and J1–J4 at `637eaefd` on 2026-11-21 in [PR #425](https://github.com/slmao/Atlantis/pull/425#issuecomment-7063983586) and authorized implementation, verification and a review-ready PR. The maintainer merged the design at `4f007b1b`.
- **ADR:** [ADR-0180](../adr/0180-shared-frame-report-exposure.md) (Accepted)
- **Design baseline:** `b26d01a5`, the maintainer merge of [#424](https://github.com/slmao/Atlantis/pull/424)

## Objective

Return to two fixed Bistro compositions with the existing Editor camera
and Runtime capture. Show each capture and its conditions next to the
corresponding credited ORCA still.

## Milestones

1. **S1 — Joint review.** Decide J1–J4, accept ADR-0180 and record which
   revision authorizes implementation before editing code. Begin on the
   latest `main` after the maintainer merges the design.
2. **S2 — Presets and frame report.** Freeze the two camera values and
   landmarks from the pinned Bistro input. Extend the existing report and
   verify native and managed codec, CLI and MCP behavior with both
   generations of report.
3. **S3 — Editor workflow.** Add reference actions in the Viewport toolbar,
   settle the existing hosted child at the required physical extent, reuse
   the camera worker and capture path, and show the two images with conditions,
   provenance and refusal states. Use the native input and document guards.
4. **S4 — Verify and submit.** Perform the Windows 100% two-visit checks,
   applicable gates and final-diff review. Keep the exact presets, settings,
   images, results and any failures in the implementation PR.

The frame report in S2 enables exposure provenance in S3. The S2 camera
values and landmarks are the inputs for both visits to each preset in S4.

## Expected files and modules

| Files | Scope |
| --- | --- |
| `src/runtime/src/frame_capture.{h,cpp}`, `src/connection/include/atlantis/connection/runtime_control.h` | Report effective exposure from the existing frame-report path. |
| `src/remote/src/codec.cpp`, `src/csharp/Atlantis.Gameplay/Control.cs`, `src/csharp/Atlantis.Gameplay/Remote/Codec.cs`, `src/cli/src/command_layer.cpp`, `docs/architecture/remote_protocol.md` | Additive shared field. Both CLI output modes report a known value. |
| `src/editor_app/Atlantis.Editor/{MainWindow,EditorSession,ViewportHost,ViewportWindow,BistroReferenceWindow}.cs`, `src/editor_app/Atlantis.Editor.Model/BistroReferenceViews.cs` | Fixed physical viewport, session guards and comparison. The last two files are new. Reuse the normal child size and input route. |
| `tests/runtime/runtime_smoke_gpu_tests.cpp`, `tests/remote/remote_control_tests.cpp`, `tests/cli/cli_command_tests.cpp`, `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}`, `src/mcp/tests/Atlantis.Mcp.Tests/Program.cs` | Capture and compatibility coverage through the existing report and MCP tool. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{Program,ViewportNavigationTests,BistroReferenceViewTests}.cs` | Model and hosted UI regression coverage. The reference tests are new. |

Use the existing pinned Bistro fetch and an isolated test scene. Gallery
originals and Windows receipts remain outside the source tree.

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — Views and viewport** | Approve Street Corner and Storefront Terrace, fixed 960 × 540 live preview and PNG, existing roll-free projection and explicit hand-authored Bistro opt-in. Review the S2 calibrated values and landmarks with the implementation evidence. |
| **J2 — Shared report** | Accept ADR-0180. Report effective exposure in the existing frame report and preserve compatibility in both directions. |
| **J3 — Source and acceptance boundary** | Use the two exact ORCA exterior stills and documented attribution. Keep unknown settings visible and judge repeatability of Atlantis framing independently from the ORCA appearance. |
| **J4 — Execution** | After explicit joint approval and maintainer merge, authorize one continuous S2–S4 run covering implementation, in-scope fixes, applicable verification and a review-ready implementation PR. Reserve final merge for the maintainer. |

## Verification checklist

- [ ] **R1–R2 — Editor model.** Validate fixed projected camera values,
  independent of pivot, selection and scene-camera lens. Exercise opt-in
  and scene replacement, tight size and DPI conversion, navigation and
  Scene Camera return, stale setter/resize and active edit guards. Verify
  submission resumes only after pose and size settle. Verify picking and
  Gizmo hit coordinates use the actual rendered area.
- [ ] **R2–R4 — Capture and shared clients.** Compare the frame report
  matrices with the settled pose and capture extents with the PNG. Test
  old report omission, zero EV, nonzero EV, invalid number and an old
  decoder accepting the new report. Exercise both CLI output modes, native
  and managed SDK values and the existing MCP tool response. Independently
  vary the active camera exposure and direct lights in a disposable test
  scene, Apply, and verify that the next capture reports the new values.
  Check refusal, paused/pending Apply and overlap, late/disconnected reply,
  wrong extent, projection mismatch, missing or altered reference file,
  and clean capture while selection overlays are enabled.
- [ ] **R1–R4 — Windows 100%, actual embedded Runtime.** Use a supported
  Windows reference machine with the pinned Bistro inputs and a native
  viewport large enough for 960 × 540. Use the built-in Bistro for calibration,
  then use a disposable editable Bistro copy retaining the original coordinates
  for acceptance. Record revision, asset pin, scene identity, driver, extent
  and capture configuration.
  1. In Street Corner, take one live preview/window screenshot and capture
     A. Navigate elsewhere by orbit and dolly. Return and capture B.
     Repeat the sequence in Storefront Terrace. Confirm both official
     references load locally and display their sources, original dimensions,
     and unknown conditions beside the recorded Atlantis values.
  2. For each A/B pair confirm identical 960 × 540 PNG dimensions, the same
     frozen camera/lens, and numerically equivalent frame matrices
     (absolute tolerance 0.0001 per element). Identify the three fixed
     landmarks named during calibration on the two unchanged Atlantis
     projections and confirm they are within one output pixel. Capture and
     show the pair even when the official reference's framing or illumination
     differs. Judge neither pixel-by-pixel color nor parity with ORCA.
  3. Keep an Inspector draft while changing views and verify it remains
     uncommitted. Record document revision, active camera GUID and values,
     dirty flag, history and Apply receipt before and after camera-only
     actions. Verify picking and Gizmo targeting in the live viewport
     once it settles. Outside the camera-only check, use an ordinary edit
     and Apply on the test scene, return to a preset, and verify the
     composition still uses the frozen pose. Use a compact window to
     show the size refusal and verify normal navigation remains available.
- [ ] **Applicable gates.** Run the affected Windows Runtime, CLI, Remote,
  C# SDK, Editor and MCP checks. Keep Vulkan validation clean for the
  hosted run and the offscreen capture test. Evaluate Android build/link,
  headless, golden and performance applicability on the final diff.
  Record the existing `--gate --applies` result and run the original
  performance protocol if selected. Preserve the baseline, golden images
  and raw results. The ORCA stills are explanatory comparison inputs.

## Rollback

Revert the Editor actions and the additive report producer. Clients already
handle report omission. No scene data or workspace preference migration is
needed.

## Definition of Done

Follow [Definition of Done](../process/definition-of-done.md). The
implementation PR links the Windows images, both frame reports per
view and the applicable gate evidence.
