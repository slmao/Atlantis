# Spec 0132 — Create and Instantiate Static Scene Prefabs

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-21
- **Related Plan:** [Plan 0132](../plans/0132-scene-prefabs.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0181](../adr/0181-static-prefab-registration-and-provenance.md) (Proposed)

## Summary

Save a table and four chairs from Hierarchy as one immutable project Prefab.
After a trusted Saved Scenes Build/Open, instantiate it twice from Assets.
Each instance is an independently editable scene subtree with its own node
identities and a persistent association to the source Prefab.

## Motivation and existing capabilities

[0102](0102-duplicate-scene-subtree.md) captures a rooted subtree's order,
names, local transforms and component values. [0108](0108-batch-duplicate-subtrees.md)
guards a bounded structural append or removal with the observed document
revision. [0130](0130-static-gltf-group-instantiation.md) already composes
one such append from an Assets query. Its cooked projection omits node names,
and its inserted nodes carry no source association after save.
[0088](0088-scene-save-as.md) publishes a trusted checkout source and catalog
fragment together. [0090](0090-saved-scene-build-open.md) makes new project
inputs available after an explicit Build/Open. Saved-slot Runtime builds
refuse the live managed asset-build profile, so first-release Prefab discovery
uses that explicit build boundary.

## Requirements

### R1 — Capture and register one static subtree

Offer **Save as Prefab** for one visible selected root in an open
hand-authored scene with a trusted, development-build Runtime and explicit
Prefab registration permission. Keep this permission distinct from Save As.
Use existing Hierarchy selection-incarnation, draft, filter and gesture
guards. Require a deliberate save name using the existing canonical
Save As slug grammar. A pending Inspector input remains in the Inspector.

Use a shared create operation addressed by logical name, selected root GUID
and expected document token. The Authoring session checks the open
occurrence and revision and freezes its own committed document snapshot in
one admission. Unapplied committed values are eligible. If admission is stale,
refuse before writing. Later document edits have no effect on the frozen
snapshot. A second selection or late result cannot retarget the request.

Admit one rooted subtree of up to 64 nodes, including its root, with
transform-only or mesh Renderable nodes and at least one mesh. Preserve
relative document order, names, parent relations, all local TRS and explicit
or absent material overrides. Remove the source root's outside parent and
retain its local TRS. Validate lossless source round-trip and resolve every
mesh and material by typed GUID in the active catalog. Camera/light
components, ambiguous or unsupported fields, nested template references,
and a selection inside or containing a linked instance refuse the whole
operation. Bound the serialized source to 64 KiB.

Allocate one new Prefab asset GUID and a separate template node GUID space.
Publish the immutable source and its exact single-record catalog fragment
as one checkout registration unit under the ownership, path, exclusivity
and non-overwrite rules in ADR-0181. Repeat dependency and frozen-byte
validation before the first authoritative rename. Reject occupied names,
ambiguous identities or unsafe paths. Only privately owned staging may be
cleaned on a confirmed failure. A lost receipt reports an unknown outcome
with the requested registration name and the instruction to check that
registration after the request ends before retrying. There is no automatic
retry or overwrite.

Success reports the Prefab GUID, logical name and captured revision in the
existing task/feedback UI. The original subtree, its GUIDs, selection,
document revision, dirty state, history and Runtime projection stay as they
were. Creation commits a project asset registration separately from scene
Save, Apply and Undo. Save the open scene explicitly if its changes are
needed in a subsequent build.

### R2 — Discover the committed Prefab after Build/Open

Use a trusted Saved Scenes Build with the checkout's Prefab registrations
included in its inputs, then explicitly Open the saved scene in the
independent Editor. The existing 0123 input receipt must become stale when
registration changes. A build or launch failure leaves the committed
Prefab and source scene available for a corrected explicit Build/Open.
The open Runtime's catalog stays fixed during registration.

