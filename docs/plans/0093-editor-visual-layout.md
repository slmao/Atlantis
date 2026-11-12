# Plan 0093: Editor Visual Layout and Controls

- **Spec:** [Spec 0093](../specs/0093-editor-visual-layout.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** The owner approved J1–J4 and Spec/Plan 0093 at `cfcd78de` on 2026-11-13, including layout A and neutral dark gray. [PR #349](https://github.com/slmao/Atlantis/pull/349) was manually merged as `b1d09255`. Authorization covers S2–S4, scoped fixes, applicable verification, valid evidence reuse and a review-ready implementation PR. Final merge remains human.

## Objective

Bring the Windows Editor and asset reference chooser to one visual language
while preserving current actions, authority, and state transitions.

## Milestones / Task Breakdown

1. **S1. Joint review.** Compare the [matched design studies](../specs/0093-editor-visual-layout.md#proposed-design).
   Review the refined three-pane layout, including the Assets, Saved Scenes
   and compact studies. Confirm J1–J4 and explicitly authorize
   implementation through verification and a review-ready PR after merge.
2. **S2. Styles and main window (R1/R2/R4).** Register Editor-private Fluent
   resources and original icon paths. Use the existing layout, list, tab,
   text and disclosure controls to recompose the command row, Viewport
   toolbar, panes, status strip and details. Establish the object and
   component hierarchy and shared Transform column headings. Style each
   vector as one editor over the existing reflected field. Preserve pointer
   routes, field bindings, vector units, keyboard order and edit settlement.
3. **S3. Workspace tabs and chooser (R2/R3/R4).** Group the current Assets
   list, selected asset, preview, Rebuild and Import controls. Group Saved
   Scenes inputs, results and logs. Keep stable search and confirmation
   regions in the chooser and reflow the preview at narrow sizes.
   Preserve current guards, draft settlement, selection, focus restoration,
   request intent and existing error recovery.
4. **S4. Verify and submit.** Run the affected suites and the Windows matrix
   below. Attach native-resolution captures, interaction results and final
   gate selection to a review-ready PR for human merge.

## Files / Modules Touched (expected)

- `src/editor_app/Atlantis.Editor/{Program,MainWindow,AssetReferencePickerWindow}.cs`
  and one private `EditorVisuals.cs` for shared resources, styles and icon
  geometry. Keep the current C# composition and project inputs.
- `EditorSession.cs` only to publish the existing `ApplyStatus` with the
  current message, including the lost-acceptance path. Capture them from
  the same state and preserve document lifetime checks. The existing apply
  control flags still determine enablement. Preserve scheduling and recovery.
- Affected UI fixtures under `src/editor_app/tests/Atlantis.Editor.Tests/`,
  limited to layout, accessible names and keyboard behavior, Assets,
  Saved Scenes and the reference picker. Navigation in `docs/specs/README.md`.
- No changes are planned for `ViewportHost`, `ViewportWindow`, the manifest,
  Editor model, protocol, Runtime, native rendering, NuGet pins, lock files
  or build system. Review any necessary file-scope adjustment under the
  repository workflow.

## Sequencing & Dependencies

Keep the current [Windows Editor architecture](../specs/0064-editor-platform-direction.md).
Preserve [0082 reference editing](../specs/0082-generic-asset-reference-picker.md),
[0083 apply outcomes](../specs/0083-authoring-apply-outcomes.md),
[0090 saved scene build/open](../specs/0090-saved-scene-build-open.md) and
[0092 combination preview](../specs/0092-mesh-material-preview.md).

| Decision | Recommendation |
| --- | --- |
| **J1. Window layout** | Approve the refined three-pane layout, centered transport, local Viewport tools, inline scene file commands, visible Apply and compact workspace scrolling. |
| **J2. Visual system and resources** | Approve neutral dark gray surfaces, the Segoe UI hierarchy, aligned XYZ summaries with one editor per vector, and project-authored line icons. |
| **J3. Details, chooser and state** | Approve grouping for Assets and Saved Scenes, reachable technical details, distinct current and candidate references, and the preview/confirmation proportions. Preserve existing focus, draft, apply, build/open and recovery rules. |
| **J4. Acceptance** | Approve the actual Windows size/DPI matrix with live reflow and Viewport input checks, long-content and interaction checks, and final-diff gate selection. |

### Recommended authorization

After a human jointly approves J1–J4, Spec 0093 and Plan 0093 and merges
this design PR, implement S2–S4 from the latest main under one authorization.
It covers implementation, ordinary fixes inside the approved scope,
applicable checks and a review-ready implementation PR. Reuse verification
evidence only where its relevant inputs and environment still match. Record
milestone results as work progresses. Follow the repository workflow for
scope deviations, verification evidence and final human merge.

## Verification Checklist

- [ ] **R1/R2. Actual Windows sizes and DPI.** Build the Windows Editor and
  run with a real embedded Runtime. Check approximately 1400 × 860 client
  DIP at 100%, 1100 × 700 at 125%, and 960 × 600 at 150% and 200% on a
  display with enough work area. Record client DIP and physical pixel
  dimensions, actual scaling and monitor work area for each case. A missing
  display configuration leaves that case unverified. Maximize, restore,
  drag both splitters and cross the compact transition by resizing.
  Move one running instance between differently scaled monitors when
  available. On a single display, exercise a Windows scale change on
  that instance and record the substitution and observed scaling.
  Verify tab, selection and draft retention through reflow. Check
  Viewport bounds and pick or gizmo hit alignment after a live scale
  change and after resizing. Capture native-resolution main and material
  chooser windows at 100% and higher scaling. Capture Assets and Saved
  Scenes at standard and compact sizes. Check clean text and icons,
  reachable commands and no child viewport overlap.
- [ ] **R2/R3. Density, contrast and long content.** Compare the approved
  mockups with actual Windows captures. Check visible distinctions between
  resting, hover, selected, active, focused and failed states and measure
  contrast as specified. Check XYZ alignment, accessible property and axis
  order, and one tab stop per vector at both sizes. Enter one vector edit
  and verify one transaction, one Undo step and the correct Redo value.
  Check Escape, invalid input, exact float text and a focused vector draft
  across the pane reflow. Verify Rotation retains radians. Use a long
  Unicode node/asset name, a long unbroken path, a full GUID and a
  multiline Runtime or build failure. Confirm complete values are readable and copyable in details
  without horizontal window scrolling. Check the material chooser at
  standard size and at 600 × 600 DIP at 150% or 200%, with search and
  footer available and preview below results at the narrow size. Check
  the non-preview variant and its existing type filter for an untyped field.
  Check current-reference feedback for an absent value, an asset outside the
  snapshot and a listed wrong-type asset. Check that keyboard navigation
  keeps the active result visible in a long list and can reach the preview
  and footer without walking the catalog.
- [ ] **R3/R4. Interaction and state.** Exercise keyboard traversal and
  visible focus. Expand reference details, edit the raw GUID and check
  Enter/Escape, blur, and a draft through refresh. Exercise F4, pointer
  Choose, Cancel and Confirm with focus restoration; verify the current
  rules for guarded Clear, stale or filtered-out candidates and full
  metadata/dependency access. Check Assets empty/loading/busy/refused
  states and an explicit preview retry. Verify that filter and Reload view
  stay reachable when a long result list, preview or details scroll.
  Reload view with a detail request pending and verify the selected asset's
  new detail wins. Exercise Rebuild and Import and verify busy or failed
  outcomes and recovery stay discoverable with the grouping collapsed.
  Switch candidates during a pending preview and check stale image/error
  suppression and the independent Confirm guard. Check inline New Scene
  and Save As, and verify that receipt hints leave local selection explicit
  in Saved Scenes. Check successful build and independent Open, stale
  results with Open disabled, failed build and retry, and copyable checkout,
  output and log paths. Exercise a visible failed or paused apply and an
  external edit requiring explicit apply. Check recovery, Undo/Redo,
  Save, the Viewport gizmo and the Scene camera reset action. Record normal
  Runtime exit and a clean Windows Vulkan validation log during this session.
- [ ] **Applicable gates.** Run the focused Editor build and affected managed
  model and UI checks, retaining locked package verification. Run
  `git diff --name-only origin/main... | atlantis_runtime --gate --applies`
  against the final candidate and record the result.
  Editor-only styles/layout are expected to leave engine golden and
  performance gates inapplicable. If affected paths or rendered output
  select a gate, follow the existing policy, including human review of
  any engine golden change. Keep the separate #340 historical hold.
- [ ] **Design PR.** Check local links, whitespace, palette contrast and
  mockup legibility at 100% and 150% rendered independently. Compare
  main window and chooser before/after at the same logical dimensions.
  Use the actual Windows matrix for implementation acceptance.

## Rollback Plan

Revert the Editor presentation change. No data, protocol, or preference
migration is required.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). The
implementation PR records the approved visual reference, actual Windows
captures, interactions and applicable gate results. Final merge remains human.
