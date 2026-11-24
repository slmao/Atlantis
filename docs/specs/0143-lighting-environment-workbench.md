# Spec 0143 — Lighting and Environment Workbench

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-24
- **Milestone:** 3D Scene and Render Effects Editor V1, first slice
- **Design baseline:** `b27d93fe` on `main`
- **Related Plan:** [Plan 0143](../plans/0143-lighting-environment-workbench.md)
- **Approval:** Pending joint Spec and Plan review
- **Related ADR:** None proposed. [ADR-0069](../adr/0069-environment-asset-preprocessing-and-ownership.md) and [ADR-0180](../adr/0180-shared-frame-report-exposure.md) continue to apply.

## Summary

Make a daylight lighting pass on an editable copy of Bistro. Add a compact
lighting workbench in the existing Hierarchy and Inspector workflow so an
editor can find the directional light and select visible point lights as a
group. Use the existing controls to edit them. Compare settled captures at
both 0131 views with exposure held fixed. Save the result and open it in an
independent Editor with Saved Scenes. Preserve the original night scene.

## Current capability and gap

| Area | Supported behavior and owner |
| --- | --- |
| Direct lighting | The authoring document owns `Light.kind`, linear RGB `Light.color`, non-negative `Light.intensity`, and point-only `Light.range`. `Transform.localEulerAnglesRadians` on the directional node drives its world aim (normalized transformed local −Z). Point position comes from its node transform. The current scene limits are one directional and 64 point lights. [Schema](../../src/asset_system/src/scene_semantic_schema.cpp) and [Runtime extraction](../../src/runtime/src/scene_extraction.cpp). |
| Selection and edits | [0113](0113-hierarchy-search-and-filter.md) filters by component and retains ancestor context. It cannot narrow `Light` to `Directional` or `Point` or select just the matching lights in one action. The existing Inspector and [0106](0106-scene-multiselect-batch-edit.md) already support single fields, mixed values and guarded batch assignment. Some Bistro lights have no authored name. |
| Environment | Runtime startup chooses an optional cooked `.aenv`, providing sky and image-based lighting. Built-in `bistro` disables it; ordinary `saved_scene` uses the configured studio environment. Neither the scene source/cooked scene nor shared authoring has a scene-owned environment reference, rotation or strength. The [launcher](../../src/runtime/main.cpp) and [ADR-0069](../adr/0069-environment-asset-preprocessing-and-ownership.md) own this boundary. |
| Camera and comparison | The authored active `Camera` owns exposure compensation, fog and bloom. The [0137](0137-bistro-light-exposure-tuning.md) locator selects that camera. [0131](0131-bistro-reference-views.md) supplies the Street Corner and Storefront Terrace preview poses, lenses and 960 × 540 physical PNGs, with frame matrices, actual EV when reported and effective direct lights. The 0137 paired view is terrace-only. |
| Save and independent Open | [0088](0088-scene-save-as.md) creates a fresh scene from a hand-authored document and [0090](0090-saved-scene-build-open.md) explicitly builds and opens it. Save As currently refuses the imported built-in Bistro. 0131 already used a disposable hand-authored copy with the same coordinate system and bindings. |

## Requirements

- **R1 — Night source and day copy.** Use the 0131 hand-authored Bistro fixture
  procedure with pinned geometry, bindings, night overlay values and the same
  coordinate system. Fixture provisioning is setup for the Windows run. In
  Editor, Save As to a new day scene, then explicitly Build and Open that copy
  in a separate window. The source remains unchanged. The built-in night is
  retained as a reference. Baseline and edited captures for the controlled
  experiment come from the **same saved day copy**, with the same startup
  environment in both. Consult the built-in night and ORCA separately for
  context. Their different or unknown environment settings exclude them from
  the lighting-only comparison.
- **R2 — Workbench selection.** Extend the existing Hierarchy filters with
  an optional light-kind filter (all lights, directional, point) and an
  explicit **Select matching lights** action. Compose it with the current
  name and component filters, retain ancestors for context, and show the
  number of actual matches. Clearing the light filter restores the ordinary
  Hierarchy projection with the other active filters.
  Selecting matches replaces the selection with the matching rows in Hierarchy
  order, including lights with fallback labels. It excludes ancestor-only and
  hidden rows. With zero matches or an incomplete kind read, give a reason and
  keep the selection. Project kind from the Worker's latest authoring snapshot
  using the shared schema. Before changing selection, recheck the current
  session, document, filter/selection intent, kind and node incarnation,
  including same-GUID replacements. Stale or unverifiable results and active
  draft or gesture protection leave selection and drafts intact. Reuse 0113
  filter clearing and row location. A compact environment note explains that
  sky and IBL are set at Runtime launch. Where configuration is unverified,
  show no inferred environment name or enabled state.
- **R3 — Inspector, Apply and history.** Use the existing Inspector directly.
  The single directional light uses rotation, color and intensity. Point
  lights use their existing fields; use common intensity for the day pass.
  A single field commit or one guarded batch **Set** yields one history step.
  A directional edit and a point batch remain separate steps. A draft changes
  only input text, and fixed-view preview only changes the Editor viewport
  camera. Existing property commits retain their automatic Apply behavior;
  the [0083](0083-authoring-apply-outcomes.md) receipt determines whether the
  document reached the Runtime projection. Failed or unknown Apply leaves the
  committed edit and settled history intact under the existing recovery rules.
  Reconcile an unknown outcome or correct a refusal and explicitly Apply;
  recovery does not resubmit the property edit. Save writes the authoring
  document independently of Runtime settlement. Undo/Redo preserves the
  existing conflict guards and restores per-node values, including mixed values.
- **R4 — Controlled comparison and persistence.** Start with the day copy's
  original night lighting. Keep the active authored camera, reported exposure
  EV, camera fog/bloom, launch environment, geometry and materials fixed. A
  missing effective EV is insufficient evidence. Capture baseline, directional
  change, point-light change and Undo restoration with the existing fixed
  views. Compare two captures only with the expected settled document revision
  and Runtime generation, matching camera matrices and framing, and no active
  edit preview. Existing ordinary 0131 capture suffices at both views; the
  terrace paired action remains available. Explicitly Save, close and use
  Saved Scenes Build/Open from the same day source. Verify the reopened
  authored values and a settled capture in the new Runtime. The reference
  views do not modify the authored camera or create history.

## Scope and architectural impact

The workbench adds Editor selection and status controls over the existing
authoring data, using the [0093](0093-editor-visual-layout.md) visual
conventions. Existing property queries, transactions and frame reports are
the shared contract for CLI, SDK,
MCP and Editor. No new request type, bootstrap override, persistent layout key,
per-frame query or storage is needed. Windows is the product Editor target.
The scene files, cook formats, Vulkan path and resource ownership stay as they
are. No new ADR is required for that scope.

Scene-owned sky/IBL selection and editing would require a reviewed authoring
and Runtime ownership decision, source/cook compatibility and a projection
resource-swap contract. Defer them along with editable-copy conversion for
arbitrary imported scenes, presets, standalone post-processing tools, new
light types and rendering algorithms. An existing studio environment is
sufficient for the controlled direct-light experiment; ORCA visual parity is
outside the acceptance criteria.

## Verification

[Plan 0143](../plans/0143-lighting-environment-workbench.md) maps R1–R4 to
Editor checks, Windows 100% evidence, independent launch and applicable gates.
