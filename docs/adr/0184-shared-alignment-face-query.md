# ADR 0184 — Shared Alignment Face Query

- **Status:** Proposed
- **Date:** 2026-11-22
- **Deciders:** Atlantis maintainers
- **Acceptance:** Pending joint review.
- **Related Spec:** [Spec 0135](../specs/0135-viewport-surface-alignment.md)

## Context

The Runtime already owns retained CPU triangle geometry and the live
transforms used by [ordinary raycast](0128-remote-viewport-camera-and-raycast.md).
That reply omits the actual face and `viewportNavigation` reports
conservative bounds. Precise alignment needs bounded, repeatable evidence
for two faces in one live scene sample. The client owns the gesture and
the authoring document has an existing guarded transform transaction.

## Decision

1. **Add one shared read.** Add `RuntimeControl::pickAlignmentFaces` and
   `control.pickAlignmentFaces` on `atlantis.remote/1`. The request is
   exactly `{ "rays": [Ray, …] }` with one or two existing finite
   world-space unit rays, in requested order. Validate count before
   detailed decoding. An `ok` contains `sceneGeneration` and `faces`,
   exactly one face per input ray. Each face contains the canonical
   non-nil `entity` and `mesh` GUIDs, two ascending `triangles`
   (zero-based cooked triangle ordinals), `distance` along the query
   ray, four world-space `corners` in cyclic order following winding,
   the outward unit `normal`, and the associated affine `worldMatrix`
   in the existing column-major convention. Rotate the cyclic corner
   order so its first corner is the lexicographically least checked
   local point after signed-zero normalization. `triangles` identify
   this cap only with its mesh and current scene generation. All values
   are owned. There are no borrowed handles or serialized geometry types.
2. **Prove only the first eligible cap.** Resolve rays during one dispatch
   on the existing Runtime control thread against one live World and
   generation, including when paused. Reuse ordinary retained pick
   geometry and its existing draw-eligibility exclusions. Extend the
   pick internals only as needed to identify the first geometric hit
   plus near ties, then apply the strict limits and eligibility in
   Spec 0135 R2. Keep the resolved mesh GUID with the existing retained
   geometry from scene load/reload, using the already verified catalog
   record. If identity or effective opaque material is unproven,
   refuse. No new geometry retention, adjacency index, frame polling,
   asset loads at query time or changes to ordinary raycast behavior.
   One dispatch processes at most two rays and validates at most
   65,536 triangles of the hit mesh per ray in addition to the
   existing scene traversal. Keep a constant-size response.
3. **Preserve errors and legacy clients.** Use the existing method-local
   `ok`/`err` envelope. A complete successful reply represents all rays.
   Refuse the whole request on failure. Use `err.code` with `InvalidRequest`
   for an invalid input count or value and `NoGeometry` when retention
   is unavailable. Use `Miss`, `UnsupportedFace`, `Ambiguous` or
   `TooLarge` with `err.rayIndex` for the first failing ray in request
   order. Malformed types or extra request fields use outer `BadRequest`.
   Clients validate closed shapes, finite values, count, geometry,
   ascending ordinals, and unit normals. A contradictory reply is a
   protocol failure. The native interface defaults to `Unsupported`.
   Map the exact older `unknown method control.pickAlignmentFaces`
   refusal to `Unsupported` without invalidating the connection. Keep
   other RPC and transport failures distinct. Expose the same typed
   query via native Remote/the gameplay client, CLI
   `runtime alignment faces --origin X Y Z --direction X Y Z` with an
   optional second origin/direction pair, C# `RemoteControl`, MCP
   `control.pick_alignment_faces` and Editor App. The CLI and MCP both
   require one or two complete rays with the same semantics. Existing peers
   continue to use ordinary raycast and Gizmos unchanged.
4. **Keep read and edit ownership distinct.** Editor computes the
   translation and represents it through existing per-client overlays.
   Authoring still admits the existing guarded position write, with
   ordinary history and independent Apply. Re-observe both rays before
   the send. The document guard makes concurrent authoring admission
   atomic. It does not lock Runtime-only writers after the geometry
   read. No new mutation, persisted face identity, renderer behavior or
   editor-only channel is needed.

## Consequences

The contract supports the same direct geometry read for all clients
and leaves ordinary selection independent of the feature. The Editor
must establish projection evidence before offering alignment. Its
rectangular criterion rejects complex triangulations and over-limit
meshes. Preview remains an outline until Apply; it does not imply
collision checking or pixel-equivalent seams. Runtime source changes
remain subject to existing verification and performance applicability.

## Alternatives considered

Adding optional face fields to ordinary raycast would mix compatibility
and the stricter pick semantics with a hot existing input path. An
Editor-side mesh read cannot establish the live Runtime front hit. A new
server-side alignment mutation would couple World projection to authoring
admission and outgrow the narrow one-position edit.
