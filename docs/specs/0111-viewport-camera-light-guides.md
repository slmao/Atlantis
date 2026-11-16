# Spec 0111 — Viewport Camera and Light Guides

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-17
- **Related Plan:** [Plan 0111](../plans/0111-viewport-camera-light-guides.md)
- **Approval:** Maintainer approved revision `a8222bf0` on 2026-11-17. See Plan 0111.
- **Related ADRs:** No new ADR. See Architectural Impact.

## Summary

Add a **Guides** toggle to the Editor Viewport. It draws the perspective
frustum, point-light range or directional-light direction of a single selected
node so the author can judge placement and edit the existing fields in the
Inspector. Guides reflect the committed authoring document. The scene image
continues to reflect the Runtime, with the existing Apply feedback.

## Motivation

[0110](0110-common-scene-objects.md) makes these objects easy to create.
Today the product Editor draws origin markers for lights and cameras without a
Renderable, mesh selection outlines and transform Gizmos. The markers locate
nodes but convey no lens, range or light direction. The existing [0066
overlay](0066-editor-overlay-and-input.md) already draws world and
screen-constant segments through RenderGraph, while [0109
navigation](0109-viewport-navigation.md) supplies the current drawable extent
and settled projection for picking.

## Requirements

### R1 — Display and selection

- **Guides** is a single Viewport toolbar toggle using [0093's
  existing controls and states](0093-editor-visual-layout.md). Default Off
  in each Editor window. Keep the choice while working in that window,
  including a document reopen or reattachment. Opening another Editor starts
  Off. Respect the Viewport toolbar's Inspector focus and draft behavior.
- When enabled, show guides for exactly one live selected node with a
  supported component, regardless of whether it also has a Renderable.
  Resolve the node from the existing Hierarchy/Viewport selection. A node
  with both a supported Camera and Light can show both. Zero selection,
  multiselection and other nodes show the ordinary overlay. A usable
  authoring document and Viewport are required.
- Use thin depth-tested wire segments with the Editor's selection accent,
  distinct from the on-top transform axes. A guide may be occluded or
  outside the view, including when looking through the camera it represents.
  Selection keeps the existing Hierarchy/Inspector feedback and, when
  applicable, the existing mesh outline. The toggle leaves the old origin
  markers, mesh outline and Gizmos available.

### R2 — Shape and numeric rules

Resolve the selected node's authoring world transform through its
ancestors. For a camera or directional light, normalize the negative
third basis column to match Runtime forward. A point light needs only
world translation and its authored radius. Resolve light kind from the
shared authoring schema.

| Component | Guide |
| --- | --- |
| Perspective Camera | Twelve world-space edges joining the near and far rectangles from the selected Camera's lens. Require finite `0 < fovY < π` and `0 < nearZ < farZ`, with FOV in radians. At distance `d`, the center is eye plus `d × forward`, half-height is `d × tan(fovY/2)` and half-width is half-height times the hosted drawable's pixel aspect. Build right/up from forward and world +Y as the Runtime does. Roll about forward leaves this basis unchanged. The current Viewport camera projects those lines into the image. |
| Point Light | Three orthogonal world-space great circles, 24 segments each. The center is world translation and the radius is the positive authored range in world units, independent of ancestor or local scale. |
| Directional Light | A short screen-constant arrow anchored at world translation and pointing along the normalized emission direction. Use one segment of one anchor unit with `pixelsPerUnit = 40`. Use four head segments around the shaft, with a perpendicular basis that works for vertical emission. The shaft can foreshorten in the view. Light range and shadow volume are outside this guide. |

Cap the addition at two groups and 84 segments. Omit an individual
unavailable guide if a required read fails, lens ordering/FOV or range is
invalid for display, or values cannot form usable Float32 geometry. Follow
the existing Runtime forward and camera basis degeneracy threshold of
`1e-6`. Reject nonfinite transforms, projection values or segment
coordinates, and endpoints that collapse at Float32 precision. Omit the
affected guide as a whole. This is a display check on otherwise editable
scene data. A zero-sized drawable suppresses guides until its positive
extent is available. Report unavailable data concisely in the existing
Viewport status area when an otherwise
eligible selection cannot be fully represented.

### R3 — Picking and shared overlay

