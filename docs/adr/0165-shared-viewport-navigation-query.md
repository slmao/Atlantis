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
The list is required and accepts zero to 64 unique non-nil GUIDs; an empty
list asks for the camera only. Use existing canonical entity GUID and
camera codecs. Provide the same request/result through the native Remote
client, CLI `runtime camera navigation [GUID ...]`, C# `RemoteControl`
and MCP `control.viewport_navigation` as a read-only exact tool. Tool and
CLI validation follow the same limit and target semantics.

The success payload carries `sceneGeneration`, `sceneCamera` and `bounds`.
`sceneCamera` uses the `ViewportCamera` shape (world matrix and perspective
lens) of Runtime's active scene camera before any override. A success always
has a usable camera. `bounds` carries exactly one entry for each requested
target in request order, with its GUID, `kind` (`mesh` or `origin`) and
`min`/`max` triples in world space. Origin entries have equal endpoints.
The camera, generation and all bounds come from one execution of the normal
control dispatch against one live Runtime scene, including while paused.
The Runtime resolves mesh data using its current mesh binding and retained
pick geometry and reads render-authoritative `WorldMatrix` values. A
canonical scene asset ID or asset catalog lookup cannot substitute for
the live entity and its mesh binding.

Use the existing method-local `ok`/`err` envelope. For a semantic refusal
use a closed read error with `code` and, for target-specific failures, the
offending `target` GUID. The codes are `InvalidRequest` (bad target set),
`SceneCameraUnavailable`, `TargetUnavailable`, `GeometryUnavailable`, and
`InvalidBounds`. For multiple failing targets report the first in request
order after request and camera validation. A malformed outer JSON shape
uses the normal RPC `BadRequest`. A valid empty request is unaffected by
pick geometry availability. Replies are all-or-nothing. Reject nonfinite
or unrepresentable coordinates and matrices before writing the reply.
SDKs treat malformed replies as protocol failures. Map only the old
host's exact unknown-method reply to `Unsupported` for this query, using
the existing additive-method compatibility pattern. Keep transport and
other RPC errors distinct. The protocol version, existing payloads and
runtime camera setter semantics stay as defined in ADR-0128.

Diagnostic decisions, limits and viewport fit stay explicit. Query dispatch
uses the existing Runtime host and introduces no persistent sampling state,
geometry retention, render pass or ownership rules. Navigation pose, aspect,
speed and selection identity are client concerns. The query does not claim
atomicity with the separate camera setter across a scene reload or another
client's camera write.

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
