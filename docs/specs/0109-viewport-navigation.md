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
Focus is unavailable for an empty selection. Disable discrete navigation
actions during a Gizmo gesture and require a new invocation after release.
Toolbar use does not commit an Inspector draft.

### R2 — Geometry and frame calculation

Take the selected GUIDs from the current selection, including selected
ancestors and descendants independently. One explicit query resolves up to
64 distinct targets in the same live Runtime scene snapshot. Above 64, the
Editor explains the limit and retains the current pose. Only live nodes
without a Renderable contribute their own `WorldMatrix` translation. Missing
targets (including authored nodes absent from the current projection), unavailable
mesh bounds and unusable transforms reject the whole operation with a
diagnostic. Use the sampled Runtime bounds while an authored edit awaits
Apply. The query is defined in [ADR-0165](../adr/0165-shared-viewport-navigation-query.md).

Union the returned contributions and use the center as the orbit pivot. Fit
a conservative sphere enclosing the union with a 10% radius margin. Use the
smaller of the vertical and horizontal half-FOV, with the latter derived from
the current physical child-window aspect. Keep the current lens and view
direction. Set the distance at least to the padded radius divided by the sine
of that limiting half-FOV. Clear the near plane and keep the entire padded
sphere inside the far plane. Point-only selections with nonzero span use that
span. A zero-span selection uses the current pivot distance (the existing
default on a fresh seed), adjusted into the usable depth interval. Use
widened arithmetic for bounds, distance and fit. Validate the quantized
pose and actual-aspect projection using the existing Runtime-matched camera
convention, including finite derived matrices and positive depth, frustum
and clip checks for the bounds. A finite eye can still make `eye + forward`
lose its direction at float precision. Nonfinite data, a missing or zero
drawable size and any fit the existing projection cannot represent preserve
the previous camera and explain the failure. A coincident point must remain
in front of the camera inside the depth interval.

### R3 — Direction views and Scene Camera

Direction views preserve the current orbit pivot, distance and perspective
lens. For the first navigation from Scene Camera, use a fresh active-camera
sample for a discrete direction or Focus, and the latest valid sample for
continuous orbit and dolly. Seed the Editor pivot at the existing default
distance along the active camera axis. Front, Back, Right and Left look
toward the pivot from world +Z, −Z, +X and −X. Top and Bottom are the
near-vertical perspective presets at yaw zero and forward pitch −89° and
+89° respectively. The shared projection derives its screen basis with
world +Y and rejects exact vertical forward. All six views use the same
projected basis for display, picking and Gizmos. Orbit continues from the
preset angle. Dolly and Focus retain the preset direction. Direction alone
changes orientation. Use Focus to fit the object after a view change.
Refuse an unrepresentable direction pose before sending it to Runtime.

Scene Camera clears the override with `setViewportCamera(null)`. After the
clear reply, make a separate camera-only read and use the returned active
pose and lens with `CameraConvention` for picking and Gizmos on an upgraded
host. Use the same world-up extraction as Runtime, without introducing camera
roll. Also read on attach and on an observed scene replacement in Scene
Camera mode via the existing connection and refresh paths. A known clear
refusal preserves the prior mode. If clear was acknowledged and its read
fails, report that the override was cleared and projection is unavailable
until a fresh read, for example by repeating Scene Camera. Keep the old
pose out of hit testing. A lost setter reply is an unknown outcome and
uses existing disconnect/reconnect handling.

Camera override ownership remains shared under ADR-0128, including last
writer precedence, an explicit clear of the shared override and cleanup
on owner disconnect. The scene-camera read samples the underlying scene
camera. Live camera animation or another client setting a later override
can change the displayed view after the sample. This slice covers alignment
on the Editor's hosted view with a stable sampled scene camera and its own
settled overrides. It adds no continuous tracking or camera arbitration.

### R4 — Settling a camera intent

Serialize camera intents on the existing worker. Tie Focus to the connection,
document lifetime, selection, observed row identity and sampled Runtime
generation. Tie directions and return to the camera intent and connection.
A newer navigation intent supersedes older unsent writes, including a
coalesced orbit or dolly. Before sending a discrete set, compare the sample
against the latest observed Runtime generation and recheck the relevant UI
and worker guards. Fit with the latest valid physical viewport size and
discard a fit if that size is already obsolete before dispatch. Disconnect,
document reopen, observed scene replacement and superseding gestures discard
stale focus work. Selection change and row replacement also discard work for
the old focus target. A late callback cannot overwrite the newer pose or
status. An acknowledged write already sent may have taken effect. A stale
callback does not trigger a compensating write.

The setter reply acknowledges processing of the control operation. For a
discrete set, use one existing status read after the setter reply to settle
the next frame-thread service boundary and check its scene generation.
For return, the separate camera-only read supplies that boundary. While
these operations settle, suspend new pick and Gizmo hit tests and drop any
outstanding pick from the old projection. Resume against the settled pose
and current physical extent. Retain the latest confirmed write or clear as
the base for subsequent intents even if its UI completion was superseded.
A later refusal before sending leaves that base in place. If confirmation
fails after sending, or a new generation invalidates an in-flight set,
hold projection-dependent tools until a fresh navigation or Scene Camera
settles. The sample and setter remain separate operations. The service read
applies to discrete actions. Observe presentation in the actual window test.

### R5 — Isolation and compatibility

Editor, native CLI/SDK, managed SDK and MCP use the same bounded read-only
RuntimeControl surface. Existing clients can continue to use current camera,
pick and overlay operations on a new Runtime. On an old Runtime, the Editor
keeps legacy orbit, dolly, pick and Reset, with local speed adjustment. Focus
and the direction views explain that they need an updated Runtime. Learn
this capability via the attach read. The session stays usable on an
unrecognized method. Successful navigation leaves the authoring document,
revision, dirty state, selection, Undo/Redo, scene camera components, Runtime scene
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
