# Spec 0137 — Bistro Storefront Lighting and Exposure Tuning

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-22
- **Related Plan(s):** [Plan 0137](../plans/0137-bistro-light-exposure-tuning.md)
- **Approval:** Pending joint review
- **Related ADR(s):** None. See Architectural Impact.
- **Design baseline:** `40ce4ad3`, maintainer merge of [PR #436](https://github.com/slmao/Atlantis/pull/436)

## Summary

On an editable Bistro scene, hold the 0131 Storefront Terrace view while
tuning point light intensity and active scene camera exposure in separate
Inspector edits. Compare two Atlantis captures tied to settled projections
and verify that Undo restores the original parameters and their rendered effect.

## Motivation / Problem Statement

The [0131 reference view](0131-bistro-reference-views.md) fixes the viewport
pose, lens and physical PNG extent. The Runtime uses that pose and lens while
retaining the active scene camera's exposure. The existing report returns
the frame matrices, effective point lights with world positions, and actual
exposure EV when available. It identifies point lights by rendered values
rather than authoring node GUID.

The reflective Inspector already exposes `Light.intensity` and
`Camera.exposureCompensationEv`. The
[0106 batch Inspector](0106-scene-multiselect-batch-edit.md) gives mixed
values and one guarded absolute assignment across the selected nodes.
[0083](0083-authoring-apply-outcomes.md) distinguishes an authoring commit
from the Runtime's later projection result. The numeric editing path needs
no dedicated tuning controls.

The current comparison window replaces its latest Atlantis capture and
can show a previous Runtime projection after a failed Apply. There is no
paired view restricted to settled revisions and no Editor action to locate
the document's active camera from the reference view. Those two gaps make
it easy to edit the wrong camera or compare an updated document against a
frame from its previous projection.

## Goals

- Tune a selected group of point lights and the active scene camera from the
  existing Inspector at one fixed Storefront Terrace viewpoint.
- Show two attributable Atlantis frames from distinct, settled revisions,
  with enough metadata to identify what was rendered and to repeat the check
  after Undo.

## Non-Goals

- Additional light types, shadow or post-processing algorithms, new render
  passes, exposure controls outside the Inspector, or separate light group data.
- Tuning from Street Corner or a freely navigated camera, persistent image
  galleries, reference calibration, or numerical similarity to ORCA.

## Requirements

### Functional

- **R1 — Parameter ownership and editing.** Use the editable, opted-in
  Bistro authoring document in the hosted Editor. In Hierarchy select the
  intended point-light nodes, identified by their existing Light kind, and
  use the common `Light.intensity` field to Set one absolute value. The
  existing guarded transaction and Undo restore each original value,
  including initially mixed values. Exposure is the existing
  `Camera.exposureCompensationEv` float in EV on the document's active scene
  camera, edited singly with its current authoring validation. Provide a
  **Locate active scene camera** action for this workflow using the existing
  authoring query and Hierarchy/Inspector selection. Honor existing draft and
  gesture protection. An absent, hidden, replaced or unverifiable target,
  or a reply invalidated by changed selection, document or a newer intent,
  leaves the current selection and draft intact and explains how to proceed.
  Camera pose and lens come from the 0131 preset. The locator changes
  selection only and never changes which camera is active.
- **R2 — Fixed view and paired capture.** Add explicit **Capture Before**
  and **Capture After** actions for Storefront Terrace, with two temporary
  comparison slots. Keep the existing fixed pose, lens and 960 × 540 physical
  drawable during ordinary successful Apply and Undo within the same open
  document. Successful Apply must revalidate the new generation before capture
  becomes ready. Recheck the acknowledged viewport camera and verify the
  frame. If readiness or identity cannot be confirmed, ask the user to
  reselect the fixed view. Ordinary navigation, returning to the scene
  camera, losing the required extent, or an incompatible 0136 visibility
  policy ends the active comparison. Document reopen and disconnect also
  retire the pair. Late replies cannot repopulate slots for a retired pair
  or overwrite a newer capture. Replacing Before clears its old After.
- **R3 — Evidence and error labels.** A comparison slot requires a readable
  current document token, an 0083 successful projection for its exact
  revision and the current Runtime generation, and the existing 0131 frame
  and extent checks. Refuse during an active Runtime preview or unverified
  viewport policy. Bracket capture with status and document reads, including
  the active camera and projection evidence. Match
  After to the same open document, Runtime, active-camera GUID and view as
  Before and require a later applied revision. Display both frames with
  their document revision, Apply ID, Runtime generation and frame number,
  reported EV and light values. Keep the 0131 attribution and unknown
  conditions for the optional ORCA reference. A missing EV is unavailable,
  never zero by inference, and is insufficient for exposure acceptance.
- **R4 — Separate commit, projection and recovery.** Use the existing
  Inspector and Apply status to distinguish draft input, committed changes,
  and the Runtime result. Save status remains independent. A failed, pending,
  superseded, mismatched or unknown Apply cannot qualify a new comparison
  frame while the current projection is unverified. Retain the last valid
  captures with their original metadata and give the existing recovery
  guidance, including waiting, checking status or explicitly applying as
  applicable. An existing 0131 ordinary capture of a prior projection retains
  its own label and is separate from the pair. Locating and comparing neither
  submit edits nor initiate Apply or Save. Recovery does not replay an edit
  or add an Undo entry.

### Non-functional

The two comparison captures are held only in the Editor session under the
existing per-image bound and disposal rules. Reuse the existing on-demand
capture and Worker flow. No additions to Runtime frame work, shared client
surface, or dependencies. Use the existing Editor visual conventions from
[0093](0093-editor-visual-layout.md).

## Proposed Design

Place the two capture actions and the locator with the existing Bistro
comparison workflow. Keep the current Inspector visible in the main Editor.
Retain the two Atlantis slots side by side. Keep the attributed ORCA image
separately viewable through the existing 0131 reference handling. Use the
[Amazon Lumberyard Bistro ORCA gallery](https://developer.nvidia.com/orca/amazon-lumberyard-bistro)
only for visual context.

The current hosted camera override already changes pose and lens while
leaving exposure to the active scene camera on each frame. Reuse its
settlement and matrix check across the authoring projection change. For
the comparison, the existing frame report describes the capture itself and
the paired admission identifies its authoring projection. The latter proves
projection origin. It cannot establish atomic isolation against arbitrary
Runtime-only gameplay mutations. The acceptance fixture is quiescent, with
no independent edits during the before/after runs. For the test, correlate
the selected authoring light GUIDs to uniquely identifiable frame-report
lights by stable world position, color and range, and check both appear in
the effective report. Do not infer a GUID from report order.

## Architectural Impact

None. The proposal composes unchanged 0131 and
[ADR-0180](../adr/0180-shared-frame-report-exposure.md) capture contracts with
the existing shared authoring query, property editing, history and 0083
Apply settlement. The work is Editor-side presentation and admission.
A shared shape change would require a separate reviewed decision.

## Alternatives Considered

- Manually export and label two 0131 PNGs. This leaves the mapping between
  each frame and its authoring revision to the operator, especially after
  a failed Apply.
- Add independent tuning sliders or a transient lighting override. This
  duplicates editing semantics, makes Undo and persistence harder to read,
  and adds a new source of rendered values.

## Testing & Verification Plan

Review the pairing and locator guards with focused Editor tests. Use the
actual hosted Editor on Windows at 100% with fatal Vulkan validation and a
disposable editable Bistro scene. Capture a baseline, a light-only result,
an exposure-only result and the restoration after each Undo. Check exact
authoring values, the projected results, three fixed landmarks, two
categories of image changes, and the history and Apply states, as specified
in the [Plan verification checklist](../plans/0137-bistro-light-exposure-tuning.md#verification-checklist).

## Risks & Open Questions

Only effective point lights appear in the report. Select two distinguishable,
visible lights with measurable contribution and check their inclusion before
using a comparison for acceptance. The paired capture can fail safely on a
current host without complete projection evidence. Review the scene fixture,
locator and strict capture boundaries in Plan J1–J3.

## Out of Scope / Future Work

Multiple sites or presets for tuning, synthesized light groups, shared
per-light capture provenance, arbitrary automated image matching and exports
beyond the existing capture workflow.
