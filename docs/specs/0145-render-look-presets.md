# Spec 0145 — Render Look Presets and A/B Comparison V1

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-26
- **Milestone:** 3D Scene and Render Effects Editor V1, third slice
- **Design baseline:** `c3e92d92` on `main`, maintainer merge of [PR #453](https://github.com/slmao/Atlantis/pull/453)
- **Related Plan:** [Plan 0145](../plans/0145-render-look-presets.md)
- **Related ADR:** [ADR-0191](../adr/0191-render-look-preview-and-adoption.md) (Accepted)
- **Approval:** [Joint review and implementation authorization](https://github.com/slmao/Atlantis/pull/454#issuecomment-7195842449)

## Scope

For one open hand-authored scene, save named looks from the authored values,
compare two saved looks at the same fixed view, and explicitly adopt one with
one Undo step. Use the [0143](0143-lighting-environment-workbench.md) direct
light fields, [0144](0144-post-processing-panel.md) camera fields, and both
[0131](0131-bistro-reference-views.md) views. A/B displays two freshly captured
PNGs and switches between them in the comparison panel. Cross-scene presets,
Volume blending, live viewport override controls, and new render algorithms
are outside V1.

## Requirements

- **R1 — Save Configuration.** Save a named snapshot of a verified authoring
  revision, including committed unsaved edits. Capture all existing direct
  lights and the active Camera. Capture directional local Euler rotation,
  color and intensity, point color, intensity and range, and the Camera's
  eight exposure, Fog and Bloom leaves. The snapshot includes full values,
  including zeros and defaults. Retain point placement and the camera pose,
  lens and active-camera assignment as scene inputs. A supported directional
  light is a leaf node so its rotation has no downstream transform effects.
  Use the existing scene limits of one directional and 64 point lights.
  Refuse an incomplete or invalid read. Saving a preset writes the local
  look store only. Name collisions require explicit replacement.
- **R2 — Storage and targets.** Store up to 16 named looks per scene in
  `%LOCALAPPDATA%/Atlantis/RenderLooks/v1/<scope-hash>.json`. Derive the
  scope hash from the full scene Asset GUID and verified canonical on-disk
  source path. Use current hand-authored session evidence and validate the
  local source identity. The key grants no checkout, source-write or launch
  access. Save As produces a new scope. Record the camera and light node GUIDs,
  component roles, the full light roster and a canonical signature of stable
  placement inputs (parent chains and transforms, direction-light transform
  except its edited rotation, and camera lens). Node names are display labels.
  Changed look values are expected. Missing or replaced targets, a changed
  light roster or kind, a different active camera, or changed placement inputs
  require a new snapshot. Refuse the whole look and identify the failed check.
  Worker observes node and component occurrences throughout an open document.
  On reopening, rebind by GUID and the verified signature. An offline reuse
  of a GUID with an identical signature has the saved target's identity under
  this contract. Never persist transient document or Runtime IDs as identity.
- **R3 — Compare and Cancel.** With an exact settled [0083](0083-authoring-apply-outcomes.md)
  projection, provide two look choices and one of the existing fixed views.
  Revalidate both against the same current document token, Apply receipt,
  Runtime generation and catalog. Render A and B from the same authoring
  snapshot, immutable render inputs and launch environment. Present both
  only after the pair succeeds. Switching displayed PNGs is local. Rendering
  a new pair repeats both captures. Show view and source projection metadata.
  Discard obsolete pairs on input, document, projection or session change.
  Capture reads no unsubmitted draft. Compare, tab switches and Cancel create
  zero document transactions, Apply requests, history records or recovery
  checkpoints. They leave the live viewport and other clients' normal
  captures unaffected. A failed or cancelled pair may finish admitted GPU
  work, then releases only its own scratch and files. Late replies cannot
  repopulate the panel. A partial pair is ineligible.
- **R4 — Explicit Adopt.** Show the addressed targets and current-to-proposed
  differences before confirmation. Re-read target occurrence, expected values
  and document token before sending. The host validates one atomic transaction
  with the new **render-look-specific document guard** and only the closed
  field set in R1. It binds the exact active camera and light roster as part
  of admission. If the document changed after confirmation, refresh and ask
  again. Empty differences settle locally. A verified adoption records one
  history step using the exact old values and addresses, then follows existing
  Apply. Undo/Redo use one guarded transaction each and refuse as a unit if
  expected values, occurrences, active camera or target basis diverge. A
  known Apply failure preserves verified history and enables explicit Apply
  recovery without resubmitting edits. An unknown receipt requires checking
  the original document and targets before any retry. Save remains explicit.
- **R5 — Sharing and compatibility.** Expose bounded capture and narrow
  adoption through the shared Runtime/authoring contracts for Editor, CLI,
  SDK and MCP. Keep names and local preset files within the Editor. Existing
  property/transform batches and ordinary capture retain their contracts.
  An older host can support local snapshot storage when identity and values
  can be verified. Compare and Adopt require their advertised capabilities.

## Architecture

[ADR-0191](../adr/0191-render-look-preview-and-adoption.md) records the
non-mutating shared capture path and closed guarded write shape. This design
reuses the scene cooker, light extraction, RenderGraph, capture readback,
Inspector fields, Apply settlement and Saved Scenes Build/Open. The ordinary
capture path reads live projection resources and can advance a frame via
`step`. The adopted design gives candidate render data and mutable GPU scratch
request-scoped ownership. It preserves the current scene source format.

## Verification

[Plan 0145](../plans/0145-render-look-presets.md) defines the Night / Daylight
workflow, repeated-capture reproducibility and the applicable gates.
