# Spec 0136 — Viewport Visibility and Picking Controls

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-22
- **Related Plan:** [Plan 0136](../plans/0136-viewport-visibility-picking.md)
- **Approval:** Pending joint review.
- **Related ADR:** [ADR-0185](../adr/0185-hosted-viewport-presentation-policy.md) (Proposed)

## Summary

Let the Editor isolate the current selection, temporarily hide a node or
subtree, and leave an object visible while locking its viewport pick. The
controls apply to the embedded view and make a cluttered storefront easier to
edit without changing the scene.

## Motivation / Problem Statement

The Editor has single and multiple selection, projected camera and marker
picks, and a shared CPU raycast. A foreground awning can still intercept a
click intended for a chair, and the ground can win a click meant for an
object behind it. Hierarchy search changes the tree's displayed rows.
Viewport overlays add selection and guides. Neither changes which meshes
appear in the embedded view or participate in its geometry picks.
[Spec 0128](0128-viewport-mesh-placement.md) and
[Spec 0135](0135-viewport-surface-alignment.md) also interpret surface hits
against the displayed view.

## Goals

- Make visibility and viewport pickability independently controllable.
- Keep temporary intentions clear in the Editor and reversible in one action.
- Keep authored and ordinary Runtime outputs independent of the view filter.

## Non-Goals

- Scene visibility or layer components, saved presets, or cross-session state.
- Changing light emission, the active scene camera, camera navigation, or simulation.
- Hidden-object picking, renderer object IDs, a new render pass, or new viewport surfaces.

## Requirements

### R1 — Actions and composition

- **Hide Node** hides that row's own mesh and Editor aids. **Hide Subtree**
  also includes its descendants. Both are additive. **Lock Viewport Pick**
  applies to the explicitly selected nodes only. It leaves their meshes
  visible whenever the other visibility rules permit. The Hierarchy remains
  navigable and editable. Show separate hidden and pick-locked indicators on
  affected rows.
- **Isolate Selection** freezes the selected roots and their descendants as
  the only visible members. Deduplicate overlapping roots. Manual hides
  still win and manual pick locks still apply. Selection changes after entry
  leave the isolated set fixed. An empty selection refuses isolation.
- **Exit Isolation** removes only the isolation set. **Restore All** clears
  manual hides, pick locks and isolation through one action. Restore is
  available with an empty selection. Show whether isolation is active and
  provide a clear indication of refusal and expiry.
- Keep ordinary Hierarchy selection and Inspector edits available. A hidden
  or pick-locked node can be selected deliberately in the Hierarchy. Omit its
  viewport marker from hit testing and disable the viewport Gizmo for a
  selection containing an ineligible member. Continue normal Inspector and
  Hierarchy edits through their own existing rules.

### R2 — Display, pick and output boundaries

- Only the owning client controls the presentation of its single hosted
  Runtime viewport. A hidden member contributes no mesh draw or mesh shadow
  to that presentation and no Editor marker, outline or guide for that member.
  For a visible pick-locked member, selection rays continue past its geometry,
  and neither a screen marker nor a Gizmo hit may select or manipulate it.
  Existing light contributions and camera state continue.
- Editor operations that depend on the displayed surface share the same
  eligible set. This includes ordinary selection, Assets mesh placement
  and the nearest surface and tie checks for precise alignment. A skipped
  surface can expose an eligible farther hit. The ordinary nearest-hit and
  ambiguity rules still apply within the eligible set.
- Ordinary shared queries, including unfiltered raycast and alignment,
  keep their existing results for every client. Regular Runtime frames,
  benchmarks, headless output and existing captures use the complete scene.
  Preserve existing camera and requester-overlay behavior for captures.
  The fixed Bistro reference comparison requires clearing the viewport
  policy first so its live view and reference capture continue to agree.
- Use only temporary presentation state. There are zero authoring or
  World writes, history entries and Apply requests from these actions.
  Saving excludes the policy and reopening starts with the default view.

### R3 — Scope, expiry and concurrency

- Root and subtree expansion uses the existing Editor tree for the current
  confirmed authoring projection, independent of the Hierarchy search. Reject
  a stale row, unconfirmed projection or incomplete membership atomically.
  Isolation, hides and locks are resolved against one projection generation.
  UI operations use an atomic full replacement with an expected generation
  and policy revision.
- Clear the policy on successful Runtime scene-generation swap, document
  replacement/reopen, session close or disconnect. Show that it expired
  after a successful Apply that replaced the projection. When Editor replaces
  its document ahead of projection replacement, settle the clear before
  resuming viewport picks. A failed Apply leaves the same live scene and
  policy. If the authoring tree has diverged, block new root-based actions
  until the projection is confirmed again and keep Restore All available.
  A save with the same projection keeps the policy.
- Use the baked Runtime entity handle with its incarnation. A new object
  using the same GUID inherits neither hide nor lock and does not enter a
  frozen isolation set. Refuse a request that names a deleted or replaced
  original object. Cancel obsolete viewport picks and surface gestures when
  the policy or projection changes. Retain the usual stale camera, extent,
  selection, draft and send checks. An older reply cannot restore state or
  select a now-ineligible object.
- Apply display and pick filtering to the same admitted policy revision.
  Guard viewport picks until the window has presented that revision.
  Failed policy replacements preserve the previous policy. A lost receipt
  prompts a bounded status reconciliation or an explicit restore after
  the state is known. No automatic write retry and no unfiltered fallback
  while a filter may be active.

## Proposed Design

Add four actions to the Hierarchy context and a compact Viewport control
for current state, Exit Isolation and Restore All. Keep the manual hide and
lock sets independent of the optional isolation membership in the Editor
model. Publish exact per-generation membership over the shared control
contract and use the returned revision for view-relative picks. Runtime owns
frame admission and entity incarnation checks. All drawing still follows the
existing RenderGraph and the picking path reuses retained CPU geometry.
[ADR-0185](../adr/0185-hosted-viewport-presentation-policy.md) defines the
lease, capability and rendering boundary.

### Non-functional

- Use bounded requests and existing frame dispatch. No new per-frame scan
  or per-entity test in the inactive path.
- Use the existing Windows Editor appearance, keyboard focus and compact
  layouts. No new dependencies, assets, graphics backend, or pipeline.
- Preserve the existing performance, Vulkan and image-regression gates
  where applicable. Evaluate Android as a shared native build consumer.

## Architectural Impact

[ADR-0185](../adr/0185-hosted-viewport-presentation-policy.md) records the
additive shared Control API, the hosted presentation owner, and separation
of the live view from normal output and unfiltered picking.

## Alternatives Considered

Use a runtime visibility component. It would change shared scene behavior
and require authoring or serialization semantics. Drop the first picked
object in the Editor. That would fail to find the next eligible hit and
leave visuals out of agreement. Treat the overlay or Hierarchy filter as
visibility. Neither controls scene draws and CPU pick traversal together.

## Testing & Verification Plan

[Plan 0136](../plans/0136-viewport-visibility-picking.md) maps R1–R3 to
focused, shared-client and GPU checks and a Windows 100% Bistro workflow.

## Risks & Open Questions

The child window has one presentation, so another client that observes
those same pixels sees the visible result along with the owner. Other
clients continue to get independent unfiltered results from existing APIs.
Review the trusted-client lease and the clear-on-Apply trade-off under
J2–J3. Subtree membership is a bounded snapshot rather than a live link.

## Out of Scope / Future Work

Persistent layers, selective show or unlock, state carried across Apply,
arbitrary masks for captures, independently rendered views and broader
non-mesh rendering controls are deferred.