- Guide strokes have no hit targets. Keep the existing order of group or
  single Gizmo handling, camera/light origin marker pick, and Runtime mesh
  raycast.
  A camera or light with a Renderable is reached through its mesh or the
  Hierarchy. A guide edge delegates to ordinary picking, including the
  existing miss behavior. Keep the current projection-readiness and
  late-pick guards from 0109.
- Compose guides into the Editor connection's existing
  `control.setViewportOverlay` value. Preserve the baseline groups,
  selection and current client isolation. Other clients keep their own
  overlays and may submit equivalent segments using the same existing
  control call through CLI, SDK or MCP. Captures keep ADR-0132 semantics:
  normal captures omit overlays, and an explicit overlay capture includes
  only the requesting client's overlay.
- The call replaces the whole client overlay. On a definite guide overlay
  refusal, try the ordinary overlay once so an older guide is not retained;
  use the existing clear fallback if that also refuses. Those fallbacks
  apply only to the current Editor connection and intent. Show a concise
  diagnostic for the guide failure. A lost reply has an unknown overlay
  result until the connection settles. Follow existing session cleanup
  and feedback.

### R4 — Edits, Apply and lifecycle

- A guide represents the latest verified **committed authoring values**.
  Read these values from the document, including after an uncertain edit.
  A draft or write reply alone cannot establish guide values.
  Suppress the guide from local Gizmo preview start through commit or
  cancellation. Restore it from verified authoring values when the
  preview ends. Undo and Redo use the same reads as other edits, including
  while projection is suspended.
- Display the source as **Authoring guides** when enabled. The existing
  Apply state describes the image separately, including pending/paused,
  failed, superseded, unknown and explicit recovery. After a failed Apply,
  the guide can show the committed edit over the last good Runtime image.
  Retry applies the latest committed document through 0083. A guide
  snapshot does not certify that any Runtime image matches it. A live-only
  Runtime edit changes the image without becoming authored guide data.
- Read only the selected node and its ancestor chain, up to 64 nodes
  including the selected node. A longer or broken chain is unavailable
  for guides and leaves ordinary editing and overlays available. Use
  complete typed reads for required component fields and all Transform
  fields in the chain. Missing fields must not acquire identity Transform
  or other display defaults.
- Verify the read against drained document events and a stable observed
  document revision. Capture session, document lifetime, row incarnation,
  and the selection, toggle, preview and drawable extent intents. Recheck
  them before the overlay send and again before publishing its feedback.
  Treat an intent that changes away and back as newer. Coalesce refreshes
  in the existing worker when inputs change. If verification fails, use
  the ordinary overlay. A read already sent can finish, but its stale
  result cannot submit a guide. If an overlay send already reached Runtime,
  the latest intent replaces that connection's overlay at the next
  available worker update and frame boundary. These are client observations
  with refresh on later edits. Use the existing event cadence.
- The guides and the toggle create no authoring or Runtime World
  transactions, history entries or Apply requests. Save writes only
  ordinary scene data. A reopened source can regenerate guides from its
  saved fields once selected, with the window's current toggle state.

## Architectural Impact

**None.** The Editor composes existing authoring values into the closed
segment primitives of [ADR-0132](../adr/0132-viewport-overlay-surface.md).
The existing shared query, picking, viewport camera, RenderGraph overlay
and correlated Apply contracts remain sufficient
([ADR-0128](../adr/0128-remote-viewport-camera-and-raycast.md),
[ADR-0144](../adr/0144-correlated-authoring-apply-outcomes.md) and
[ADR-0165](../adr/0165-shared-viewport-navigation-query.md)). A new ADR is
unnecessary for this Editor-side composition.

## Alternatives considered and scope

Guides for every light and camera add clutter and scale with the scene.
Reading live World state would tie the guide to the last projected scene
and hide the effect of a committed edit while Apply is paused or failed.
Picking the lines would introduce new hit priorities and interfere with
scene and Gizmo clicks. Bulk guides, drag handles on guide geometry,
camera switching, new light/camera types, cones, orthographic projection,
shadow volume and rendering changes are outside the first release.

## Verification

[Plan 0111](../plans/0111-viewport-camera-light-guides.md) maps the geometry,
read/selection guards, shared overlay and recovery requirements to focused
checks and a Windows 100% desktop run. Applicable Vulkan, capture, golden
and performance gates follow the existing repository policy.
