# ADR 0185 — Hosted Viewport Presentation Policy

- **Status:** Proposed
- **Date:** 2026-11-22
- **Deciders:** Atlantis maintainers
- **Acceptance:** Pending joint review.
- **Related Spec:** [Spec 0136](../specs/0136-viewport-visibility-picking.md)

## Context

[ADR-0128](0128-remote-viewport-camera-and-raycast.md) gives shared clients
camera and CPU raycast controls. [ADR-0132](0132-viewport-overlay-surface.md)
provides each connection's overlay and requester-only overlay capture.
Rendering and capture already build separate draw lists. The Runtime has
one hosted child surface, and its baked GUID map contains the original
entity handles. The authoring hierarchy stays with Authoring.
Visibility needs a coordinated display and pick policy without changing
those existing ownership boundaries.

## Decision

1. **Use one hosted surface with an exclusive policy holder.** Scope this
   to the existing hosted Runtime child mode with retained pick
   geometry. The first authenticated
   connection that successfully installs a nonempty policy holds it on
   that child. Other clients cannot replace, clear or use its filtered picks.
   Empty restore and disconnect release ownership. Scene generation swap
   also clears it before the next frame. The session uses the existing
   trusted local clients and credential model. The lease arbitrates
   cooperating clients; it grants no new trust or edit permissions.
2. **Publish exact, bounded snapshots.** Add `control.viewportVisibility`
   to obtain availability, scene generation, revision and ownership
   (none, caller or another connection). Return the active sets only to the
   owner so it can reconcile a lost reply. Add `control.setViewportVisibility`
   with expected generation, expected policy revision and one complete
   policy or null to clear. A non-null policy has explicit hidden and
   pick-locked GUID sets and an optional isolation keep set. Use null
   for the completely restored state. Canonical, non-nil unique
   GUIDs within each set, stable ordering and at most 8,192 total list
   entries across the three sets keep requests and replies below the
   existing default line limit. Overlap between sets is legal. No isolation
   and an empty isolation set are distinct; the latter is refused.
   Refuse unavailable mode, ownership conflict, over-limit input, stale
   generation or revision, or invalid member without a partial update.
   Policy revisions are monotonic for the Runtime lifetime, including clear.
3. **Resolve membership against the baked scene.** Editor expands checked
   authoring roots using the current confirmed tree. Runtime only admits
   members whose original baked handles are still live. A destroyed handle
   never matches a later incarnation with the same GUID. Evaluate isolation
   as a keep set and manual hide as independent exclusion. Pick lock adds
   exclusion only for the view's interactive queries. The display and
   opt-in read consume the same resolved policy. Runtime-only entities
   created later are outside an existing frozen isolation set.
4. **Use the existing frame and query paths.** Filter renderables only
   when building draws for the hosted presentation with a visibility
   policy. Preserve the existing ordering and omit those mesh draws from
   both scene and shadow work in that presentation. Keep ordinary extraction
   inputs, light collection, cameras and RenderGraph interfaces. Bypass the
   filter when inactive or when only pick lock is active. Filter entity
   selection outlines against visibility without changing arbitrary
   client primitives. The Editor filters its own node-bound guides and
   markers. `control.pickViewport` explicitly opts in with expected
   generation and policy revision and a closed query kind. It supports one
   ordinary world ray or one/two alignment rays as defined by the existing
   pick contracts. Reuse the native traversal with eligibility applied
   before first-hit and tie tests. Replies identify the sampled generation
   and revision. When there is no owner, a stamped pick can use the
   default view. This settles the transition after Restore All. Refuse
   a caller that does not hold an active policy and refuse a revision
   that has yet to be successfully presented. Use the existing frame-thread
   control boundary and successful present point. An unavailable surface
   that skips presentation does not advance the presented revision.
   Return a typed transient refusal so the next user gesture can try
   after presentation. This adds no background polling or parallel render
   submission.
5. **Keep the existing public behavior.** Add these operations uniformly to
   native Remote, CLI, C# SDK and MCP under `atlantis.remote/1`. An older or
   unhosted Runtime leaves the new Editor controls unavailable. Legacy
   camera, overlay, raycast and alignment commands and all existing capture
   APIs preserve their current meaning. An ordinary capture includes all
   scene meshes regardless of caller or filter. A non-owner still sees
   any pixels it is literally observing on the same child window, and can
   query or capture the unfiltered scene independently. Runtime owns
   presentation and native liveness. Remote owns requester identity and
   disconnect cleanup. Editor owns the temporary user intentions.

## Consequences

### Positive

One policy explains what is shown and picked, including placement and
alignment. The shared scene, save format, history and captures keep their
existing contracts.

### Negative / Trade-offs

There is one policy per hosted child, no takeover and no continuity across
a projection replacement. Changes to Runtime extraction and control dispatch
require performance and GPU verification even with the inactive fast path.

## Alternatives Considered

Per-client render surfaces would isolate pixels independently at the cost of
new output and GPU lifetime contracts. A visibility component in the World
would affect peers and authoring. The Editor cannot reliably implement
pick-through by discarding the single nearest result returned by a legacy
raycast.