Reuse the cooked Scene catalog carrier internally. Present a distinct
**Prefab** classification in updated Assets list/get views using additive
metadata. Only canonical, built registrations can be classified this way.
Add a shared bounded `assets.prefab` read of the immutable build snapshot
by Prefab GUID. Return one ordered tree with an explicit parent index or
root, names, local TRS and optional typed mesh/material bindings. Validate
the frozen source and its cooked carrier together. Missing or inconsistent
payload or dependency data refuses the whole query. The shared surface
carries no arbitrary filesystem path or filesystem handle.

Use the same read and authoring contracts in Editor, CLI, native and C#
SDKs, and MCP. Existing clients keep their current Scene and glTF group
behavior. Prefabs do not appear as Saved Scenes launch targets. Named
generation registration/publication is outside v1.

### R3 — Instantiate as one guarded edit

Offer **Instantiate Prefab** for an eligible visible asset in an open
hand-authored scene. Before sending, recheck asset selection, worker/build
snapshot, the open document token and active interaction/draft guards.
Query the current frozen Prefab from the same snapshot used for admission.
Allocate fresh scene EntityGuids for every node. Map the complete parent
tree, append the block in template order and attach its one root at scene
level. There is no added wrapper. Keep the captured local TRS initially.
When the source had an outside parent, the same root TRS is interpreted in
scene-root space. Use the Prefab display name and deterministic ordinal
root-level suffixing under the existing UTF-8 limit. Keep descendant labels.

Use the [0108 guarded structural transaction](0108-batch-duplicate-subtrees.md)
with ADR-0181's narrow root provenance addition. Validate the complete
candidate and settle the exact ticket range. A known preflight or transaction
refusal creates zero nodes and leaves selection, Undo/Redo and Apply
untouched. A lost or invalid result after possible submission states that
the outcome is unknown, gives the attempted root GUID and instructs the user
to inspect before retry. A confirmed transaction with incomplete read-back
reports known commit and unavailable Undo. A settled operation selects the
new root only while its request is current.

### R4 — Move, undo, save and reopen

Persist one read-only source Prefab GUID on each instance root in the scene
document. Do not reconstruct the relationship from the name, path, geometry
or retained dialogs. A source can be missing while the scene's independent
nodes remain valid and editable. The Inspector shows the source and whether
it can be found in the current Assets snapshot. There is no live expansion,
comparison with a template version or stored override set.

Move either root with the normal Gizmo or Inspector, independently of the
other instance. One Undo removes one entire verified instantiation. Redo
restores its same GUIDs, order, parent links, values and Prefab association.
Include the provenance field in exact history verification and the existing
incarnation, child-membership and append-boundary conflict guards. Normal
component, transform and name edits carry the field forward. Ordinary
structural replay must preserve it or explicitly refuse before mutation.
Refuse operations that would place one linked root below another linked
root. If restoration requires a source unavailable in the current snapshot,
leave history and the document intact and explain the refusal.

Use the existing [correlated Apply](0083-authoring-apply-outcomes.md).
A verified transaction may settle independently of Apply. Failed or unknown
Apply retains the confirmed document edit and verified history, with the
last good projection where applicable. Explicit Apply retries projection
only. Ordinary Save, Save As and recovery preserve root provenance.
After saving, reopen the same source or use 0090 to build and independently
open it. Read-back retains the association even though the rendered World
contains only the expanded scene nodes.

## Architectural Impact

[ADR-0181](../adr/0181-static-prefab-registration-and-provenance.md) defines
the shared registration and snapshot read, narrowly extended guarded edit,
and versioned authoring provenance. Keep cooked Scene, World, Renderer and
RenderGraph projection unchanged. No new runtime prefab expansion layer.

## Testing and verification

[Plan 0132](../plans/0132-scene-prefabs.md) maps the table-and-four-chairs
acceptance to R1–R4. The central persistence proof is two distinct node
blocks with the same source GUID in a saved and reopened scene. Existing
component and material rendering gates still apply where selected.

## Out of scope

Source edits or propagation, instance overrides, nested templates, variants,
bulk creation, cross-checkout links, drag placement, named generation
publication and a live asset-build profile for saved-slot Runtimes.
