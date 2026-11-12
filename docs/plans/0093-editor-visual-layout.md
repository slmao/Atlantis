# Plan 0093: Editor Visual Layout and Controls

- **Spec:** [Spec 0093](../specs/0093-editor-visual-layout.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending review of the Spec, Plan and visual direction

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
   component hierarchy and a shared set of Transform column headings.
   Preserve existing pointer routes, field bindings and keyboard order.
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
- `EditorSession.cs` only to pass the existing `ApplyStatus` alongside
  the current apply message to the window. Preserve scheduling, results,
  enablement and recovery rules.
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
| **J1. Window layout** | Approve the refined three-pane layout, centered transport, local Viewport tools, inline scene file commands and compact workspace scrolling. |
| **J2. Visual system and resources** | Approve neutral dark gray surfaces, the Segoe UI hierarchy, aligned XYZ columns and project-authored line icons in existing controls. |
| **J3. Details, chooser and state** | Approve grouping for Assets and Saved Scenes, reachable technical details and the preview/confirmation proportions. Preserve existing focus, draft, apply, build/open and recovery rules. |
| **J4. Acceptance** | Approve the actual Windows size/DPI matrix, long-content and interaction checks, and final-diff gate selection below. |

## Verification Checklist

- [ ] **R1/R2. Actual Windows sizes and DPI.** Build the Windows Editor and
  run with a real embedded Runtime. Check approximately 1400 × 860 client
  DIP at 100%, 1100 × 700 at 125%, and 960 × 600 at 150% and 200% on a
  display with enough work area. Record actual client dimensions, scaling
  and monitor/work area. Maximize, restore and resize through the compact
  transition. Move one instance between differently scaled monitors when
  available, or exercise it through a Windows scale change and report the
  substitution. Capture native-resolution main and material-chooser
  windows at 100% and higher scaling. Capture Assets and Saved Scenes at
  standard and compact sizes with preview and current build outcomes.
  Check clean text and icons, usable
  pane widths, reachable actions, no clipping, and no child viewport
  overlap or input offset.
- [ ] **R2/R3. Density, contrast and long content.** Compare the approved
  mockups with actual Windows captures. Check visible distinctions between
  resting, hover, selected, active, focused and failed states and measure
  contrast as specified. Check XYZ alignment, accessible axis labels and
  stable keyboard order at both sizes. Use a long Unicode node/asset name,
  a long unbroken path, a full GUID and a multiline Runtime or build
  failure. Confirm complete values are readable and copyable in details
  without horizontal window scrolling. Check the material chooser at
  standard size and at 600 × 600 DIP at 150% or 200%, with search and
  footer available and preview below results at the narrow size. Check
  the non-preview variant as well.
- [ ] **R3/R4. Interaction and state.** Exercise keyboard traversal and
  visible focus. Expand reference details, edit the raw GUID and check
  Enter/Escape, blur, and a draft through refresh. Exercise F4, pointer
  Choose, Cancel and Confirm with focus restoration; verify the current
  rules for guarded Clear, stale or filtered-out candidates and full
  metadata/dependency access. Check Assets empty/loading/busy/refused
  states and an explicit preview retry. Verify that filter and Reload view
  stay reachable when the preview or details scroll. Exercise the existing
  Rebuild and Import entry points and verify busy or failed results and
  their next actions remain reachable through the grouping. Switch candidates during an
  outstanding preview: no stale image or error may reappear and Confirm
  follows its existing guard. Check inline New Scene / Save As and
  Saved Scenes build/open when available. Check successful build and
  independent Open, stale results with Open disabled, a failed build and
  retry, and copyable checkout, output and log paths. Exercise an authoring
  edit with a visible failed or paused apply result, explicit recovery,
  Undo/Redo, Save and the Viewport gizmo. Record normal Runtime exit and
  a clean Windows Vulkan validation log during this session.
- [ ] **Applicable gates.** Run the focused Editor build and affected managed
  model and UI checks, retaining locked package verification. Run
  `atlantis_runtime --gate --applies` on the final diff and record the result.
  Editor-only styles/layout are expected to leave engine golden and
  performance gates inapplicable. If affected paths or rendered output
  select a gate, follow the existing policy, including human review of
  any engine golden change. Keep the separate #340 historical hold.
- [ ] **Design PR.** Check local links, whitespace, palette contrast and
  mockup legibility at 100% and 150% rendered independently. Compare
  main window and chooser before/after at the same logical dimensions. These
  studies support visual review; the actual Windows matrix remains
  implementation acceptance.

## Rollback Plan

Revert the Editor presentation change. No data, protocol, or preference
migration is required.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). The
implementation PR records the approved visual reference, actual Windows
captures, interactions and applicable gate results. Final merge remains human.
