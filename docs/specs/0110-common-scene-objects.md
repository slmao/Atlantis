# Spec 0110 — Create Common Scene Objects

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-16
- **Related Plan:** [Plan 0110](../plans/0110-common-scene-objects.md)
- **Approval:** Pending joint human review.
- **Related ADRs:** None new; see Architectural Impact.

## Summary

Give Hierarchy one **Create** menu for empty nodes, cameras, the supported
lights and three catalog-backed shapes. Each preset creates one editable node
in one transaction. The user can see what committed, recover a failed Runtime
reload, undo or redo the creation, and explicitly save and reopen it.

## Motivation / Problem Statement

Hierarchy already creates an unnamed transform-only node at the root or under
the selection. The Inspector adds components using the authoring schema, and
[0087](0087-add-mesh-to-scene.md) adds a selected catalog mesh at the root with
no explicit material. These are useful building blocks. A camera's raw lens
defaults are zero, and a point light needs a positive range, so raw component
defaults alone are not usable presets. The scene schema has Directional and
Point lights, with a cap of one directional and 64 point lights per scene.
There is no scene-level shape generator; the checked-in catalog already has
static meshes suitable for this purpose.

## Goals and non-goals

- Create one common object at a time in the open hand-authored scene through
  existing shared authoring and asset operations.
- Make the initial values and ownership predictable and keep existing
  Editor history, apply recovery and explicit Save.
- Exclude new light kinds, procedural modeling, generated or imported shape
  assets, asset publication, prefab or bulk creation, cursor/camera-relative
  placement, automatic framing, and automatic activation of a newly created
  camera. Keep Assets → Add to Scene for arbitrary meshes.

## Requirements

### R1 — Entry, target and placement

Replace the Hierarchy **Create child** button with a **Create** menu in that
toolbar. Group entries as node/camera, lights and geometry, using [0093's
existing controls, type and feedback states](0093-editor-visual-layout.md).
No new dialog, shortcut, icon set or dependency. Opening or dismissing the
menu never submits a creation. A named preset targets the root regardless of
selection (including multi-selection), and requires an open hand-authored
document. Preserve the old empty-node operation in this menu: **Empty child**
targets exactly one selected live row, and **Empty root (unnamed)** provides
the old no-selection operation in an imported document. These legacy options
retain their current document eligibility and unnamed result. Do not silently
fall back to root when a child target becomes stale. Both root and child
creation use the end of the observed document order; children use identity
local TRS, so they inherit the parent's transform.

Disable or decline creation without an eligible live document, while the
action is settling, or during an active/pending Gizmo gesture or overlapping
structural edit. Explain an unavailable option; a declined action does not
change selection. Preserve ordinary Inspector focus-loss and draft semantics;
any such edit is separate from creation and is never folded into its history.

### R2 — Named root presets and shared composition

The display name below is also the initial authored name. Repeated names
are allowed, as in existing scene naming; identity is a fresh explicit
EntityGuid for every creation. All root presets have unit scale, zero
rotation and position at world origin unless explicitly listed. Values are
in existing scene units and angle fields use radians.

| Preset | Component and initial values |
| --- | --- |
| **Empty** | Transform only. |
| **Camera** | Camera at `(0, 0, 5)`, looking along −Z; vertical FOV 60°, near `0.1`, far `100`, default exposure/fog/bloom. Keep the document's active camera and the Editor's viewport mode unchanged. |
| **Directional Light** | Light kind Directional, linear white `(1, 1, 1)`, intensity `1`, range `0`, pitch −45° toward the ground. |
| **Point Light** | Light kind Point at `(0, 2, 2)`, linear white `(1, 1, 1)`, intensity `1`, range `10`. |
| **Cube** | Renderable using catalog GUID `3b9b8177-eac1-4139-b203-a49d5577eb3b` (`minimal_cube`, side length 1). |
| **Sphere** | Renderable using catalog GUID `da09198f-d590-44f3-9049-ba5b43fe76cd` (`pbr_sphere`, radius 1). |
| **Plane** | Renderable using catalog GUID `caa7dc9c-6c16-4f93-bdec-53ac93791e16` (`ground_plane`, 10 × 10 in XZ, normal +Y). |

The three mesh presets leave `Renderable.materialAsset` absent so the
existing Runtime fallback applies. Check the fixed GUID and Mesh type using
the current attached Runtime snapshot through shared `assets.get` before
submission. A missing GUID, wrong type, unavailable snapshot or failed lookup
refuses that preset and explains why. No substitution by path, asset label,
pending Assets selection or inactive generation. Lookup success establishes
eligibility; actual artifact load and draw still settle through ordinary apply.
Non-geometry presets need no catalog read. Shapes use the existing mesh
dimensions and appearance; overlap or being out of view is possible at the
origin. Normal Inspector edits and 0109 navigation remain available.

Resolve component/field IDs and the light enum values from the authoring
schema. Submit one existing `CreateNode` (with the optional component already
listed), plus `SetNodeName`, the necessary `SetProperty` writes, and `SetParent`
only for a child, through `authoring.submitTransaction`. Use existing defaults
for the rest. The normal host validator and source cook decide the whole
transaction, including light limits. For example, a second directional light
or 65th point light is refused, with no change to a pre-existing light and no
partial node or Apply from this action. A redo that would exceed a limit also
refuses whole. No renderer, authoring schema, catalog or wire change.

