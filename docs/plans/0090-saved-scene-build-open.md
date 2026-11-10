# Plan: Build and Open Saved Scenes

- **Spec:** [Spec 0090](../specs/0090-saved-scene-build-open.md)
- **ADR:** [ADR-0150](../adr/0150-local-saved-scene-build-launch.md) (Proposed)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending

## Objective

Deliver a local Windows Editor flow that takes a saved scene unit through an
explicit build and a separate Open action. Preserve the existing publication,
saved-slot and ordinary authoring contracts.

## Milestones / Task Breakdown

1. **S1 — Joint design review.** Resolve J1–J4 and record joint Spec/Plan
   approval and ADR acceptance before implementation.
2. **S2 — Local selection and build.** Add trusted launch inputs and discovery.
   Implement a UI-independent attempt model and a private CMake adapter.
   Capture source fingerprints, collect phase results and logs, and resolve
   the fresh Debug executable through the CMake File API.
3. **S3 — Editor flow and launch.** Add receipt hints, selection, Build, result
   and Open. Integrate attempt state with window close and the duplicate child
   guard. Start the ordinary child apphost and leave it responsible for its Runtime.
4. **S4 — Verification and implementation PR.** Map evidence to R1–R5 and
   report the applicable gates. Final merge is human.

## Files / Modules Touched (expected)

- `src/editor_app/Atlantis.Editor/Program.cs` and `MainWindow.cs` for
  launch settings, receipt hints and the panel. Keep shell launch in the
  Editor app and reuse its ordinary child Runtime entry path.
- A small saved-scene attempt model under
  `src/editor_app/Atlantis.Editor.Model/` and a private adapter under
  `src/editor_app/Atlantis.Editor/` for filesystem, CMake and Editor
  process work. Use .NET and the existing CMake installation.
- Extend focused tests under `src/editor_app/tests/` using the existing
  test targets and Windows live test approach.
- README usage and spec navigation. No changes planned to the CMake
  saved slot, engine, wire, shared client surfaces, package list, build
  targets or baseline.

## Sequencing & Dependencies

Base on [0088 / PR #340](https://github.com/slmao/Atlantis/pull/340) and
[0089 / PR #342](https://github.com/slmao/Atlantis/pull/342). Start S2 only
after joint approval, ADR acceptance and manual design merge.

| Decision | Recommendation |
| --- | --- |
| **J1 — Local authority and selection** | Approve explicit trusted checkout and CMake inputs, shallow discovery and confirmed-receipt hints. Keep catalog validation in CMake (R1). |
| **J2 — Build profile and assets** | Approve Windows x64 Debug on Visual Studio 18 2026, one fresh saved-slot build per attempt, explicit committed managed root and no generation transfer (R2/R3). |
| **J3 — Ownership, retries and Open** | Approve a launcher-owned build with retained logs, saved-unit freshness checks and an explicit independent Editor launch. Approve session-local duplicate guards, ordinary close deferral and retry rules (R3–R5). |
| **J4 — Acceptance and delivery** | Approve the checks below and one implementation authorization through S4 after human design merge. Preserve existing gates and final human merge. |

Suggested authorization: approve J1–J4, Spec/Plan 0090 and ADR-0150 at the
reviewed revision, then authorize S2–S4 from current main after manual design
merge, including scoped fixes, applicable verification, valid evidence reuse
and an implementation PR for review.

## Verification Checklist

- [ ] **R1 — Discovery and receipts.** Exercise explicit workspace opt-in,
  canonical complete units, incomplete and private staging entries, reparse
  points, and Refresh. Check both confirmed receipt hints, including a receipt
  from a different workspace with the same name. Check unknown and refused
  outcomes keep their existing inspection/settlement guidance and trigger
  no automatic action.
- [ ] **R2/R3 — Build and recovery.** Check argument vectors including paths
  with spaces, phase order, exit results, retained logs, and exact Debug
  target resolution. Reject missing, ambiguous or out-of-tree artifacts.
  Test duplicate Build, pending window close, configure failure, build
  failure and retry in a fresh directory, a prior successful artifact
  followed by failure, and source changes during build and before Open.
  Confirm the saved build uses the explicit committed managed root and
  never receives Runtime profile/generation arguments.
- [ ] **R4 — Independent launch.** Check duplicate Open during process start
  and while the child is live, retry after start failure and child exit,
  and child Runtime startup failure with visible diagnostics. Confirm the
  child survives launcher closure and uses a distinct Runtime/session file.
  Cross-check the same artifact with the documented ordinary command-line
  launch recipe.
- [ ] **R5 — Live Windows flow.** With unsaved edits in the original
  development Editor, create a camera starter and separately Save As a
  hand-authored scene that references a committed managed mesh. Select and
  build each saved unit using the panel. On the managed copy, omit the
  managed root in a controlled attempt, verify build failure and source
  preservation, then set the root and retry from the same launcher. Use a source
  Runtime with an active generation for the managed case and check its
  active generation and session identity after the build and Open.
  Open each successful build in a separate Editor and verify the expected
  scene and source bind. Add a mesh to the starter and edit the copied mesh
  in the other case. Observe apply, save and reopen the saved documents.
  Verify scene and node identities, references and saved edits. Check the
  original document, history, unsaved edits and source remain intact while
  both flows run. Validate child startup/rendering with Vulkan validation.
- [ ] **Applicable gates.** Run focused Windows managed and model/UI tests,
  live Vulkan checks and documentation checks. Run the full-diff performance
  applicability selector and keep the existing gate if selected. Run or
  justify reuse for other applicable engine, headless and golden checks.
  Record candidate, configuration and provenance in the implementation PR.
  Keep the historical #340 performance record parked.

## Rollback Plan

Remove the panel and local build/launch settings. Previously published units
and retained build directories continue to use the existing CMake and
ordinary Editor workflow.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md).
The implementation PR records verification mapped to R1–R5. Joint approval
and final merge remain human.
