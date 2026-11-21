# Spec 0130 — Import and Add a Static glTF Group

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-21
- **Related Plan:** [Plan 0130](../plans/0130-static-gltf-group-instantiation.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0179](../adr/0179-static-gltf-group-projection-and-instantiation.md) (Proposed)

## Summary

Register one self-contained static glTF through the existing project inbox.
After activating a catalog that contains it, add its storefront wall, window
frames and awning to an open hand-authored scene as one editable group. One
new root moves the group and one history entry removes or restores it.

## Motivation and scope

The ordinary glTF importer already cooks a scene with local transforms,
hierarchy, derived mesh GUIDs and per-primitive material bindings. It uses
synthetic renderable children for a mesh with multiple primitives.
[0086](0086-new-asset-import.md) deliberately limits managed registration to
one mesh node with one primitive. [0081](0081-asset-browser-shared-interface.md)
exposes records and dependency GUIDs but cannot describe a node tree.
[0087](0087-add-mesh-to-scene.md) and [0128](0128-viewport-mesh-placement.md)
create a single renderable. [0108](0108-batch-duplicate-subtrees.md) already
provides a revision-guarded append and removal transaction suitable for the
group's document writes.

This slice adds one registered asset group to one current scene through an
explicit Assets action. Viewport drop placement, FBX, GLB, animation, skins,
lights, cameras, textured or transparent materials, bulk import and prefab
update propagation are out of scope. Descendant labels are outside the cooked
scene contract for this slice. Existing single-mesh import, Add to Scene and
Viewport drop remain available.

## Requirements

### R1. Managed import, identity and publication

Keep [0086](0086-new-asset-import.md)'s trusted Windows profile, immutable
`registered/managed_<name>` unit, `inbox/<name>.gltf`, 16 MiB JSON ceiling,
single checked embedded buffer, shared `assets.import` Register/Resume and
status contracts. The new branch admits a single glTF 2.0 scene with one or
more roots, at least two emitted renderables and up to 63 cooked nodes,
including empty transforms and synthetic primitive children. This leaves one
slot for the instance root. Count each referenced mesh instance in the node
budget. Admit only a rooted forest with all declared nodes, meshes and
materials reachable, retaining the importer’s cycle, accessor, triangle,
index, normal, UV and local transform validation. Preflight the node bound
and checked aggregate budgets of 262,144 input POSITION samples and 786,432
indices across distinct primitives before expanding geometry. The existing
single-mesh profile retains its admitted inputs.

Use the existing importer for finite representable local TRS and untextured
opaque metallic-roughness PBR. Preserve base color (alpha 1), metallic and
roughness factors and explicit material assignment per primitive, including
the current generated white fallback texture. An absent material remains
absent. Refuse zero scale and local matrices that cannot be represented by
the existing TRS conversion. Refuse external resources, images, textures,
extensions, camera/light nodes, animation, skins, morph targets and
unsupported material options in the group branch before loading their data.
In particular, refuse non-default
emissive, double-sided and non-opaque requests. The ordinary importer and
unrestricted engine builds keep their existing feature scope.

Reuse root GUID minting and the importer’s existing `scene`, `mesh/...`,
`material/...` and fallback subkeys. Before registration, validate the exact
new producer delta, identities and asset dependency closure and confirm that
the cooked group is admissible under R2. Use the existing workspace exclusion,
staging, frozen inputs, generation publication and recovery. If the job is
known to have failed before registration, retain the original inbox and
active catalog with no committed unit or published generation from the
attempt. If registration committed and
publication failed, retain the registered source and root GUID and use the
existing same-name Resume. An uncertain result follows the existing repair
hold and status flow. Import does not edit the destination scene. A published
group becomes visible only on explicit Runtime activation or a fresh trusted
Saved Scenes build using the registered managed inputs.

### R2. One shared, bounded group description

Add read-only `assets.staticGroup` by cooked Scene GUID to the shared asset
connection. Give the Editor, CLI, native/C# SDKs and MCP the same values and
refusals. Read the existing active catalog snapshot and use the cooked scene
as the authority for order, parents and TRS, with mesh and optional material
AssetIds resolved to GUIDs in that snapshot. Restrict admission to the built
managed glTF scene record and its direct typed dependencies. Use at most 63
nodes. A response carries the same Scene GUID, managed import name and an
ordered list of parent indices, local TRS and optional renderable values
containing mesh GUID and explicitly optional material GUID. No file data or
paths cross this surface. Reject missing records, unsupported group shapes,
unreadable or incompatible artifacts and incomplete dependency resolution as
a whole. See [ADR-0179](../adr/0179-static-gltf-group-projection-and-instantiation.md)
for the admission and compatibility boundary.

