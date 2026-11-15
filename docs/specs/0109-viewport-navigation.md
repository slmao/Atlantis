# Spec 0109 — Viewport Navigation and Selection Framing

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-16
- **Related Plan:** [Plan 0109](../plans/0109-viewport-navigation.md)
- **Approval:** Pending joint human review
- **Related ADR:** [ADR-0165](../adr/0165-shared-viewport-navigation-query.md) (Proposed)

## Summary

Add Focus Selection, six perspective directions, a navigation speed control,
and an explicit Scene Camera mode to the product Editor App. Frame the
single or multiple nodes selected in Hierarchy or Viewport, using their live
Runtime bounds. Reuse the existing camera override and perspective projection.

## Motivation

The product Viewport already picks through the forwarded child-window input,
orbits on a drag, dollies on the wheel and can clear its camera override. The
managed camera model also has pan, fly and a vertical-FOV-only framing helper.
The in-process debug editor already has access to Runtime mesh-local bounds.
The product Editor has no shared read for those bounds, and its scene camera
seed is the first Camera seen during World enumeration. The Runtime chooses
the active camera independently. See [Spec 0062](0062-editor-viewport-navigation-picking.md)
and [ADR-0128](../adr/0128-remote-viewport-camera-and-raycast.md).

## Goals and scope

- Fit one or multiple selected nodes in the current embedded Viewport.
- Provide Front, Back, Left, Right, Top and Bottom, three navigation speeds,
  and a visible return to Scene Camera in the existing Viewport toolbar.
- Keep camera navigation outside authoring, Undo/Redo, Apply and Save.

Orthographic projection, new input routes or fly controls, framing unselected
descendants, animated transitions, persisted bookmarks and renderer changes
are future work. The in-process debug editor keeps its existing behavior.

## Requirements

### R1 — Toolbar and navigation

Follow [Spec 0093](0093-editor-visual-layout.md) for the Viewport toolbar,
including an accessible compact layout. Use the existing click, drag and wheel
routes with their current pick and Gizmo precedence. Expose `Slow` (0.5×),
`Normal` (1×) and `Fast` (2×) for orbit and wheel dolly. Scale the existing
motion inputs after gesture classification. Motion at Normal matches the
existing behavior, and the drag threshold, click picking and Gizmo motion
are independent of the speed. Speed is local to the Editor window, defaults
to Normal on startup and survives a camera-mode change within that window.
Focus is unavailable for an empty selection and navigation commands defer
while a Gizmo owns the gesture. Toolbar use does not commit an Inspector draft.

### R2 — Geometry and frame calculation

Take the selected GUIDs from the current selection, including selected
ancestors and descendants independently. One explicit query resolves up to
64 distinct targets in the same live Runtime scene snapshot. Above 64, the
Editor explains the limit and retains the current pose. Mesh contributions
are world-space AABBs from all eight transformed mesh-local corners using
the live `WorldMatrix`. A node without a Renderable contributes its world
origin. Missing targets (including authored nodes absent from the current
projection), unavailable mesh bounds and unusable transforms reject the
whole frame operation with a diagnostic. Use the currently projected bounds
even while an authored edit awaits Apply. See [ADR-0165](../adr/0165-shared-viewport-navigation-query.md).

Union the returned contributions and use the center as the orbit pivot. Fit
a conservative sphere enclosing the union with a 10% radius margin. Use the
smaller of the vertical and horizontal half-FOV, with the latter derived from
the current physical child-window aspect. Keep the current lens and view
direction. Set the distance at least to the padded radius divided by the sine
of that limiting half-FOV. Clear the near plane and keep the entire padded
sphere inside the far plane. Point-only selections with nonzero span use that
span. A zero-span selection uses the current pivot distance (the existing
default on a fresh seed), adjusted into the usable depth interval. Use
widened arithmetic for bounds, distance and fit; validate the final pose at
the precision sent to Runtime. Nonfinite data, a missing or zero drawable
size, and finite bounds which cannot fit the lens or whose pose cannot be
represented preserve the previous camera and explain the failure. A
coincident point must remain in front of the camera inside the depth interval.

### R3 — Direction views and Scene Camera

