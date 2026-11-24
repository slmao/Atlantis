# Spec 0144 — Post Processing Panel V1

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-25
- **Milestone:** 3D Scene and Render Effects Editor V1, second slice
- **Design baseline:** `8c75b99b` on `main`, maintainer merge of [PR #451](https://github.com/slmao/Atlantis/pull/451)
- **Related Plan:** [Plan 0144](../plans/0144-post-processing-panel.md)
- **Approval:** [Maintainer joint approval of `bf52f24c`](https://github.com/slmao/Atlantis/pull/452#issuecomment-7137943834)
- **Related ADR:** None proposed

## Problem and scope

The reflective Inspector already edits all eight post-processing fields of a
selected Camera. The [0137 locator](0137-bistro-light-exposure-tuning.md) can
select the active camera at Storefront Terrace. Editing today requires finding
that camera in the selection workflow and working among its other fields.
A dedicated view needs a reliable authoring target even when the Hierarchy
selection changes or a reference camera is driving the viewport.

Add **Post Processing** as a mode within the existing Inspector pane. It
shows the current authoring document's active camera and its three groups of
fields. The scope is the existing single-field commit workflow for editable
hand-authored scenes, including guarded Undo/Redo and independent Save. Render
algorithms, presets, camera switching controls, scene-wide overrides, lens and
transform editing, and changes to the capture or workspace persistence surface
are outside this slice.

## Existing fields

Reuse the reflective schema and its field parsing and validation. The schema
and the existing authoring/cook checks remain authoritative.

| Group | Camera fields | Existing semantics |
| --- | --- | --- |
| Exposure | `Camera.exposureCompensationEv` | Finite EV in `[-16, 16]`, with a default of `0 EV`. [ADR-0075](../adr/0075-manual-camera-exposure-data-and-output-transform-contract.md). |
| Fog | `Camera.fog.color`, `density`, `height`, `heightFalloff`, `maxOpacity` | Existing height fog. Color is linear HDR RGB with channels in `[0, 65504]`. Density and falloff are finite and non-negative. Height is finite and max opacity is in `[0, 1]`. Density `0` disables fog. [Spec 0043](0043-height-fog.md). |
| Bloom | `Camera.bloom.strength`, `threshold` | Strength in `[0, 1]`, with `0` disabling Bloom. Threshold is finite, non-negative and in scene-referred HDR units before exposure. [Spec 0044](0044-bloom.md). |

## Requirements

- **R1 — Identify the edited camera.** In the Inspector mode header show the
  authoring active camera's name and GUID, with an unnamed fallback and a way
  to read or copy the full GUID. List the three groups in schema order within
  each group. Obtain the camera through `authoring.activeCamera` and read its
  fields by GUID. Changing Hierarchy selection or entering/leaving either 0131
  view leaves the target bound to the authoring active camera. Resolve on entry,
  on relevant document/camera events and on explicit refresh. The panel shows
  unavailable, loading, or read-only status when its document or camera cannot
  be verified or guarded writes are unavailable. An unconfirmed value remains
  unavailable. [Spec 0131](0131-bistro-reference-views.md) changes only the
  viewport pose and lens. The rendered settings follow the applied scene's
  active camera even while the Editor observes from a fixed view.
- **R2 — Bind drafts and requests.** Use the existing Inspector controls,
  parser and validation with one active draft and an explicit field **Set**
  action (Enter also commits, Escape cancels). Focus loss alone does not
  submit a draft. Follow [0112](0112-unsaved-document-protection.md) for
  leaving, reopening, or closing with a draft. The draft captures session and
  document lifetime, document token, camera GUID and node/component occurrence,
  leaf address, original value and field stamp. Invalidate it on an observed
  revision change, active-camera change, target replacement, document reload
  or disconnect. Keep invalidated text available to copy or discard while
  the panel remains with the current document. A confirmed leave or reopen
  settles the draft under 0112. A switch away and back requires a fresh draft,
  even with the same GUID. Retire stale reads on a newer panel intent or
  document/camera observation. Writes require a freshly verified target.
- **R3 — Commit only to the verified camera.** At read and before send, the
  Worker brackets active-camera/field reads with document tokens, consumes
  existing authoring events and rechecks intent, target occurrence and value.
  Send one `SetProperty` for the captured camera GUID and leaf address with
  the existing `RequireDocumentRevision` guard in the same transaction.
  Reuse [ADR-0162](../adr/0162-revision-guarded-authoring-property-batch.md)
  and the existing property settlement path, including capability checks.
  The host advances its revision for an active-camera change and for an open
  within the same session. If a switch wins before admission, the transaction
  is refused. If the guarded edit wins first, only the captured camera can
  be edited. For a no-op or refused edit, leave history and Apply unchanged.
  Settle an already-sent command for its original camera, while an obsolete
  panel reply cannot overwrite the new camera's display or draft.
- **R4 — Separate document, projection and persistence.** A verified edit
  records one Undo step before the normal correlated Apply. Name the original
  camera and field in the edit/history feedback. Undo/Redo target that camera
  under existing occurrence and conflict checks, including when another
  camera is active. Use the existing [0083](0083-authoring-apply-outcomes.md)
  status and explicit Apply recovery. Apply failure keeps committed edits and
  verified history. An unknown write or Apply outcome requires reconciliation
  before retry. Keep its original camera in the diagnostic. Recovery retries
  projection as appropriate through the existing Apply action. Save persists
  committed document values independently of projection settlement.
- **R5 — Reproducible captures.** Use both existing 0131 views and the same
  0143 daylight scene configuration. For each family, compare initial,
  successfully applied and Undo-restored frames. Keep the active camera,
  launch environment, lighting, geometry, materials and fixed composition
  constant. Qualify frames against the exact committed revision, successful
  Apply and Runtime generation. The existing frame report provides effective
  EV when available, view/projection matrices, frame number and light data.
  Record fog and Bloom through shared authoring readback tied to that settled
  projection. Save the intended combined settings and use the existing
  [0090 Saved Scenes Build/Open](0090-saved-scene-build-open.md) to verify
  values and frames in a fresh process.

## Architectural impact

The panel composes existing Editor models, authoring queries and guarded
transactions. The Inspector schema, scene formats, client interfaces,
Runtime ownership, RenderGraph and 0131 capture surface remain as defined
in their accepted decisions. The pane mode is session-local UI state within
the existing workspace tab. No ADR is required for this scope. An ordinary
selection-based `CommitField(path, text)` call cannot express the panel's
captured camera ownership and is unsuitable for this new entry point.

## Verification

[Plan 0144](../plans/0144-post-processing-panel.md) maps R1–R5 to focused
checks, Windows 100% evidence and the applicable gates.
