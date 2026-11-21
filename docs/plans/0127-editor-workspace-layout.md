# Plan 0127 — Save and Reset the Editor Workspace Layout

- **Spec:** [Spec 0127](../specs/0127-editor-workspace-layout.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** 2026-11-21, [J1–J4 and S2–S4 authorized](https://github.com/slmao/Atlantis/pull/418#issuecomment-7059371967) for `b4e65a82`, effective after maintainer merge `1f14c1b8`.
- **ADR:** [ADR-0176](../adr/0176-editor-workspace-layout-preferences.md) (Accepted)

## Objective and Milestones

Restore the three-pane layout and chosen workspace tab after an Editor
restart. Baseline `a04c67b2` is the maintainer merge of
[PR #416](https://github.com/slmao/Atlantis/pull/416).

1. **S1 — Joint review.** Resolve J1–J4 and ADR-0176. Begin implementation
   after approval and maintainer merge of the design.
2. **S2 — Preferences and sizing.** Add the private record, writer lease and
   pure layout projection. Verify constraints, field updates and reset ordering.
3. **S3 — Main-window integration.** Restore after measured layout and tab
   registration. Capture explicit splitter and tab intent. Add the guarded
   reset action and local failure feedback. Verify existing Viewport and edit
   behavior.
4. **S4 — Verification and PR.** Retain Windows 100% and applicable gate
   evidence, review the final diff, and submit an implementation PR for
   maintainer merge.

## Expected Files

| Files | Scope |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/WorkspaceLayout.cs` (new) | Preference values, default widths, tab fallback and constrained projection. |
| `src/editor_app/Atlantis.Editor/WorkspaceLayoutStore.cs` (new) | Local versioned record, independent writes and atomic reset. |
| `src/editor_app/Atlantis.Editor/MainWindow.cs` | Existing grid and tab intent, size and scale reflow, guarded reset and feedback. |
| `src/editor_app/tests/Atlantis.Editor.Tests/WorkspaceLayoutTests.cs` (new), `src/editor_app/tests/Atlantis.Editor.Tests/Program.cs`, `src/editor_app/tests/Atlantis.Editor.Tests/InspectorTests.cs` | Model/store tests and existing hosted Windows test entry. |
| `docs/specs/README.md`, Spec 0127, Plan 0127 and ADR-0176 approval metadata | Navigation and approval. |

## Joint Review

| Decision | Recommendation |
| --- | --- |
| **J1 — Scope and storage** | Accept ADR-0176's per-user, per-machine preferences and explicit last-commit policy. Include independent ordinary Editor windows. |
| **J2 — Restore** | Accept two preferred DIP widths plus the fill Viewport. Use current minima and proportional compression. Persist direct tab choices, resolve availability, and preserve intent through automatic fallback. |
| **J3 — Reset** | Accept one guarded command and complete default fallback on corrupt settings. Persist reset atomically and leave editing and Runtime state with their existing owners. |
| **J4 — Evidence and authorization** | Approve the checks below. After joint approval and design merge, authorize S2–S4 once for scope-preserving implementation, applicable verification and a review-ready PR. Final merge remains manual. |

## Verification Checklist

- [ ] **R1/R2 — Layout and DPI.** Verify the width pair reproduces the layout
  at the same measured DIP size. Check compact and standard defaults, excessive
  saved widths, shrink and re-expand, and transient unmeasured bounds. Drive a
  deterministic DPI transition through the same size/scale layout path with
  changed physical-to-logical budget and back. Inspect all three pane minima,
  splitter use and the resulting Viewport bounds. Verify no automatic write.
- [ ] **R1/R3 — Storage and concurrency.** Use isolated settings. Test missing,
  corrupt, oversized, non-finite/invalid widths, unknown version/tab, unsafe or
  unavailable storage, and a failed atomic replacement. Test independent
  width/tab writers, no close-time or fallback overwrite, explicit selection of
  a tab already visible as a fallback, reset followed by a delayed same-window
  update, and another window's later explicit edit.
  Inspect the file to verify the closed schema and absence of document state.
- [ ] **R1–R3 — Native Windows 100%.** Adjust a splitter and choose a tab,
  restart and verify restoration. Shrink to an existing compact size, recover
  useful controls and the Viewport, and expand back. Restore Saved Scenes in
  an authorized launcher, then launch without it and verify Inspector fallback
  without granting access or erasing the preference. Exercise corrupt settings
  and explicit reset through restart. Verify draft input remains live during
  reflow and blocks reset while active. After editing has settled, reset with a
  dirty document and verify its revision, selection, Undo/Redo and Runtime
  state. Capture standard, compact, and reset or fallback evidence. Confirm a
  representative Viewport click stays aligned at 100% after width changes.
- [ ] **Applicable gates.** Run affected Editor tests and applicable native
  checks for the hosted launch. Assess renderer/golden, platform and performance
  applicability from the final diff, using `atlantis_runtime --gate --applies`
  and the existing protocol where selected. Reuse matching evidence only.

## Rollback

Revert the layout integration and leave the local preference record in place.
Follow the [Definition of Done](../process/definition-of-done.md) for the final
implementation candidate.
