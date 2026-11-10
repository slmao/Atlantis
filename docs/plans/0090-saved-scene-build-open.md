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
   and fingerprint the fresh Debug executable through the CMake File API.
3. **S3 — Editor flow and launch.** Add receipt hints, selection, Build, result
   and Open. Integrate attempt state with window close and the duplicate child
   guard. Start the ordinary child apphost and leave it responsible for its Runtime.
4. **S4 — Verification and implementation PR.** Map evidence to R1–R5 and
   report the applicable gates. Final merge is human.

## Files / Modules Touched (expected)

- `src/editor_app/Atlantis.Editor/Program.cs` and `MainWindow.cs` for
  launch settings, receipt hints and the panel. Extend `EditorSession.cs`
  to pass confirmed receipt values to the panel without parsing status text.
  Keep process launch in the Editor app and reuse its ordinary child Runtime
  entry path.
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
| **J1 — Local authority and selection** | Approve explicit local build authority separate from source-write permission, shallow discovery, and typed receipt hints with explicit local selection. Keep catalog validation in CMake (R1). |
| **J2 — Build profile and assets** | Approve Windows x64 Debug on Visual Studio 18 2026, one fresh saved-slot build per attempt, an explicit committed managed root and no generation transfer. Treat build and Runtime load failures as separate phases (R2/R3). |
| **J3 — Ownership, retries and Open** | Approve a launcher-owned build with retained logs and source/executable freshness checks. Approve separate child startup, per-launcher source reservation, and phase-specific close and retry rules (R3–R5). |
| **J4 — Acceptance and delivery** | Approve both Windows acceptance paths, the negative cases below, and one implementation authorization through S4 after human design merge. Preserve applicable gates and final human merge. |

Suggested authorization: approve J1–J4, Spec/Plan 0090 and ADR-0150 at the
reviewed revision, then authorize S2–S4 from current main after manual design
merge, including scoped fixes, applicable verification, valid evidence reuse
and an implementation PR for review.

## Verification Checklist

- [ ] **R1 — Discovery and receipts.** Exercise explicit workspace and tool
  opt-in, separation from source-write authority, and refusal of launcher
  settings in measurement, attach and `saved_scene` modes. Check canonical
  complete units, missing root, path/access error, incomplete entries,
  reparse points, and Refresh. Check typed success hints from both operations,
  including a different checkout with the same name. Confirm explicit
  selection is still required. Unknown and refused outcomes trigger no action.
- [ ] **R2/R3 — Build and recovery.** Check arguments with spaces, fixed
  generator/architecture/configuration, phase order, exits, and retained
  logs. Resolve the exact Debug target from the fresh CMake reply. Reject
  missing, ambiguous, reparse or out-of-tree artifacts. Test duplicate Build,
  close while active, configure and build failures, and retry in a fresh
  directory. Verify a failed attempt cannot reopen a previous artifact.
  Test source changes during build and before Open, a removed selection,
  managed root changes, and a deleted or replaced executable after success.
  Confirm build and launch use the explicit committed inputs without
  forwarding Runtime profile or generation.
- [ ] **R4 — Independent launch.** Check duplicate Open and Build for one
  source during process start and while the child is live, including
  selection away and back. Check close during start, retry after process
  start failure, and release of the guard after child exit. Confirm child
  startup or authoring failure has its own diagnostics and is distinct from
  process start. Confirm the child survives launcher closure and uses a
  distinct Runtime/session file. Cross-check an artifact with the documented
  command-line recipe.
- [ ] **R5 — Live Windows flow.** Keep the original development Editor open
  with a distinguishable unsaved edit and history. Create a camera starter
  and Save As a hand-authored scene that references a committed managed
  mesh. Select and build each unit using the panel. For the managed copy,
  omit the root in a controlled attempt and follow the actual failure phase.
  If CMake succeeds, Open and observe Runtime startup failure. Preserve both
  unit files, then set the correct root and Build again from the same
  launcher. Verify the original Runtime's explicit active generation,
  session and document before and after both workflows.

  Open each successful build in a second ordinary Editor and verify the
  expected scene GUID and source binding. Add a mesh to the starter and edit
  the copied mesh in the other case. Observe apply and Save in both
  children. Reopen the sources and verify scene and node identities,
  references and saved edits. Verify a Save that changes either unit makes
  the old build ineligible for Open after the child exits. Verify the
  original document, source, edits and history remain intact. Validate
  child startup and rendering with Vulkan validation enabled.
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