### R3 — Current request, settlement and Runtime recovery

Use the existing Editor worker. Capture worker, document-open lifetime and
request identity at activation; for an empty child also capture the chosen
row and its incarnation. After any schema/catalog reads, refresh observations
and check the request, document provenance, current order, gesture and, for
a child, the same selected live row immediately before submission. Reject a
successful observed reopen even to the same source, an observed replacement
by a row with the same GUID, or an unsuccessful observation. A refused open
does not advance lifetime. Root presets never adopt a later parent. These are
client observations; there is no host-side compare-and-swap for this command
shape. Another client can write between a check and submit, so the insertion
position is the observed end and the host validates the actual transaction.

Settle the submit reply and drain failures for its well-formed ticket range
(nonzero first ticket, expected count and no overflow) before recording
history, selecting the node or invoking automatic Apply.
A definite refusal is distinct from lost or invalid settlement. If the
transaction might have reached the host, report **creation outcome unknown**
with the attempted GUID; inspect the document after reconnect before any
explicit retry. Do not infer commit from a matching name, retry automatically,
record speculative history, or trigger Apply for unverified creation. If
still attached to the same document, wait for explicit Apply rather than
coalescing a follow-up for a revision that may include the unverified edit.

After confirmed commit, verify the created node's full expected state and its
observed creation identity before recording one structural creation entry.
Do not record a changed/replaced node or install history in a reopened
document. Guard delayed selection and feedback by worker and document; select
the still-current created row in Hierarchy/Inspector, without changing the
Assets selection or camera mode. Do not steal selection from a newer user
selection; when the row is hidden by a filter, use existing filtered-selection
feedback. Selection is document feedback and may precede the Runtime swap.
When history or the document can no longer be verified, report known creation
separately and wait for explicit Apply on the current document. Clear stale
redo for a known new edit in the same document if an undo entry is unavailable.

For a verified edit, use [0083's tracked Apply and existing coalescing and
recovery](0083-authoring-apply-outcomes.md). If intervening work is observed
before automatic Apply and ownership of the current revision cannot be
attributed to this local edit, wait for explicit Apply, letting an older
accepted receipt settle independently. A failed reload retains the committed
node, available history and the last good Runtime projection. A lost Apply
reply after verified creation leaves only the projection outcome unknown.
Pending/paused, superseded and suspended outcomes use existing feedback.
**Apply / Retry** reapplies the current document; it never submits another
creation or resets history. Reopen clears local history in the usual way.

### R4 — History and persistence

An ordinary successful creation adds exactly one undo entry. Undo removes
the whole node; Redo recreates the same GUID, name, parent, insertion
position and complete component values, with unchanged asset references. Use
the existing structural replay and refusal semantics: e.g. external changes
to the node, children later attached to it, GUID collision on redo, a lost
parent or the created camera becoming active can prevent replay. That must
not delete dependent state, overwrite a different node or switch the active
camera. The existing observational replay checks and atomic host validation
remain the boundary; this is not a new revision-guarded structural command.
Replay keeps the normal history selection policy. Saving is explicit and
uses ordinary hand-authored scene source. The same source reopens with the
same saved nodes, identities and values using a generation that contains the
referenced meshes. The old imported-document empty operation remains transient.

### Non-functional

Reuse the existing C# SDK, UI-free model, single worker and Avalonia resources.
Do no new per-frame work, asset processing or GPU scheduling, and add no
dependencies. Keep the Windows hosted Editor and existing Vulkan-only scope.

## Architectural Impact

**None.** Presets are Editor-side compositions of existing schema values,
asset IDs and shared commands. CLI (`scene tx`), native/C# SDKs and MCP
(`authoring.submit_transaction`, failure drain, then Apply when requested)
can compose the same document mutation. They need no preset verb, capability
or version negotiation. Editor selection and history remain local.
The existing contracts in [ADR-0125](../adr/0125-out-of-process-editor-platform-and-uniform-client-interface.md),
[ADR-0130](../adr/0130-authoring-access-interface-and-module-boundary.md),
[ADR-0136](../adr/0136-structural-edit-records.md),
[ADR-0143](../adr/0143-shared-cooked-asset-catalog-queries.md) and
[ADR-0144](../adr/0144-correlated-authoring-apply-outcomes.md) suffice.

## Alternatives and open decisions

Per-component edits after a blank create would introduce partial commits and
multiple undo steps. Putting every preset under the selection would inherit
unknown scale/orientation and require parent policy for cameras and lights.
Generating shapes or injecting internal meshes would introduce asset lifecycle
rules that existing catalog assets already avoid. Origin placement and
ordinary catalog availability are deliberate trade-offs; [Plan 0110](../plans/0110-common-scene-objects.md)
collects these and the settlement and verification decisions for human review.

## Verification

Plan 0110 maps R1–R4 to focused model/worker/UI checks and a Windows 100%
desktop scenario: create representative objects, verify the authored values
and Runtime outcome, undo/redo, save, reopen and cold-open the same source
through the existing Saved Scenes flow. Prove refusal and recovery separately
from the success path. Keep the applicable repository gates.