Direction views preserve the current orbit pivot, distance and perspective
lens. In Scene Camera mode seed the Editor pivot at the existing default
distance along the actual active camera axis. Front, Back, Right and Left
look toward the pivot from world +Z, −Z, +X and −X with +Y up. Top and Bottom
look from +Y and −Y with −Z and +Z up respectively. At the poles use exact
canonical poses for display, picking and Gizmo math. A nonzero orbit input
leaves the pole from the yaw-zero basis under the existing pitch
clamp; dolly and Focus retain the pole direction. Direction alone changes
orientation; use Focus to fit the object after a view change. Refuse an
unrepresentable direction pose before sending it to Runtime.

Scene Camera clears the override with `setViewportCamera(null)` and reads
the current active scene pose and lens before using them for Editor picking
and Gizmo projection on an upgraded host. Use the Runtime pose directly
for this math, including camera roll. Also obtain the camera-only read on
attach and when a scene replacement is observed in Scene Camera mode, using
the existing connection and refresh paths. If the read fails after an acknowledged
clear, report that the scene camera is active and picking projection is
unavailable until a fresh read (for example by repeating Scene Camera); keep
the old pose out of hit testing. A known
clear refusal preserves the prior mode. A lost reply makes the outcome
unknown and uses the existing disconnect/reconnect path. Camera override
ownership remains shared under ADR-0128, including last writer precedence,
explicit clear and owner disconnect. Separate clients can read bounds
independently of whoever last set the shared override.

### R4 — Settling a camera intent

Identify pending work by Runtime connection, document lifetime, selection
and available observed row identity for Focus, Runtime scene generation
and a camera-intent sequence. A newer Focus, direction, gesture or return
supersedes an older focus result. Invalidate on disconnect, document reopen,
observed scene replacement, stale row identity and selection change. Check
again at worker dispatch before sending the existing camera override; use
the latest nonzero physical viewport size for the calculation. Never publish
the pose as accepted before a successful setter reply. A late callback must
neither replace the latest pose nor display stale status. The bounds sample
and the camera setter are separate operations. A reload or external writer
between them can supersede the view; react to observed replacements with the
existing refresh path and require a fresh user intent to frame again.

### R5 — Isolation and compatibility

Editor, native CLI/SDK, managed SDK and MCP use the same bounded read-only
RuntimeControl surface. Existing clients can continue to use current camera,
pick and overlay operations on a new Runtime. On an old Runtime, the Editor
keeps legacy orbit, dolly, pick and Reset and explains that Focus and the new
direction views need an updated Runtime. Learn this capability once via the
attach read; an unrecognized method does not disconnect it. Successful
navigation leaves the authoring document, revision,
dirty state, selection, Undo/Redo, scene camera components, Runtime scene
generation and Apply receipts untouched. The query runs on navigation intent
or existing lifecycle notifications through normal Runtime control dispatch.
It adds no World verb, render work, resource loads, frame polling or new
retained geometry.

## Proposed Design and Architectural Impact

Use the single additive query in [ADR-0165](../adr/0165-shared-viewport-navigation-query.md)
for an authoritative read of the active scene camera and requested bounds.
Use an empty selection for a camera-only read. Keep navigation fit and input
policy in the Editor model. Retain the existing camera setter, perspective
convention and child-window route. The ADR extends ADR-0128 at the shared
control boundary; module dependencies and camera ownership follow that ADR.

## Alternatives Considered

- The first Camera in World enumeration and a fixed sphere around each
  transform origin are simple to read remotely. These cannot reliably frame
  mesh geometry or identify the active Runtime projection.
- A combined Runtime `focus` operation would tie shared clients to one
  Editor's viewport size, orbit pivot, fit policy and mode. The bounded read
  keeps that policy with the client.

## Testing and Verification

Map contract and fit tests to R1–R5 in
[Plan 0109](../plans/0109-viewport-navigation.md). Verify on Windows at 100%
with the actual embedded Viewport, including a narrow window and a scene
whose active camera differs from the first enumerated camera. Use the
applicable existing Vulkan, golden and performance gates on the implementation.

## Review Questions

The pending joint decisions, including the proposed implementation
authorization, are listed in [Plan 0109](../plans/0109-viewport-navigation.md#joint-review).
