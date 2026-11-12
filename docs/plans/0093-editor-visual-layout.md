# Plan 0093: Editor Visual Layout and Controls

- **Spec:** [Spec 0093](../specs/0093-editor-visual-layout.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending review of the Spec, Plan and visual mockups

## Objective

Bring the ordinary Windows Editor and asset reference chooser to one visual
language while preserving current actions, authority, and state transitions.

## Milestones / Task Breakdown

1. **S1. Joint review.** Review the [design views](../specs/0093-editor-visual-layout.md#proposed-design),
   agree on the scope and visual direction, and obtain explicit authorization
   through verification and an implementation PR after the design merges.
2. **S2. Shared styles and main window.** Register Editor-private Fluent
   resources and original icon paths. Recompose existing controls into the
   command row, panes, state strip and diagnostics section. Keep all pointer
   and keyboard routes, sizing and session bindings. Group structural actions
   within the Hierarchy. Give the Inspector, Assets and Saved Scenes common list,
   component, form and result treatments.
3. **S3. Reference chooser and constrained sizes.** Apply the same visual
   rules to the generic and material variants, including the narrow flow,
   selectable long text, stable action footer and Confirm focus restoration.
4. **S4. Verify and submit.** Run the affected suites and the Windows matrix
   below. Attach captures and the interaction results to a review-ready PR.

## Files / Modules Touched (expected)

- `src/editor_app/Atlantis.Editor/{Program,MainWindow,AssetReferencePickerWindow}.cs`
  and one private `EditorVisuals.cs` for shared resources, styles and icon
  geometry. Keep the current C# composition and project inputs.
- `EditorSession.cs` only to pass the existing `ApplyStatus` alongside the
  current apply message to the window. Keep existing scheduling, results,
  enablement and recovery rules.
- Affected Editor UI fixtures under `src/editor_app/tests/Atlantis.Editor.Tests/`,
  limited to layout, name/keyboard, Assets, saved scene and reference picker
  checks. Navigation in `docs/specs/README.md`.
- No changes are planned for `ViewportHost`, `ViewportWindow`, the manifest,
  the Editor model, protocol, Runtime, native rendering,
  NuGet pins, lock files or the build system. Use existing controls and data.
  Review any necessary file-scope adjustment under the repository workflow.

## Sequencing & Dependencies

Use [0064](../specs/0064-editor-platform-direction.md) and the current Windows Editor.
Preserve [0082](../specs/0082-generic-asset-reference-picker.md),
[0083](../specs/0083-authoring-apply-outcomes.md),
[0090](../specs/0090-saved-scene-build-open.md) and
[0092](../specs/0092-mesh-material-preview.md). Visual approval precedes S2.

| Decision | Recommendation |
| --- | --- |
| **J1. Window layout** | Approve the three panes, centered transport controls, local hierarchy actions and separate diagnostics, with compact reflow inside the existing panes. |
| **J2. Visual system and resources** | Approve neutral graphite Fluent, restrained blue emphasis, the Windows UI font and project-authored icon paths. Keep existing dependencies and window chrome. |
| **J3. Dialog and state behavior** | Approve the two forms of one chooser, fixed action footer and existing keyboard/recovery semantics. Keep New Scene / Save As inline. |
| **J4. Acceptance** | Approve the real Windows matrix and representative screenshots and interactions below. |

## Verification Checklist

- [ ] **R1/R2. Actual Windows sizes and DPI.** Build the Windows Editor and
  run with a real embedded Runtime. Check approximately 1400 × 860 client
  DIP at 100%, 1100 × 700 at 125%, and 960 × 600 at 150% and 200% on a
  display with enough work area. Record actual client dimensions, scaling
  and monitor/work area. Maximize, restore, and resize across the compact
  transition. Move between two differently scaled monitors when available,
  or exercise the same instance through a Windows scale change and report
  that substitution. Capture standard and compact layouts and the material
  chooser at higher scaling. Check no clipped controls or hidden required
  actions, reachable Hierarchy Actions, sensible pane widths, and no child
  viewport overlap or input offset.
- [ ] **R2/R3. Long and constrained content.** Use a long Unicode node
  label, a long unbroken source/checkout path, a full GUID, and a multiline
  Runtime/build failure. Check readable full values, no horizontal scroll
  at window level, scrollable content with a reachable footer, and text,
  focus and control-boundary contrast against the chosen palette. Check that
  selected rows, active tabs and keyboard focus remain distinct. Verify
  normal and compact material chooser (check a 600 × 600 DIP client area
  at 150% or 200%) plus the non-preview variant.
- [ ] **R3/R4. Interaction and state.** Check keyboard traversal and visible
  focus, pointer/keyboard selection, a text draft through refresh, chooser
  F4, Cancel and Confirm with focus restoration, and a filtered or stale
  target. Check Assets empty/loading/busy/refused states and an explicit
  preview retry, without coupling preview to Confirm. Check inline
  New Scene/Save As controls and Saved Scenes build/open results when
  the workspace is enabled. Exercise one authoring edit through apply,
  visible failed/paused state and explicit recovery, Undo/Redo, Save and
  the Viewport gizmo. Record the normal Runtime exit and a clean Windows
  Vulkan validation log during this session.
- [ ] **Applicable gates.** Run the focused Editor build and affected managed
  model and UI checks, retaining locked package verification. Run
  `atlantis_runtime --gate --applies` on the final diff and record the
  result. Editor-only styles and layout are expected to leave engine golden
  and performance gates inapplicable. If affected paths or rendered output
  select a gate, use the existing policy, including human review of any
  engine golden change. Retain the separate #340 historical hold.
- [ ] **Design PR.** Check links, whitespace, screenshot legibility and
  visual consistency with the Spec. The Windows matrix is implementation
  acceptance.

## Rollback Plan

Revert the Editor presentation change. No data, protocol, or preference
migration is required.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). The
implementation PR records the approved visual reference, Windows captures,
interactions and applicable gate results. Final merge remains human.