### R3. Add once to the open scene

In Assets show **Add Group to Scene** for a selected, visible Scene with a
verified group description and a capable, open hand-authored document.
Follow [0093](0093-editor-visual-layout.md) for disabled and error feedback. Use
the existing focus/draft and interaction guards; refuse a pending draft or
active gesture without committing it as part of Add. Capture selection,
worker/catalog connection and document-open lifetime. Drop a late query result
if those identities change. Repeat the authoritative checks on the worker.
Fence the document order and append boundary with one observed revision and
recheck intent at actual send. A replacement document receives no result,
history or Apply from the original request.

Append one ordinary transform-only root at world origin with identity
rotation/scale. Choose the managed logical import name if unused at root,
then the first available `name 2`, `name 3` and so on. Use ordinal
comparison and the existing UTF-8 node-name limit. Append the
decoded nodes in their cooked relative order. Parent source roots to the new
root and remap each internal parent link using a complete source-index to
fresh EntityGuid map. Parents are linked after creation. Preserve child local
TRS, asset GUID bindings and absent material overrides. Keep the scene GUID,
active camera, existing nodes and their order. Select the new root after
verification if the user's selection intent is still current. Moving it uses
the ordinary Move Gizmo or Transform Inspector, with its own history entry.

Use one existing guarded append transaction under
[ADR-0164](../adr/0164-guarded-duplicate-transactions.md), with no widened
host admission. Settle the exact ticket range and drain failures. A known
refusal leaves zero nodes, history and Apply and keeps the asset registered.
A possibly-sent request with missing or invalid settlement reports the
attempted root GUID and waits for inspection before retry. A confirmed edit
without verifiable history reports known commit with unavailable Undo.
Neither outcome starts another Add. Follow 0108's Redo invalidation and
explicit Apply recovery when settlement is uncertain.

### R4. History, Apply and persistence

Verify the exact appended block at the expected next revision before
publishing one 0108-style guarded history entry. Undo removes the complete
block in one transaction; Redo recreates its exact EntityGuids, hierarchy,
ordered slots, values and asset references from the entry. Require the
existing incarnation, complete value, child membership and append-boundary
checks. Refuse conflicts atomically, including external replacement with the
same GUID, added children and insertion boundary changes. Apply remains
independently correlated under [0083](0083-authoring-apply-outcomes.md).
Failed or unknown Apply keeps the known document edit, verified history and
last good projection as applicable; explicit Apply retries only projection.
Save writes ordinary scene nodes. Reopen and a clean build resolve the same
asset GUIDs from the registered inputs. No authoring state or source-to-copy
link is stored outside the ordinary scene document.

## Proposed Design

Keep import and Add as separate operations. A published catalog describes
what a client may copy. The guarded authoring operation owns what actually
enters the scene. Only the existing Save and Apply paths persist or project
that edit.

## Architectural Impact

[ADR-0179](../adr/0179-static-gltf-group-projection-and-instantiation.md)
records the new read-only shared asset capability and the widened managed
producer boundary. Registration and publication retain their existing
owners. Runtime supplies a CPU-only asset projection. Editor composes the
existing authoring writes and client-local history; the host remains
authoritative about the revision and transaction. There is no Renderer,
RenderGraph, World, native window or artifact schema change.

## Testing & Verification Plan

[Plan 0130](../plans/0130-static-gltf-group-instantiation.md) maps input,
publication and protocol tests to R1–R2 and conflict, settlement and history
tests to R3–R4. The Windows 100% fixture uses a wall, nested window frames,
an awning and distinct material factors. It covers registration and discovery,
adding and moving a whole group, one-step Undo/Redo, save/reopen and a cold
Saved Scenes Build/Open from the same saved source and managed inputs.

## Alternatives considered

Adding each mesh separately would lose the authored hierarchy and could
leave a partial group. Copying an imported authoring document would replace
the current document or require a second authoring session. A linked instance
would introduce scene data and update semantics beyond this slice. The
decision and approval questions are in the ADR and Plan.
