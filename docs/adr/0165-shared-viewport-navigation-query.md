# ADR 0165 — Shared Viewport Navigation Query

- **Status:** Proposed
- **Date:** 2026-11-16
- **Deciders:** Pending joint human review
- **Acceptance:** Pending
- **Related Spec:** [Spec 0109](../specs/0109-viewport-navigation.md)

## Context

The existing camera setter and raycast are shared Runtime controls.
Framing needs Runtime geometry and current world transforms. A camera-only
return also needs the Runtime's active camera, which may differ from the
first entity with a Camera component. The Runtime already retains CPU mesh
bounds for picking when configured to do so and resolves the active camera
before it substitutes any Viewport override. Both are available on the
existing control dispatch thread. [ADR-0123](0123-editor-viewport-camera-and-picking-host-services.md)
and [ADR-0128](0128-remote-viewport-camera-and-raycast.md) define these boundaries.

## Decision

Add `RuntimeControl::viewportNavigation` and additive `atlantis.remote/1`
method `control.viewportNavigation`, with `{ "targets": [EntityGuid, …] }`.
The request has one required `targets` list and accepts zero to 64 unique
non-nil GUIDs. An empty list asks for the camera only. Use existing canonical
entity GUID and camera codecs. Provide the same request/result through the native Remote
client, CLI `runtime camera navigation [GUID ...]`, C# `RemoteControl`
and MCP `control.viewport_navigation` as a read-only exact tool. Tool and
CLI validation follow the same limit and target semantics.

The success payload carries `sceneGeneration`, `sceneCamera` and `bounds`.
`sceneCamera` uses the `ViewportCamera` shape (world matrix and perspective
lens) of Runtime's active scene camera before any override. A success always
has a usable camera under the existing world-up extraction, including finite
derived matrices. `bounds` carries exactly one entry for each requested
target in request order, with `entity` (GUID), `kind` (`mesh` or `origin`)
and finite float `min`/`max` triples in world space. Origin entries have both
endpoints set to that node's world translation.
The camera, generation and all bounds come from one execution of the normal
control dispatch against one live Runtime scene, including while paused.
Resolve each GUID directly in the live World. Distinguish an unknown GUID,
a confirmed absence of the Renderable component, and a present Renderable
with unavailable mesh geometry. Use the current mesh binding and retained
CPU pick bounds. Transform the eight corners by the live `WorldMatrix` with
the existing affine point convention. Accumulate in widened precision and
round endpoints outward at wire precision so the published AABB still covers
the transformed corners. Refuse unordered or unrepresentable bounds. This
is a geometry bound. Draw eligibility, occlusion and the visibility of
individual triangles do not affect the query.

Use the existing method-local `ok`/`err` envelope. For a semantic refusal
use a closed read error with `code` and, for target-specific failures, the
offending `target` GUID. The codes are `InvalidRequest` (bad target set),
`SceneCameraUnavailable`, `TargetUnavailable`, `GeometryUnavailable`, and
`InvalidBounds`. For multiple failing targets report the first in request
order after request and camera validation. A malformed outer shape, extra
field or invalid GUID encoding uses RPC `BadRequest`. Check the list size
before decoding entries, with `InvalidRequest` for an over-limit list.
Within the limit, canonical nil and repeated GUIDs also use `InvalidRequest`.
A valid empty request
is unaffected by pick geometry availability. Replies are all-or-nothing.
Treat a malformed reply, including a target/order mismatch or contradictory
bounds, as a protocol failure. A native control implementation may decline
the additive read with `Unsupported` by default. On the wire, clients also
map the exact legacy RPC error `BadRequest` with message
`unknown method control.viewportNavigation` to `Unsupported` for this
query. Keep other RPC and transport failures distinct and leave the
connection usable on `Unsupported`. CLI refusals exit nonzero. MCP refusals
are tool errors. The protocol version, existing payloads and runtime camera
setter semantics stay as defined in ADR-0128.

Diagnostic decisions, limits and viewport fit stay explicit. Query dispatch
uses the existing Runtime host and introduces no persistent sampling state,
geometry retention, render pass or ownership rules. Navigation pose, aspect,
speed and selection identity are client concerns. The read and setter are
separate operations across a scene reload or another client's camera write.

## Consequences

- Non-Editor clients can obtain the same exact active camera and geometry
  inputs on demand. Returning to the scene camera requires no new setter.
- A Runtime without retained pick geometry can provide its camera and
  transform-only origins. A selected renderable without usable CPU bounds
  causes a diagnostic refusal.
- A world-space AABB of transformed local bounds may be conservative for
  rotated or sparsely filled meshes. The client keeps all contributions in
  the frame under the chosen fit policy.

## Alternatives Considered

- Existing raycasts provide one intersection at a time. They cannot produce
  the required full extents or camera pose.
- An Editor-specific bounds feed would duplicate the shared boundary and
  add ongoing work for a user-triggered operation.
- A server-side fit would take client-specific viewport and pivot state.
  The bounded result lets clients choose how to use the data.
